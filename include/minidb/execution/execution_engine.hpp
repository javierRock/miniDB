#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minidb/catalog/catalog.hpp"
#include "minidb/execution/physical_operator.hpp"
#include "minidb/index/hash_index.hpp"
#include "minidb/parser/statement.hpp"
#include "minidb/storage/table_heap.hpp"

namespace minidb {

/// What a statement produced: either rows, or a count of affected rows.
struct QueryResult {
    std::vector<std::string> column_names;
    std::vector<Record> rows;
    std::uint64_t affected_rows = 0;
    /// Human-readable summary shown by the CLI.
    std::string message;
    /// Operator names from the root down, so the plan can be shown and tested.
    std::vector<std::string> plan;
};

/// Turns a parsed statement into work against the storage layer.
///
/// SELECT builds a Volcano tree and streams it. UPDATE and DELETE build the
/// same kind of tree to find the rows they affect, but they are not operators
/// themselves: the Volcano model describes iterators that produce tuples, and
/// modelling a statement that produces only a count as one would add classes
/// without adding behaviour.
///
/// They run in two phases: collect the matching RecordIds with the plan, close
/// it, and only then modify. Mutating pages while a scan is still open would
/// invalidate the very offsets the scan is walking.
class ExecutionEngine {
public:
    ExecutionEngine(BufferPoolManager& pool, Catalog& catalog, TableHeap& heap, HashIndex& index)
        : pool_(pool), catalog_(catalog), heap_(heap), index_(index) {}

    [[nodiscard]] QueryResult Execute(const Statement& statement);

    /// Builds the physical plan for a SELECT without running it. Exposed so the
    /// tests can assert which plan was chosen.
    [[nodiscard]] std::unique_ptr<PhysicalOperator> BuildPlan(const SelectStatement& select) const;

private:
    [[nodiscard]] QueryResult ExecuteInsert(const InsertStatement& statement);
    [[nodiscard]] QueryResult ExecuteSelect(const SelectStatement& statement);
    [[nodiscard]] QueryResult ExecuteUpdate(const UpdateStatement& statement);
    [[nodiscard]] QueryResult ExecuteDelete(const DeleteStatement& statement);

    /// Scan half of a plan: an index lookup when the condition is an equality
    /// on the primary key, a sequential scan plus filter otherwise.
    [[nodiscard]] std::unique_ptr<PhysicalOperator> BuildScan(
        const std::optional<Condition>& where) const;

    /// Runs a plan to completion and returns the location of every record it
    /// produced. This is phase one of UPDATE and DELETE.
    [[nodiscard]] std::vector<RecordId> CollectMatchingRecordIds(
        const std::optional<Condition>& where) const;

    /// True when the condition is `<primary key> = <literal>`, the only shape a
    /// hash index can answer.
    [[nodiscard]] bool CanUseIndex(const std::optional<Condition>& where) const;

    /// Operator names from the root down, walked through Child().
    [[nodiscard]] static std::vector<std::string> DescribePlan(const PhysicalOperator& root);

    BufferPoolManager& pool_;
    Catalog& catalog_;
    TableHeap& heap_;
    HashIndex& index_;
};

}  // namespace minidb
