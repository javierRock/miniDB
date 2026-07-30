#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minidb/database/database.hpp"
#include "minidb/execution/aggregate_operator.hpp"
#include "minidb/execution/sequential_scan_operator.hpp"
#include "minidb/execution/sort_operator.hpp"
#include "test_helpers.hpp"

namespace minidb {
namespace {

using testing::OperatorStack;
using testing::TempDatabase;

const std::vector<std::string> kTableColumns = {"id", "name", "age", "career"};

/// Drives an operator to exhaustion and returns everything it produced.
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

[[nodiscard]] std::vector<std::string> StringColumn(const std::vector<Record>& rows,
                                                    std::size_t index) {
    std::vector<std::string> values;
    for (const Record& row : rows) {
        values.push_back(std::get<std::string>(row.GetValue(index)));
    }
    return values;
}

[[nodiscard]] std::unique_ptr<PhysicalOperator> ScanOf(OperatorStack& stack) {
    return std::make_unique<SequentialScanOperator>(stack.Heap());
}

// --- SortOperator --------------------------------------------------------

TEST(SortOperatorTest, OrdersByAnIntegerColumnAscendingAndDescending) {
    TempDatabase temp("sort_int");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 25, "C");
    stack.Add(2, "Luis", 19, "I");
    stack.Add(3, "María", 22, "S");

    SortOperator ascending(ScanOf(stack), kTableColumns, "age", false);
    EXPECT_EQ(IntColumn(Drain(ascending), 2), (std::vector<std::int32_t>{19, 22, 25}));

    SortOperator descending(ScanOf(stack), kTableColumns, "age", true);
    EXPECT_EQ(IntColumn(Drain(descending), 2), (std::vector<std::int32_t>{25, 22, 19}));
}

/// Strings compare byte by byte, so the ordering is UTF-8 code-unit order.
TEST(SortOperatorTest, OrdersByAVarcharColumn) {
    TempDatabase temp("sort_text");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Zoe", 20, "C");
    stack.Add(2, "Ana", 20, "I");
    stack.Add(3, "Luis", 20, "S");

    SortOperator sort(ScanOf(stack), kTableColumns, "name", false);
    EXPECT_EQ(StringColumn(Drain(sort), 1), (std::vector<std::string>{"Ana", "Luis", "Zoe"}));
}

/// The sort is stable, so rows that tie keep the order the scan produced them
/// in, which for a heap scan is insertion order.
TEST(SortOperatorTest, TiesKeepTheOrderTheChildProducedThemIn) {
    TempDatabase temp("sort_stable");
    OperatorStack stack(temp.Path());
    stack.Add(10, "primero", 20, "C");
    stack.Add(20, "segundo", 20, "C");
    stack.Add(30, "tercero", 20, "C");

    SortOperator sort(ScanOf(stack), kTableColumns, "age", false);
    EXPECT_EQ(IntColumn(Drain(sort), 0), (std::vector<std::int32_t>{10, 20, 30}));
}

TEST(SortOperatorTest, OpenRewindsTheOperatorForReuse) {
    TempDatabase temp("sort_reopen");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 25, "C");
    stack.Add(2, "Luis", 19, "I");

    SortOperator sort(ScanOf(stack), kTableColumns, "age", false);
    const std::vector<std::int32_t> first = IntColumn(Drain(sort), 2);
    const std::vector<std::int32_t> second = IntColumn(Drain(sort), 2);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, (std::vector<std::int32_t>{19, 25}));
}

TEST(SortOperatorTest, EmptyInputProducesNoRows) {
    TempDatabase temp("sort_empty");
    OperatorStack stack(temp.Path());

    SortOperator sort(ScanOf(stack), kTableColumns, "age", false);
    EXPECT_TRUE(Drain(sort).empty());
}

TEST(SortOperatorTest, RejectsAColumnTheChildDoesNotProduce) {
    TempDatabase temp("sort_badcol");
    OperatorStack stack(temp.Path());

    EXPECT_THROW(SortOperator(ScanOf(stack), kTableColumns, "inexistente", false), QueryError);
}

/// Sorting materialises its input, which is exactly when a pin leak would be
/// most likely: the whole table passes through the operator before it answers.
TEST(SortOperatorTest, ReleasesEveryPageAfterMaterialising) {
    TempDatabase temp("sort_pins");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 200; ++i) {
        stack.Add(i, "nombre" + std::to_string(i), 20 + (i % 40), "carrera");
    }

    SortOperator sort(ScanOf(stack), kTableColumns, "age", false);
    const std::vector<Record> rows = Drain(sort);

    EXPECT_EQ(rows.size(), 200u);
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned()) << "el ordenamiento dejó páginas fijadas";
}

