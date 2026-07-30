#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "minidb/database/database.hpp"
#include "minidb/execution/filter_operator.hpp"
#include "minidb/execution/index_scan_operator.hpp"
#include "minidb/execution/projection_operator.hpp"
#include "minidb/execution/sequential_scan_operator.hpp"
#include "minidb/parser/parser.hpp"
#include "test_helpers.hpp"

namespace minidb {
namespace {

using testing::OperatorStack;
using testing::TempDatabase;

constexpr const char* kCreate =
    "CREATE TABLE students ("
    "  id INT PRIMARY KEY,"
    "  name VARCHAR(50),"
    "  age INT,"
    "  career VARCHAR(50))";

/// A database with the demo table and a few rows already in it.
class QueryTest : public ::testing::Test {
protected:
    QueryTest() : temp_("volcano"), db_(temp_.Path()) {
        (void)db_.Execute(kCreate);
        (void)db_.Execute("INSERT INTO students VALUES (1, 'Ana', 20, 'Ciencia de la Computación')");
        (void)db_.Execute("INSERT INTO students VALUES (2, 'Luis', 22, 'Ingeniería de Sistemas')");
        (void)db_.Execute("INSERT INTO students VALUES (9, 'María', 19, 'Ingeniería de Software')");
    }

    [[nodiscard]] std::set<std::int32_t> IdsOf(const QueryResult& result) {
        std::set<std::int32_t> ids;
        for (const Record& row : result.rows) {
            ids.insert(std::get<std::int32_t>(row.GetValue(0)));
        }
        return ids;
    }

    TempDatabase temp_;
    Database db_;
};

// --- The Volcano interface itself ----------------------------------------

TEST(VolcanoOperatorTest, NextYieldsOneRecordAtATimeAndThenNullopt) {
    TempDatabase temp("nextone");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "C");
    stack.Add(2, "Luis", 22, "I");
    stack.Add(3, "María", 19, "S");

    SequentialScanOperator scan(stack.Heap());
    scan.Open();

    int produced = 0;
    while (scan.Next().has_value()) {
        ++produced;
        ASSERT_LE(produced, 3) << "el operador no terminó nunca";
    }
    EXPECT_EQ(produced, 3);

    // Exhaustion is stable: asking again keeps returning nullopt.
    EXPECT_FALSE(scan.Next().has_value());
    EXPECT_FALSE(scan.Next().has_value());
    scan.Close();
}

TEST(VolcanoOperatorTest, OpenRewindsTheOperatorForReuse) {
    TempDatabase temp("reopen");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "C");
    stack.Add(2, "Luis", 22, "I");

    SequentialScanOperator scan(stack.Heap());

    scan.Open();
    EXPECT_TRUE(scan.Next().has_value());
    EXPECT_TRUE(scan.Next().has_value());
    EXPECT_FALSE(scan.Next().has_value());
    scan.Close();

    scan.Open();
    EXPECT_TRUE(scan.Next().has_value());
    scan.Close();
}

TEST(VolcanoOperatorTest, NextBeforeOpenIsReportedRatherThanUndefined) {
    TempDatabase temp("noopen");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "C");

    SequentialScanOperator scan(stack.Heap());
    EXPECT_THROW((void)scan.Next(), StorageError);
}

TEST(VolcanoOperatorTest, CloseIsSafeToCallTwice) {
    TempDatabase temp("doubleclose");
    OperatorStack stack(temp.Path());
    stack.Add(1, "Ana", 20, "C");

    SequentialScanOperator scan(stack.Heap());
    scan.Open();
    scan.Close();
    EXPECT_NO_THROW(scan.Close());
}

TEST(VolcanoOperatorTest, FilterPullsFromItsChildAndKeepsOnlyMatches) {
    TempDatabase temp("filterchild");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 20; ++i) {
        stack.Add(i, "N", 18 + i, "C");
    }

    auto scan = std::make_unique<SequentialScanOperator>(stack.Heap());
    FilterOperator filter(std::move(scan), stack.GetSchema(),
                          Condition{"age", CompareOperator::kGreaterEqual, Value{std::int32_t{30}}});

    filter.Open();
    int matched = 0;
    while (auto record = filter.Next()) {
        EXPECT_GE(std::get<std::int32_t>(record->GetValue(2)), 30);
        ++matched;
    }
    filter.Close();

    EXPECT_EQ(matched, 8);  // ages 30..37
    EXPECT_EQ(filter.Child()->Name(), "SequentialScanOperator");
}

