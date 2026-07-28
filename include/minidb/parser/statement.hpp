#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "minidb/catalog/schema.hpp"
#include "minidb/common/value.hpp"

namespace minidb {

/// A single WHERE condition: one column compared against one literal.
/// The grammar allows exactly one, so there is no AND/OR to represent.
struct Condition {
    std::string column;
    CompareOperator op = CompareOperator::kEqual;
    Value value;
};

/// One assignment in an UPDATE ... SET list.
struct Assignment {
    std::string column;
    Value value;
};

struct CreateTableStatement {
    std::string table_name;
    std::vector<Column> columns;
};

struct InsertStatement {
    std::string table_name;
    std::vector<Value> values;
};

struct SelectStatement {
    std::string table_name;
    /// Empty means SELECT *, which projects every column in schema order.
    std::vector<std::string> columns;
    std::optional<Condition> where;
};

struct UpdateStatement {
    std::string table_name;
    std::vector<Assignment> assignments;
    std::optional<Condition> where;
};

struct DeleteStatement {
    std::string table_name;
    std::optional<Condition> where;
};

/// Pure data produced by the parser and consumed by the execution engine. The
/// AST types deliberately know nothing about storage.
using Statement = std::variant<CreateTableStatement, InsertStatement, SelectStatement,
                               UpdateStatement, DeleteStatement>;

}  // namespace minidb
