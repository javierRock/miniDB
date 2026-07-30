// What the system reports about its own cost: per-operator counters, elapsed
// time, and pages read. These are the numbers the evaluation section of the
// report is built from, so they get their own test file.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "minidb/database/database.hpp"
#include "test_helpers.hpp"

namespace minidb {
namespace {

using testing::TempDatabase;

constexpr const char* kCreate =
    "CREATE TABLE students ("
    "  id INT PRIMARY KEY,"
    "  name VARCHAR(50),"
    "  age INT,"
    "  career VARCHAR(50))";

/// A table big enough to span many pages, so a sequential scan really does cost
/// more than an index lookup instead of everything fitting in the buffer pool.
class MeasuredDatabase {
public:
    /// Six frames is the floor the design allows: the costliest statement pins
    /// six pages at once. Tests that want eviction ask for that minimum and then
    /// only need a handful of table pages to exceed it.
    MeasuredDatabase(const std::filesystem::path& path, std::int32_t rows,
                     std::size_t buffer_pool_frames = 8)
        : db_(path, buffer_pool_frames) {
        (void)db_.Execute(kCreate);
        for (std::int32_t i = 1; i <= rows; ++i) {
            (void)db_.Execute("INSERT INTO students VALUES (" + std::to_string(i) + ", 'nombre" +
                              std::to_string(i) + "', " + std::to_string(20 + (i % 40)) +
                              ", 'carrera" + std::to_string(i % 3) + "')");
        }
    }

    Database& Db() { return db_; }

private:
    Database db_;
};

/// Finds an operator in the reported plan by name.
[[nodiscard]] const OperatorMetrics* Find(const QueryResult& result, const std::string& name) {
    for (const OperatorMetrics& op : result.metrics) {
        if (op.name == name) {
            return &op;
        }
    }
    return nullptr;
}

// --- Elapsed time --------------------------------------------------------

/// Only that time is measured at all: asserting an upper bound would make the
/// suite fail on a loaded machine rather than on a real defect.
TEST(MeasurementTest, EveryStatementReportsAPositiveElapsedTime) {
    TempDatabase temp("elapsed");
    MeasuredDatabase fixture(temp.Path(), 20);

    for (const char* sql : {"SELECT * FROM students", "SELECT * FROM students WHERE id = 7",
                            "SELECT career, COUNT(*) FROM students GROUP BY career",
                            "UPDATE students SET age = 30 WHERE id = 7",
                            "DELETE FROM students WHERE id = 7"}) {
        const QueryResult result = fixture.Db().Execute(sql);
        EXPECT_GT(result.elapsed_ms, 0.0) << sql;
    }
}

// --- Per-operator counters ----------------------------------------------

TEST(MeasurementTest, PlanAndMetricsDescribeTheSameOperators) {
    TempDatabase temp("plan_metrics");
    MeasuredDatabase fixture(temp.Path(), 20);

    const QueryResult result = fixture.Db().Execute("SELECT * FROM students WHERE age >= 20");

    ASSERT_EQ(result.plan.size(), result.metrics.size());
    for (std::size_t i = 0; i < result.plan.size(); ++i) {
        EXPECT_EQ(result.plan[i], result.metrics[i].name);
    }
}

/// A scan is pulled once per record plus once more for the end of input, and a
/// filter reports only what passed. The gap between the two is the work the
/// filter did for nothing, which is exactly what an index avoids.
TEST(MeasurementTest, CountersShowHowManyRecordsTheFilterDiscarded) {
    TempDatabase temp("counters");
    MeasuredDatabase fixture(temp.Path(), 300);

    const QueryResult result = fixture.Db().Execute("SELECT * FROM students WHERE id = 42");
    ASSERT_EQ(result.rows.size(), 1u);

    const QueryResult scanned = fixture.Db().Execute("SELECT * FROM students WHERE id >= 42");
    const OperatorMetrics* scan = Find(scanned, "SequentialScanOperator");
    const OperatorMetrics* filter = Find(scanned, "FilterOperator");
    ASSERT_NE(scan, nullptr);
    ASSERT_NE(filter, nullptr);

    EXPECT_EQ(scan->rows_produced, 300u);
    EXPECT_EQ(scan->next_calls, 301u);  // one extra call to learn it ended
    EXPECT_EQ(filter->rows_produced, scanned.rows.size());
    EXPECT_EQ(scan->rows_produced - filter->rows_produced, 300u - scanned.rows.size());
}

TEST(MeasurementTest, IndexScanTouchesOneRecordInsteadOfTheWholeTable) {
    TempDatabase temp("index_counters");
    MeasuredDatabase fixture(temp.Path(), 300);

    const QueryResult result = fixture.Db().Execute("SELECT * FROM students WHERE id = 42");

    const OperatorMetrics* scan = Find(result, "IndexScanOperator");
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->rows_produced, 1u);
    EXPECT_EQ(scan->next_calls, 2u);
    EXPECT_EQ(Find(result, "SequentialScanOperator"), nullptr);
}

/// A blocking operator consumes its whole input inside Open, so its child's
/// counters cover the table while its own cover only the groups it emitted.
TEST(MeasurementTest, BlockingOperatorsReportTheirOwnOutputNotTheirInput) {
    TempDatabase temp("blocking_counters");
    MeasuredDatabase fixture(temp.Path(), 90);

    const QueryResult result =
        fixture.Db().Execute("SELECT career, COUNT(*) FROM students GROUP BY career");

    const OperatorMetrics* aggregate = Find(result, "AggregateOperator");
    const OperatorMetrics* scan = Find(result, "SequentialScanOperator");
    ASSERT_NE(aggregate, nullptr);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->rows_produced, 90u);
    EXPECT_EQ(aggregate->rows_produced, 3u);
}

