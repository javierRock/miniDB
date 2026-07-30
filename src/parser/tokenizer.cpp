#include "minidb/parser/tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "minidb/common/types.hpp"

namespace minidb {
namespace {

/// Keyword lookup, keyed by the upper-cased word.
const std::unordered_map<std::string, TokenType>& Keywords() {
    static const std::unordered_map<std::string, TokenType> keywords = {
        {"CREATE", TokenType::kCreate}, {"TABLE", TokenType::kTable},
        {"INSERT", TokenType::kInsert}, {"INTO", TokenType::kInto},
        {"VALUES", TokenType::kValues}, {"SELECT", TokenType::kSelect},
        {"FROM", TokenType::kFrom},     {"WHERE", TokenType::kWhere},
        {"UPDATE", TokenType::kUpdate}, {"SET", TokenType::kSet},
        {"DELETE", TokenType::kDelete}, {"INT", TokenType::kInt},
        {"VARCHAR", TokenType::kVarchar}, {"PRIMARY", TokenType::kPrimary},
        {"KEY", TokenType::kKey},       {"ORDER", TokenType::kOrder},
        {"GROUP", TokenType::kGroup},   {"BY", TokenType::kBy},
        {"ASC", TokenType::kAsc},       {"DESC", TokenType::kDesc},
        {"COUNT", TokenType::kCount},
    };
    return keywords;
}

[[nodiscard]] std::string ToUpper(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

/// Identifiers are ASCII letters, digits and underscore. Accented characters
/// are allowed only inside string literals, which keeps identifier handling
/// byte-oriented and simple.
[[nodiscard]] bool IsWordStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] bool IsWordPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

}  // namespace

std::string TokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::kIdentifier: return "un identificador";
        case TokenType::kIntegerLiteral: return "un número entero";
        case TokenType::kStringLiteral: return "una cadena";
        case TokenType::kCreate: return "CREATE";
        case TokenType::kTable: return "TABLE";
        case TokenType::kInsert: return "INSERT";
        case TokenType::kInto: return "INTO";
        case TokenType::kValues: return "VALUES";
        case TokenType::kSelect: return "SELECT";
        case TokenType::kFrom: return "FROM";
        case TokenType::kWhere: return "WHERE";
        case TokenType::kUpdate: return "UPDATE";
        case TokenType::kSet: return "SET";
        case TokenType::kDelete: return "DELETE";
        case TokenType::kInt: return "INT";
        case TokenType::kVarchar: return "VARCHAR";
        case TokenType::kPrimary: return "PRIMARY";
        case TokenType::kKey: return "KEY";
        case TokenType::kOrder: return "ORDER";
        case TokenType::kGroup: return "GROUP";
        case TokenType::kBy: return "BY";
        case TokenType::kAsc: return "ASC";
        case TokenType::kDesc: return "DESC";
        case TokenType::kCount: return "COUNT";
        case TokenType::kEqual: return "'='";
        case TokenType::kNotEqual: return "'!='";
        case TokenType::kLess: return "'<'";
        case TokenType::kLessEqual: return "'<='";
        case TokenType::kGreater: return "'>'";
        case TokenType::kGreaterEqual: return "'>='";
        case TokenType::kLeftParenthesis: return "'('";
        case TokenType::kRightParenthesis: return "')'";
        case TokenType::kComma: return "','";
        case TokenType::kAsterisk: return "'*'";
        case TokenType::kSemicolon: return "';'";
        case TokenType::kEndOfInput: return "el final de la sentencia";
    }
    return "un token desconocido";
}

char Tokenizer::PeekNext() const {
    return (cursor_ + 1 < input_.size()) ? input_[cursor_ + 1] : '\0';
}

void Tokenizer::SkipWhitespace() {
    while (!AtEnd() && std::isspace(static_cast<unsigned char>(Peek())) != 0) {
        ++cursor_;
    }
}

Token Tokenizer::ReadWord() {
    const std::size_t start = cursor_;
    while (!AtEnd() && IsWordPart(Peek())) {
        ++cursor_;
    }

    const std::string word = input_.substr(start, cursor_ - start);
    const auto keyword = Keywords().find(ToUpper(word));

    return Token{keyword != Keywords().end() ? keyword->second : TokenType::kIdentifier, word,
                 start + 1};
}

Token Tokenizer::ReadNumber() {
    const std::size_t start = cursor_;
    if (Peek() == '-') {
        ++cursor_;
    }
    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
        ++cursor_;
    }
    return Token{TokenType::kIntegerLiteral, input_.substr(start, cursor_ - start), start + 1};
}

Token Tokenizer::ReadString() {
    const std::size_t start = cursor_;
    ++cursor_;  // opening quote

    std::string value;
    while (true) {
        if (AtEnd()) {
            throw QueryError("Cadena sin cerrar que empieza en la posición " +
                             std::to_string(start + 1));
        }
        if (Peek() == '\'') {
            // Two quotes in a row are an escaped quote, as in standard SQL.
            if (PeekNext() == '\'') {
                value.push_back('\'');
                cursor_ += 2;
                continue;
            }
            ++cursor_;  // closing quote
            break;
        }
        // Bytes are copied verbatim, so multi-byte UTF-8 characters survive
        // without the tokenizer needing to understand them.
        value.push_back(input_[cursor_]);
        ++cursor_;
    }

    return Token{TokenType::kStringLiteral, value, start + 1};
}

Token Tokenizer::ReadOperatorOrPunctuation() {
    const std::size_t start = cursor_;
    const char c = Peek();

    auto make = [&](TokenType type, std::size_t length) {
        cursor_ += length;
        return Token{type, input_.substr(start, length), start + 1};
    };

    switch (c) {
        case '=': return make(TokenType::kEqual, 1);
        case '(': return make(TokenType::kLeftParenthesis, 1);
        case ')': return make(TokenType::kRightParenthesis, 1);
        case ',': return make(TokenType::kComma, 1);
        case '*': return make(TokenType::kAsterisk, 1);
        case ';': return make(TokenType::kSemicolon, 1);
        case '<':
            if (PeekNext() == '=') return make(TokenType::kLessEqual, 2);
            if (PeekNext() == '>') return make(TokenType::kNotEqual, 2);
            return make(TokenType::kLess, 1);
        case '>':
            if (PeekNext() == '=') return make(TokenType::kGreaterEqual, 2);
            return make(TokenType::kGreater, 1);
        case '!':
            if (PeekNext() == '=') return make(TokenType::kNotEqual, 2);
            throw QueryError("Se esperaba '!=' en la posición " + std::to_string(start + 1));
        default:
            throw QueryError("Carácter no reconocido '" + std::string(1, c) +
                             "' en la posición " + std::to_string(start + 1));
    }
}

std::vector<Token> Tokenizer::Tokenize() {
    std::vector<Token> tokens;
    cursor_ = 0;

    while (true) {
        SkipWhitespace();
        if (AtEnd()) {
            break;
        }

        const char c = Peek();
        if (IsWordStart(c)) {
            tokens.push_back(ReadWord());
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            tokens.push_back(ReadNumber());
        } else if (c == '-' && std::isdigit(static_cast<unsigned char>(PeekNext())) != 0) {
            tokens.push_back(ReadNumber());
        } else if (c == '\'') {
            tokens.push_back(ReadString());
        } else {
            tokens.push_back(ReadOperatorOrPunctuation());
        }
    }

    tokens.push_back(Token{TokenType::kEndOfInput, "", input_.size() + 1});
    return tokens;
}

}  // namespace minidb
