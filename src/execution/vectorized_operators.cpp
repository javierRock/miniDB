// The vectorized execution path: batch-at-a-time instead of tuple-at-a-time.
//
// The Volcano interface is untouched — these are ordinary physical operators with
// Open, Next and Close. What changes is what travels between them: a RecordBatch
// of about a thousand records instead of one record, which turns the per-record
// virtual call into a per-batch one, lets the scan pin each page once instead of
// once per record, and puts the predicate evaluation in a loop shape the compiler
// can turn into SIMD instructions.
//
// Both operators inherit the record-at-a-time face from BatchOperator, so they
// can sit anywhere in a plan, including under an operator that knows nothing
// about batches.

#include <utility>

#include "minidb/execution/vectorized_filter_operator.hpp"
#include "minidb/execution/vectorized_scan_operator.hpp"

namespace minidb {

// --- BatchOperator -------------------------------------------------------

void BatchOperator::ResetBatchCursor() {
    batch_.Clear();
    position_ = 0;
    exhausted_ = false;
    last_rid_ = RecordId{};
}

std::optional<Record> BatchOperator::Next() {
    while (position_ >= batch_.SelectedCount()) {
        if (exhausted_) {
            return CountedCall(std::nullopt);
        }
        // A batch can come back with nothing selected — a filter that rejected
        // every record in it — and that is not the end of the input.
        if (!NextBatch(batch_)) {
            exhausted_ = true;
            return CountedCall(std::nullopt);
        }
        position_ = 0;
    }

    const std::uint16_t slot = batch_.Selection()[position_++];
    last_rid_ = batch_.RecordIdAt(slot);
    return CountedCall(batch_.RecordAt(slot));
}

// --- VectorizedScanOperator ----------------------------------------------

void VectorizedScanOperator::Open() {
    ResetBatchCursor();
    next_page_ = heap_.FirstPageId();
    pages_read_ = 0;
}

bool VectorizedScanOperator::NextBatch(RecordBatch& batch) {
    batch.Clear();

    // One page per iteration, each fetched exactly once. Filling whole pages
    // means the operator never has to remember a position inside one.
    while (next_page_ != kInvalidPageId && !batch.Full()) {
        next_page_ = heap_.ForEachRecordInPage(
            next_page_, [&batch](RecordId rid, Record record) {
                batch.Append(rid, std::move(record));
            });
        ++pages_read_;
    }

    if (batch.Size() == 0) {
        return false;
    }
    CountBatch(batch.SelectedCount());
    return true;
}

void VectorizedScanOperator::Close() {
    ResetBatchCursor();
    next_page_ = kInvalidPageId;
}

// --- VectorizedFilterOperator --------------------------------------------

VectorizedFilterOperator::VectorizedFilterOperator(std::unique_ptr<PhysicalOperator> child,
                                                   const Schema& schema, Condition condition)
    : child_(std::move(child)), condition_(std::move(condition)) {
    const auto index = schema.FindColumn(condition_.column);
    if (!index.has_value()) {
        throw QueryError("No existe la columna '" + condition_.column + "' en la tabla");
    }
    column_index_ = *index;

    // Rejected while building the plan rather than once per record, exactly as
    // FilterOperator does: comparing an INT against a string can never mean
    // anything.
    const Column& column = schema.GetColumn(column_index_);
    const bool value_is_integer = std::holds_alternative<std::int32_t>(condition_.value);
    if ((column.type == ColumnType::kInteger) != value_is_integer) {
        throw QueryError("La columna '" + column.name + "' es " +
                         (column.type == ColumnType::kInteger ? "INT" : "VARCHAR") +
                         " y se comparó con un valor de otro tipo");
    }

    column_is_integer_ = column.type == ColumnType::kInteger;
    if (column_is_integer_) {
        integer_literal_ = std::get<std::int32_t>(condition_.value);
    }
}

namespace {

/// Writes 0 or 1 per value: the vectorizable core of the filter.
///
/// Three details are what let the compiler emit one comparison for several values
/// at a time, and all three are easy to lose by accident:
///
///  - **The loop body has no branch and no indirect call.** The predicate is a
///    template parameter, not a runtime enum, so it inlines to a bare comparison.
///  - **The mask is int32_t, not uint8_t or bool.** A write through a `char`-like
///    type may alias any other object, so the compiler could not rule out that
///    writing a byte of the mask modified the next value to read, and it gave up
///    on vectorizing entirely. Four bytes per flag is a cheap price.
///  - **The pointers are `__restrict`.** It states that the two arrays do not
///    overlap, which they cannot: they are separate members of the operator.
///
/// Verified by disassembling the -O2 build: the loop compiles to `pcmpgtd`
/// comparing four values per instruction.
template <typename Predicate>
void CompareInto(const std::int32_t* __restrict values, std::size_t count,
                 std::int32_t* __restrict matches, Predicate predicate) {
    for (std::size_t i = 0; i < count; ++i) {
        matches[i] = predicate(values[i]) ? 1 : 0;
    }
}

/// Runs the comparison for one operator. The switch happens once per batch, not
/// once per record, which is what keeps it out of the loop above.
void CompareColumn(CompareOperator op, std::int32_t literal,
                   const std::vector<std::int32_t>& values, std::vector<std::int32_t>& matches) {
    matches.resize(values.size());
    const std::int32_t* input = values.data();
    std::int32_t* output = matches.data();
    const std::size_t count = values.size();

    switch (op) {
        case CompareOperator::kEqual:
            CompareInto(input, count, output, [literal](std::int32_t v) { return v == literal; });
            return;
        case CompareOperator::kNotEqual:
            CompareInto(input, count, output, [literal](std::int32_t v) { return v != literal; });
            return;
        case CompareOperator::kLess:
            CompareInto(input, count, output, [literal](std::int32_t v) { return v < literal; });
            return;
        case CompareOperator::kLessEqual:
            CompareInto(input, count, output, [literal](std::int32_t v) { return v <= literal; });
            return;
        case CompareOperator::kGreater:
            CompareInto(input, count, output, [literal](std::int32_t v) { return v > literal; });
            return;
        case CompareOperator::kGreaterEqual:
            CompareInto(input, count, output, [literal](std::int32_t v) { return v >= literal; });
            return;
    }
    throw StorageError("Operador de comparación desconocido en el filtro vectorizado");
}

}  // namespace

std::size_t VectorizedFilterOperator::ApplyToBatch(RecordBatch& batch) {
    const std::vector<std::uint16_t>& incoming = batch.Selection();
    rows_evaluated_ += incoming.size();

    selection_.clear();
    selection_.reserve(incoming.size());

    if (column_is_integer_) {
        // Gather, compare, then select. Only the middle step vectorizes, and it
        // is the one that does the arithmetic.
        batch.ExtractInt32Column(column_index_, values_);
        CompareColumn(condition_.op, integer_literal_, values_, matches_);

        for (std::size_t i = 0; i < incoming.size(); ++i) {
            if (matches_[i] != 0) {
                selection_.push_back(incoming[i]);
            }
        }
    } else {
        // VARCHAR: the values are variable-length and scattered, so there is
        // nothing to vectorize. Batching still removes the virtual call per
        // record.
        for (std::uint16_t position : incoming) {
            if (CompareValues(batch.RecordAt(position).GetValue(column_index_), condition_.op,
                              condition_.value)) {
                selection_.push_back(position);
            }
        }
    }

    batch.SetSelection(selection_);
    return batch.SelectedCount();
}

void VectorizedFilterOperator::Open() {
    ResetBatchCursor();
    child_->Open();
    rows_evaluated_ = 0;
}

bool VectorizedFilterOperator::NextBatch(RecordBatch& batch) {
    // Keep pulling while batches come back empty: a filter that rejects every
    // record of a batch has not reached the end of its input.
    while (child_->NextBatch(batch)) {
        if (ApplyToBatch(batch) > 0) {
            CountBatch(batch.SelectedCount());
            return true;
        }
    }
    return false;
}

void VectorizedFilterOperator::Close() {
    ResetBatchCursor();
    child_->Close();
}

}  // namespace minidb
