#pragma once

#include <optional>

#include "minidb/storage/record.hpp"

namespace minidb {

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
};

}  // namespace minidb
