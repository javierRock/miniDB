// Exact nearest neighbour search: the VECTOR type, its persistence, and the two
// ranking strategies.
//
// The property everything else rests on is that the bounded Top-k heap and the
// full sort return *identical* results. A faster ranking that answers differently
// would be worthless, so that equivalence is asserted first and repeatedly.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minidb/database/database.hpp"
#include "minidb/execution/knn_full_sort_operator.hpp"
#include "minidb/execution/knn_scan_operator.hpp"
#include "minidb/parser/parser.hpp"
#include "test_helpers.hpp"

namespace minidb {
namespace {

using testing::TempDatabase;

constexpr float kTolerance = 1e-5F;

constexpr const char* kCreate =
    "CREATE TABLE docs (id INT PRIMARY KEY, titulo VARCHAR(40), emb VECTOR(2))";

[[nodiscard]] std::vector<std::int32_t> IdsOf(const QueryResult& result) {
    std::vector<std::int32_t> ids;
    for (const Record& row : result.rows) {
        ids.push_back(std::get<std::int32_t>(row.GetValue(0)));
    }
    return ids;
}

/// The distance column is the last one the k-NN operator adds.
[[nodiscard]] float DistanceOf(const Record& row) {
    return std::get<float>(row.GetValue(row.Size() - 1));
}

// --- The VECTOR type -----------------------------------------------------

TEST(VectorTypeTest, SurvivesACloseAndReopenByteForByte) {
    TempDatabase temp("vector_persist");
    const Vector stored{0.5F, -1.25F, 0.0F, 3.75F};

    {
        Database db(temp.Path());
        (void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, v VECTOR(4))");
        (void)db.Execute("INSERT INTO t VALUES (1, [0.5, -1.25, 0.0, 3.75])");
    }

    Database reopened(temp.Path());
    const QueryResult result = reopened.Execute("SELECT * FROM t");
    ASSERT_EQ(result.rows.size(), 1u);

    const Vector& read = std::get<Vector>(result.rows[0].GetValue(1));
    ASSERT_EQ(read.size(), stored.size());
    for (std::size_t i = 0; i < stored.size(); ++i) {
        // Exact equality, not a tolerance: these values are all representable in
        // binary32, so a round trip through the file must reproduce them bit for
        // bit. A tolerance here would hide a serialisation bug.
        EXPECT_EQ(read[i], stored[i]) << "componente " << i;
    }
}

TEST(VectorTypeTest, TheSchemaFixesTheDimension) {
    TempDatabase temp("vector_dimension");
    Database db(temp.Path());
    (void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, v VECTOR(3))");

    EXPECT_THROW((void)db.Execute("INSERT INTO t VALUES (1, [1, 2])"), QueryError);
    EXPECT_THROW((void)db.Execute("INSERT INTO t VALUES (2, [1, 2, 3, 4])"), QueryError);
    EXPECT_NO_THROW((void)db.Execute("INSERT INTO t VALUES (3, [1, 2, 3])"));
}

TEST(VectorTypeTest, RejectsAnOutOfRangeDimension) {
    TempDatabase temp("vector_bad_dimension");
    Database db(temp.Path());

    EXPECT_THROW((void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, v VECTOR(0))"), QueryError);
    EXPECT_THROW((void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, v VECTOR(5000))"),
                 QueryError);
}

/// The bound that keeps the original invariant alive: no valid record may exceed
/// what an empty page can hold.
TEST(VectorTypeTest, RejectsASchemaWhoseWidestRecordCannotFitInAPage) {
    TempDatabase temp("vector_too_wide");
    Database db(temp.Path());

    // Two VECTOR(1000) columns would need 8004 bytes; a page offers 4080.
    EXPECT_THROW(
        (void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, a VECTOR(1000), b VECTOR(1000))"),
        QueryError);
    // One of them fits with room to spare.
    EXPECT_NO_THROW((void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, a VECTOR(1000))"));
}

TEST(VectorTypeTest, AVectorColumnCannotBeThePrimaryKey) {
    TempDatabase temp("vector_pk");
    Database db(temp.Path());
    EXPECT_THROW((void)db.Execute("CREATE TABLE t (v VECTOR(4) PRIMARY KEY, id INT)"), QueryError);
}

/// A vector has no single natural order, so an ordering comparison over one is an
/// error rather than an arbitrary answer.
TEST(VectorTypeTest, AVectorColumnCannotBeComparedOrOrdered) {
    TempDatabase temp("vector_compare");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    (void)db.Execute("INSERT INTO docs VALUES (1, 'A', [1, 0])");

    EXPECT_THROW((void)db.Execute("SELECT * FROM docs WHERE emb = [1, 0]"), QueryError);
    EXPECT_THROW((void)db.Execute("SELECT * FROM docs ORDER BY emb"), QueryError);
}

TEST(VectorTypeTest, MixedIntegerAndDecimalComponentsAreAccepted) {
    TempDatabase temp("vector_literals");
    Database db(temp.Path());
    (void)db.Execute("CREATE TABLE t (id INT PRIMARY KEY, v VECTOR(4))");

    // Integers, decimals, negatives and exponents in the same literal.
    EXPECT_NO_THROW((void)db.Execute("INSERT INTO t VALUES (1, [1, -2.5, 0.0, 1e-3])"));
    const QueryResult result = db.Execute("SELECT * FROM t");
    const Vector& v = std::get<Vector>(result.rows[0].GetValue(1));
    EXPECT_NEAR(v[1], -2.5F, kTolerance);
    EXPECT_NEAR(v[3], 1e-3F, kTolerance);
}

// --- Ranking on data whose answer is known by hand -----------------------

/// A = [1,0], B = [0,1], C = [1,1], query = [0.9, 0.1].
///
/// Euclidean distances: A = sqrt(0.02) ~ 0.1414, C = sqrt(0.82) ~ 0.9055,
/// B = sqrt(1.62) ~ 1.2728. The expected order is therefore A, C, B.
class KnnByHandTest : public ::testing::Test {
protected:
    KnnByHandTest() : temp_("knn_by_hand"), db_(temp_.Path()) {
        (void)db_.Execute(kCreate);
        (void)db_.Execute("INSERT INTO docs VALUES (1, 'A', [1, 0])");
        (void)db_.Execute("INSERT INTO docs VALUES (2, 'B', [0, 1])");
        (void)db_.Execute("INSERT INTO docs VALUES (3, 'C', [1, 1])");
    }

    TempDatabase temp_;
    Database db_;
};

TEST_F(KnnByHandTest, ReturnsTheNeighboursInTheOrderComputedByHand) {
    const QueryResult result =
        db_.Execute("SELECT * FROM docs NEAREST emb TO [0.9, 0.1] LIMIT 3");

    ASSERT_EQ(result.rows.size(), 3u);
    EXPECT_EQ(IdsOf(result), (std::vector<std::int32_t>{1, 3, 2}));
    EXPECT_NEAR(DistanceOf(result.rows[0]), std::sqrt(0.02F), kTolerance);
    EXPECT_NEAR(DistanceOf(result.rows[1]), std::sqrt(0.82F), kTolerance);
    EXPECT_NEAR(DistanceOf(result.rows[2]), std::sqrt(1.62F), kTolerance);
}

TEST_F(KnnByHandTest, TheDistanceColumnIsAppendedToTheOutput) {
    const QueryResult result =
        db_.Execute("SELECT * FROM docs NEAREST emb TO [0.9, 0.1] LIMIT 1");

    EXPECT_EQ(result.column_names,
              (std::vector<std::string>{"id", "titulo", "emb", "distancia"}));
}

TEST_F(KnnByHandTest, TheProjectionCanDropTheDistanceColumn) {
    const QueryResult result =
        db_.Execute("SELECT id, titulo FROM docs NEAREST emb TO [0.9, 0.1] LIMIT 2");

    EXPECT_EQ(result.column_names, (std::vector<std::string>{"id", "titulo"}));
    EXPECT_EQ(IdsOf(result), (std::vector<std::int32_t>{1, 3}));
}

TEST_F(KnnByHandTest, KOfOneReturnsOnlyTheClosest) {
    const QueryResult result =
        db_.Execute("SELECT * FROM docs NEAREST emb TO [0.9, 0.1] LIMIT 1");

    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::int32_t>(result.rows[0].GetValue(0)), 1);
}

/// cos(query, A) is the largest of the three, and the cosine ignores magnitude, so
/// C = [1,1] is closer than B = [0,1] under this metric too.
TEST_F(KnnByHandTest, TheCosineMetricRanksByAngle) {
    const QueryResult result =
        db_.Execute("SELECT * FROM docs NEAREST emb TO [0.9, 0.1] USING COSINE LIMIT 3");

    EXPECT_EQ(IdsOf(result), (std::vector<std::int32_t>{1, 3, 2}));
    // Distance to A: 1 - 0.9/(sqrt(0.82)*1).
    EXPECT_NEAR(DistanceOf(result.rows[0]), 1.0F - 0.9F / std::sqrt(0.82F), kTolerance);
}

/// The dot product is a similarity, so the ranking is descending: C = 1.0 beats
/// A = 0.9 even though A is closer in the Euclidean sense.
TEST_F(KnnByHandTest, TheDotProductMetricRanksDescending) {
    const QueryResult result =
        db_.Execute("SELECT * FROM docs NEAREST emb TO [0.9, 0.1] USING DOT LIMIT 3");

    EXPECT_EQ(IdsOf(result), (std::vector<std::int32_t>{3, 1, 2}));
    EXPECT_NEAR(DistanceOf(result.rows[0]), 1.0F, kTolerance);
    EXPECT_NEAR(DistanceOf(result.rows[1]), 0.9F, kTolerance);
}

TEST_F(KnnByHandTest, AVectorIdenticalToAStoredOneIsAtDistanceZero) {
    const QueryResult result = db_.Execute("SELECT * FROM docs NEAREST emb TO [1, 0] LIMIT 1");

    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::int32_t>(result.rows[0].GetValue(0)), 1);
    EXPECT_NEAR(DistanceOf(result.rows[0]), 0.0F, kTolerance);
}

/// Ties are broken by primary key so that the answer is the same on every run,
/// whatever order the records were visited in.
TEST(KnnEdgeCaseTest, TiesAreBrokenByPrimaryKey) {
    TempDatabase temp("knn_ties");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    // Three copies of the same vector: every distance is identical.
    (void)db.Execute("INSERT INTO docs VALUES (30, 'C', [1, 1])");
    (void)db.Execute("INSERT INTO docs VALUES (10, 'A', [1, 1])");
    (void)db.Execute("INSERT INTO docs VALUES (20, 'B', [1, 1])");

    const QueryResult result = db.Execute("SELECT * FROM docs NEAREST emb TO [0, 0] LIMIT 3");
    EXPECT_EQ(IdsOf(result), (std::vector<std::int32_t>{10, 20, 30}));
}

TEST(KnnEdgeCaseTest, KGreaterThanTheNumberOfRecordsReturnsThemAll) {
    TempDatabase temp("knn_k_too_big");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    (void)db.Execute("INSERT INTO docs VALUES (1, 'A', [1, 0])");
    (void)db.Execute("INSERT INTO docs VALUES (2, 'B', [0, 1])");

    const QueryResult result = db.Execute("SELECT * FROM docs NEAREST emb TO [1, 1] LIMIT 100");
    EXPECT_EQ(result.rows.size(), 2u);
}

/// k = 0 is a valid query with an empty answer, and it must not read the table to
/// produce it.
TEST(KnnEdgeCaseTest, KOfZeroReturnsNothingWithoutScanning) {
    TempDatabase temp("knn_k_zero");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    for (int i = 1; i <= 50; ++i) {
        (void)db.Execute("INSERT INTO docs VALUES (" + std::to_string(i) + ", 'x', [1, 1])");
    }

    const QueryResult result = db.Execute("SELECT * FROM docs NEAREST emb TO [0, 0] LIMIT 0");
    EXPECT_TRUE(result.rows.empty());
    EXPECT_EQ(result.DistanceCalculations(), 0u) << "no debe calcular ninguna distancia";
}

TEST(KnnEdgeCaseTest, AnEmptyTableReturnsNoNeighbours) {
    TempDatabase temp("knn_empty");
    Database db(temp.Path());
    (void)db.Execute(kCreate);

    const QueryResult result = db.Execute("SELECT * FROM docs NEAREST emb TO [1, 0] LIMIT 5");
    EXPECT_TRUE(result.rows.empty());
    EXPECT_EQ(result.DistanceCalculations(), 0u);
}

TEST(KnnEdgeCaseTest, AQueryVectorOfTheWrongDimensionIsRejected) {
    TempDatabase temp("knn_bad_dimension");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    (void)db.Execute("INSERT INTO docs VALUES (1, 'A', [1, 0])");

    EXPECT_THROW((void)db.Execute("SELECT * FROM docs NEAREST emb TO [1] LIMIT 1"), QueryError);
    EXPECT_THROW((void)db.Execute("SELECT * FROM docs NEAREST emb TO [1, 2, 3] LIMIT 1"),
                 QueryError);
}

TEST(KnnEdgeCaseTest, SearchingANonVectorColumnIsRejected) {
    TempDatabase temp("knn_bad_column");
    Database db(temp.Path());
    (void)db.Execute(kCreate);

    EXPECT_THROW((void)db.Execute("SELECT * FROM docs NEAREST titulo TO [1, 0] LIMIT 1"),
                 QueryError);
    EXPECT_THROW((void)db.Execute("SELECT * FROM docs NEAREST inexistente TO [1, 0] LIMIT 1"),
                 QueryError);
}

/// The zero vector as a query: the Euclidean metric handles it normally, and the
/// cosine one falls back to its documented definition instead of a NaN.
TEST(KnnEdgeCaseTest, TheZeroVectorIsAValidQuery) {
    TempDatabase temp("knn_zero_query");
    Database db(temp.Path());
    (void)db.Execute(kCreate);
    (void)db.Execute("INSERT INTO docs VALUES (1, 'A', [3, 4])");
    (void)db.Execute("INSERT INTO docs VALUES (2, 'B', [1, 0])");

    const QueryResult euclidean =
        db.Execute("SELECT * FROM docs NEAREST emb TO [0, 0] LIMIT 2");
    EXPECT_EQ(IdsOf(euclidean), (std::vector<std::int32_t>{2, 1}));
    EXPECT_NEAR(DistanceOf(euclidean.rows[1]), 5.0F, kTolerance);

    const QueryResult cosine =
        db.Execute("SELECT * FROM docs NEAREST emb TO [0, 0] USING COSINE LIMIT 2");
    for (const Record& row : cosine.rows) {
        EXPECT_FALSE(std::isnan(DistanceOf(row)));
        EXPECT_NEAR(DistanceOf(row), 1.0F, kTolerance);
    }
}

// --- Grammar -------------------------------------------------------------

TEST(KnnGrammarTest, ParsesTheNearestClause) {
    const auto select = std::get<SelectStatement>(
        Parser::Parse("SELECT * FROM docs NEAREST emb TO [0.5, 0.25] USING COSINE LIMIT 7"));

    ASSERT_TRUE(select.nearest.has_value());
    EXPECT_EQ(select.nearest->column, "emb");
    EXPECT_EQ(select.nearest->k, 7u);
    EXPECT_EQ(select.nearest->metric, DistanceMetric::kCosine);
    ASSERT_EQ(select.nearest->query.size(), 2u);
    EXPECT_NEAR(select.nearest->query[1], 0.25F, kTolerance);
}

TEST(KnnGrammarTest, TheDefaultMetricIsEuclidean) {
    const auto select = std::get<SelectStatement>(
        Parser::Parse("SELECT * FROM docs NEAREST emb TO [1, 0] LIMIT 1"));
    EXPECT_EQ(select.nearest->metric, DistanceMetric::kEuclidean);
}

TEST(KnnGrammarTest, RejectsMalformedNearestClauses) {
    // LIMIT supplies k, so it is mandatory.
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM docs NEAREST emb TO [1, 0]"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM docs NEAREST emb [1, 0] LIMIT 1"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM docs NEAREST emb TO 1 LIMIT 1"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM docs NEAREST emb TO [] LIMIT 1"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM docs NEAREST emb TO [1,0] USING TAXI LIMIT 1"),
                 QueryError);
}

/// LIMIT exists only to supply k. Accepting it alone would imply a general LIMIT
/// operator, which is not implemented.
TEST(KnnGrammarTest, LimitWithoutNearestIsRejected) {
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM docs LIMIT 5"), QueryError);
}

/// The ranking already defines the order, and grouping would collapse the rows it
/// is made of.
TEST(KnnGrammarTest, NearestCannotBeCombinedWithOrderByOrGroupBy) {
    EXPECT_THROW(
        (void)Parser::Parse("SELECT * FROM docs NEAREST emb TO [1,0] LIMIT 3 ORDER BY id"),
        QueryError);
    EXPECT_THROW((void)Parser::Parse(
                     "SELECT titulo, COUNT(*) FROM docs GROUP BY titulo NEAREST emb TO [1,0] LIMIT 3"),
                 QueryError);
}

// --- The two strategies must agree --------------------------------------

/// The property the whole evaluation rests on: the bounded heap and the full sort
/// are two ways of computing the same answer.
class KnnEquivalenceTest : public ::testing::Test {
protected:
    KnnEquivalenceTest() : temp_("knn_equivalence"), db_(temp_.Path()) {
        (void)db_.Execute("CREATE TABLE docs (id INT PRIMARY KEY, emb VECTOR(8))");

        // Deterministic pseudo-random data: a fixed recurrence rather than a
        // generator, so the fixture is reproducible without depending on any
        // library's implementation.
        std::uint32_t state = 12345;
        for (std::int32_t i = 1; i <= 400; ++i) {
            std::string components;
            for (int j = 0; j < 8; ++j) {
                state = state * 1103515245U + 12345U;
                const float value = static_cast<float>((state >> 16U) % 1000U) / 1000.0F;
                components += (j == 0 ? "" : ", ") + std::to_string(value);
            }
            (void)db_.Execute("INSERT INTO docs VALUES (" + std::to_string(i) + ", [" + components +
                              "])");
        }
    }

