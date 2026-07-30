#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "minidb/execution/physical_operator.hpp"

namespace minidb {

/// Counts the records of its child, either per group (GROUP BY) or in total.
///
/// Blocking, like SortOperator: a count is only known once the last input
/// record has been seen, so Open() drains the child and Next() then hands out
/// one row per group.
///
/// COUNT(*) is the only aggregate. Its output column is named "COUNT(*)", which
/// no real column can be called because an SQL identifier cannot contain
/// parentheses — so the name needs no escaping and no special case anywhere
/// downstream.
///
/// Groups come out in ascending order of the grouping value, which makes the
/// result deterministic without needing an ORDER BY.
class AggregateOperator : public PhysicalOperator {
public:
    /// Groups by `group_column`, resolved against `input_names`.
    AggregateOperator(std::unique_ptr<PhysicalOperator> child,
                      const std::vector<std::string>& input_names, const std::string& group_column);

    /// No grouping: produces exactly one row with the total count, even when the
    /// input is empty.
    explicit AggregateOperator(std::unique_ptr<PhysicalOperator> child);

    void Open() override;
    [[nodiscard]] std::optional<Record> Next() override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "AggregateOperator"; }

    /// Not meaningful: an aggregate row summarises many records and corresponds
    /// to none of them.
    [[nodiscard]] RecordId LastRecordId() const override { return RecordId{}; }
    [[nodiscard]] const PhysicalOperator* Child() const override { return child_.get(); }

    /// {<grouping column>, "COUNT(*)"}, or just {"COUNT(*)"} without GROUP BY.
    [[nodiscard]] const std::vector<std::string>& OutputColumnNames() const override {
        return names_;
    }

private:
    std::unique_ptr<PhysicalOperator> child_;
    /// Absent when there is no GROUP BY, i.e. one single group.
    std::optional<std::size_t> group_column_index_;
    std::vector<std::string> names_;

    /// One entry per group, ordered by value. Filled by Open, released by Close.
    std::vector<std::pair<Value, std::uint64_t>> groups_;
    std::size_t position_ = 0;
};

}  // namespace minidb
