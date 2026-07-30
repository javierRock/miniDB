#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "minidb/catalog/schema.hpp"
#include "minidb/execution/physical_operator.hpp"

namespace minidb {

/// Selects and reorders the columns of the records its child produces.
///
/// For `SELECT *` it forwards records unchanged; for an explicit column list it
/// builds a narrower record. Supporting the column list is what keeps this a
/// real operator rather than an identity step that only exists to make the plan
/// diagram look complete.
class ProjectionOperator : public PhysicalOperator {
public:
    /// `columns` empty means every input column, in order.
    ///
    /// The names come from the child rather than from a schema, so the same
    /// operator projects a table scan or the output of an aggregate — which is
    /// what makes `SELECT COUNT(*), career ... GROUP BY career` work without a
    /// second projection operator.
    ProjectionOperator(std::unique_ptr<PhysicalOperator> child,
                       const std::vector<std::string>& input_names,
                       const std::vector<std::string>& columns);

    /// Convenience overload for a plan whose input is the table itself.
    ProjectionOperator(std::unique_ptr<PhysicalOperator> child, const Schema& schema,
                       const std::vector<std::string>& columns);

    void Open() override;
    [[nodiscard]] std::optional<Record> Next() override;
    void Close() override;
    [[nodiscard]] std::string Name() const override { return "ProjectionOperator"; }
    [[nodiscard]] RecordId LastRecordId() const override { return child_->LastRecordId(); }
    [[nodiscard]] const PhysicalOperator* Child() const override { return child_.get(); }

    /// Names of the projected columns, used for the result header.
    [[nodiscard]] const std::vector<std::string>& OutputColumnNames() const override {
        return names_;
    }

private:
    std::unique_ptr<PhysicalOperator> child_;
    /// Index in the input record for each output column.
    std::vector<std::size_t> indices_;
    std::vector<std::string> names_;
    bool identity_ = false;
};

}  // namespace minidb
