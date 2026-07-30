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

/// ORDER BY over a single column. ASC is the default, as in standard SQL.
struct OrderByClause {
    std::string column;
    bool descending = false;
};

/// GROUP BY over a single column. The only aggregate is COUNT(*).
struct GroupByClause {
    std::string column;
};

/// A nearest neighbour query: `NEAREST <col> TO [...] [USING <metric>] LIMIT <k>`.
///
/// `k` comes from LIMIT and is mandatory: a similarity search without a bound
/// would rank the whole table, which is never what the query means.
struct NearestClause {
    std::string column;
    Vector query;
    DistanceMetric metric = DistanceMetric::kEuclidean;
    std::size_t k = 0;
};

struct SelectStatement {
    std::string table_name;
    /// Empty means SELECT *, which projects every column in schema order.
    ///
    /// COUNT(*) appears here as the pseudo-column name "COUNT(*)". An SQL
    /// identifier cannot contain parentheses, so it can never collide with a
    /// real column and needs no special case downstream.
    std::vector<std::string> columns;
    std::optional<Condition> where;
    std::optional<GroupByClause> group_by;
    std::optional<OrderByClause> order_by;
    std::optional<NearestClause> nearest;
};

/// Output column name the aggregate operator gives to its counter.
inline constexpr const char* kCountStarColumn = "COUNT(*)";

/// Output column name the nearest neighbour operators give to the computed
/// distance. Like COUNT(*), it cannot collide with a real column.
inline constexpr const char* kDistanceColumn = "distancia";

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
