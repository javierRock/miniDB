#pragma once

#include <cstddef>
#include <optional>

#include "minidb/execution/physical_operator.hpp"

namespace minidb {

/// Base for operators whose real implementation is NextBatch.
///
/// It closes the loop the other way round from PhysicalOperator::NextBatch: that
/// one lets a tuple-at-a-time operator serve a batch, and this one lets a
/// vectorized operator serve single records, by handing them out from the batch
/// it is holding.
///
/// Both adapters together are what make the two execution models mix freely. A
/// vectorized scan can sit under a Sort that knows nothing about batches, and the
/// batching still pays off underneath.
///
/// A subclass implements Open, NextBatch, Close and Name as usual. The only extra
/// obligation is to call ResetBatchCursor() from its Open, so that reopening the
/// operator starts serving from the beginning again.
class BatchOperator : public PhysicalOperator {
public:
    /// Serves one record from the current batch, fetching the next batch when it
    /// runs out.
    ///
    /// Marked final: a batch operator produces records in exactly one way, and
    /// letting a subclass override this would let the two faces of the operator
    /// disagree with each other.
    [[nodiscard]] std::optional<Record> Next() final;

    /// Left abstract on purpose. If a subclass did not implement it, it would
    /// inherit the adapter that calls Next(), and Next() calls NextBatch: the two
    /// adapters would call each other forever. Making it pure virtual turns that
    /// mistake into a compile error.
    bool NextBatch(RecordBatch& batch) override = 0;

    [[nodiscard]] RecordId LastRecordId() const override { return last_rid_; }

protected:
    /// Rewinds the record-at-a-time cursor. Every subclass calls this from Open.
    void ResetBatchCursor();

    /// The batch Next() is currently serving from.
    RecordBatch batch_;

private:
    /// Index into batch_.Selection().
    std::size_t position_ = 0;
    bool exhausted_ = false;
    RecordId last_rid_;
};

}  // namespace minidb
