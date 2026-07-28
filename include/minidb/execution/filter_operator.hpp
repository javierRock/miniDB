#pragma once

#include <memory>
#include <optional>

#include "minidb/catalog/schema.hpp"
#include "minidb/execution/physical_operator.hpp"
#include "minidb/parser/statement.hpp"

namespace minidb {

/// Passes through only the records of its child that satisfy a condition.
///
/// It pulls from the child in a loop until one matches, so it streams: no
/// intermediate result is ever materialised.
class FilterOperator : public PhysicalOperator {
public:
    FilterOperator(std::unique_ptr<PhysicalOperator> child, const Schema& schema,
                   Condition condition);

    void Open() override;
    [[nodiscard]] std::optional<Record> Next() override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "FilterOperator"; }
    [[nodiscard]] RecordId LastRecordId() const override { return child_->LastRecordId(); }
    [[nodiscard]] const PhysicalOperator* Child() const override { return child_.get(); }

private:
    std::unique_ptr<PhysicalOperator> child_;
    Condition condition_;
    std::size_t column_index_ = 0;
};

}  // namespace minidb
