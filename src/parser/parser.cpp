#include "minidb/parser/parser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

#include "minidb/common/constants.hpp"
#include "minidb/common/types.hpp"

namespace minidb {
namespace {

/// Metric names are keywords in spirit but identifiers in the grammar, so they
/// are matched without regard to case like every other keyword.
[[nodiscard]] bool EqualsIgnoreCase(const std::string& left, const char* right) {
    const std::string_view other(right);
    return std::ranges::equal(left, other, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

}  // namespace

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
    } else if (Match(TokenType::kVector)) {
        column.type = ColumnType::kVector;
        Expect(TokenType::kLeftParenthesis);
        const std::int32_t dimension = ParseInteger("la dimensión del VECTOR");
        if (dimension <= 0 || static_cast<std::size_t>(dimension) > kMaxVectorDimension) {
            throw QueryError("VECTOR(" + std::to_string(dimension) + ") en la columna '" +
                             column.name + "': la dimensión debe estar entre 1 y " +
                             std::to_string(kMaxVectorDimension));
        }
        column.max_length = static_cast<std::uint16_t>(dimension);
        Expect(TokenType::kRightParenthesis);
    } else {
        throw Unexpected("INT, VARCHAR o VECTOR");
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

float Parser::ParseFloat(const char* what) {
    // An integer literal is accepted where a float is expected, so `[1, 0]` does
    // not have to be written `[1.0, 0.0]`.
    if (!Check(TokenType::kFloatLiteral) && !Check(TokenType::kIntegerLiteral)) {
        throw Unexpected(std::string("un número para ") + what);
    }
    const Token& token = tokens_[cursor_++];

    float value = 0.0F;
    const auto* first = token.text.data();
    const auto* last = first + token.text.size();
    const auto result = std::from_chars(first, last, value);

    if (result.ec != std::errc{} || result.ptr != last) {
        throw QueryError("El número '" + token.text + "' en la posición " +
                         std::to_string(token.position) +
                         " no es un valor válido de coma flotante de 32 bits");
    }
    return value;
}

Vector Parser::ParseVectorLiteral() {
    Expect(TokenType::kLeftBracket);

    Vector vector;
    do {
        if (vector.size() >= kMaxVectorDimension) {
            throw QueryError("Un literal de vector no puede tener más de " +
                             std::to_string(kMaxVectorDimension) + " componentes");
        }
        vector.push_back(ParseFloat("una componente del vector"));
    } while (Match(TokenType::kComma));

    Expect(TokenType::kRightBracket);
    return vector;
}

Value Parser::ParseValue() {
    if (Check(TokenType::kIntegerLiteral)) {
        return Value{ParseInteger("un valor")};
    }
    if (Check(TokenType::kStringLiteral)) {
        return Value{tokens_[cursor_++].text};
    }
    if (Check(TokenType::kLeftBracket)) {
        return Value{ParseVectorLiteral()};
    }
    throw Unexpected("un número, una cadena entre comillas simples o un vector entre corchetes");
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

std::string Parser::ParseSelectItem() {
    if (Match(TokenType::kCount)) {
        Expect(TokenType::kLeftParenthesis);
        Expect(TokenType::kAsterisk);
        Expect(TokenType::kRightParenthesis);
        return kCountStarColumn;
    }
    return ParseIdentifier("columna");
}

std::optional<GroupByClause> Parser::ParseOptionalGroupBy() {
    if (!Match(TokenType::kGroup)) {
        return std::nullopt;
    }
    Expect(TokenType::kBy);
    return GroupByClause{ParseIdentifier("columna")};
}

std::optional<OrderByClause> Parser::ParseOptionalOrderBy() {
    if (!Match(TokenType::kOrder)) {
        return std::nullopt;
    }
    Expect(TokenType::kBy);

    OrderByClause clause;
    clause.column = ParseSelectItem();
    if (Match(TokenType::kDesc)) {
        clause.descending = true;
    } else {
        Match(TokenType::kAsc);  // explicit ASC is allowed and is the default
    }
    return clause;
}

std::optional<NearestClause> Parser::ParseOptionalNearest() {
    if (!Match(TokenType::kNearest)) {
        return std::nullopt;
    }

    NearestClause clause;
    clause.column = ParseIdentifier("columna");
    Expect(TokenType::kTo);
    clause.query = ParseVectorLiteral();

    if (Match(TokenType::kUsing)) {
        const std::string metric = ParseIdentifier("métrica");
        if (EqualsIgnoreCase(metric, "EUCLIDEAN") || EqualsIgnoreCase(metric, "EUCLIDIANA")) {
            clause.metric = DistanceMetric::kEuclidean;
        } else if (EqualsIgnoreCase(metric, "COSINE") || EqualsIgnoreCase(metric, "COSENO")) {
            clause.metric = DistanceMetric::kCosine;
        } else if (EqualsIgnoreCase(metric, "DOT") || EqualsIgnoreCase(metric, "PRODUCTO")) {
            clause.metric = DistanceMetric::kDotProduct;
        } else {
            throw QueryError("Métrica desconocida '" + metric +
                             "'; se admiten EUCLIDEAN, COSINE y DOT");
        }
    }

    // LIMIT is what supplies k, so it is mandatory here. A similarity search with
    // no bound would rank the entire table, which is never the intent.
    Expect(TokenType::kLimit);
    const std::int32_t k = ParseInteger("el número de vecinos (LIMIT)");
    if (k < 0) {
        throw QueryError("LIMIT no puede ser negativo");
    }
    clause.k = static_cast<std::size_t>(k);
    return clause;
}

SelectStatement Parser::ParseSelect() {
    Expect(TokenType::kSelect);

    SelectStatement statement;
    // An empty column list means SELECT *; the projection operator then uses
    // the whole schema.
    if (!Match(TokenType::kAsterisk)) {
        do {
            statement.columns.push_back(ParseSelectItem());
        } while (Match(TokenType::kComma));
    }

    Expect(TokenType::kFrom);
    statement.table_name = ParseIdentifier("tabla");
    statement.where = ParseOptionalWhere();
    statement.group_by = ParseOptionalGroupBy();
    statement.order_by = ParseOptionalOrderBy();
    statement.nearest = ParseOptionalNearest();

    if (statement.nearest.has_value()) {
        // A nearest neighbour query already defines the order of its results —
        // ascending distance — so an ORDER BY on top would either be redundant or
        // contradict it. GROUP BY collapses the rows the ranking is made of.
        if (statement.order_by.has_value()) {
            throw QueryError(
                "NEAREST ya ordena por distancia; no se admite ORDER BY en la misma consulta");
        }
        if (statement.group_by.has_value()) {
            throw QueryError("No se admite GROUP BY en una consulta de vecinos más cercanos");
        }
    } else if (Check(TokenType::kLimit)) {
        // LIMIT exists only to supply k. Accepting it on its own would suggest a
        // general LIMIT operator, which is not implemented.
        throw QueryError(
            "LIMIT solo se admite en una consulta de vecinos más cercanos "
            "(NEAREST <columna> TO [...] LIMIT <k>)");
    }

    // With GROUP BY the output columns are the grouping column and COUNT(*),
    // not the table's columns, so `SELECT *` would be misleading rather than
    // wrong. Rejecting it here gives a better message than letting the
    // projection forward two columns the user did not ask for.
    if (statement.group_by.has_value() && statement.columns.empty()) {
        throw QueryError(
            "SELECT * no se admite con GROUP BY: indique las columnas, "
            "por ejemplo SELECT " +
            statement.group_by->column + ", COUNT(*) FROM " + statement.table_name + " GROUP BY " +
            statement.group_by->column);
    }
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
