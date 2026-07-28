#include "minidb/parser/parser.hpp"

#include <gtest/gtest.h>

#include <string>

#include "minidb/common/types.hpp"

namespace minidb {
namespace {

/// Returns a copy on purpose: binding a reference to a member of a temporary
/// Statement would dangle as soon as the full expression ends.
template <typename T>
T As(const Statement& statement) {
    return std::get<T>(statement);
}

// --- Tokenizer -----------------------------------------------------------

TEST(TokenizerTest, ProducesTheExpectedTokenSequence) {
    const auto tokens = Tokenizer("SELECT * FROM students;").Tokenize();

    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::kSelect);
    EXPECT_EQ(tokens[1].type, TokenType::kAsterisk);
    EXPECT_EQ(tokens[2].type, TokenType::kFrom);
    EXPECT_EQ(tokens[3].type, TokenType::kIdentifier);
    EXPECT_EQ(tokens[3].text, "students");
    EXPECT_EQ(tokens[4].type, TokenType::kSemicolon);
    EXPECT_EQ(tokens[5].type, TokenType::kEndOfInput);
}

TEST(TokenizerTest, RecognisesEveryComparisonOperator) {
    const auto tokens = Tokenizer("= != <> < <= > >=").Tokenize();

    EXPECT_EQ(tokens[0].type, TokenType::kEqual);
    EXPECT_EQ(tokens[1].type, TokenType::kNotEqual);
    EXPECT_EQ(tokens[2].type, TokenType::kNotEqual);  // SQL's <> spelling
    EXPECT_EQ(tokens[3].type, TokenType::kLess);
    EXPECT_EQ(tokens[4].type, TokenType::kLessEqual);
    EXPECT_EQ(tokens[5].type, TokenType::kGreater);
    EXPECT_EQ(tokens[6].type, TokenType::kGreaterEqual);
}

TEST(TokenizerTest, KeywordsAreCaseInsensitiveButIdentifiersKeepTheirText) {
    const auto tokens = Tokenizer("SeLeCt * FrOm Students").Tokenize();

    EXPECT_EQ(tokens[0].type, TokenType::kSelect);
    EXPECT_EQ(tokens[2].type, TokenType::kFrom);
    EXPECT_EQ(tokens[3].type, TokenType::kIdentifier);
    EXPECT_EQ(tokens[3].text, "Students");  // original case preserved
}

TEST(TokenizerTest, ReadsStringsWithSpacesAndAccents) {
    const auto tokens = Tokenizer("'Ciencia de la Computación'").Tokenize();

    ASSERT_EQ(tokens[0].type, TokenType::kStringLiteral);
    EXPECT_EQ(tokens[0].text, "Ciencia de la Computación");
    EXPECT_EQ(tokens[0].text.size(), 26u);  // bytes, not characters
}

TEST(TokenizerTest, DoubledQuoteIsAnEscapedQuote) {
    const auto tokens = Tokenizer("'D''Angelo'").Tokenize();
    EXPECT_EQ(tokens[0].text, "D'Angelo");
}

TEST(TokenizerTest, ReadsNegativeIntegers) {
    const auto tokens = Tokenizer("-42").Tokenize();
    EXPECT_EQ(tokens[0].type, TokenType::kIntegerLiteral);
    EXPECT_EQ(tokens[0].text, "-42");
}

TEST(TokenizerTest, ReportsUnterminatedStringsAndUnknownCharacters) {
    EXPECT_THROW((void)Tokenizer("'sin cerrar").Tokenize(), QueryError);
    EXPECT_THROW((void)Tokenizer("SELECT # FROM t").Tokenize(), QueryError);
    EXPECT_THROW((void)Tokenizer("a ! b").Tokenize(), QueryError);
}

TEST(TokenizerTest, PositionsPointAtTheOffendingCharacter) {
    try {
        (void)Tokenizer("SELECT # FROM t").Tokenize();
        FAIL() << "se esperaba un error";
    } catch (const QueryError& error) {
        EXPECT_NE(std::string(error.what()).find("8"), std::string::npos)
            << "el mensaje debería señalar la posición 8: " << error.what();
    }
}

// --- CREATE TABLE --------------------------------------------------------

TEST(ParserTest, ParsesCreateTable) {
    const Statement statement = Parser::Parse(
        "CREATE TABLE students ("
        "    id INT PRIMARY KEY,"
        "    name VARCHAR(50),"
        "    age INT,"
        "    career VARCHAR(50)"
        ");");
    const auto create = As<CreateTableStatement>(statement);

    EXPECT_EQ(create.table_name, "students");
    ASSERT_EQ(create.columns.size(), 4u);

    EXPECT_EQ(create.columns[0].name, "id");
    EXPECT_EQ(create.columns[0].type, ColumnType::kInteger);
    EXPECT_TRUE(create.columns[0].is_primary_key);

    EXPECT_EQ(create.columns[1].name, "name");
    EXPECT_EQ(create.columns[1].type, ColumnType::kVarchar);
    EXPECT_EQ(create.columns[1].max_length, 50u);
    EXPECT_FALSE(create.columns[1].is_primary_key);

    EXPECT_EQ(create.columns[3].name, "career");
    EXPECT_EQ(create.columns[3].max_length, 50u);
}

TEST(ParserTest, RejectsBadVarcharLengths) {
    EXPECT_THROW((void)Parser::Parse("CREATE TABLE t (id INT PRIMARY KEY, s VARCHAR(0))"),
                 QueryError);
    EXPECT_THROW((void)Parser::Parse("CREATE TABLE t (id INT PRIMARY KEY, s VARCHAR(9999))"),
                 QueryError);
    EXPECT_THROW((void)Parser::Parse("CREATE TABLE t (id INT PRIMARY KEY, s VARCHAR)"), QueryError);
}

TEST(ParserTest, RejectsUnknownColumnTypesAndMalformedDefinitions) {
    EXPECT_THROW((void)Parser::Parse("CREATE TABLE t (id FLOAT)"), QueryError);
    EXPECT_THROW((void)Parser::Parse("CREATE TABLE t (id INT PRIMARY)"), QueryError);
    EXPECT_THROW((void)Parser::Parse("CREATE TABLE t (id INT"), QueryError);
    EXPECT_THROW((void)Parser::Parse("CREATE TABLE (id INT)"), QueryError);
}

// --- INSERT --------------------------------------------------------------

TEST(ParserTest, ParsesInsert) {
    const Statement statement = Parser::Parse(
        "INSERT INTO students VALUES (1, 'Ana', 20, 'Ciencia de la Computación')");
    const auto insert = As<InsertStatement>(statement);

    EXPECT_EQ(insert.table_name, "students");
    ASSERT_EQ(insert.values.size(), 4u);
    EXPECT_EQ(std::get<std::int32_t>(insert.values[0]), 1);
    EXPECT_EQ(std::get<std::string>(insert.values[1]), "Ana");
    EXPECT_EQ(std::get<std::int32_t>(insert.values[2]), 20);
    EXPECT_EQ(std::get<std::string>(insert.values[3]), "Ciencia de la Computación");
}

TEST(ParserTest, ParsesInsertWithNegativeValues) {
    const auto insert =
        As<InsertStatement>(Parser::Parse("INSERT INTO t VALUES (-5, 'x', -273, 'y')"));
    EXPECT_EQ(std::get<std::int32_t>(insert.values[0]), -5);
    EXPECT_EQ(std::get<std::int32_t>(insert.values[2]), -273);
}

TEST(ParserTest, RejectsIntegersThatDoNotFitInThirtyTwoBits) {
    EXPECT_THROW((void)Parser::Parse("INSERT INTO t VALUES (99999999999999)"), QueryError);
}

// --- SELECT --------------------------------------------------------------

TEST(ParserTest, ParsesSelectStar) {
    const auto select = As<SelectStatement>(Parser::Parse("SELECT * FROM students"));

    EXPECT_EQ(select.table_name, "students");
    EXPECT_TRUE(select.columns.empty());  // empty means "every column"
    EXPECT_FALSE(select.where.has_value());
}

TEST(ParserTest, ParsesSelectWithAnExplicitColumnList) {
    const auto select = As<SelectStatement>(Parser::Parse("SELECT id, name FROM students"));

    ASSERT_EQ(select.columns.size(), 2u);
    EXPECT_EQ(select.columns[0], "id");
    EXPECT_EQ(select.columns[1], "name");
}

TEST(ParserTest, ParsesSelectWithEveryComparisonOperator) {
    struct Case {
        std::string sql;
        CompareOperator expected;
    };
    const Case cases[] = {
        {"SELECT * FROM t WHERE age = 20", CompareOperator::kEqual},
        {"SELECT * FROM t WHERE age != 20", CompareOperator::kNotEqual},
        {"SELECT * FROM t WHERE age <> 20", CompareOperator::kNotEqual},
        {"SELECT * FROM t WHERE age < 20", CompareOperator::kLess},
        {"SELECT * FROM t WHERE age <= 20", CompareOperator::kLessEqual},
        {"SELECT * FROM t WHERE age > 20", CompareOperator::kGreater},
        {"SELECT * FROM t WHERE age >= 20", CompareOperator::kGreaterEqual},
    };

    for (const Case& test_case : cases) {
        const auto select = As<SelectStatement>(Parser::Parse(test_case.sql));
        ASSERT_TRUE(select.where.has_value()) << test_case.sql;
        EXPECT_EQ(select.where->op, test_case.expected) << test_case.sql;
        EXPECT_EQ(select.where->column, "age");
        EXPECT_EQ(std::get<std::int32_t>(select.where->value), 20);
    }
}

TEST(ParserTest, ParsesSelectWithAStringCondition) {
    const auto select = As<SelectStatement>(
        Parser::Parse("SELECT * FROM students WHERE career = 'Ingeniería de Sistemas'"));

    ASSERT_TRUE(select.where.has_value());
    EXPECT_EQ(select.where->column, "career");
    EXPECT_EQ(std::get<std::string>(select.where->value), "Ingeniería de Sistemas");
}

// --- UPDATE and DELETE ---------------------------------------------------

TEST(ParserTest, ParsesUpdate) {
    const auto update =
        As<UpdateStatement>(Parser::Parse("UPDATE students SET age = 21 WHERE id = 1"));

    EXPECT_EQ(update.table_name, "students");
    ASSERT_EQ(update.assignments.size(), 1u);
    EXPECT_EQ(update.assignments[0].column, "age");
    EXPECT_EQ(std::get<std::int32_t>(update.assignments[0].value), 21);
    ASSERT_TRUE(update.where.has_value());
    EXPECT_EQ(update.where->column, "id");
}

TEST(ParserTest, ParsesUpdateWithSeveralAssignments) {
    const auto update = As<UpdateStatement>(
        Parser::Parse("UPDATE students SET age = 22, career = 'Ingeniería' WHERE id = 3"));

    ASSERT_EQ(update.assignments.size(), 2u);
    EXPECT_EQ(update.assignments[0].column, "age");
    EXPECT_EQ(update.assignments[1].column, "career");
    EXPECT_EQ(std::get<std::string>(update.assignments[1].value), "Ingeniería");
}

TEST(ParserTest, ParsesUpdateAndDeleteWithoutWhere) {
    const auto update = As<UpdateStatement>(Parser::Parse("UPDATE students SET age = 0"));
    EXPECT_FALSE(update.where.has_value());

    const auto remove = As<DeleteStatement>(Parser::Parse("DELETE FROM students"));
    EXPECT_EQ(remove.table_name, "students");
    EXPECT_FALSE(remove.where.has_value());
}

TEST(ParserTest, ParsesDelete) {
    const auto remove = As<DeleteStatement>(Parser::Parse("DELETE FROM students WHERE id = 2"));

    EXPECT_EQ(remove.table_name, "students");
    ASSERT_TRUE(remove.where.has_value());
    EXPECT_EQ(remove.where->op, CompareOperator::kEqual);
    EXPECT_EQ(std::get<std::int32_t>(remove.where->value), 2);
}

// --- Syntax and formatting -----------------------------------------------

TEST(ParserTest, SemicolonIsOptionalAndWhitespaceIsFree) {
    EXPECT_NO_THROW((void)Parser::Parse("SELECT * FROM students"));
    EXPECT_NO_THROW((void)Parser::Parse("SELECT * FROM students;"));
    EXPECT_NO_THROW((void)Parser::Parse("  SELECT\n *\t FROM\n  students  ;  "));
}

TEST(ParserTest, KeywordsWorkInAnyCase) {
    EXPECT_NO_THROW((void)Parser::Parse("select * from students where id = 1"));
    EXPECT_NO_THROW((void)Parser::Parse("SeLeCt * FrOm students WhErE id = 1"));
    EXPECT_NO_THROW(
        (void)Parser::Parse("create table t (id int primary key, n varchar(10))"));
}

TEST(ParserTest, RejectsTrailingTokensAfterTheStatement) {
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM students basura"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM students; SELECT * FROM students"),
                 QueryError);
}