/// UPDATE and DELETE are not operators, but they do use a plan to find their
/// rows, and they report which one they used.
TEST(MeasurementTest, UpdateAndDeleteReportThePlanTheyUsedToFindRows) {
    TempDatabase temp("modify_plan");
    MeasuredDatabase fixture(temp.Path(), 20);

    const QueryResult updated = fixture.Db().Execute("UPDATE students SET age = 31 WHERE id = 3");
    EXPECT_EQ(updated.plan, (std::vector<std::string>{"IndexScanOperator"}));

    const QueryResult deleted = fixture.Db().Execute("DELETE FROM students WHERE age = 31");
    EXPECT_EQ(deleted.plan,
              (std::vector<std::string>{"FilterOperator", "SequentialScanOperator"}));
}

// --- Pages read ----------------------------------------------------------

/// The measurement the two access paths really differ on, and the one that does
/// not depend on how busy the machine is.
TEST(MeasurementTest, PageReadsAreReportedPerStatement) {
    TempDatabase temp("pages");
    MeasuredDatabase fixture(temp.Path(), 1000, /*buffer_pool_frames=*/6);
    ASSERT_GT(fixture.Db().TablePageCount(), fixture.Db().Pool().FrameCount())
        << "la tabla debe superar el pool para que el disco intervenga";

    // A fresh scan of a table larger than the pool has to reach the disk.
    const QueryResult scanned = fixture.Db().Execute("SELECT * FROM students WHERE age = 25");
    EXPECT_GT(scanned.pages_read, 0u);
    EXPECT_GT(scanned.buffer_misses, 0u);

    // Reading the same single record twice in a row: the second time its pages
    // are already resident, so the statement costs no disk reads at all.
    (void)fixture.Db().Execute("SELECT * FROM students WHERE id = 1");
    const QueryResult again = fixture.Db().Execute("SELECT * FROM students WHERE id = 1");
    EXPECT_EQ(again.pages_read, 0u);
    EXPECT_GT(again.buffer_hits, 0u);
}

// --- With and without the index ------------------------------------------

TEST(EvaluationTest, DisablingTheIndexChangesThePlanForTheSameQuery) {
    TempDatabase temp("index_switch");
    MeasuredDatabase fixture(temp.Path(), 50);

    EXPECT_TRUE(fixture.Db().IndexEnabled());
    const QueryResult with = fixture.Db().Execute("SELECT * FROM students WHERE id = 17");
    EXPECT_EQ(with.plan, (std::vector<std::string>{"ProjectionOperator", "IndexScanOperator"}));

    fixture.Db().SetIndexEnabled(false);
    EXPECT_FALSE(fixture.Db().IndexEnabled());
    const QueryResult without = fixture.Db().Execute("SELECT * FROM students WHERE id = 17");
    EXPECT_EQ(without.plan, (std::vector<std::string>{"ProjectionOperator", "FilterOperator",
                                                      "SequentialScanOperator"}));

    fixture.Db().SetIndexEnabled(true);
    EXPECT_EQ(fixture.Db().Execute("SELECT * FROM students WHERE id = 17").plan[1],
              "IndexScanOperator");
}

