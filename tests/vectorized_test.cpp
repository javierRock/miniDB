// The vectorized execution path.
//
// The property that matters most is not speed, it is *equivalence*: a batched
// plan and a tuple-at-a-time plan must return exactly the same rows in the same
// order. A faster engine that answers differently is worthless, so that is what
// most of these tests check. The savings are then asserted with the counters,
// which are deterministic, rather than with the clock, which is not.

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "minidb/database/database.hpp"
#include "minidb/execution/filter_operator.hpp"
#include "minidb/execution/sequential_scan_operator.hpp"
#include "minidb/execution/vectorized_filter_operator.hpp"
#include "minidb/execution/vectorized_scan_operator.hpp"
#include "test_helpers.hpp"

namespace minidb {
namespace {

using testing::OperatorStack;
using testing::TempDatabase;

[[nodiscard]] std::vector<Record> Drain(PhysicalOperator& op) {
    std::vector<Record> rows;
    op.Open();
    while (auto record = op.Next()) {
        rows.push_back(std::move(*record));
    }
    op.Close();
    return rows;
}

[[nodiscard]] std::vector<std::int32_t> IntColumn(const std::vector<Record>& rows,
                                                 std::size_t index) {
    std::vector<std::int32_t> values;
    for (const Record& row : rows) {
        values.push_back(std::get<std::int32_t>(row.GetValue(index)));
    }
    return values;
}

[[nodiscard]] Condition Where(const std::string& column, CompareOperator op, Value value) {
    return Condition{column, op, std::move(value)};
}

// --- RecordBatch ---------------------------------------------------------

TEST(RecordBatchTest, EveryAppendedRecordStartsSelected) {
    RecordBatch batch;
    batch.Append(RecordId{7, 1}, Record({std::int32_t{10}, std::string("a")}));
    batch.Append(RecordId{7, 2}, Record({std::int32_t{20}, std::string("b")}));

    EXPECT_EQ(batch.Size(), 2u);
    EXPECT_EQ(batch.SelectedCount(), 2u);
    EXPECT_EQ(batch.Selection(), (std::vector<std::uint16_t>{0, 1}));
    EXPECT_EQ(batch.RecordIdAt(1), (RecordId{7, 2}));
}

/// The selection vector narrows what is visible without moving any record: that
/// is what makes a filter over a batch nearly free.
TEST(RecordBatchTest, SelectionNarrowsTheViewWithoutMovingRecords) {
    RecordBatch batch;
    for (std::int32_t i = 0; i < 5; ++i) {
        batch.Append(RecordId{1, static_cast<SlotId>(i)}, Record({i, std::string("x")}));
    }

    batch.SetSelection({1, 3});
    EXPECT_EQ(batch.Size(), 5u) << "los registros siguen ahí";
    EXPECT_EQ(batch.SelectedCount(), 2u);
    // The records stayed at their original positions.
    EXPECT_EQ(std::get<std::int32_t>(batch.RecordAt(3).GetValue(0)), 3);
}

TEST(RecordBatchTest, RejectsASelectionOutsideTheBatch) {
    RecordBatch batch;
    batch.Append(RecordId{1, 0}, Record({std::int32_t{1}, std::string("x")}));

    EXPECT_THROW(batch.SetSelection({0, 5}), StorageError);
    EXPECT_THROW((void)batch.RecordAt(9), StorageError);
}

TEST(RecordBatchTest, ExtractsAnIntColumnOfTheSelectedRecords) {
    RecordBatch batch;
    for (std::int32_t i = 0; i < 4; ++i) {
        batch.Append(RecordId{1, static_cast<SlotId>(i)}, Record({i * 10, std::string("x")}));
    }
    batch.SetSelection({0, 2});

    std::vector<std::int32_t> values;
    batch.ExtractInt32Column(0, values);
    EXPECT_EQ(values, (std::vector<std::int32_t>{0, 20}));
}

TEST(RecordBatchTest, ClearEmptiesTheBatch) {
    RecordBatch batch;
    batch.Append(RecordId{1, 0}, Record({std::int32_t{1}, std::string("x")}));
    batch.Clear();

    EXPECT_EQ(batch.Size(), 0u);
    EXPECT_TRUE(batch.Empty());
}

// --- The two adapters between the models ---------------------------------

/// A tuple-at-a-time operator asked for a batch: the default adapter turns its
/// Next() into batches, which is what lets any operator join a vectorized plan.
TEST(BatchAdapterTest, ATupleAtATimeOperatorCanServeBatches) {
    TempDatabase temp("adapter_up");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 30; ++i) {
        stack.Add(i, "nombre", 20, "carrera");
    }