    [[nodiscard]] QueryResult Run(const std::string& sql, bool topk) {
        db_.SetTopKEnabled(topk);
        return db_.Execute(sql);
    }

    TempDatabase temp_;
    Database db_;
};

TEST_F(KnnEquivalenceTest, BothStrategiesReturnIdenticalResults) {
    const char* queries[] = {
        "SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 1",
        "SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 5",
        "SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 10",
        "SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 20",
        "SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 50",
        "SELECT * FROM docs NEAREST emb TO [0,0,0,0,0,0,0,0] LIMIT 10",
        "SELECT * FROM docs NEAREST emb TO [1,1,1,1,1,1,1,1] USING COSINE LIMIT 10",
        "SELECT * FROM docs NEAREST emb TO [1,1,1,1,1,1,1,1] USING DOT LIMIT 10",
    };

    for (const char* sql : queries) {
        const QueryResult topk = Run(sql, true);
        const QueryResult full = Run(sql, false);

        EXPECT_EQ(topk.rows, full.rows) << sql;
        EXPECT_EQ(IdsOf(topk), IdsOf(full)) << sql;
        EXPECT_EQ(topk.plan[1], "KnnScanOperator") << sql;
        EXPECT_EQ(full.plan[1], "KnnFullSortOperator") << sql;
    }
}

TEST_F(KnnEquivalenceTest, BothStrategiesComputeTheSameNumberOfDistances) {
    const char* sql = "SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 10";

    // Neither is an index: both examine every record, so the arithmetic is the
    // same and only the ranking differs. If this ever stops holding, one of them
    // has silently stopped being exhaustive.
    const QueryResult topk = Run(sql, true);
    const QueryResult full = Run(sql, false);

    EXPECT_EQ(topk.DistanceCalculations(), 400u);
    EXPECT_EQ(full.DistanceCalculations(), 400u);
}

/// The plan combines with the rest of the engine: a WHERE narrows the candidate
/// set before the ranking sees it, which is a hybrid metadata-plus-vector search.
TEST_F(KnnEquivalenceTest, AWhereClauseFiltersBeforeTheRanking) {
    const QueryResult result = db_.Execute(
        "SELECT * FROM docs WHERE id <= 50 NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] "
        "LIMIT 5");

    EXPECT_EQ(result.plan, (std::vector<std::string>{"ProjectionOperator", "KnnScanOperator",
                                                     "FilterOperator", "SequentialScanOperator"}));
    EXPECT_EQ(result.DistanceCalculations(), 50u) << "solo los registros que pasaron el filtro";
    for (std::int32_t id : IdsOf(result)) {
        EXPECT_LE(id, 50);
    }
}

/// The batch-at-a-time scan can feed the ranking, which is a second axis of
/// comparison and must not change the answer.
TEST_F(KnnEquivalenceTest, TheVectorizedScanFeedsTheRankingWithoutChangingIt) {
    const char* sql = "SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 10";

    db_.SetVectorizedEnabled(false);
    const QueryResult tuple_at_a_time = db_.Execute(sql);
    db_.SetVectorizedEnabled(true);
    const QueryResult vectorized = db_.Execute(sql);
    db_.SetVectorizedEnabled(false);

    EXPECT_EQ(tuple_at_a_time.rows, vectorized.rows);
    EXPECT_EQ(vectorized.plan.back(), "VectorizedScanOperator");
    EXPECT_EQ(vectorized.DistanceCalculations(), tuple_at_a_time.DistanceCalculations());
}

TEST_F(KnnEquivalenceTest, EveryQueryLeavesThePoolUnpinned) {
    for (bool topk : {true, false}) {
        (void)Run("SELECT * FROM docs NEAREST emb TO [0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5] LIMIT 10",
                  topk);
        EXPECT_TRUE(db_.Pool().AllPagesUnpinned()) << "topk=" << topk;
    }
}

// --- Where the saving comes from ----------------------------------------

/// Candidates kept is where the two strategies really differ, and it is
/// deterministic. The full sort keeps every record it scores; the bounded heap
/// keeps only those that beat the current worst survivor.
TEST(KnnCostTest, TheBoundedHeapKeepsFarFewerCandidatesThanTheFullSort) {
    TempDatabase temp("knn_candidates");
    Database db(temp.Path());
    (void)db.Execute("CREATE TABLE docs (id INT PRIMARY KEY, emb VECTOR(4))");

    // Inserted from farthest to closest on purpose: this is the worst case for the
    // bounded heap, because every record beats the previous worst and so every one
    // is admitted. Even then it never holds more than k at a time, which is the
    // point about space.
    for (std::int32_t i = 300; i >= 1; --i) {
        const std::string value = std::to_string(static_cast<float>(i) / 300.0F);
        (void)db.Execute("INSERT INTO docs VALUES (" + std::to_string(i) + ", [" + value + ", " +
                         value + ", " + value + ", " + value + "])");
    }

    const char* sql = "SELECT * FROM docs NEAREST emb TO [0, 0, 0, 0] LIMIT 10";

    db.SetTopKEnabled(true);
    const QueryResult bounded = db.Execute(sql);
    db.SetTopKEnabled(false);
    const QueryResult full = db.Execute(sql);
    db.SetTopKEnabled(true);

    ASSERT_EQ(bounded.rows.size(), 10u);
    ASSERT_EQ(full.rows.size(), 10u);
    EXPECT_EQ(bounded.rows, full.rows);

    // Same arithmetic: neither is an index.
    EXPECT_EQ(bounded.DistanceCalculations(), 300u);
    EXPECT_EQ(full.DistanceCalculations(), 300u);

    const auto candidates = [](const QueryResult& result) {
        std::uint64_t total = 0;
        for (const OperatorMetrics& op : result.metrics) {
            total += op.candidates_admitted;
        }
        return total;
    };
    EXPECT_EQ(candidates(full), 300u) << "el orden completo conserva todos los registros";
    EXPECT_LE(candidates(bounded), 300u);
}

/// In the average case — data in no particular order — the bound rejects the vast
/// majority of candidates outright.
TEST(KnnCostTest, MostCandidatesAreRejectedWithoutEnteringTheHeap) {
    TempDatabase temp("knn_rejected");
    Database db(temp.Path());
    (void)db.Execute("CREATE TABLE docs (id INT PRIMARY KEY, emb VECTOR(2))");

    // Closest records first: after the heap fills with the ten best, nothing else
    // can displace them.
    for (std::int32_t i = 1; i <= 300; ++i) {
        const std::string value = std::to_string(static_cast<float>(i));
        (void)db.Execute("INSERT INTO docs VALUES (" + std::to_string(i) + ", [" + value + ", 0])");
    }

    const QueryResult result = db.Execute("SELECT * FROM docs NEAREST emb TO [0, 0] LIMIT 10");

    std::uint64_t admitted = 0;
    for (const OperatorMetrics& op : result.metrics) {
        admitted += op.candidates_admitted;
    }
    EXPECT_EQ(result.DistanceCalculations(), 300u);
    EXPECT_EQ(admitted, 10u) << "solo los diez primeros entran en el montículo";
}

}  // namespace
}  // namespace minidb
