#pragma once

#include <cstdint>
#include <string>

#include "minidb/execution/batch_operator.hpp"
#include "minidb/storage/table_heap.hpp"

namespace minidb {

/// Reads the table a page at a time and hands the records over in batches.
///
/// This is the vectorized counterpart of SequentialScanOperator, and the
/// difference is not only in how many virtual calls it saves. The tuple-at-a-time
/// scan fetches its page from the buffer pool **once per record**: the iterator
/// pins the page, reads one slot, unpins it, and pins it again for the next slot.
/// This one fetches each page once and deserialises every live record on it, so
/// the pool sees one round trip per page instead of one per record. On a page
/// holding 90 records that is 90 accesses turned into one.
///
/// It fills the batch page by page until the target size is reached, so it never
/// has to remember a position in the middle of a page. That keeps the operator's
/// entire state down to "which page comes next".
class VectorizedScanOperator : public BatchOperator {
public:
    explicit VectorizedScanOperator(const TableHeap& heap) : heap_(heap) {}

    void Open() override;
    bool NextBatch(RecordBatch& batch) override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "VectorizedScanOperator"; }

    /// Pages read since Open. Reported so the saving over a record-at-a-time
    /// scan can be stated as a number rather than asserted.
    [[nodiscard]] std::uint64_t PagesRead() const { return pages_read_; }

private:
    const TableHeap& heap_;
    PageId next_page_ = kInvalidPageId;
    std::uint64_t pages_read_ = 0;
};

}  // namespace minidb
