// Exact nearest neighbour search over the table.
//
// Two operators answer the same query by different means, and the pair is the
// experiment: a bounded heap that keeps only the best k candidates, and a full
// sort of all n. Both examine every record — neither is an index — so the
// arithmetic they perform is identical and the only difference is how much
// ranking work they do on top of it.

#include <algorithm>
#include <utility>

#include "minidb/execution/knn_full_sort_operator.hpp"
#include "minidb/execution/knn_scan_operator.hpp"
#include "minidb/vector/distance.hpp"

namespace minidb {

KnnScanOperator::KnnScanOperator(std::unique_ptr<PhysicalOperator> child, const Schema& schema,
                                 NearestClause clause)
    : child_(std::move(child)), clause_(std::move(clause)) {
    const auto column = schema.FindColumn(clause_.column);
    if (!column.has_value()) {
        throw QueryError("No existe la columna '" + clause_.column + "' en la tabla");
    }
    vector_column_ = *column;

    if (schema.GetColumn(vector_column_).type != ColumnType::kVector) {
        throw QueryError("La columna '" + schema.GetColumn(vector_column_).name +
                         "' no es de tipo VECTOR, así que no admite una búsqueda por similitud");
    }

    // Checked while building the plan rather than once per record: a query vector
    // of the wrong dimension can never match anything, and finding out on the
    // first record would waste a scan and give a worse message.
    const std::uint16_t dimension = schema.GetColumn(vector_column_).max_length;
    if (clause_.query.size() != dimension) {
        throw QueryError("La columna '" + schema.GetColumn(vector_column_).name + "' es VECTOR(" +
                         std::to_string(dimension) + ") y el vector de consulta tiene " +
                         std::to_string(clause_.query.size()) + " componentes");
    }

    key_column_ = schema.PrimaryKeyIndex();

    for (const Column& schema_column : schema.Columns()) {
        names_.push_back(schema_column.name);
    }
    names_.push_back(kDistanceColumn);
}

bool KnnScanOperator::Closer(const Candidate& left, const Candidate& right) {
    // The primary key breaks ties so that two records at the same distance always
    // come out in the same order. Without it the result would depend on the order
    // the heap happened to evict, which changes with the physical layout.
    if (left.score != right.score) {
        return left.score < right.score;
    }
    return left.key < right.key;
}

KnnScanOperator::Candidate KnnScanOperator::Score(Record record) {
    const Value& stored = record.GetValue(vector_column_);
    if (!std::holds_alternative<Vector>(stored)) {
        throw StorageError("Se esperaba un vector en la columna '" + clause_.column + "'");
    }

    CountDistance();
    Candidate candidate;
    candidate.score =
        vector_metrics::RankingScore(clause_.metric, std::get<Vector>(stored), clause_.query);
    candidate.key = std::get<std::int32_t>(record.GetValue(key_column_));
    candidate.record = std::move(record);
    return candidate;
}

Record KnnScanOperator::ToOutputRecord(const Candidate& candidate) const {
    std::vector<Value> values = candidate.record.Values();
    values.emplace_back(vector_metrics::ReportedDistance(clause_.metric, candidate.score));
    return Record(std::move(values));
}

void KnnScanOperator::Open() {
    child_->Open();
    results_.clear();
    position_ = 0;
    ResetCounters();

    // k = 0 is a valid query with an empty answer. Draining the child anyway would
    // read the whole table to produce nothing.
    if (clause_.k == 0) {
        child_->Close();
        return;
    }

    // A max-heap of the k best candidates: the *worst* of the survivors sits on
    // top, which is exactly what has to be compared against and evicted.
    const auto farther = [](const Candidate& left, const Candidate& right) {
        return Closer(left, right);
    };
    results_.reserve(clause_.k + 1);

    while (auto record = child_->Next()) {
        Candidate candidate = Score(std::move(*record));

        if (results_.size() < clause_.k) {
            CountCandidate();
            results_.push_back(std::move(candidate));
            std::push_heap(results_.begin(), results_.end(), farther);
            continue;
        }
        // The heap is full: only a candidate better than the current worst is
        // worth admitting. This is the comparison that keeps the cost at
        // O(n log k) instead of O(n log n).
        if (Closer(candidate, results_.front())) {
            CountCandidate();
            std::pop_heap(results_.begin(), results_.end(), farther);
            results_.back() = std::move(candidate);
            std::push_heap(results_.begin(), results_.end(), farther);
        }
    }
    child_->Close();

    // sort_heap leaves the survivors in ascending order, which is the order the
    // rows are reported in.
    std::sort_heap(results_.begin(), results_.end(), farther);
}

std::optional<Record> KnnScanOperator::Next() {
    if (position_ >= results_.size()) {
        return Counted(std::nullopt);
    }
    return Counted(ToOutputRecord(results_[position_++]));
}

void KnnScanOperator::Close() {
    child_->Close();
    results_.clear();
    results_.shrink_to_fit();
    position_ = 0;
}

// --- KnnFullSortOperator -------------------------------------------------

void KnnFullSortOperator::Open() {
    child_->Open();
    results_.clear();
    position_ = 0;
    ResetCounters();

    // Every record is scored *and kept*, which is the baseline's defining cost:
    // memory grows with the size of the table rather than with k.
    while (auto record = child_->Next()) {
        results_.push_back(Score(std::move(*record)));
        CountCandidate();
    }
    child_->Close();

    // The whole result set is sorted, not just the part that will be returned.
    std::sort(results_.begin(), results_.end(), Closer);

    if (results_.size() > clause_.k) {
        results_.resize(clause_.k);
    }
}

}  // namespace minidb
