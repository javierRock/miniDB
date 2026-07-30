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
    /// The same plan with the counters each operator gathered while running.
    std::vector<OperatorMetrics> metrics;

    /// What the statement cost. Filled by Database, which is where the clock and
    /// the buffer pool statistics are both in reach.
    ///
    /// `pages_read` is the number the two access paths differ on the most, and
    /// unlike the wall clock it is deterministic: the same query over the same
    /// data always reads the same pages.
    double elapsed_ms = 0.0;
    std::uint64_t pages_read = 0;
    std::uint64_t buffer_hits = 0;
    std::uint64_t buffer_misses = 0;
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
    ExecutionEngine(Catalog& catalog, TableHeap& heap, HashIndex& index)
        : catalog_(catalog), heap_(heap), index_(index) {}

    [[nodiscard]] QueryResult Execute(const Statement& statement);

    /// Turns the index off, so a query that could use it falls back to a
    /// sequential scan.
    ///
    /// This exists for the evaluation the course asks for: comparing the same
    /// query with and without an index is only honest if the data, the plan
    /// above the scan and the buffer pool are otherwise identical, and a switch
    /// here is the smallest change that achieves that. It is not a mode the
    /// system is meant to run in.
    void SetIndexEnabled(bool enabled) { index_enabled_ = enabled; }
    [[nodiscard]] bool IndexEnabled() const { return index_enabled_; }

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
    ///
    /// It also describes the plan into `result`, so a statement that modifies
    /// rows reports which access path it used to find them, exactly as a SELECT
    /// does.
    [[nodiscard]] std::vector<RecordId> CollectMatchingRecordIds(
        const std::optional<Condition>& where, QueryResult& result) const;

    /// True when the condition is `<primary key> = <literal>`, the only shape a
    /// hash index can answer.
    [[nodiscard]] bool CanUseIndex(const std::optional<Condition>& where) const;

    /// Walks the plan through Child() and fills in both descriptions of it: the
    /// operator names and their counters.
    static void DescribePlan(const PhysicalOperator& root, QueryResult& result);

    Catalog& catalog_;
    TableHeap& heap_;
    HashIndex& index_;
    /// Only ever false while measuring; see SetIndexEnabled.
    bool index_enabled_ = true;
};

}  // namespace minidb