TEST(VolcanoOperatorTest, IndexScanFetchesThroughTheIndexAndYieldsAtMostOneRow) {
    TempDatabase temp("indexscan");
    OperatorStack stack(temp.Path());
    for (std::int32_t i = 0; i < 50; ++i) {
        stack.Add(i, "Nombre" + std::to_string(i), 20, "C");
    }

    IndexScanOperator scan(stack.Heap(), stack.Index(), 33);
    scan.Open();

    const auto record = scan.Next();
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(std::get<std::int32_t>(record->GetValue(0)), 33);
    EXPECT_EQ(ValueToString(record->GetValue(1)), "Nombre33");
    EXPECT_EQ(scan.LastRecordId(), stack.Index().Search(33).value());

    EXPECT_FALSE(scan.Next().has_value());  // the primary key is unique
    scan.Close();

    IndexScanOperator missing(stack.Heap(), stack.Index(), 9999);
    missing.Open();
    EXPECT_FALSE(missing.Next().has_value());
    missing.Close();
}

TEST(VolcanoOperatorTest, ProjectionForwardsRecordIdFromItsChild) {
    TempDatabase temp("projectrid");
    OperatorStack stack(temp.Path());
    stack.Add(7, "Ana", 20, "C");

    auto scan = std::make_unique<IndexScanOperator>(stack.Heap(), stack.Index(), 7);
    ProjectionOperator projection(std::move(scan), stack.GetSchema(),
                                  std::vector<std::string>{"name"});

    projection.Open();
    const auto record = projection.Next();
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->Size(), 1u);
    EXPECT_EQ(ValueToString(record->GetValue(0)), "Ana");
    EXPECT_EQ(projection.LastRecordId(), stack.Index().Search(7).value());
    projection.Close();
}

// An operator tree must not hold pages open between calls, or a deep plan
// would exhaust a small pool.
TEST(VolcanoOperatorTest, ScanningHoldsNoPinsBetweenCalls) {
    TempDatabase temp("nopins");
    OperatorStack stack(temp.Path(), /*frames=*/3);
    for (std::int32_t i = 0; i < 200; ++i) {
        stack.Add(i, "Estudiante " + std::to_string(i), 20, "Ingeniería");
    }

    SequentialScanOperator scan(stack.Heap());
    scan.Open();
    while (scan.Next().has_value()) {
        EXPECT_TRUE(stack.Pool().AllPagesUnpinned())
            << "el escaneo dejó una página fijada entre llamadas a Next";
    }
    scan.Close();
}

TEST_F(QueryTest, ProjectionSitsAtTheTopOfEverySelectPlan) {
    const QueryResult result = db_.Execute("SELECT * FROM students");
    ASSERT_FALSE(result.plan.empty());
    EXPECT_EQ(result.plan.front(), "ProjectionOperator");
}

// --- Plan selection ------------------------------------------------------

TEST_F(QueryTest, SelectWithoutWhereUsesASequentialScan) {
    const QueryResult result = db_.Execute("SELECT * FROM students");

    EXPECT_EQ(result.plan, (std::vector<std::string>{"ProjectionOperator",
                                                     "SequentialScanOperator"}));
    EXPECT_EQ(result.rows.size(), 3u);
}

TEST_F(QueryTest, EqualityOnThePrimaryKeyUsesTheIndex) {
    const QueryResult result = db_.Execute("SELECT * FROM students WHERE id = 1");

    EXPECT_EQ(result.plan, (std::vector<std::string>{"ProjectionOperator", "IndexScanOperator"}));
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(ValueToString(result.rows[0].GetValue(1)), "Ana");
}

TEST_F(QueryTest, FilterOnANonIndexedColumnUsesScanPlusFilter) {
    const QueryResult result = db_.Execute("SELECT * FROM students WHERE age >= 20");

    EXPECT_EQ(result.plan, (std::vector<std::string>{"ProjectionOperator", "FilterOperator",
                                                     "SequentialScanOperator"}));
    EXPECT_EQ(IdsOf(result), (std::set<std::int32_t>{1, 2}));
}

