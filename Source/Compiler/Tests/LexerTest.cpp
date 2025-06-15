#include "Lexer/Lexer.hpp"
#include "Lexer/Token.hpp"

#include <gtest/gtest.h>
#include <memory>

// 词法分析器测试夹具
class LexerTest : public ::testing::Test
{
protected:
    std::shared_ptr<Context> context;
    std::unique_ptr<Lexer> lexer;

    void SetUp() override
    {
        context = std::make_shared<Context>();
        context->filePath = "test.lis";
        lexer = std::make_unique<Lexer>(context);
    }

    void runLexer(const std::string &source)
    {
        context->fileValue = source;
        lexer->run();
    }

    void expectToken(size_t index, TokenCode code, const std::string &value, size_t line, size_t col)
    {
        const TokenStream &tokens = context->tokenStream;
        ASSERT_GT(tokens.size(), index)
            << "Token index out of range: " << index;

        const Token &tok = tokens[index];
        EXPECT_EQ(tok.code, code)
            << "Expected code: " << static_cast<int>(code)
            << ", got: " << static_cast<int>(tok.code);
        EXPECT_EQ(tok.value, value)
            << "Expected value: " << value << ", got: " << tok.value;
        EXPECT_EQ(tok.line, line)
            << "Expected line: " << line << ", got: " << tok.line;
        EXPECT_EQ(tok.col, col)
            << "Expected col: " << col << ", got: " << tok.col;
    }
};

// 测试用例
TEST_F(LexerTest, HandlesEmptyInput)
{
    runLexer("");
    EXPECT_EQ(context->tokenStream.size(), 0);
}

TEST_F(LexerTest, RecognizesKeywords)
{
    runLexer("impt struct fn let mut pub ret if else for while");

    expectToken(0, TokenCode::IMPT, "impt", 1, 1);
    expectToken(1, TokenCode::STRUCT, "struct", 1, 6);
    expectToken(2, TokenCode::FN, "fn", 1, 13);
    expectToken(3, TokenCode::LET, "let", 1, 16);
    expectToken(4, TokenCode::MUT, "mut", 1, 20);
    expectToken(5, TokenCode::PUB, "pub", 1, 24);
    expectToken(6, TokenCode::RET, "ret", 1, 28);
    expectToken(7, TokenCode::IF, "if", 1, 32);
    expectToken(8, TokenCode::ELSE, "else", 1, 35);
    expectToken(9, TokenCode::FOR, "for", 1, 40);
    expectToken(10, TokenCode::WHILE, "while", 1, 44);
}

TEST_F(LexerTest, RecognizesTypeKeywords)
{
    runLexer("i8 i16 i32 i64 f32 f64 bool char");

    expectToken(0, TokenCode::I8, "i8", 1, 1);
    expectToken(1, TokenCode::I16, "i16", 1, 4);
    expectToken(2, TokenCode::I32, "i32", 1, 8);
    expectToken(3, TokenCode::I64, "i64", 1, 12);
    expectToken(4, TokenCode::F32, "f32", 1, 16);
    expectToken(5, TokenCode::F64, "f64", 1, 20);
    expectToken(6, TokenCode::BOOL, "bool", 1, 24);
    expectToken(7, TokenCode::CHAR, "char", 1, 29);
}

TEST_F(LexerTest, RecognizesBooleanLiterals)
{
    runLexer("true false");
    expectToken(0, TokenCode::BOOLEAN_TRUE, "true", 1, 1);
    expectToken(1, TokenCode::BOOLEAN_FALSE, "false", 1, 6);
}

TEST_F(LexerTest, RecognizesIdentifiers)
{
    runLexer("variable_name _test123 TestClass");
    expectToken(0, TokenCode::IDENTIFIER, "variable_name", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "_test123", 1, 15);
    expectToken(2, TokenCode::IDENTIFIER, "TestClass", 1, 24);
}