/// The assertion the whole comparison rests on: the two access paths are being
/// measured against each other only because they answer the same question.
TEST(EvaluationTest, BothAccessPathsReturnTheSameRows) {
    TempDatabase temp("same_rows");
    MeasuredDatabase fixture(temp.Path(), 200);

    for (std::int32_t key : {1, 57, 199, 200, 9999}) {
        const std::string sql = "SELECT * FROM students WHERE id = " + std::to_string(key);

        fixture.Db().SetIndexEnabled(true);
        const QueryResult with = fixture.Db().Execute(sql);
        fixture.Db().SetIndexEnabled(false);
        const QueryResult without = fixture.Db().Execute(sql);
        fixture.Db().SetIndexEnabled(true);

        EXPECT_EQ(with.rows, without.rows) << sql;
        EXPECT_EQ(with.affected_rows, without.affected_rows) << sql;
    }
}

/// Records examined is where the index wins structurally: it reads the one row
/// it was asked for, while the scan reads every row in the table.
TEST(EvaluationTest, WithoutTheIndexEveryRecordIsExamined) {
    TempDatabase temp("examined");
    MeasuredDatabase fixture(temp.Path(), 200);
    const std::string sql = "SELECT * FROM students WHERE id = 100";

    const QueryResult with = fixture.Db().Execute(sql);
    fixture.Db().SetIndexEnabled(false);
    const QueryResult without = fixture.Db().Execute(sql);
    fixture.Db().SetIndexEnabled(true);

    const OperatorMetrics* index_scan = Find(with, "IndexScanOperator");
    const OperatorMetrics* sequential_scan = Find(without, "SequentialScanOperator");
    ASSERT_NE(index_scan, nullptr);
    ASSERT_NE(sequential_scan, nullptr);

    EXPECT_EQ(index_scan->rows_produced, 1u);
    EXPECT_EQ(sequential_scan->rows_produced, 200u);
}

/// On a table larger than the buffer pool, the scan also has to reach the disk
/// far more often. This is deterministic, unlike the elapsed time.
TEST(EvaluationTest, WithoutTheIndexMorePagesAreReadFromDisk) {
    TempDatabase temp("pages_compared");
    MeasuredDatabase fixture(temp.Path(), 1000, /*buffer_pool_frames=*/6);
    ASSERT_GT(fixture.Db().TablePageCount(), fixture.Db().Pool().FrameCount());

    const std::string sql = "SELECT * FROM students WHERE id = 900";

    // Ten lookups per round, so a single warm cache cannot decide the outcome.
    std::uint64_t with_index = 0;
    std::uint64_t without_index = 0;
    for (int i = 0; i < 10; ++i) {
        fixture.Db().SetIndexEnabled(true);
        with_index += fixture.Db().Execute(sql).pages_read;
        fixture.Db().SetIndexEnabled(false);
        without_index += fixture.Db().Execute(sql).pages_read;
    }
    fixture.Db().SetIndexEnabled(true);

    EXPECT_GT(without_index, with_index);
}

/// Switching the index off must not corrupt anything: it only changes how rows
/// are found, so writes still keep the table and the index consistent.
TEST(EvaluationTest, ModifyingWithTheIndexDisabledKeepsItConsistent) {
    TempDatabase temp("modify_without_index");
    MeasuredDatabase fixture(temp.Path(), 30);

    fixture.Db().SetIndexEnabled(false);
    (void)fixture.Db().Execute("UPDATE students SET age = 77 WHERE id = 11");
    (void)fixture.Db().Execute("DELETE FROM students WHERE id = 12");
    fixture.Db().SetIndexEnabled(true);

    // The index must still find the updated row and must no longer find the
    // deleted one, even though neither statement went through it.
    const QueryResult updated = fixture.Db().Execute("SELECT age FROM students WHERE id = 11");
    ASSERT_EQ(updated.plan[1], "IndexScanOperator");
    ASSERT_EQ(updated.rows.size(), 1u);
    EXPECT_EQ(std::get<std::int32_t>(updated.rows[0].GetValue(0)), 77);

    const QueryResult deleted = fixture.Db().Execute("SELECT * FROM students WHERE id = 12");
    ASSERT_EQ(deleted.plan[1], "IndexScanOperator");
    EXPECT_TRUE(deleted.rows.empty());
}

}  // namespace
}  // namespace minidb
