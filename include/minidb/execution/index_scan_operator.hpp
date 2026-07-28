#pragma once

#include <cstdint>
#include <optional>

#include "minidb/execution/physical_operator.hpp"
#include "minidb/index/hash_index.hpp"
#include "minidb/storage/table_heap.hpp"

namespace minidb {

/// Fetches a single record by primary key, using the hash index.
///
/// Only usable for equality on the primary key, which is all a hash index can
/// answer. Because the key is unique, the operator yields at most one record
/// and needs no filter above it.
class IndexScanOperator : public PhysicalOperator {
public:
    IndexScanOperator(const TableHeap& heap, const HashIndex& index, std::int32_t key)
        : heap_(heap), index_(index), key_(key) {}

    void Open() override;
    [[nodiscard]] std::optional<Record> Next() override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "IndexScanOperator"; }
    [[nodiscard]] RecordId LastRecordId() const override { return last_rid_; }

private:
    const TableHeap& heap_;
    const HashIndex& index_;
    std::int32_t key_;
    bool consumed_ = true;
    RecordId last_rid_;
};

}  // namespace minidb