    SequentialScanOperator scan(stack.Heap());
    RecordBatch batch;
    scan.Open();
    ASSERT_TRUE(scan.NextBatch(batch));
    EXPECT_EQ(batch.SelectedCount(), 30u);
    EXPECT_FALSE(scan.NextBatch(batch)) << "una segunda llamada agota la entrada";
    scan.Close();
}

/// And the other way round: a vectorized operator asked for one record at a time
/// serves it out of the batch it is holding.
TEST(BatchAdapterTest, AVectorizedOperatorCanServeSingleRecords) {
    TempDatabase temp("adapter_down");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 30; ++i) {
        stack.Add(i, "nombre", 20, "carrera");
    }

    VectorizedScanOperator scan(stack.Heap());
    EXPECT_EQ(Drain(scan).size(), 30u);
}

TEST(BatchAdapterTest, OpenRewindsAVectorizedOperator) {
    TempDatabase temp("adapter_reopen");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "C");
    stack.Add(2, "Luis", 22, "I");

    VectorizedScanOperator scan(stack.Heap());
    const std::vector<std::int32_t> first = IntColumn(Drain(scan), 0);
    const std::vector<std::int32_t> second = IntColumn(Drain(scan), 0);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, (std::vector<std::int32_t>{1, 2}));
}

// --- VectorizedScanOperator ----------------------------------------------

/// The reason the batched scan is cheaper: the tuple-at-a-time iterator pins its
/// page again for every record, this one pins each page once.
TEST(VectorizedScanTest, FetchesEachPageOnceInsteadOfOncePerRecord) {
    TempDatabase temp("scan_pages");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 400; ++i) {
        stack.Add(i, "nombre" + std::to_string(i), 20, "carrera");
    }

    const BufferPoolStatistics before = stack.Pool().Statistics();
    SequentialScanOperator tuple_scan(stack.Heap());
    const std::size_t tuple_rows = Drain(tuple_scan).size();
    const BufferPoolStatistics after_tuple = stack.Pool().Statistics();

    VectorizedScanOperator batch_scan(stack.Heap());
    const std::size_t batch_rows = Drain(batch_scan).size();
    const BufferPoolStatistics after_batch = stack.Pool().Statistics();

    ASSERT_EQ(tuple_rows, 400u);
    ASSERT_EQ(batch_rows, 400u);

    const std::uint64_t tuple_accesses = (after_tuple.hits + after_tuple.misses) -
                                         (before.hits + before.misses);
    const std::uint64_t batch_accesses = (after_batch.hits + after_batch.misses) -
                                         (after_tuple.hits + after_tuple.misses);

    EXPECT_GT(tuple_accesses, 400u) << "el escaneo tupla a tupla fija una página por registro";
    EXPECT_LT(batch_accesses, 50u) << "el escaneo por lotes fija una página por página";
    EXPECT_EQ(batch_scan.PagesRead(), stack.Heap().PageCountInChain());
}