TEST_F(LexerTest, DistinguishesKeywordsFromIdentifiers)
{
    runLexer("iff rett true_value");
    expectToken(0, TokenCode::IDENTIFIER, "iff", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "rett", 1, 5);
    expectToken(2, TokenCode::IDENTIFIER, "true_value", 1, 10);
}

TEST_F(LexerTest, HandlesIntegerLiterals)
{
    runLexer("0 123 999 42");
    expectToken(0, TokenCode::INT_LITERAL, "0", 1, 1);
    expectToken(1, TokenCode::INT_LITERAL, "123", 1, 3);
    expectToken(2, TokenCode::INT_LITERAL, "999", 1, 7);
    expectToken(3, TokenCode::INT_LITERAL, "42", 1, 11);
}

TEST_F(LexerTest, HandlesFloatLiterals)
{
    runLexer("3.14 0.5 2.71828 1e10 2.5e-3");
    expectToken(0, TokenCode::FLOAT_LITERAL, "3.14", 1, 1);
    expectToken(1, TokenCode::FLOAT_LITERAL, "0.5", 1, 6);
    expectToken(2, TokenCode::FLOAT_LITERAL, "2.71828", 1, 10);
    expectToken(3, TokenCode::FLOAT_LITERAL, "1e10", 1, 18);
    expectToken(4, TokenCode::FLOAT_LITERAL, "2.5e-3", 1, 23);
}

TEST_F(LexerTest, RecognizesOperators)
{
    runLexer("+ - * / == != < > <= >= = ! | || &&");

    expectToken(0, TokenCode::PLUS, "+", 1, 1);
    expectToken(1, TokenCode::MINUS, "-", 1, 3);
    expectToken(2, TokenCode::STAR, "*", 1, 5);
    expectToken(3, TokenCode::SLASH, "/", 1, 7);
    expectToken(4, TokenCode::EQ_EQ, "==", 1, 9);
    expectToken(5, TokenCode::NOT_EQ, "!=", 1, 12);
    expectToken(6, TokenCode::LT, "<", 1, 15);
    expectToken(7, TokenCode::GT, ">", 1, 17);
    expectToken(8, TokenCode::LT_EQ, "<=", 1, 19);
    expectToken(9, TokenCode::GT_EQ, ">=", 1, 22);
    expectToken(10, TokenCode::ASSIGN, "=", 1, 25);
    expectToken(11, TokenCode::NOT, "!", 1, 27);
    expectToken(12, TokenCode::BOR, "|", 1, 29);
    expectToken(13, TokenCode::OR, "||", 1, 31);
    expectToken(14, TokenCode::AND, "&&", 1, 34);
}

TEST_F(LexerTest, RecognizesDelimiters)
{
    runLexer("() {} , ; . :: -> => &");

    expectToken(0, TokenCode::LPAREN, "(", 1, 1);
    expectToken(1, TokenCode::RPAREN, ")", 1, 2);
    expectToken(2, TokenCode::LBRACE, "{", 1, 4);
    expectToken(3, TokenCode::RBRACE, "}", 1, 5);
    expectToken(4, TokenCode::COMMA, ",", 1, 7);
    expectToken(5, TokenCode::SEMI, ";", 1, 9);
    expectToken(6, TokenCode::DOT, ".", 1, 11);
    expectToken(7, TokenCode::DOUBLE_COLON, "::", 1, 13);
    expectToken(8, TokenCode::ARROW, "->", 1, 16);
    expectToken(9, TokenCode::DOUBLE_ARROW, "=>", 1, 19);
    expectToken(10, TokenCode::REFERENCE, "&", 1, 22);
}

