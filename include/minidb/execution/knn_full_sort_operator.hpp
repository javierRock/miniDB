#pragma once

#include <cstddef>
#include <string>

#include "minidb/execution/knn_scan_operator.hpp"

namespace minidb {

/// The naive way to answer the same query: score every record, sort all `n`
/// results, then return the first `k`.
///
/// This is the **baseline** of the experiments, not a fallback the system uses in
/// production. It is a real implementation and it returns exactly the same rows as
/// `KnnScanOperator` — that equivalence is asserted by the tests, and it is what
/// makes the comparison between them meaningful.
///
/// It differs from its parent in two costs, both of which grow with `n` rather
/// than with `k`:
///
///   - Time: `O(n log n)` for the sort instead of `O(n log k)` for the bounded
///     heap.
///   - Space: `O(n)` candidates held at once instead of `O(k)`. With a large table
///     this is the more damaging of the two, because every candidate carries a
///     whole record and its vector.
///
/// The distance arithmetic, `Theta(n·d)`, is identical in both: neither is an
/// index, and both examine every record.
class KnnFullSortOperator : public KnnScanOperator {
public:
    using KnnScanOperator::KnnScanOperator;

    void Open() override;
    [[nodiscard]] std::string Name() const override { return "KnnFullSortOperator"; }

    /// The number of candidates held at the peak is `n`, and it is reported through
    /// OperatorMetrics::candidates_admitted like every other counter: this operator
    /// admits every record it scores, which is exactly what the bounded heap does
    /// not do.
};

}  // namespace minidb
