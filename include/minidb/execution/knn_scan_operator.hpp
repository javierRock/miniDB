#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "minidb/catalog/schema.hpp"
#include "minidb/execution/physical_operator.hpp"
#include "minidb/parser/statement.hpp"

namespace minidb {

/// Returns the `k` records whose vector column is closest to a query vector.
///
/// The search is **exact and exhaustive**: every record of the input is examined,
/// so this is not an index. What it avoids is ranking more than it has to. Instead
/// of sorting all `n` distances it keeps a bounded max-heap of the best `k`
/// candidates seen so far, and a candidate whose score is worse than the current
/// `k`-th is discarded without ever entering the heap.
///
/// Cost: `Theta(n·d)` distance arithmetic plus `O(n log k)` heap operations, and
/// `O(k)` extra space. The baseline it is compared against,
/// `KnnFullSortOperator`, pays `O(n log n)` and `O(n)` instead — the difference
/// this operator exists to demonstrate.
///
/// Blocking, like Sort and Aggregate: the closest record cannot be known before
/// the last one has been examined, so Open() drains the child.
///
/// Output: the child's columns plus a computed distance column named
/// `kDistanceColumn`, and rows come out ordered from closest to farthest.
class KnnScanOperator : public PhysicalOperator {
public:
    KnnScanOperator(std::unique_ptr<PhysicalOperator> child, const Schema& schema,
                    NearestClause clause);

    void Open() override;
    [[nodiscard]] std::optional<Record> Next() override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "KnnScanOperator"; }

    /// Not meaningful: the ranking materialises its rows and adds a computed
    /// column, so they no longer correspond to a stored record.
    [[nodiscard]] RecordId LastRecordId() const override { return RecordId{}; }
    [[nodiscard]] const PhysicalOperator* Child() const override { return child_.get(); }
    [[nodiscard]] const std::vector<std::string>& OutputColumnNames() const override {
        return names_;
    }

    /// Distances computed and candidates kept are reported through Metrics(), like
    /// every other counter in the system: see OperatorMetrics.

protected:
    /// One scored candidate: its ranking score, the record, and its primary key
    /// for a reproducible tie-break.
    struct Candidate {
        float score = 0.0F;
        std::int32_t key = 0;
        Record record;
    };

    /// Reads one record's vector, scores it and counts the work. Shared with the
    /// baseline operator so both measure the same thing in the same place.
    [[nodiscard]] Candidate Score(Record record);

    /// Emits `candidate` as an output row: its columns plus the distance.
    [[nodiscard]] Record ToOutputRecord(const Candidate& candidate) const;

    /// Orders two candidates: by score, then by primary key so that ties are
    /// broken the same way on every run and the result is reproducible.
    [[nodiscard]] static bool Closer(const Candidate& left, const Candidate& right);

    std::unique_ptr<PhysicalOperator> child_;
    NearestClause clause_;
    std::size_t vector_column_ = 0;
    std::size_t key_column_ = 0;
    std::vector<std::string> names_;

    std::vector<Candidate> results_;
    std::size_t position_ = 0;
};

}  // namespace minidb
