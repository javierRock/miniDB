#include "minidb/execution/record_batch.hpp"

#include <limits>
#include <utility>

#include "minidb/execution/physical_operator.hpp"

namespace minidb {

void RecordBatch::Clear() {
    // clear() keeps the capacity, which is the point: a batch is refilled
    // thousands of times and must not allocate again each round.
    records_.clear();
    record_ids_.clear();
    selection_.clear();
}

void RecordBatch::Append(RecordId rid, Record record) {
    if (records_.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw StorageError("Un lote no puede tener más de 65536 registros");
    }
    selection_.push_back(static_cast<std::uint16_t>(records_.size()));
    records_.push_back(std::move(record));
    record_ids_.push_back(rid);
}

void RecordBatch::SetSelection(std::vector<std::uint16_t> selection) {
    for (std::uint16_t position : selection) {
        if (position >= records_.size()) {
            throw StorageError("El vector de selección apunta fuera del lote");
        }
    }
    selection_ = std::move(selection);
}

const Record& RecordBatch::RecordAt(std::size_t position) const {
    if (position >= records_.size()) {
        throw StorageError("Posición fuera del lote");
    }
    return records_[position];
}

RecordId RecordBatch::RecordIdAt(std::size_t position) const {
    if (position >= record_ids_.size()) {
        throw StorageError("Posición fuera del lote");
    }
    return record_ids_[position];
}

void RecordBatch::ExtractInt32Column(std::size_t column, std::vector<std::int32_t>& out) const {
    out.clear();
    out.reserve(selection_.size());
    for (std::uint16_t position : selection_) {
        out.push_back(std::get<std::int32_t>(records_[position].GetValue(column)));
    }
}

bool PhysicalOperator::NextBatch(RecordBatch& batch) {
    batch.Clear();

    // The adapter that lets a tuple-at-a-time operator take part in a vectorized
    // plan: pull one record at a time until the batch is full.
    while (!batch.Full()) {
        std::optional<Record> record = Next();
        if (!record.has_value()) {
            break;
        }
        batch.Append(LastRecordId(), std::move(*record));
    }

    if (batch.Size() == 0) {
        return false;
    }
    // Rows are already counted by Counted() inside Next(), so only the batch is
    // added here — otherwise every record would be counted twice.
    CountBatch(0);
    return true;
}

}  // namespace minidb
