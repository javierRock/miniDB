#include "minidb/vector/distance.hpp"

#include <cmath>

namespace minidb::vector_metrics {
namespace {

/// A distance is only defined between vectors of the same dimension, so a
/// mismatch is a query error and not a value.
void RequireSameDimension(const Vector& left, const Vector& right) {
    if (left.size() != right.size()) {
        throw QueryError("No se puede medir la distancia entre un vector de " +
                         std::to_string(left.size()) + " componentes y otro de " +
                         std::to_string(right.size()));
    }
    if (left.empty()) {
        throw QueryError("No se puede medir la distancia entre vectores vacíos");
    }
}

/// Accumulating in double and returning float is deliberate: with 1000 dimensions
/// a float accumulator loses precision to rounding, and the extra cost is
/// negligible next to the memory traffic of reading the vectors.
[[nodiscard]] double SquaredNorm(const Vector& vector) {
    double total = 0.0;
    for (float component : vector) {
        total += static_cast<double>(component) * static_cast<double>(component);
    }
    return total;
}

}  // namespace

float SquaredEuclideanDistance(const Vector& left, const Vector& right) {
    RequireSameDimension(left, right);

    double total = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const double difference = static_cast<double>(left[i]) - static_cast<double>(right[i]);
        total += difference * difference;
    }
    return static_cast<float>(total);
}

float EuclideanDistance(const Vector& left, const Vector& right) {
    return std::sqrt(SquaredEuclideanDistance(left, right));
}

float DotProduct(const Vector& left, const Vector& right) {
    RequireSameDimension(left, right);

    double total = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        total += static_cast<double>(left[i]) * static_cast<double>(right[i]);
    }
    return static_cast<float>(total);
}

float CosineSimilarity(const Vector& left, const Vector& right) {
    RequireSameDimension(left, right);

    const double left_norm = SquaredNorm(left);
    const double right_norm = SquaredNorm(right);

    // The zero vector has no direction, so the angle is undefined. Defining the
    // similarity as 0 keeps the ranking well ordered instead of propagating a NaN
    // through the comparison, which would make the result depend on the order the
    // records happened to be visited in.
    if (left_norm == 0.0 || right_norm == 0.0) {
        return 0.0F;
    }

    double total = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        total += static_cast<double>(left[i]) * static_cast<double>(right[i]);
    }
    return static_cast<float>(total / std::sqrt(left_norm * right_norm));
}

float CosineDistance(const Vector& left, const Vector& right) {
    return 1.0F - CosineSimilarity(left, right);
}

float RankingScore(DistanceMetric metric, const Vector& left, const Vector& right) {
    switch (metric) {
        case DistanceMetric::kEuclidean:
            // Squared: the root is monotonic and would only cost time.
            return SquaredEuclideanDistance(left, right);
        case DistanceMetric::kCosine:
            return CosineDistance(left, right);
        case DistanceMetric::kDotProduct:
            // Negated so that "lower is better" holds for every metric and one
            // comparison serves all three.
            return -DotProduct(left, right);
    }
    throw StorageError("Métrica de distancia desconocida");
}

float ReportedDistance(DistanceMetric metric, float ranking_score) {
    switch (metric) {
        case DistanceMetric::kEuclidean:
            return std::sqrt(ranking_score);
        case DistanceMetric::kCosine:
            return ranking_score;
        case DistanceMetric::kDotProduct:
            return -ranking_score;
    }
    throw StorageError("Métrica de distancia desconocida");
}

std::string MetricName(DistanceMetric metric) {
    switch (metric) {
        case DistanceMetric::kEuclidean:
            return "euclidiana";
        case DistanceMetric::kCosine:
            return "coseno";
        case DistanceMetric::kDotProduct:
            return "producto punto";
    }
    return "desconocida";
}

bool LowerIsCloser(DistanceMetric metric) { return metric != DistanceMetric::kDotProduct; }

}  // namespace minidb::vector_metrics