TEST(VectorizedScanTest, ReturnsTheSameRecordsAsTheTupleAtATimeScan) {
    TempDatabase temp("scan_equivalence");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 250; ++i) {
        stack.Add(i, "nombre" + std::to_string(i), 20 + (i % 7), "carrera");
    }

    SequentialScanOperator tuple_scan(stack.Heap());
    VectorizedScanOperator batch_scan(stack.Heap());
    EXPECT_EQ(Drain(tuple_scan), Drain(batch_scan));
}

TEST(VectorizedScanTest, HandlesAnEmptyTable) {
    TempDatabase temp("scan_empty");
    OperatorStack stack(temp.Path());

    VectorizedScanOperator scan(stack.Heap());
    EXPECT_TRUE(Drain(scan).empty());
}

/// More records than fit in one batch, so the operator has to produce several.
TEST(VectorizedScanTest, ProducesSeveralBatchesForALargeTable) {
    TempDatabase temp("scan_many_batches");
    OperatorStack stack(temp.Path());
    const std::int32_t rows = static_cast<std::int32_t>(RecordBatch::kTargetSize) * 2 + 100;
    for (std::int32_t i = 0; i < rows; ++i) {
        stack.Add(i, "n", 20, "c");
    }

    VectorizedScanOperator scan(stack.Heap());
    RecordBatch batch;
    scan.Open();

    std::size_t batches = 0;
    std::size_t total = 0;
    while (scan.NextBatch(batch)) {
        ++batches;
        total += batch.SelectedCount();
        ASSERT_LE(batches, 100u) << "el operador no terminó nunca";
    }
    scan.Close();

    EXPECT_EQ(total, static_cast<std::size_t>(rows));
    // More than one batch, but not necessarily one per kTargetSize records: a
    // batch stops at the page boundary that crosses the target, so it overshoots
    // by up to one page's worth of records.
    EXPECT_GE(batches, 2u);
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned());
}

// --- VectorizedFilterOperator --------------------------------------------

/// Every comparison operator, checked against the tuple-at-a-time filter on the
/// same data. This is the test that would catch an off-by-one in the SIMD loop.
TEST(VectorizedFilterTest, MatchesTheTupleAtATimeFilterForEveryOperator) {
    TempDatabase temp("filter_equivalence");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 300; ++i) {
        stack.Add(i, "nombre" + std::to_string(i), 20 + (i % 10), "carrera");
    }

    const CompareOperator operators[] = {
        CompareOperator::kEqual,   CompareOperator::kNotEqual, CompareOperator::kLess,
        CompareOperator::kLessEqual, CompareOperator::kGreater, CompareOperator::kGreaterEqual};

    for (CompareOperator op : operators) {
        const Condition condition = Where("age", op, std::int32_t{25});

        FilterOperator tuple_filter(std::make_unique<SequentialScanOperator>(stack.Heap()),
                                    stack.GetSchema(), condition);
        VectorizedFilterOperator batch_filter(
            std::make_unique<VectorizedScanOperator>(stack.Heap()), stack.GetSchema(), condition);

        const std::vector<Record> expected = Drain(tuple_filter);
        const std::vector<Record> actual = Drain(batch_filter);
        EXPECT_EQ(expected, actual) << "operador de comparación " << static_cast<int>(op);
        EXPECT_FALSE(expected.empty()) << "el caso de prueba no filtra nada";
    }
}

/// A VARCHAR predicate cannot be vectorized — the values are variable-length and
/// scattered — so it falls back to comparing record by record inside the batch.
/// It still has to give the same answer.
TEST(VectorizedFilterTest, FallsBackToRecordAtATimeForVarcharColumns) {
    TempDatabase temp("filter_varchar");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "Computación");
    stack.Add(2, "Luis", 22, "Sistemas");
    stack.Add(3, "María", 19, "Computación");

    const Condition condition = Where("career", CompareOperator::kEqual, std::string("Computación"));
    VectorizedFilterOperator filter(std::make_unique<VectorizedScanOperator>(stack.Heap()),
                                    stack.GetSchema(), condition);
    EXPECT_FALSE(filter.UsesVectorizedComparison());

    const std::vector<Record> rows = Drain(filter);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(IntColumn(rows, 0), (std::vector<std::int32_t>{1, 3}));
}

