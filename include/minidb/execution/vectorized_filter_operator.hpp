#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minidb/catalog/schema.hpp"
#include "minidb/execution/batch_operator.hpp"
#include "minidb/parser/statement.hpp"

namespace minidb {

/// Evaluates a condition over a whole batch and narrows its selection vector.
///
/// Where FilterOperator asks its child for one record, compares it and returns
/// it, this one asks for a batch and evaluates the predicate over all of it. Two
/// things follow from that:
///
///  1. **No copying.** The surviving records are not moved anywhere; the operator
///     just rewrites the batch's selection vector with the positions that
///     matched.
///  2. **The comparison can be vectorized.** For an INT column the values are
///     first copied into a contiguous array, and then compared in a loop with no
///     branches and no virtual calls, of the shape a compiler turns into SIMD
///     instructions. A predicate over a VARCHAR column cannot be: the values are
///     variable-length and live at scattered offsets, so those fall back to a
///     record-at-a-time comparison inside the batch, which still saves the
///     virtual call per record but not more.
///
/// The selection pass that follows the comparison is inherently sequential —
/// turning a mask into a list of positions depends on how many bits were set so
/// far. Real systems use a dedicated SIMD compress instruction for it; here it
/// stays a plain loop, and the comparison is the part that vectorizes.
class VectorizedFilterOperator : public BatchOperator {
public:
    VectorizedFilterOperator(std::unique_ptr<PhysicalOperator> child, const Schema& schema,
                             Condition condition);

    void Open() override;
    bool NextBatch(RecordBatch& batch) override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "VectorizedFilterOperator"; }
    [[nodiscard]] const PhysicalOperator* Child() const override { return child_.get(); }

    /// Records the predicate was evaluated against, and how many of them were
    /// compared in a vectorizable loop rather than one by one.
    [[nodiscard]] std::uint64_t RowsEvaluated() const { return rows_evaluated_; }
    [[nodiscard]] bool UsesVectorizedComparison() const { return column_is_integer_; }

private:
    /// Narrows `batch` in place. Returns how many records survived.
    [[nodiscard]] std::size_t ApplyToBatch(RecordBatch& batch);

    std::unique_ptr<PhysicalOperator> child_;
    Condition condition_;
    std::size_t column_index_ = 0;
    bool column_is_integer_ = false;
    std::int32_t integer_literal_ = 0;

    /// Reused between batches so the operator stops allocating after the first
    /// one. This is as much the point of batching as the tight loops are.
    std::vector<std::int32_t> values_;
    /// int32 rather than a byte or a bool on purpose: a write through a
    /// `char`-like type may alias anything, which stops the comparison loop from
    /// being vectorized at all. See CompareInto in the .cpp.
    std::vector<std::int32_t> matches_;
    std::vector<std::uint16_t> selection_;

    std::uint64_t rows_evaluated_ = 0;
};

}  // namespace minidb
