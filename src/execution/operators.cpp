#include <algorithm>
#include <cctype>
#include <utility>

#include "minidb/execution/filter_operator.hpp"
#include "minidb/execution/index_scan_operator.hpp"
#include "minidb/execution/projection_operator.hpp"
#include "minidb/execution/sequential_scan_operator.hpp"

namespace minidb {

// --- SequentialScanOperator ----------------------------------------------

void SequentialScanOperator::Open() {
    iterator_ = heap_.Begin();
    last_rid_ = RecordId{};
}

std::optional<Record> SequentialScanOperator::Next() {
    if (!iterator_.has_value()) {
        throw StorageError("SequentialScanOperator::Next llamado antes de Open");
    }

    auto row = iterator_->Next();
    if (!row.has_value()) {
        return Counted(std::nullopt);
    }
    last_rid_ = row->first;
    return Counted(std::move(row->second));
}

void SequentialScanOperator::Close() {
    iterator_.reset();
    last_rid_ = RecordId{};
}

// --- IndexScanOperator ---------------------------------------------------

void IndexScanOperator::Open() {
    consumed_ = false;
    last_rid_ = RecordId{};
}

std::optional<Record> IndexScanOperator::Next() {
    // The primary key is unique, so this operator yields at most one record.
    if (consumed_) {
        return Counted(std::nullopt);
    }
    consumed_ = true;

    const std::optional<RecordId> rid = index_.Search(key_);
    if (!rid.has_value()) {
        return Counted(std::nullopt);
    }

    std::optional<Record> record = heap_.GetRecord(*rid);
    if (!record.has_value()) {
        // The index pointed at a slot that no longer holds a record. That means
        // a delete or a relocation failed to update the index, so it is a
        // corruption of an invariant rather than an empty result.
        throw StorageError("El índice apunta a un registro inexistente en la página " +
                           std::to_string(rid->page_id) + ", slot " +
                           std::to_string(rid->slot_id));
    }

    last_rid_ = *rid;
    return Counted(std::move(record));
}

void IndexScanOperator::Close() {
    consumed_ = true;
    last_rid_ = RecordId{};
}

// --- FilterOperator ------------------------------------------------------

FilterOperator::FilterOperator(std::unique_ptr<PhysicalOperator> child, const Schema& schema,
                               Condition condition)
    : child_(std::move(child)), condition_(std::move(condition)) {
    const auto index = schema.FindColumn(condition_.column);
    if (!index.has_value()) {
        throw QueryError("No existe la columna '" + condition_.column + "' en la tabla");
    }
    column_index_ = *index;

    // Comparing an INT against a string (or the other way round) can never be
    // meaningful, so it is rejected while building the plan rather than once
    // per record.
    const Column& column = schema.GetColumn(column_index_);
    const bool value_is_integer = std::holds_alternative<std::int32_t>(condition_.value);
    if ((column.type == ColumnType::kInteger) != value_is_integer) {
        throw QueryError("La columna '" + column.name + "' es " +
                         (column.type == ColumnType::kInteger ? "INT" : "VARCHAR") +
                         " y se comparó con un valor de otro tipo");
    }
}

void FilterOperator::Open() { child_->Open(); }

std::optional<Record> FilterOperator::Next() {
    // Pull until something matches; the child streams, so nothing accumulates.
    while (auto record = child_->Next()) {
        if (CompareValues(record->GetValue(column_index_), condition_.op, condition_.value)) {
            return Counted(std::move(record));
        }
    }
    return Counted(std::nullopt);
}

void FilterOperator::Close() { child_->Close(); }

// --- ProjectionOperator --------------------------------------------------

namespace {

/// Case-insensitive lookup of a column name among the child's output names.
/// Matches Schema::FindColumn, so `SELECT NAME` and `SELECT name` behave alike.
[[nodiscard]] std::optional<std::size_t> FindName(const std::vector<std::string>& names,
                                                  const std::string& wanted) {
    const auto equal_ignoring_case = [](const std::string& left, const std::string& right) {
        return std::ranges::equal(left, right, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    };

    for (std::size_t i = 0; i < names.size(); ++i) {
        if (equal_ignoring_case(names[i], wanted)) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::string> SchemaColumnNames(const Schema& schema) {
    std::vector<std::string> names;
    names.reserve(schema.ColumnCount());
    for (const Column& column : schema.Columns()) {
        names.push_back(column.name);
    }
    return names;
}

}  // namespace

ProjectionOperator::ProjectionOperator(std::unique_ptr<PhysicalOperator> child,
                                       const std::vector<std::string>& input_names,
                                       const std::vector<std::string>& columns)
    : child_(std::move(child)) {
    if (columns.empty()) {
        // SELECT *: forward records untouched.
        identity_ = true;
        names_ = input_names;
        return;
    }

    for (const std::string& name : columns) {
        const auto index = FindName(input_names, name);
        if (!index.has_value()) {
            throw QueryError("No existe la columna '" + name + "' en el resultado");
        }
        indices_.push_back(*index);
        names_.push_back(input_names[*index]);
    }
}

ProjectionOperator::ProjectionOperator(std::unique_ptr<PhysicalOperator> child,
                                       const Schema& schema,
                                       const std::vector<std::string>& columns)
    : ProjectionOperator(std::move(child), SchemaColumnNames(schema), columns) {}

void ProjectionOperator::Open() { child_->Open(); }

std::optional<Record> ProjectionOperator::Next() {
    auto record = child_->Next();
    if (!record.has_value() || identity_) {
        return Counted(std::move(record));
    }

    std::vector<Value> projected;
    projected.reserve(indices_.size());
    for (std::size_t index : indices_) {
        projected.push_back(record->GetValue(index));
    }
    return Counted(Record(std::move(projected)));
}

void ProjectionOperator::Close() { child_->Close(); }

}  // namespace minidb
