#pragma once

#include <string>

#include "minidb/common/types.hpp"
#include "minidb/common/value.hpp"

namespace minidb {

/// Distance and similarity functions between two vectors of equal dimension.
///
/// Every function here is pure arithmetic over two `Vector`s: this module knows
/// nothing about pages, records or plans, which is what lets it be tested against
/// values computed by hand.
///
/// Complexity is Theta(d) in time and O(1) in extra space for all of them: each
/// one is a single pass over the two vectors accumulating a scalar.
namespace vector_metrics {

/// Squared Euclidean distance: sum over i of (x_i - y_i)^2.
///
/// The square root is deliberately not taken. It is a monotonically increasing
/// function, so it does not change the ranking, and skipping it saves one sqrt per
/// candidate — n per query. The root is applied only to the k distances that are
/// actually reported.
[[nodiscard]] float SquaredEuclideanDistance(const Vector& left, const Vector& right);

/// Euclidean distance: the square root of the above. Lower is more similar.
[[nodiscard]] float EuclideanDistance(const Vector& left, const Vector& right);

/// Dot product: sum over i of x_i * y_i. Higher is more similar.
[[nodiscard]] float DotProduct(const Vector& left, const Vector& right);

/// Cosine similarity: the dot product divided by the product of the norms, so it
/// measures the angle and ignores the magnitudes. Ranges over [-1, 1], and higher
/// is more similar.
///
/// Undefined when either vector is the zero vector, because that vector has no
/// direction. This implementation defines the similarity as 0 in that case, which
/// is the value of a right angle, and documents it rather than returning a NaN
/// that would silently corrupt a ranking.
[[nodiscard]] float CosineSimilarity(const Vector& left, const Vector& right);

/// Cosine distance: 1 - cosine similarity. Ranges over [0, 2] and lower is more
/// similar, which is what makes it comparable with the Euclidean distance.
///
/// Note that this is *not* the same quantity as the cosine similarity, and mixing
/// the two up inverts the ranking.
[[nodiscard]] float CosineDistance(const Vector& left, const Vector& right);

/// The value a nearest neighbour scan ranks by, for which **lower is always
/// better** whatever the metric.
///
/// This is what lets one operator implement all three metrics with a single
/// comparison: a similarity is negated so that its ranking direction matches a
/// distance's. The Euclidean case returns the *squared* distance.
[[nodiscard]] float RankingScore(DistanceMetric metric, const Vector& left, const Vector& right);

/// Turns a ranking score back into the quantity the user asked for: the square
/// root for the Euclidean metric, the sign restored for the dot product, and the
/// value unchanged for the cosine distance.
[[nodiscard]] float ReportedDistance(DistanceMetric metric, float ranking_score);

[[nodiscard]] std::string MetricName(DistanceMetric metric);

/// True when a lower reported value means more similar. False for the dot
/// product, whose reported value is a similarity.
[[nodiscard]] bool LowerIsCloser(DistanceMetric metric);

}  // namespace vector_metrics
}  // namespace minidb
