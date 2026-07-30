// Distance and similarity metrics, checked against values computed by hand.
//
// These are the arithmetic foundations of every ranking the system produces, so
// they are verified against closed-form results rather than against each other.

#include "minidb/vector/distance.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace minidb {
namespace {

using vector_metrics::CosineDistance;
using vector_metrics::CosineSimilarity;
using vector_metrics::DotProduct;
using vector_metrics::EuclideanDistance;
using vector_metrics::RankingScore;
using vector_metrics::ReportedDistance;
using vector_metrics::SquaredEuclideanDistance;

/// Tolerance for a float computation. Wide enough for accumulated rounding, tight
/// enough that a wrong formula cannot pass.
constexpr float kTolerance = 1e-5F;

// --- Euclidean -----------------------------------------------------------

/// d([1,0],[0,1]) = sqrt(1 + 1) = sqrt(2).
TEST(DistanceTest, EuclideanMatchesTheClosedFormValue) {
    const Vector a{1.0F, 0.0F};
    const Vector b{0.0F, 1.0F};

    EXPECT_NEAR(SquaredEuclideanDistance(a, b), 2.0F, kTolerance);
    EXPECT_NEAR(EuclideanDistance(a, b), std::sqrt(2.0F), kTolerance);
}

/// The 3-4-5 triangle: d([0,0],[3,4]) = 5 exactly.
TEST(DistanceTest, EuclideanOverAPythagoreanTriple) {
    EXPECT_NEAR(EuclideanDistance(Vector{0.0F, 0.0F}, Vector{3.0F, 4.0F}), 5.0F, kTolerance);
}

TEST(DistanceTest, EuclideanIsZeroBetweenIdenticalVectors) {
    const Vector v{0.5F, -1.5F, 2.25F};
    EXPECT_NEAR(EuclideanDistance(v, v), 0.0F, kTolerance);
}

TEST(DistanceTest, EuclideanIsSymmetric) {
    const Vector a{1.0F, 2.0F, 3.0F};
    const Vector b{-4.0F, 0.5F, 2.0F};
    EXPECT_NEAR(EuclideanDistance(a, b), EuclideanDistance(b, a), kTolerance);
}

TEST(DistanceTest, EuclideanHandlesNegativeComponents) {
    // d([-1,-1],[1,1]) = sqrt(4 + 4) = 2*sqrt(2).
    EXPECT_NEAR(EuclideanDistance(Vector{-1.0F, -1.0F}, Vector{1.0F, 1.0F}), 2.0F * std::sqrt(2.0F),
                kTolerance);
}

/// The squared form is what the ranking uses, so it must order candidates exactly
/// as the rooted form does.
TEST(DistanceTest, SquaredDistancePreservesTheOrderOfTheRootedOne) {
    const Vector query{0.0F, 0.0F};
    const Vector near{1.0F, 0.0F};
    const Vector far{3.0F, 4.0F};

    EXPECT_LT(SquaredEuclideanDistance(query, near), SquaredEuclideanDistance(query, far));
    EXPECT_LT(EuclideanDistance(query, near), EuclideanDistance(query, far));
}

// --- Dot product ---------------------------------------------------------

TEST(DistanceTest, DotProductMatchesTheClosedFormValue) {
    // [1,2,3] . [4,5,6] = 4 + 10 + 18 = 32.
    EXPECT_NEAR(DotProduct(Vector{1.0F, 2.0F, 3.0F}, Vector{4.0F, 5.0F, 6.0F}), 32.0F, kTolerance);
}

TEST(DistanceTest, DotProductOfOrthogonalVectorsIsZero) {
    EXPECT_NEAR(DotProduct(Vector{1.0F, 0.0F}, Vector{0.0F, 1.0F}), 0.0F, kTolerance);
}

// --- Cosine --------------------------------------------------------------

TEST(DistanceTest, CosineSimilarityOfParallelVectorsIsOne) {
    // Magnitude is ignored: [2,0] and [7,0] point the same way.
    EXPECT_NEAR(CosineSimilarity(Vector{2.0F, 0.0F}, Vector{7.0F, 0.0F}), 1.0F, kTolerance);
    EXPECT_NEAR(CosineDistance(Vector{2.0F, 0.0F}, Vector{7.0F, 0.0F}), 0.0F, kTolerance);
}

TEST(DistanceTest, CosineSimilarityOfOrthogonalVectorsIsZero) {
    EXPECT_NEAR(CosineSimilarity(Vector{1.0F, 0.0F}, Vector{0.0F, 1.0F}), 0.0F, kTolerance);
    EXPECT_NEAR(CosineDistance(Vector{1.0F, 0.0F}, Vector{0.0F, 1.0F}), 1.0F, kTolerance);
}

TEST(DistanceTest, CosineSimilarityOfOppositeVectorsIsMinusOne) {
    EXPECT_NEAR(CosineSimilarity(Vector{1.0F, 1.0F}, Vector{-1.0F, -1.0F}), -1.0F, kTolerance);
    // Which makes the distance 2, the maximum it can reach.
    EXPECT_NEAR(CosineDistance(Vector{1.0F, 1.0F}, Vector{-1.0F, -1.0F}), 2.0F, kTolerance);
}

/// cos([1,1],[1,0]) = 1 / (sqrt(2) * 1) = 0.7071...
TEST(DistanceTest, CosineSimilarityAtFortyFiveDegrees) {
    EXPECT_NEAR(CosineSimilarity(Vector{1.0F, 1.0F}, Vector{1.0F, 0.0F}), 1.0F / std::sqrt(2.0F),
                kTolerance);
}

/// The zero vector has no direction. Returning 0 keeps the ranking well ordered
/// instead of letting a NaN decide it.
TEST(DistanceTest, CosineWithTheZeroVectorIsDefinedAsZeroSimilarity) {
    const Vector zero{0.0F, 0.0F};
    const Vector other{1.0F, 2.0F};

    EXPECT_FALSE(std::isnan(CosineSimilarity(zero, other)));
    EXPECT_NEAR(CosineSimilarity(zero, other), 0.0F, kTolerance);
    EXPECT_NEAR(CosineSimilarity(zero, zero), 0.0F, kTolerance);
    EXPECT_NEAR(CosineDistance(zero, other), 1.0F, kTolerance);
}

/// Similarity and distance are different quantities, and confusing them inverts a
/// ranking. This test exists to make that impossible to break silently.
TEST(DistanceTest, CosineDistanceIsOneMinusTheSimilarity) {
    const Vector a{0.3F, 0.9F, -0.2F};
    const Vector b{0.8F, 0.1F, 0.5F};
    EXPECT_NEAR(CosineDistance(a, b), 1.0F - CosineSimilarity(a, b), kTolerance);
}

// --- Ranking scores ------------------------------------------------------

/// Lower must mean closer for every metric, which is what lets one operator
/// implement all three with a single comparison.
TEST(DistanceTest, LowerRankingScoreAlwaysMeansMoreSimilar) {
    const Vector query{1.0F, 0.0F};
    const Vector similar{0.99F, 0.01F};
    const Vector dissimilar{-1.0F, 0.0F};

    for (DistanceMetric metric :
         {DistanceMetric::kEuclidean, DistanceMetric::kCosine, DistanceMetric::kDotProduct}) {
        EXPECT_LT(RankingScore(metric, similar, query), RankingScore(metric, dissimilar, query))
            << "métrica " << static_cast<int>(metric);
    }
}

TEST(DistanceTest, ReportedDistanceUndoesTheRankingTransform) {
    const Vector a{0.0F, 0.0F};
    const Vector b{3.0F, 4.0F};

    // Euclidean: the ranking score is squared, the reported value is not.
    EXPECT_NEAR(ReportedDistance(DistanceMetric::kEuclidean,
                                 RankingScore(DistanceMetric::kEuclidean, a, b)),
                5.0F, kTolerance);
    // Dot product: negated for ranking, restored for reporting.
    EXPECT_NEAR(ReportedDistance(DistanceMetric::kDotProduct,
                                 RankingScore(DistanceMetric::kDotProduct, Vector{1.0F, 2.0F},
                                              Vector{3.0F, 4.0F})),
                11.0F, kTolerance);
    // Cosine distance: unchanged.
    EXPECT_NEAR(ReportedDistance(DistanceMetric::kCosine,
                                 RankingScore(DistanceMetric::kCosine, Vector{1.0F, 0.0F},
                                              Vector{0.0F, 1.0F})),
                1.0F, kTolerance);
}

// --- Errors --------------------------------------------------------------

TEST(DistanceTest, MismatchedDimensionsAreRejected) {
    const Vector two{1.0F, 2.0F};
    const Vector three{1.0F, 2.0F, 3.0F};

    EXPECT_THROW((void)EuclideanDistance(two, three), QueryError);
    EXPECT_THROW((void)CosineSimilarity(two, three), QueryError);
    EXPECT_THROW((void)DotProduct(two, three), QueryError);
}

TEST(DistanceTest, EmptyVectorsAreRejected) {
    const Vector empty;
    EXPECT_THROW((void)EuclideanDistance(empty, empty), QueryError);
    EXPECT_THROW((void)CosineSimilarity(empty, empty), QueryError);
}

/// With 1000 dimensions a float accumulator would visibly lose precision, which is
/// why the implementation accumulates in double.
TEST(DistanceTest, HighDimensionalSumsStayAccurate) {
    const Vector ones(1000, 1.0F);
    const Vector zeros(1000, 0.0F);

    // Every component differs by exactly 1, so the squared distance is 1000.
    EXPECT_NEAR(SquaredEuclideanDistance(ones, zeros), 1000.0F, 1e-3F);
    EXPECT_NEAR(DotProduct(ones, ones), 1000.0F, 1e-3F);
}

}  // namespace
}  // namespace minidb
