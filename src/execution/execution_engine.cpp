#include "minidb/execution/execution_engine.hpp"

#include <utility>

#include "minidb/execution/filter_operator.hpp"
#include "minidb/execution/index_scan_operator.hpp"
#include "minidb/execution/projection_operator.hpp"
#include "minidb/execution/sequential_scan_operator.hpp"

namespace minidb {
namespace {

/// Spanish plural agreement for the result messages: "1 fila insertada." but
/// "3 filas insertadas."
std::string RowsMessage(std::uint64_t count, const std::string& participle) {
    return std::to_string(count) + (count == 1 ? " fila " : " filas ") + participle +
           (count == 1 ? "" : "s") + ".";
}

}  // namespace

bool ExecutionEngine::CanUseIndex(const std::optional<Condition>& where) const {
    if (!where.has_value() || where->op != CompareOperator::kEqual) {
        return false;
    }
    const Schema& schema = catalog_.GetSchema();
    const auto column = schema.FindColumn(where->column);

    // A hash index answers exactly one question: which record has this key.
    // Ranges and non-key columns fall back to a scan.
    return column.has_value() && *column == schema.PrimaryKeyIndex() &&
           std::holds_alternative<std::int32_t>(where->value);
}

std::unique_ptr<PhysicalOperator> ExecutionEngine::BuildScan(
    const std::optional<Condition>& where) const {
    if (CanUseIndex(where)) {
        return std::make_unique<IndexScanOperator>(heap_, index_,
                                                   std::get<std::int32_t>(where->value));
    }

    auto scan = std::make_unique<SequentialScanOperator>(heap_);
    if (!where.has_value()) {
        return scan;
    }
    return std::make_unique<FilterOperator>(std::move(scan), catalog_.GetSchema(), *where);
}

std::unique_ptr<PhysicalOperator> ExecutionEngine::BuildPlan(const SelectStatement& select) const {
    return std::make_unique<ProjectionOperator>(BuildScan(select.where), catalog_.GetSchema(),
                                                select.columns);
}

std::vector<std::string> ExecutionEngine::DescribePlan(const PhysicalOperator& root) {
    std::vector<std::string> names;
    for (const PhysicalOperator* op = &root; op != nullptr; op = op->Child()) {
        names.push_back(op->Name());
    }
    return names;
}

std::vector<RecordId> ExecutionEngine::CollectMatchingRecordIds(
    const std::optional<Condition>& where) const {
    std::unique_ptr<PhysicalOperator> plan = BuildScan(where);
    std::vector<RecordId> rids;

    plan->Open();
    while (plan->Next().has_value()) {
        rids.push_back(plan->LastRecordId());
    }
    plan->Close();
    // The plan is finished and every page it touched is released before the
    // caller starts modifying anything.
    return rids;
}

QueryResult ExecutionEngine::ExecuteInsert(const InsertStatement& statement) {
    catalog_.RequireTable(statement.table_name);
    const Schema& schema = catalog_.GetSchema();

    Record record(statement.values);
    record.Validate(schema);

    const std::int32_t key = std::get<std::int32_t>(record.GetValue(schema.PrimaryKeyIndex()));

    // Check the index before touching the table, so a duplicate leaves the
    // table exactly as it was.
    if (index_.Contains(key)) {
        throw QueryError("Ya existe un registro con la clave primaria " + std::to_string(key));
    }

    const RecordId rid = heap_.InsertRecord(record);
    try {
        index_.Insert(key, rid);
    } catch (...) {
        // The only place the system compensates: without this the table would
        // keep a record the index cannot find. A general rollback mechanism
        // would need a write-ahead log, which is out of scope.
        heap_.DeleteRecord(rid);
        throw;
    }
    catalog_.IncrementRecordCount();

    QueryResult result;
    result.affected_rows = 1;
    result.message = RowsMessage(1, "insertada");
    return result;
}

QueryResult ExecutionEngine::ExecuteSelect(const SelectStatement& statement) {
    catalog_.RequireTable(statement.table_name);

    std::unique_ptr<PhysicalOperator> plan = BuildPlan(statement);
    QueryResult result;
    result.plan = DescribePlan(*plan);
    result.column_names =
        static_cast<ProjectionOperator&>(*plan).OutputColumnNames();

    plan->Open();
    while (auto record = plan->Next()) {
        result.rows.push_back(std::move(*record));
    }
    plan->Close();

    result.affected_rows = result.rows.size();
    result.message = RowsMessage(result.rows.size(), "devuelta");
    return result;
}

QueryResult ExecutionEngine::ExecuteUpdate(const UpdateStatement& statement) {
    catalog_.RequireTable(statement.table_name);
    const Schema& schema = catalog_.GetSchema();

    // Resolve the assignments once, before touching anything.
    std::vector<std::pair<std::size_t, Value>> assignments;
    for (const Assignment& assignment : statement.assignments) {
        const auto index = schema.FindColumn(assignment.column);
        if (!index.has_value()) {
            throw QueryError("No existe la columna '" + assignment.column + "' en la tabla");
        }
        if (*index == schema.PrimaryKeyIndex()) {
            throw QueryError("No se permite actualizar la clave primaria '" + assignment.column +
                             "'");
        }
        assignments.emplace_back(*index, assignment.value);
    }

    // Phase one: find the rows using the query pipeline, then let it go.
    const std::vector<RecordId> targets = CollectMatchingRecordIds(statement.where);

    // Phase two: modify, with no scan open over the pages being rewritten.
    std::uint64_t updated = 0;
    for (RecordId rid : targets) {
        auto record = heap_.GetRecord(rid);
        if (!record.has_value()) {
            continue;  // vanished between the phases; nothing to do
        }

        for (const auto& [index, value] : assignments) {
            record->SetValue(index, value);
        }
        record->Validate(schema);

        const RecordId moved = heap_.UpdateRecord(rid, *record);
        if (moved != rid) {
            // The record no longer fits where it was. The index must follow it,
            // or its entry would point at a slot that is now free.
            const std::int32_t key =
                std::get<std::int32_t>(record->GetValue(schema.PrimaryKeyIndex()));
            index_.UpdateRecordId(key, moved);
        }
        ++updated;
    }

    QueryResult result;
    result.affected_rows = updated;
    result.message = RowsMessage(updated, "actualizada");
    return result;
}

QueryResult ExecutionEngine::ExecuteDelete(const DeleteStatement& statement) {
    catalog_.RequireTable(statement.table_name);
    const Schema& schema = catalog_.GetSchema();

    const std::vector<RecordId> targets = CollectMatchingRecordIds(statement.where);

    std::uint64_t deleted = 0;
    for (RecordId rid : targets) {
        auto record = heap_.GetRecord(rid);
        if (!record.has_value()) {
            continue;
        }
        // Read the key before the record disappears.
        const std::int32_t key =
            std::get<std::int32_t>(record->GetValue(schema.PrimaryKeyIndex()));

        if (heap_.DeleteRecord(rid)) {
            index_.Remove(key);
            catalog_.DecrementRecordCount();
            ++deleted;
        }
    }

    QueryResult result;
    result.affected_rows = deleted;
    result.message = RowsMessage(deleted, "eliminada");
    return result;
}

QueryResult ExecutionEngine::Execute(const Statement& statement) {
    return std::visit(
        [this](const auto& concrete) -> QueryResult {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, InsertStatement>) {
                return ExecuteInsert(concrete);
            } else if constexpr (std::is_same_v<T, SelectStatement>) {
                return ExecuteSelect(concrete);
            } else if constexpr (std::is_same_v<T, UpdateStatement>) {
                return ExecuteUpdate(concrete);
            } else if constexpr (std::is_same_v<T, DeleteStatement>) {
                return ExecuteDelete(concrete);
            } else {
                // CREATE TABLE is handled by Database, which owns the lifetime
                // of the heap and the index this engine holds references to.
                throw StorageError("CREATE TABLE no se ejecuta en el motor de consultas");
            }
        },
        statement);
}

}  // namespace minidb
