// The blocking operators, kept apart from operators.cpp on purpose.
//
// Filter and Projection are streaming: they answer a Next() with at most one
// call to their child, so a plan made only of those uses memory proportional to
// one record. Sort and Aggregate cannot: neither the first sorted row nor a
// count is known before the last input record has been read, so both drain
// their child inside Open(). That difference is the reason they live in their
// own translation unit.

#include <algorithm>
#include <map>
#include <utility>

#include "minidb/execution/aggregate_operator.hpp"
#include "minidb/execution/sort_operator.hpp"
#include "minidb/parser/statement.hpp"

namespace minidb {
namespace {

/// Resolves a column name against the names the child produces.
[[nodiscard]] std::size_t ResolveColumn(const std::vector<std::string>& input_names,
                                        const std::string& column, const char* clause) {
    for (std::size_t i = 0; i < input_names.size(); ++i) {
        if (input_names[i] == column) {
            return i;
        }
    }
    throw QueryError("No se puede usar '" + column + "' en " + clause +
                     ": no es una columna del resultado");
}

}  // namespace

// --- SortOperator --------------------------------------------------------

SortOperator::SortOperator(std::unique_ptr<PhysicalOperator> child,
                           const std::vector<std::string>& input_names, const std::string& column,
                           bool descending)
    : child_(std::move(child)),
      column_index_(ResolveColumn(input_names, column, "ORDER BY")),
      descending_(descending) {}

void SortOperator::Open() {
    child_->Open();

    rows_.clear();
    position_ = 0;

    // Blocking step: the whole input is consumed here.
    while (auto record = child_->Next()) {
        rows_.push_back(std::move(*record));
    }
    child_->Close();

    const std::size_t index = column_index_;
    const bool descending = descending_;
    // Stable so that ties keep the order the child produced them in, which for
    // a sequential scan is insertion order.
    std::stable_sort(rows_.begin(), rows_.end(),
                     [index, descending](const Record& left, const Record& right) {
                         const ValueLess less;
                         return descending ? less(right.GetValue(index), left.GetValue(index))
                                           : less(left.GetValue(index), right.GetValue(index));
                     });
}

std::optional<Record> SortOperator::Next() {
    if (position_ >= rows_.size()) {
        return Counted(std::nullopt);
    }
    return Counted(rows_[position_++]);
}

void SortOperator::Close() {
    // Open already closed the child once it was drained; closing again is
    // harmless and covers the case where Open threw halfway through.
    child_->Close();
    rows_.clear();
    rows_.shrink_to_fit();
    position_ = 0;
}

// --- AggregateOperator ---------------------------------------------------

AggregateOperator::AggregateOperator(std::unique_ptr<PhysicalOperator> child,
                                     const std::vector<std::string>& input_names,
                                     const std::string& group_column)
    : child_(std::move(child)),
      group_column_index_(ResolveColumn(input_names, group_column, "GROUP BY")),
      names_{group_column, kCountStarColumn} {}

AggregateOperator::AggregateOperator(std::unique_ptr<PhysicalOperator> child)
    : child_(std::move(child)), names_{kCountStarColumn} {}

void AggregateOperator::Open() {
    child_->Open();

    groups_.clear();
    position_ = 0;

    if (!group_column_index_.has_value()) {
        // No GROUP BY: one single group covering every record. The key is never
        // read — Next only emits the count when there is no grouping column.
        std::uint64_t total = 0;
        while (child_->Next().has_value()) {
            ++total;
        }
        child_->Close();
        groups_.emplace_back(Value{std::int32_t{0}}, total);
        return;
    }

    // Ordered map so the groups come out sorted by value and the result is
    // deterministic without an ORDER BY.
    std::map<Value, std::uint64_t, ValueLess> counts;
    while (auto record = child_->Next()) {
        ++counts[record->GetValue(*group_column_index_)];
    }
    child_->Close();

    groups_.assign(counts.begin(), counts.end());
}

std::optional<Record> AggregateOperator::Next() {
    if (position_ >= groups_.size()) {
        return Counted(std::nullopt);
    }
    const auto& [key, count] = groups_[position_++];

    // A count cannot overflow an INT here: the smallest record is 12 bytes, so
    // reaching 2^31 rows would need a file of tens of gigabytes.
    const Value counted{static_cast<std::int32_t>(count)};

    std::vector<Value> values;
    if (group_column_index_.has_value()) {
        values.push_back(key);
    }
    values.push_back(counted);
    return Counted(Record(std::move(values)));
}

void AggregateOperator::Close() {
    child_->Close();
    groups_.clear();
    groups_.shrink_to_fit();
    position_ = 0;
}

}  // namespace minidb