TEST(VectorizedFilterTest, ReportsHowManyRecordsThePredicateSaw) {
    TempDatabase temp("filter_evaluated");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 200; ++i) {
        stack.Add(i, "n", 20 + (i % 4), "c");
    }

    VectorizedFilterOperator filter(std::make_unique<VectorizedScanOperator>(stack.Heap()),
                                    stack.GetSchema(),
                                    Where("age", CompareOperator::kEqual, std::int32_t{21}));
    const std::vector<Record> rows = Drain(filter);

    EXPECT_TRUE(filter.UsesVectorizedComparison());
    EXPECT_EQ(filter.RowsEvaluated(), 200u) << "el predicado se evaluó sobre toda la tabla";
    EXPECT_EQ(rows.size(), 50u);
}

/// A batch where nothing matches is not the end of the input. Getting this wrong
/// would silently truncate results whenever a whole batch was rejected.
TEST(VectorizedFilterTest, ABatchWithNoMatchesDoesNotEndTheScan) {
    TempDatabase temp("filter_empty_batch");
    OperatorStack stack(temp.Path());

    // Only the very last rows match, so the first batches come back empty.
    const std::int32_t rows = static_cast<std::int32_t>(RecordBatch::kTargetSize) * 2 + 50;
    for (std::int32_t i = 0; i < rows; ++i) {
        stack.Add(i, "n", i >= rows - 10 ? 99 : 20, "c");
    }

    VectorizedFilterOperator filter(std::make_unique<VectorizedScanOperator>(stack.Heap()),
                                    stack.GetSchema(),
                                    Where("age", CompareOperator::kEqual, std::int32_t{99}));
    EXPECT_EQ(Drain(filter).size(), 10u);
}

TEST(VectorizedFilterTest, RejectsAMissingColumnAndAMismatchedType) {
    TempDatabase temp("filter_errors");
    OperatorStack stack(temp.Path());

    EXPECT_THROW(VectorizedFilterOperator(std::make_unique<VectorizedScanOperator>(stack.Heap()),
                                          stack.GetSchema(),
                                          Where("inexistente", CompareOperator::kEqual,
                                                std::int32_t{1})),
                 QueryError);
    EXPECT_THROW(VectorizedFilterOperator(std::make_unique<VectorizedScanOperator>(stack.Heap()),
                                          stack.GetSchema(),
                                          Where("age", CompareOperator::kEqual,
                                                std::string("veinte"))),
                 QueryError);
}

TEST(VectorizedFilterTest, ReleasesEveryPage) {
    TempDatabase temp("filter_pins");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 300; ++i) {
        stack.Add(i, "n", 20, "c");
    }

    VectorizedFilterOperator filter(std::make_unique<VectorizedScanOperator>(stack.Heap()),
                                    stack.GetSchema(),
                                    Where("age", CompareOperator::kEqual, std::int32_t{20}));
    EXPECT_EQ(Drain(filter).size(), 300u);
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned());
}

// --- Through SQL ---------------------------------------------------------

class VectorizedSqlTest : public ::testing::Test {
protected:
    VectorizedSqlTest() : temp_("vectorized_sql"), db_(temp_.Path()) {
        (void)db_.Execute(
            "CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(50), age INT,"
            " career VARCHAR(50))");
        for (std::int32_t i = 1; i <= 400; ++i) {
            (void)db_.Execute("INSERT INTO students VALUES (" + std::to_string(i) + ", 'nombre" +
                              std::to_string(i) + "', " + std::to_string(18 + (i % 12)) +
                              ", 'carrera" + std::to_string(i % 3) + "')");
        }
    }

    TempDatabase temp_;
    Database db_;
};

