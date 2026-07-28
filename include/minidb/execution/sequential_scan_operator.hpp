#pragma once

#include <optional>
#include <vector>

#include "minidb/execution/physical_operator.hpp"
#include "minidb/storage/table_heap.hpp"

namespace minidb {

/// Reads every live record of the table, one page at a time.
///
/// Nothing is buffered in RAM beyond the record being returned: the underlying
/// iterator walks the page chain and releases each page before moving on, so a
/// scan works on a table far larger than the buffer pool.
class SequentialScanOperator : public PhysicalOperator {
public:
    explicit SequentialScanOperator(const TableHeap& heap) : heap_(heap) {}

    void Open() override;
    [[nodiscard]] std::optional<Record> Next() override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "SequentialScanOperator"; }
    [[nodiscard]] RecordId LastRecordId() const override { return last_rid_; }

private:
    const TableHeap& heap_;
    std::optional<TableHeap::Iterator> iterator_;
    RecordId last_rid_;
};

}  // namespace minidb