TEST_F(LexerTest, HandlesMixedTokens)
{
    runLexer("fn add(a: i32, b: i32) -> i32 { ret a + b }");

    expectToken(0, TokenCode::FN, "fn", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "add", 1, 4);
    expectToken(2, TokenCode::LPAREN, "(", 1, 7);
    expectToken(3, TokenCode::IDENTIFIER, "a", 1, 8);
    expectToken(4, TokenCode::COLON, ":", 1, 9);
    expectToken(5, TokenCode::I32, "i32", 1, 11);
    expectToken(6, TokenCode::COMMA, ",", 1, 14);
    expectToken(7, TokenCode::IDENTIFIER, "b", 1, 16);
    expectToken(8, TokenCode::COLON, ":", 1, 17);
    expectToken(9, TokenCode::I32, "i32", 1, 19);
    expectToken(10, TokenCode::RPAREN, ")", 1, 22);
    expectToken(11, TokenCode::ARROW, "->", 1, 24);
    expectToken(12, TokenCode::I32, "i32", 1, 27);
    expectToken(13, TokenCode::LBRACE, "{", 1, 31);
    expectToken(14, TokenCode::RET, "ret", 1, 33);
    expectToken(15, TokenCode::IDENTIFIER, "a", 1, 37);
    expectToken(16, TokenCode::PLUS, "+", 1, 39);
    expectToken(17, TokenCode::IDENTIFIER, "b", 1, 41);
    expectToken(18, TokenCode::RBRACE, "}", 1, 43);
}

TEST_F(LexerTest, SkipsWhitespace)
{
    runLexer("  \t\n  let \t\nmut ");

    expectToken(0, TokenCode::LET, "let", 2, 3);
    expectToken(1, TokenCode::MUT, "mut", 3, 1);
}

TEST_F(LexerTest, SkipsLineComments)
{
    runLexer("// This is a comment\nlet x = 42; // Another comment");

    expectToken(0, TokenCode::LET, "let", 2, 1);
    expectToken(1, TokenCode::IDENTIFIER, "x", 2, 5);
    expectToken(2, TokenCode::ASSIGN, "=", 2, 7);
    expectToken(3, TokenCode::INT_LITERAL, "42", 2, 9);
    expectToken(4, TokenCode::SEMI, ";", 2, 11);
}

TEST_F(LexerTest, SkipsBlockComments)
{
    runLexer("/* Comment \n spanning \n multiple \n lines */ let x");

    expectToken(0, TokenCode::LET, "let", 4, 11);
    expectToken(1, TokenCode::IDENTIFIER, "x", 4, 15);
}

TEST_F(LexerTest, HandlesPositionTracking)
{
    runLexer("fn\n  add(\n a : i32)");

    expectToken(0, TokenCode::FN, "fn", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "add", 2, 3);
    expectToken(2, TokenCode::LPAREN, "(", 2, 6);
    expectToken(3, TokenCode::IDENTIFIER, "a", 3, 2);
    expectToken(4, TokenCode::COLON, ":", 3, 4);
    expectToken(5, TokenCode::I32, "i32", 3, 6);
    expectToken(6, TokenCode::RPAREN, ")", 3, 9);
}

TEST_F(LexerTest, HandlesComplexExpressions)
{
    runLexer("if x >= 10 { ret x * 2.5; } else { ret 0; }");

    expectToken(0, TokenCode::IF, "if", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "x", 1, 4);
    expectToken(2, TokenCode::GT_EQ, ">=", 1, 6);
    expectToken(3, TokenCode::INT_LITERAL, "10", 1, 9);
    expectToken(4, TokenCode::LBRACE, "{", 1, 12);
    expectToken(5, TokenCode::RET, "ret", 1, 14);
    expectToken(6, TokenCode::IDENTIFIER, "x", 1, 18);
    expectToken(7, TokenCode::STAR, "*", 1, 20);
    expectToken(8, TokenCode::FLOAT_LITERAL, "2.5", 1, 22);
    expectToken(9, TokenCode::SEMI, ";", 1, 25);
    expectToken(10, TokenCode::RBRACE, "}", 1, 27);
    expectToken(11, TokenCode::ELSE, "else", 1, 29);
    expectToken(12, TokenCode::LBRACE, "{", 1, 34);
    expectToken(13, TokenCode::RET, "ret", 1, 36);
    expectToken(14, TokenCode::INT_LITERAL, "0", 1, 40);
    expectToken(15, TokenCode::SEMI, ";", 1, 41);
    expectToken(16, TokenCode::RBRACE, "}", 1, 43);
}