TEST_F(VectorizedSqlTest, OffByDefaultSoTheVolcanoPathIsTheReference) {
    EXPECT_FALSE(db_.VectorizedEnabled());
    EXPECT_EQ(db_.Execute("SELECT * FROM students WHERE age = 20").plan,
              (std::vector<std::string>{"ProjectionOperator", "FilterOperator",
                                        "SequentialScanOperator"}));
}

TEST_F(VectorizedSqlTest, EnablingItSwapsTheScanAndTheFilter) {
    db_.SetVectorizedEnabled(true);
    EXPECT_TRUE(db_.VectorizedEnabled());

    EXPECT_EQ(db_.Execute("SELECT * FROM students WHERE age = 20").plan,
              (std::vector<std::string>{"ProjectionOperator", "VectorizedFilterOperator",
                                        "VectorizedScanOperator"}));
    EXPECT_EQ(db_.Execute("SELECT * FROM students").plan,
              (std::vector<std::string>{"ProjectionOperator", "VectorizedScanOperator"}));
}

/// A point lookup has one row to fetch, so there is nothing to batch and the
/// planner leaves the index path alone.
TEST_F(VectorizedSqlTest, ThePrimaryKeyLookupStillUsesTheIndex) {
    db_.SetVectorizedEnabled(true);
    EXPECT_EQ(db_.Execute("SELECT * FROM students WHERE id = 42").plan,
              (std::vector<std::string>{"ProjectionOperator", "IndexScanOperator"}));
}

/// The property the whole feature stands on.
TEST_F(VectorizedSqlTest, BothModelsReturnExactlyTheSameRows) {
    const char* queries[] = {
        "SELECT * FROM students",
        "SELECT * FROM students WHERE age = 20",
        "SELECT * FROM students WHERE age >= 25",
        "SELECT * FROM students WHERE age < 0",
        "SELECT name, age FROM students WHERE age != 20",
        "SELECT * FROM students WHERE career = 'carrera1'",
        "SELECT * FROM students ORDER BY age DESC",
        "SELECT career, COUNT(*) FROM students WHERE age >= 20 GROUP BY career",
        "SELECT COUNT(*) FROM students",
    };

    for (const char* sql : queries) {
        db_.SetVectorizedEnabled(false);
        const QueryResult tuple_at_a_time = db_.Execute(sql);
        db_.SetVectorizedEnabled(true);
        const QueryResult vectorized = db_.Execute(sql);

        EXPECT_EQ(tuple_at_a_time.rows, vectorized.rows) << sql;
        EXPECT_EQ(tuple_at_a_time.column_names, vectorized.column_names) << sql;
        EXPECT_EQ(tuple_at_a_time.affected_rows, vectorized.affected_rows) << sql;
    }
}

/// The blocking operators know nothing about batches, yet a vectorized scan
/// underneath still works: that is what the two adapters are for.
TEST_F(VectorizedSqlTest, BlockingOperatorsSitOnTopOfAVectorizedScan) {
    db_.SetVectorizedEnabled(true);
    EXPECT_EQ(db_.Execute("SELECT career, COUNT(*) FROM students GROUP BY career").plan,
              (std::vector<std::string>{"ProjectionOperator", "AggregateOperator",
                                        "VectorizedScanOperator"}));
    EXPECT_EQ(db_.Execute("SELECT * FROM students ORDER BY age").plan,
              (std::vector<std::string>{"ProjectionOperator", "SortOperator",
                                        "VectorizedScanOperator"}));
}

