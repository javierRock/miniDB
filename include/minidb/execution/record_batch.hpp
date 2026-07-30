#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "minidb/storage/record.hpp"

namespace minidb {

/// A block of records travelling through the plan together, plus the selection
/// vector that says which of them are still alive.
///
/// This is what turns the tuple-at-a-time Volcano pipeline into a vectorized
/// one. Instead of one virtual call and one page fetch per record, an operator
/// asks for a whole batch and works over it in tight loops.
///
/// The **selection vector** is the reason a filter costs almost nothing: it does
/// not copy or move any record, it just writes down the positions that satisfied
/// the predicate. Records stay exactly where the scan put them for as long as
/// the batch lives.
///
/// Buffers are reused across calls. The whole point of batching is to stop
/// allocating per record, so an operator keeps one batch and refills it.
class RecordBatch {
public:
    /// Target number of records per batch.
    ///
    /// It is a target rather than a hard limit: a scan fills the batch page by
    /// page and stops once it is reached, so the last page may push slightly
    /// over. Allowing that keeps the scan from having to remember a position in
    /// the middle of a page, which is where a batched scan would get its bugs.
    static constexpr std::size_t kTargetSize = 1024;

    /// Drops the contents but keeps the allocated capacity.
    void Clear();

    void Append(RecordId rid, Record record);

    /// Records held, selected or not.
    [[nodiscard]] std::size_t Size() const { return records_.size(); }

    /// True once the batch has reached its target size; see kTargetSize.
    [[nodiscard]] bool Full() const { return records_.size() >= kTargetSize; }

    /// Positions of the records that are still selected, in order.
    [[nodiscard]] const std::vector<std::uint16_t>& Selection() const { return selection_; }
    [[nodiscard]] std::size_t SelectedCount() const { return selection_.size(); }
    [[nodiscard]] bool Empty() const { return selection_.empty(); }

    /// Replaces the selection. Every position must be below Size().
    void SetSelection(std::vector<std::uint16_t> selection);

    [[nodiscard]] const Record& RecordAt(std::size_t position) const;
    [[nodiscard]] RecordId RecordIdAt(std::size_t position) const;

    /// Copies one INT column of the selected records into a contiguous buffer.
    ///
    /// This is the step that makes SIMD possible: the values of a column are
    /// scattered across variable-length records, and a comparison loop can only
    /// be vectorized over an array. `out` is reused between calls.
    void ExtractInt32Column(std::size_t column, std::vector<std::int32_t>& out) const;

private:
    std::vector<Record> records_;
    std::vector<RecordId> record_ids_;
    /// Indices into records_. Not a bitmap: iterating the survivors is the
    /// common operation, and with a selective filter there are far fewer of them
    /// than there are records.
    std::vector<std::uint16_t> selection_;
};

}  // namespace minidb