// --- AggregateOperator ---------------------------------------------------

TEST(AggregateOperatorTest, CountsRecordsPerGroupInAscendingOrderOfTheGroup) {
    TempDatabase temp("agg_groups");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "Computación");
    stack.Add(2, "Luis", 22, "Sistemas");
    stack.Add(3, "María", 19, "Computación");
    stack.Add(4, "Diego", 25, "Sistemas");
    stack.Add(5, "Elena", 22, "Computación");

    AggregateOperator aggregate(ScanOf(stack), kTableColumns, "career");
    EXPECT_EQ(aggregate.OutputColumnNames(),
              (std::vector<std::string>{"career", "COUNT(*)"}));

    const std::vector<Record> rows = Drain(aggregate);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(StringColumn(rows, 0), (std::vector<std::string>{"Computación", "Sistemas"}));
    EXPECT_EQ(IntColumn(rows, 1), (std::vector<std::int32_t>{3, 2}));
}

TEST(AggregateOperatorTest, WithoutGroupingProducesOneRowWithTheTotal) {
    TempDatabase temp("agg_total");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "C");
    stack.Add(2, "Luis", 22, "I");

    AggregateOperator aggregate(ScanOf(stack));
    EXPECT_EQ(aggregate.OutputColumnNames(), (std::vector<std::string>{"COUNT(*)"}));

    const std::vector<Record> rows = Drain(aggregate);
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_EQ(rows[0].Size(), 1u);
    EXPECT_EQ(std::get<std::int32_t>(rows[0].GetValue(0)), 2);
}

/// COUNT(*) over an empty table is 0, not "no answer"; with GROUP BY there are
/// simply no groups to report.
TEST(AggregateOperatorTest, EmptyInputYieldsZeroWithoutGroupingAndNoRowsWithIt) {
    TempDatabase temp("agg_empty");
    OperatorStack stack(temp.Path());

    AggregateOperator total(ScanOf(stack));
    const std::vector<Record> rows = Drain(total);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::int32_t>(rows[0].GetValue(0)), 0);

    AggregateOperator grouped(ScanOf(stack), kTableColumns, "career");
    EXPECT_TRUE(Drain(grouped).empty());
}

TEST(AggregateOperatorTest, OpenRewindsTheOperatorForReuse) {
    TempDatabase temp("agg_reopen");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "C");
    stack.Add(2, "Luis", 22, "C");

    AggregateOperator aggregate(ScanOf(stack), kTableColumns, "career");
    const std::vector<Record> first = Drain(aggregate);
    const std::vector<Record> second = Drain(aggregate);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first, second);
}

TEST(AggregateOperatorTest, RejectsAGroupColumnTheChildDoesNotProduce) {
    TempDatabase temp("agg_badcol");
    OperatorStack stack(temp.Path());

    EXPECT_THROW(AggregateOperator(ScanOf(stack), kTableColumns, "inexistente"), QueryError);
}

TEST(AggregateOperatorTest, ReleasesEveryPageAfterCounting) {
    TempDatabase temp("agg_pins");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 200; ++i) {
        stack.Add(i, "nombre" + std::to_string(i), 20, "carrera" + std::to_string(i % 3));
    }

    AggregateOperator aggregate(ScanOf(stack), kTableColumns, "career");
    EXPECT_EQ(Drain(aggregate).size(), 3u);
    EXPECT_TRUE(stack.Pool().AllPagesUnpinned()) << "el agregado dejó páginas fijadas";
}

// --- Through SQL, on a database reopened from disk ------------------------

class BlockingSqlTest : public ::testing::Test {
protected:
    BlockingSqlTest() : temp_("blocking_sql") {
        Database db(temp_.Path());
        (void)db.Execute(
            "CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(50), age INT,"
            " career VARCHAR(50))");
        (void)db.Execute("INSERT INTO students VALUES (1, 'Ana', 20, 'Computación')");
        (void)db.Execute("INSERT INTO students VALUES (2, 'Luis', 25, 'Sistemas')");
        (void)db.Execute("INSERT INTO students VALUES (3, 'María', 19, 'Computación')");
        (void)db.Execute("INSERT INTO students VALUES (4, 'Diego', 22, 'Software')");
    }

    TempDatabase temp_;
};

TEST_F(BlockingSqlTest, OrderByRunsOverAReopenedDatabase) {
    Database db(temp_.Path());
    const QueryResult result = db.Execute("SELECT name, age FROM students ORDER BY age DESC");

    EXPECT_EQ(result.plan, (std::vector<std::string>{"ProjectionOperator", "SortOperator",
                                                     "SequentialScanOperator"}));
    ASSERT_EQ(result.rows.size(), 4u);
    EXPECT_EQ(IntColumn(result.rows, 1), (std::vector<std::int32_t>{25, 22, 20, 19}));
}