TEST(ParserTest, RejectsEmptyInputAndUnknownStatements) {
    EXPECT_THROW((void)Parser::Parse(""), QueryError);
    EXPECT_THROW((void)Parser::Parse("   "), QueryError);
    EXPECT_THROW((void)Parser::Parse("DROP TABLE students"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECCIONAR * FROM students"), QueryError);
}

TEST(ParserTest, RejectsIncompleteStatements) {
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT FROM students"), QueryError);
    EXPECT_THROW((void)Parser::Parse("INSERT INTO students VALUES"), QueryError);
    EXPECT_THROW((void)Parser::Parse("INSERT INTO students VALUES (1, 'Ana'"), QueryError);
    EXPECT_THROW((void)Parser::Parse("UPDATE students SET"), QueryError);
    EXPECT_THROW((void)Parser::Parse("UPDATE students SET age 21"), QueryError);
    EXPECT_THROW((void)Parser::Parse("DELETE students"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM t WHERE age"), QueryError);
    EXPECT_THROW((void)Parser::Parse("SELECT * FROM t WHERE age >"), QueryError);
}

// Error messages have to help the user fix the statement, so they carry a
// position, what was expected and what actually turned up.
TEST(ParserTest, ErrorMessagesNamePositionExpectedAndReceived) {
    try {
        (void)Parser::Parse("SELECT * FROM 123");
        FAIL() << "se esperaba un error";
    } catch (const QueryError& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("posición"), std::string::npos) << message;
        EXPECT_NE(message.find("se esperaba"), std::string::npos) << message;
        EXPECT_NE(message.find("se encontró"), std::string::npos) << message;
        EXPECT_NE(message.find("123"), std::string::npos) << message;
    }
}

TEST(ParserTest, ErrorMessagesAreInSpanish) {
    try {
        (void)Parser::Parse("SELECT * FROM t WHERE age");
        FAIL() << "se esperaba un error";
    } catch (const QueryError& error) {
        EXPECT_NE(std::string(error.what()).find("operador de comparación"), std::string::npos)
            << error.what();
    }
}

}  // namespace
}  // namespace minidb
