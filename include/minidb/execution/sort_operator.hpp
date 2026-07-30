#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "minidb/execution/physical_operator.hpp"

namespace minidb {

/// Orders the records of its child by one column (ORDER BY).
///
/// This is the first *blocking* operator of the system: unlike Filter and
/// Projection, which forward one record at a time, sorting cannot emit anything
/// until it has seen the last input record. So Open() drains the child into a
/// vector and sorts it, and Next() then walks that vector.
///
/// The materialised rows live in RAM, bounded by the size of the table. A real
/// system would spill to a temporary file and merge externally once the input
/// stopped fitting in memory; that is the next step and not implemented here.
///
/// The sort is *stable*: records that tie on the ordering column keep the order
/// their child produced them in, which for a sequential scan is insertion order.
class SortOperator : public PhysicalOperator {
public:
    /// `input_names` are the columns the child produces, supplied by the
    /// planner; `column` is resolved against them. Sorting therefore works the
    /// same over a table scan as over the output of an aggregate.
    SortOperator(std::unique_ptr<PhysicalOperator> child, const std::vector<std::string>& input_names,
                 const std::string& column, bool descending);

    void Open() override;
    [[nodiscard]] std::optional<Record> Next() override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "SortOperator"; }

    /// Not meaningful: the records were materialised and are no longer tied to
    /// the page they came from. UPDATE and DELETE never build a plan with an
    /// ORDER BY, so nothing asks for it.
    [[nodiscard]] RecordId LastRecordId() const override { return RecordId{}; }
    [[nodiscard]] const PhysicalOperator* Child() const override { return child_.get(); }

private:
    std::unique_ptr<PhysicalOperator> child_;
    std::size_t column_index_ = 0;
    bool descending_ = false;

    /// The whole input, sorted. Filled by Open and released by Close.
    std::vector<Record> rows_;
    std::size_t position_ = 0;
};

}  // namespace minidb
