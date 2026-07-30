#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "minidb/parser/statement.hpp"
#include "minidb/parser/tokenizer.hpp"

namespace minidb {

/// Recursive-descent parser for the supported SQL subset.
///
/// It only builds a Statement: it never touches the catalog, the storage layer
/// or the buffer pool, and this header includes nothing from them. Keeping the
/// two apart is what lets the grammar be tested without a database file.
///
/// Grammar:
///
///   statement   := create | insert | select | update | delete [';']
///   create      := CREATE TABLE name '(' column {',' column} ')'
///   column      := name (INT | VARCHAR '(' int ')') [PRIMARY KEY]
///   insert      := INSERT INTO name VALUES '(' value {',' value} ')'
///   select      := SELECT ('*' | item {',' item}) FROM name [where]
///                         [group_by] [order_by]
///   item        := name | COUNT '(' '*' ')'
///   update      := UPDATE name SET assign {',' assign} [where]
///   delete      := DELETE FROM name [where]
///   assign      := name '=' value
///   where       := WHERE name op value
///   group_by    := GROUP BY name
///   order_by    := ORDER BY (name | COUNT '(' '*' ')') [ASC | DESC]
///   op          := '=' | '!=' | '<' | '<=' | '>' | '>='
///   value       := int | string
class Parser {
public:
    /// Parses one statement. Throws QueryError with a position and the expected
    /// token on any syntax error, including trailing tokens after the end.
    [[nodiscard]] static Statement Parse(const std::string& sql);

private:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    [[nodiscard]] const Token& Current() const { return tokens_[cursor_]; }
    [[nodiscard]] bool Check(TokenType type) const { return Current().type == type; }
    bool Match(TokenType type);
    const Token& Expect(TokenType type);
    [[nodiscard]] QueryError Unexpected(const std::string& expected) const;

    [[nodiscard]] Statement ParseStatement();
    [[nodiscard]] CreateTableStatement ParseCreateTable();
    [[nodiscard]] InsertStatement ParseInsert();
    [[nodiscard]] SelectStatement ParseSelect();
    [[nodiscard]] UpdateStatement ParseUpdate();
    [[nodiscard]] DeleteStatement ParseDelete();

    [[nodiscard]] Column ParseColumnDefinition();
    [[nodiscard]] std::optional<Condition> ParseOptionalWhere();
    [[nodiscard]] std::optional<GroupByClause> ParseOptionalGroupBy();
    [[nodiscard]] std::optional<OrderByClause> ParseOptionalOrderBy();
    /// A column name or COUNT(*), which yields the pseudo-name "COUNT(*)".
    [[nodiscard]] std::string ParseSelectItem();
    [[nodiscard]] CompareOperator ParseCompareOperator();
    [[nodiscard]] Value ParseValue();
    [[nodiscard]] std::string ParseIdentifier(const char* what);
    [[nodiscard]] std::int32_t ParseInteger(const char* what);

    std::vector<Token> tokens_;
    std::size_t cursor_ = 0;
};

}  // namespace minidb