/// ORDER BY resolves against the columns its child produces, which are the
/// table's — so ordering by a column that is not projected works.
TEST_F(BlockingSqlTest, OrderByAcceptsAColumnThatIsNotProjected) {
    Database db(temp_.Path());
    const QueryResult result = db.Execute("SELECT name FROM students ORDER BY age");

    EXPECT_EQ(result.column_names, (std::vector<std::string>{"name"}));
    EXPECT_EQ(StringColumn(result.rows, 0),
              (std::vector<std::string>{"María", "Ana", "Diego", "Luis"}));
}

TEST_F(BlockingSqlTest, GroupByCountsPerGroup) {
    Database db(temp_.Path());
    const QueryResult result =
        db.Execute("SELECT career, COUNT(*) FROM students GROUP BY career");

    EXPECT_EQ(result.plan, (std::vector<std::string>{"ProjectionOperator", "AggregateOperator",
                                                     "SequentialScanOperator"}));
    EXPECT_EQ(result.column_names, (std::vector<std::string>{"career", "COUNT(*)"}));
    ASSERT_EQ(result.rows.size(), 3u);
    EXPECT_EQ(IntColumn(result.rows, 1), (std::vector<std::int32_t>{2, 1, 1}));
}

TEST_F(BlockingSqlTest, GroupByCombinedWithOrderByPutsSortAboveTheAggregate) {
    Database db(temp_.Path());
    const QueryResult result = db.Execute(
        "SELECT career, COUNT(*) FROM students GROUP BY career ORDER BY COUNT(*) DESC");

    EXPECT_EQ(result.plan,
              (std::vector<std::string>{"ProjectionOperator", "SortOperator", "AggregateOperator",
                                        "SequentialScanOperator"}));
    ASSERT_EQ(result.rows.size(), 3u);
    EXPECT_EQ(std::get<std::int32_t>(result.rows[0].GetValue(1)), 2);
    EXPECT_EQ(std::get<std::string>(result.rows[0].GetValue(0)), "Computación");
}

TEST_F(BlockingSqlTest, CountStarWithoutGroupByCountsTheWholeTable) {
    Database db(temp_.Path());
    const QueryResult result = db.Execute("SELECT COUNT(*) FROM students");

    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::int32_t>(result.rows[0].GetValue(0)), 4);
}

TEST_F(BlockingSqlTest, WhereIsAppliedBeforeGrouping) {
    Database db(temp_.Path());
    const QueryResult result =
        db.Execute("SELECT career, COUNT(*) FROM students WHERE age >= 20 GROUP BY career");

    EXPECT_EQ(result.plan,
              (std::vector<std::string>{"ProjectionOperator", "AggregateOperator", "FilterOperator",
                                        "SequentialScanOperator"}));
    ASSERT_EQ(result.rows.size(), 3u);
    // María (19) is filtered out, so Computación drops from 2 to 1.
    EXPECT_EQ(IntColumn(result.rows, 1), (std::vector<std::int32_t>{1, 1, 1}));
}

/// The aggregate's output is what the projection sees, so asking for a table
/// column that the grouping threw away is an error rather than silent nonsense.
TEST_F(BlockingSqlTest, ProjectingANonGroupedColumnIsRejected) {
    Database db(temp_.Path());
    EXPECT_THROW((void)db.Execute("SELECT name, COUNT(*) FROM students GROUP BY career"),
                 QueryError);
}

TEST_F(BlockingSqlTest, ColumnOrderInTheSelectListIsRespected) {
    Database db(temp_.Path());
    const QueryResult result =
        db.Execute("SELECT COUNT(*), career FROM students GROUP BY career");

    EXPECT_EQ(result.column_names, (std::vector<std::string>{"COUNT(*)", "career"}));
    ASSERT_FALSE(result.rows.empty());
    EXPECT_EQ(std::get<std::int32_t>(result.rows[0].GetValue(0)), 2);
}

/// Statements must leave the pool clean, blocking operators included.
TEST_F(BlockingSqlTest, EveryStatementLeavesThePoolUnpinned) {
    Database db(temp_.Path());
    for (const char* sql : {"SELECT * FROM students ORDER BY name",
                            "SELECT career, COUNT(*) FROM students GROUP BY career",
                            "SELECT COUNT(*) FROM students",
                            "SELECT career, COUNT(*) FROM students GROUP BY career ORDER BY career"}) {
        (void)db.Execute(sql);
        EXPECT_TRUE(db.Pool().AllPagesUnpinned()) << "quedaron páginas fijadas tras: " << sql;
    }
}

}  // namespace
}  // namespace minidb
