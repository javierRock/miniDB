#include "minidb/parser/parser.hpp"

#include <charconv>
#include <limits>

#include "minidb/common/constants.hpp"
#include "minidb/common/types.hpp"

namespace minidb {

Statement Parser::Parse(const std::string& sql) {
    Parser parser(Tokenizer(sql).Tokenize());

    if (parser.Check(TokenType::kEndOfInput)) {
        throw QueryError("No se recibió ninguna sentencia SQL");
    }

    Statement statement = parser.ParseStatement();

    // The trailing semicolon is optional, but anything after the statement is
    // a mistake worth reporting rather than ignoring.
    parser.Match(TokenType::kSemicolon);
    if (!parser.Check(TokenType::kEndOfInput)) {
        throw parser.Unexpected("el final de la sentencia");
    }
    return statement;
}

bool Parser::Match(TokenType type) {
    if (!Check(type)) {
        return false;
    }
    ++cursor_;
    return true;
}

const Token& Parser::Expect(TokenType type) {
    if (!Check(type)) {
        throw Unexpected(TokenTypeName(type));
    }
    return tokens_[cursor_++];
}

QueryError Parser::Unexpected(const std::string& expected) const {
    const Token& token = Current();
    const std::string received =
        token.type == TokenType::kEndOfInput
            ? TokenTypeName(token.type)
            : TokenTypeName(token.type) + (token.text.empty() ? "" : " ('" + token.text + "')");

    return QueryError("Error de sintaxis en la posición " + std::to_string(token.position) +
                      ": se esperaba " + expected + " y se encontró " + received);
}

std::string Parser::ParseIdentifier(const char* what) {
    if (!Check(TokenType::kIdentifier)) {
        throw Unexpected(std::string("un nombre de ") + what);
    }
    return tokens_[cursor_++].text;
}

std::int32_t Parser::ParseInteger(const char* what) {
    if (!Check(TokenType::kIntegerLiteral)) {
        throw Unexpected(std::string("un número entero para ") + what);
    }
    const Token& token = tokens_[cursor_++];

    std::int32_t value = 0;
    const auto* first = token.text.data();
    const auto* last = first + token.text.size();
    const auto result = std::from_chars(first, last, value);

    if (result.ec != std::errc{} || result.ptr != last) {
        throw QueryError("El número '" + token.text + "' en la posición " +
                         std::to_string(token.position) + " no cabe en un INT de 32 bits");
    }
    return value;
}

Statement Parser::ParseStatement() {
    if (Check(TokenType::kCreate)) return ParseCreateTable();
    if (Check(TokenType::kInsert)) return ParseInsert();
    if (Check(TokenType::kSelect)) return ParseSelect();
    if (Check(TokenType::kUpdate)) return ParseUpdate();
    if (Check(TokenType::kDelete)) return ParseDelete();

    throw Unexpected("CREATE, INSERT, SELECT, UPDATE o DELETE");
}

Column Parser::ParseColumnDefinition() {
    Column column;
    column.name = ParseIdentifier("columna");

    if (Match(TokenType::kInt)) {
        column.type = ColumnType::kInteger;
    } else if (Match(TokenType::kVarchar)) {
        column.type = ColumnType::kVarchar;
        Expect(TokenType::kLeftParenthesis);
        const std::int32_t length = ParseInteger("la longitud del VARCHAR");
        if (length <= 0 || static_cast<std::size_t>(length) > kMaxVarcharLength) {
            throw QueryError("VARCHAR(" + std::to_string(length) + ") en la columna '" +
                             column.name + "': la longitud debe estar entre 1 y " +
                             std::to_string(kMaxVarcharLength));
        }
        column.max_length = static_cast<std::uint16_t>(length);
        Expect(TokenType::kRightParenthesis);
    } else {
        throw Unexpected("INT o VARCHAR");
    }

    if (Match(TokenType::kPrimary)) {
        Expect(TokenType::kKey);
        column.is_primary_key = true;
    }
    return column;
}

CreateTableStatement Parser::ParseCreateTable() {
    Expect(TokenType::kCreate);
    Expect(TokenType::kTable);

    CreateTableStatement statement;
    statement.table_name = ParseIdentifier("tabla");

    Expect(TokenType::kLeftParenthesis);
    do {
        statement.columns.push_back(ParseColumnDefinition());
    } while (Match(TokenType::kComma));
    Expect(TokenType::kRightParenthesis);

    return statement;
}

Value Parser::ParseValue() {
    if (Check(TokenType::kIntegerLiteral)) {
        return Value{ParseInteger("un valor")};
    }
    if (Check(TokenType::kStringLiteral)) {
        return Value{tokens_[cursor_++].text};
    }
    throw Unexpected("un número o una cadena entre comillas simples");
}

InsertStatement Parser::ParseInsert() {
    Expect(TokenType::kInsert);
    Expect(TokenType::kInto);

    InsertStatement statement;
    statement.table_name = ParseIdentifier("tabla");

    Expect(TokenType::kValues);
    Expect(TokenType::kLeftParenthesis);
    do {
        statement.values.push_back(ParseValue());
    } while (Match(TokenType::kComma));
    Expect(TokenType::kRightParenthesis);

    return statement;
}

CompareOperator Parser::ParseCompareOperator() {
    if (Match(TokenType::kEqual)) return CompareOperator::kEqual;
    if (Match(TokenType::kNotEqual)) return CompareOperator::kNotEqual;
    if (Match(TokenType::kLess)) return CompareOperator::kLess;
    if (Match(TokenType::kLessEqual)) return CompareOperator::kLessEqual;
    if (Match(TokenType::kGreater)) return CompareOperator::kGreater;
    if (Match(TokenType::kGreaterEqual)) return CompareOperator::kGreaterEqual;

    throw Unexpected("un operador de comparación (=, !=, <, <=, > o >=)");
}

std::optional<Condition> Parser::ParseOptionalWhere() {
    if (!Match(TokenType::kWhere)) {
        return std::nullopt;
    }

    Condition condition;
    condition.column = ParseIdentifier("columna");
    condition.op = ParseCompareOperator();
    condition.value = ParseValue();
    return condition;
}

SelectStatement Parser::ParseSelect() {
    Expect(TokenType::kSelect);

    SelectStatement statement;
    // An empty column list means SELECT *; the projection operator then uses
    // the whole schema.
    if (!Match(TokenType::kAsterisk)) {
        do {
            statement.columns.push_back(ParseIdentifier("columna"));
        } while (Match(TokenType::kComma));
    }

    Expect(TokenType::kFrom);
    statement.table_name = ParseIdentifier("tabla");
    statement.where = ParseOptionalWhere();

    return statement;
}

UpdateStatement Parser::ParseUpdate() {
    Expect(TokenType::kUpdate);

    UpdateStatement statement;
    statement.table_name = ParseIdentifier("tabla");

    Expect(TokenType::kSet);
    do {
        Assignment assignment;
        assignment.column = ParseIdentifier("columna");
        Expect(TokenType::kEqual);
        assignment.value = ParseValue();
        statement.assignments.push_back(std::move(assignment));
    } while (Match(TokenType::kComma));

    statement.where = ParseOptionalWhere();
    return statement;
}

DeleteStatement Parser::ParseDelete() {
    Expect(TokenType::kDelete);
    Expect(TokenType::kFrom);

    DeleteStatement statement;
    statement.table_name = ParseIdentifier("tabla");
    statement.where = ParseOptionalWhere();

    return statement;
}

}  // namespace minidb
