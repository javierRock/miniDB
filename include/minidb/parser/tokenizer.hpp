#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace minidb {

enum class TokenType {
    // Literals and names
    kIdentifier,
    kIntegerLiteral,
    kStringLiteral,

    // Keywords
    kCreate,
    kTable,
    kInsert,
    kInto,
    kValues,
    kSelect,
    kFrom,
    kWhere,
    kUpdate,
    kSet,
    kDelete,
    kInt,
    kVarchar,
    kPrimary,
    kKey,

    // Comparison operators
    kEqual,
    kNotEqual,
    kLess,
    kLessEqual,
    kGreater,
    kGreaterEqual,

    // Punctuation
    kLeftParenthesis,
    kRightParenthesis,
    kComma,
    kAsterisk,
    kSemicolon,

    kEndOfInput,
};

/// Human-readable name of a token type, used in error messages.
[[nodiscard]] std::string TokenTypeName(TokenType type);

struct Token {
    TokenType type = TokenType::kEndOfInput;
    /// Raw text, for identifiers, integers and string contents.
    std::string text;
    /// 1-based character position where the token starts, for error messages.
    std::size_t position = 0;
};

/// Turns SQL text into a token list.
///
/// Written as a straightforward character scan rather than one large regular
/// expression, so each token kind is visible in the code and error positions
/// are exact.
///
/// Keywords are recognised without regard to case. String literals are read as
/// raw bytes between single quotes, which keeps multi-byte UTF-8 characters
/// intact.
class Tokenizer {
public:
    explicit Tokenizer(std::string input) : input_(std::move(input)) {}

    /// Scans the whole input. The list always ends with kEndOfInput.
    /// Throws QueryError on an unterminated string or an unknown character.
    [[nodiscard]] std::vector<Token> Tokenize();

private:
    [[nodiscard]] bool AtEnd() const { return cursor_ >= input_.size(); }
    [[nodiscard]] char Peek() const { return input_[cursor_]; }
    [[nodiscard]] char PeekNext() const;

    void SkipWhitespace();
    [[nodiscard]] Token ReadWord();
    [[nodiscard]] Token ReadNumber();
    [[nodiscard]] Token ReadString();
    [[nodiscard]] Token ReadOperatorOrPunctuation();

    std::string input_;
    std::size_t cursor_ = 0;
};

}  // namespace minidb
