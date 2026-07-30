#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "minidb/storage/record.hpp"

namespace minidb {

/// What one operator did during a run, for the plan the CLI prints.
///
/// `next_calls` counts how many times the operator above pulled from it and
/// `rows_produced` how many of those calls returned a record. The difference
/// between a filter's rows and its child's calls is the number of records the
/// filter had to read and throw away, which is the cost an index avoids.
struct OperatorMetrics {
    std::string name;
    std::uint64_t next_calls = 0;
    std::uint64_t rows_produced = 0;
};

/// Base class of every physical operator, following the Volcano model.
///
/// Each operator is an iterator: Open prepares it, Next produces one record at
/// a time and returns std::nullopt when the input is exhausted, and Close
/// releases whatever Open acquired. Operators compose by holding a child and
/// pulling from it, so a query plan is a tree that streams records upward
/// without ever materialising the whole result.
///
/// The method names are capitalised to match the model as it is usually
/// written: Open, Next, Close.
class PhysicalOperator {
public:
    virtual ~PhysicalOperator() = default;

    PhysicalOperator() = default;
    PhysicalOperator(const PhysicalOperator&) = delete;
    PhysicalOperator& operator=(const PhysicalOperator&) = delete;

    /// Prepares the operator. Must be called before the first Next.
    virtual void Open() = 0;

    /// Produces the next record, or std::nullopt when there are no more.
    [[nodiscard]] virtual std::optional<Record> Next() = 0;

    /// Releases resources. Calling it twice is harmless.
    virtual void Close() = 0;

    /// Name used by the CLI and the tests to show which plan was chosen.
    [[nodiscard]] virtual std::string Name() const = 0;

    /// Where the record last returned by Next lives.
    ///
    /// Scan operators know this; Filter and Projection forward it from their
    /// child. It lets UPDATE and DELETE collect the rows to modify from the
    /// root of any plan without having to know which operators it is made of.
    [[nodiscard]] virtual RecordId LastRecordId() const = 0;

    /// The operator this one pulls from, or nullptr for a leaf. Lets a plan be
    /// walked and described without knowing what it is made of.
    [[nodiscard]] virtual const PhysicalOperator* Child() const { return nullptr; }

    /// Names of the columns this operator produces.
    ///
    /// Only the operators that reshape the tuple know them: Projection and
    /// Aggregate override this. Filter and Sort pass their child's names
    /// through, and a scan returns nothing because it does not hold a schema —
    /// the planner is the one that owns schema knowledge and hands names to
    /// whoever needs them.
    ///
    /// It lets the engine read the result header off the root of any plan
    /// without knowing which operator ended up on top.
    [[nodiscard]] virtual const std::vector<std::string>& OutputColumnNames() const {
        static const std::vector<std::string> kNoNames;
        const PhysicalOperator* child = Child();
        return child != nullptr ? child->OutputColumnNames() : kNoNames;
    }

    /// What this operator did. The counters accumulate over its lifetime, and
    /// since a plan is built fresh for each statement, that is per statement.
    [[nodiscard]] OperatorMetrics Metrics() const {
        return OperatorMetrics{Name(), next_calls_, rows_produced_};
    }

protected:
    /// Counts one Next() call and its outcome.
    ///
    /// Operators return their record through this instead of incrementing two
    /// counters by hand, so a new operator cannot end up missing from the plan's
    /// measurements.
    [[nodiscard]] std::optional<Record> Counted(std::optional<Record> record) {
        ++next_calls_;
        if (record.has_value()) {
            ++rows_produced_;
        }
        return record;
    }

private:
    std::uint64_t next_calls_ = 0;
    std::uint64_t rows_produced_ = 0;
};

}  // namespace minidb