// A hash index cannot answer a range query, so the planner must not reach for
// it just because the column happens to be the primary key.
TEST_F(QueryTest, RangeOnThePrimaryKeyFallsBackToScanPlusFilter) {
    const QueryResult result = db_.Execute("SELECT * FROM students WHERE id > 1");

    EXPECT_EQ(result.plan, (std::vector<std::string>{"ProjectionOperator", "FilterOperator",
                                                     "SequentialScanOperator"}));
    EXPECT_EQ(IdsOf(result), (std::set<std::int32_t>{2, 9}));
}

TEST_F(QueryTest, InequalityOnThePrimaryKeyAlsoFallsBack) {
    const QueryResult result = db_.Execute("SELECT * FROM students WHERE id != 1");

    EXPECT_EQ(result.plan.size(), 3u);
    EXPECT_EQ(result.plan[1], "FilterOperator");
    EXPECT_EQ(IdsOf(result), (std::set<std::int32_t>{2, 9}));
}

TEST_F(QueryTest, IndexScanOnAMissingKeyReturnsNoRows) {
    const QueryResult result = db_.Execute("SELECT * FROM students WHERE id = 12345");

    EXPECT_EQ(result.plan[1], "IndexScanOperator");
    EXPECT_TRUE(result.rows.empty());
    EXPECT_EQ(result.affected_rows, 0u);
}

// --- Projection ----------------------------------------------------------

TEST_F(QueryTest, ProjectionSelectsAndOrdersColumns) {
    const QueryResult result = db_.Execute("SELECT name, id FROM students WHERE id = 2");

    ASSERT_EQ(result.rows.size(), 1u);
    ASSERT_EQ(result.rows[0].Size(), 2u);
    EXPECT_EQ(result.column_names, (std::vector<std::string>{"name", "id"}));
    EXPECT_EQ(ValueToString(result.rows[0].GetValue(0)), "Luis");
    EXPECT_EQ(std::get<std::int32_t>(result.rows[0].GetValue(1)), 2);
}

TEST_F(QueryTest, SelectStarReturnsEveryColumnInSchemaOrder) {
    const QueryResult result = db_.Execute("SELECT * FROM students WHERE id = 1");

    EXPECT_EQ(result.column_names, (std::vector<std::string>{"id", "name", "age", "career"}));
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(result.rows[0].Size(), 4u);
}

TEST_F(QueryTest, RejectsUnknownColumnsInProjectionAndFilter) {
    EXPECT_THROW((void)db_.Execute("SELECT inexistente FROM students"), QueryError);
    EXPECT_THROW((void)db_.Execute("SELECT * FROM students WHERE inexistente = 1"), QueryError);
}

TEST_F(QueryTest, RejectsComparingAColumnAgainstTheWrongType) {
    EXPECT_THROW((void)db_.Execute("SELECT * FROM students WHERE age = 'veinte'"), QueryError);
    EXPECT_THROW((void)db_.Execute("SELECT * FROM students WHERE name = 5"), QueryError);
}

TEST_F(QueryTest, FilterOnAStringColumnWorks) {
    const QueryResult result =
        db_.Execute("SELECT * FROM students WHERE career = 'Ingeniería de Sistemas'");

    EXPECT_EQ(IdsOf(result), (std::set<std::int32_t>{2}));
}

// --- Streaming, not materialising ----------------------------------------

// A table much larger than the buffer pool must still scan correctly, which
// only works if operators pull one record at a time.
TEST(VolcanoScaleTest, ScanStreamsATableLargerThanTheBufferPool) {
    TempDatabase temp("streaming");
    Database db(temp.Path(), /*buffer_pool_frames=*/4);
    (void)db.Execute(kCreate);

    for (std::int32_t i = 0; i < 500; ++i) {
        (void)db.Execute("INSERT INTO students VALUES (" + std::to_string(i) +
                         ", 'Estudiante " + std::to_string(i) + "', " +
                         std::to_string(18 + i % 10) + ", 'Ingeniería')");
    }

    const QueryResult all = db.Execute("SELECT * FROM students");
    EXPECT_EQ(all.rows.size(), 500u);

    const QueryResult filtered = db.Execute("SELECT * FROM students WHERE age >= 25");
    EXPECT_EQ(filtered.rows.size(), 150u);  // ages 25..27 -> 3 of every 10

    EXPECT_GT(db.Pool().Statistics().evictions, 0u);
    EXPECT_TRUE(db.Pool().AllPagesUnpinned());
}

}  // namespace
}  // namespace minidb