/// UPDATE and DELETE locate their rows with a plan too, so they work batched as
/// well — and must keep the index consistent while doing it.
TEST_F(VectorizedSqlTest, UpdateAndDeleteWorkOnTheVectorizedPath) {
    db_.SetVectorizedEnabled(true);

    const QueryResult updated = db_.Execute("UPDATE students SET age = 99 WHERE age = 20");
    EXPECT_EQ(updated.plan, (std::vector<std::string>{"VectorizedFilterOperator",
                                                      "VectorizedScanOperator"}));
    EXPECT_GT(updated.affected_rows, 0u);

    const QueryResult deleted = db_.Execute("DELETE FROM students WHERE age = 99");
    EXPECT_EQ(deleted.affected_rows, updated.affected_rows);

    // The index must agree with the table afterwards.
    db_.SetVectorizedEnabled(false);
    EXPECT_EQ(db_.Execute("SELECT COUNT(*) FROM students").rows.size(), 1u);
    EXPECT_EQ(db_.GetCatalog().RecordCount(), db_.Execute("SELECT * FROM students").rows.size());
}

/// The saving, stated as counters rather than as a stopwatch.
TEST_F(VectorizedSqlTest, BatchingCollapsesNextCallsAndBufferPoolAccesses) {
    const char* sql = "SELECT * FROM students WHERE age < 0";

    db_.SetVectorizedEnabled(false);
    const QueryResult tuple_at_a_time = db_.Execute(sql);
    db_.SetVectorizedEnabled(true);
    const QueryResult vectorized = db_.Execute(sql);

    ASSERT_TRUE(tuple_at_a_time.rows.empty());
    ASSERT_TRUE(vectorized.rows.empty());

    std::uint64_t tuple_calls = 0;
    std::uint64_t vector_calls = 0;
    std::uint64_t vector_batches = 0;
    for (const OperatorMetrics& op : tuple_at_a_time.metrics) {
        tuple_calls += op.next_calls;
    }
    for (const OperatorMetrics& op : vectorized.metrics) {
        vector_calls += op.next_calls;
        vector_batches += op.batches_produced;
    }

    // 400 records plus the call that discovers the end, at the scan. The filter
    // above it is called only once because it drains its child internally before
    // reporting that nothing matched.
    EXPECT_GT(tuple_calls, 400u) << "una llamada por registro en el escaneo";
    EXPECT_LT(vector_calls, 20u) << "una llamada por lote";
    EXPECT_GT(vector_batches, 0u);

    const std::uint64_t tuple_accesses =
        tuple_at_a_time.buffer_hits + tuple_at_a_time.buffer_misses;
    const std::uint64_t vector_accesses = vectorized.buffer_hits + vectorized.buffer_misses;
    EXPECT_LT(vector_accesses * 10, tuple_accesses)
        << "fijar una página por página en lugar de por registro";
}

TEST_F(VectorizedSqlTest, TheSettingSurvivesCreateTable) {
    TempDatabase fresh("vectorized_create");
    Database db(fresh.Path(), kDefaultBufferPoolFrames, /*vectorized=*/true);
    ASSERT_TRUE(db.VectorizedEnabled());

    // CREATE TABLE builds a new execution engine; the setting must carry over.
    (void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, age INT)");
    (void)db.Execute("INSERT INTO t VALUES (1, 20)");
    EXPECT_EQ(db.Execute("SELECT * FROM t WHERE age = 20").plan,
              (std::vector<std::string>{"ProjectionOperator", "VectorizedFilterOperator",
                                        "VectorizedScanOperator"}));
}

TEST(VectorizedConfigTest, TheConfigFileCanTurnItOn) {
    TempDatabase temp("vectorized_config");
    const std::filesystem::path config_path =
        temp.Path().parent_path() / "minidb.conf";

    std::ofstream(config_path) << "vectorized=true\n";
    EXPECT_TRUE(DatabaseConfig::Load(config_path).vectorized);

    std::ofstream(config_path) << "vectorized=false\n";
    EXPECT_FALSE(DatabaseConfig::Load(config_path).vectorized);

    std::ofstream(config_path) << "vectorized=quizás\n";
    EXPECT_THROW((void)DatabaseConfig::Load(config_path), QueryError);
}

}  // namespace
}  // namespace minidb
