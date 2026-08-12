/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include "Lexer/Lexer.hpp"
#include "Lexer/Token.hpp"
#include "Logger/Logger.hpp"

#include <gtest/gtest.h>
#include <memory>

// The test class for lexer
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
        EXPECT_EQ(tok.position.line, line)
            << "Expected line: " << line << ", got: " << tok.position.line;
        EXPECT_EQ(tok.position.col, col)
            << "Expected col: " << col << ", got: " << tok.position.col;
    }
};

// Test samples
TEST_F(LexerTest, HandlesEmptyInput)
{
    runLexer("");
    EXPECT_EQ(context->tokenStream.size(), 0);
}

TEST_F(LexerTest, RecognizesKeywords)
{
    // P15: `impt` is lexed as a keyword token, but the import system is NOT
    // implemented — the Parser rejects `impt` at any scope ("illegal global
    // statement"). This assertion only pins the lexer's keyword table; it is
    // NOT a promise that `impt` is usable source. Do not add an import feature
    // without a language-level decision (it is out of scope for this compiler).
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

// ── P1 regression: malformed / overflowing numeric literals ──────────────────
// Before the fix, `1e`/`1e+` (exponent with no digits) and integers too large
// for int64 were accepted by the lexer and crashed HIRBuilder's unguarded
// std::stoll/std::stod with std::terminate. The lexer now reports a clean
// E_InvalidLiteralType instead.

TEST_F(LexerTest, MalformedFloatExponentReportsError)
{
    runLexer("1e");
    ASSERT_GT(context->tokenStream.size(), 0);
    EXPECT_EQ(context->tokenStream[0].code, TokenCode::FLOAT_LITERAL);
    EXPECT_GT(Logger::GetErrorCount(), 0) << "malformed exponent must report an error";
}

TEST_F(LexerTest, MalformedFloatExponentWithSignReportsError)
{
    runLexer("1e+ 2e-");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "exponent with sign but no digits must report an error";
    EXPECT_GT(context->tokenStream.size(), 1) << "lexing continues after the error (both tokens emitted)";
}

TEST_F(LexerTest, HugeIntegerLiteralLexesCleanly)
{
    // A 20-digit integer is lexically a valid digit run; the int64 overflow is
    // reported by the SEMANTIC analyzer (RuntimeTest.ValueIntegerLiteralOverflow
    // Rejected), NOT here. It must not be an error at lex time — the array-size
    // parser relies on huge literals reaching it as tokens so it can clamp them
    // to its own sentinel and sema reports the nicer "exceeds the limit" error.
    runLexer("99999999999999999999");
    ASSERT_GT(context->tokenStream.size(), 0);
    EXPECT_EQ(context->tokenStream[0].code, TokenCode::INT_LITERAL);
    EXPECT_EQ(Logger::GetErrorCount(), 0) << "huge integer must lex cleanly (overflow is a sema error)";
}

TEST_F(LexerTest, ValidNumericLiteralsReportNoError)
{
    runLexer("0 42 1e10 2.5e-3 3.14");
    EXPECT_EQ(Logger::GetErrorCount(), 0) << "valid numeric literals must not report errors";
}

// ── D: string & char literals ──────────────────────────────────────────────────

TEST_F(LexerTest, HandlesStringLiteral)
{
    runLexer("\"hello\"");
    expectToken(0, TokenCode::STRING_LITERAL, "hello", 1, 1);
}

TEST_F(LexerTest, HandlesEmptyStringLiteral)
{
    runLexer("\"\"");
    expectToken(0, TokenCode::STRING_LITERAL, "", 1, 1);
}

TEST_F(LexerTest, HandlesStringNewlineEscape)
{
    // .lis `"a\nb"` → the escape is processed into a real newline in the value.
    runLexer("\"a\\nb\"");
    expectToken(0, TokenCode::STRING_LITERAL, "a\nb", 1, 1);
}

TEST_F(LexerTest, HandlesStringTabEscape)
{
    runLexer("\"a\\tb\"");
    expectToken(0, TokenCode::STRING_LITERAL, "a\tb", 1, 1);
}

TEST_F(LexerTest, HandlesStringQuoteEscape)
{
    runLexer("\"say \\\"hi\\\"\"");
    expectToken(0, TokenCode::STRING_LITERAL, "say \"hi\"", 1, 1);
}

TEST_F(LexerTest, HandlesStringBackslashEscape)
{
    runLexer("\"a\\\\b\"");
    expectToken(0, TokenCode::STRING_LITERAL, "a\\b", 1, 1);
}

TEST_F(LexerTest, HandlesCharLiteral)
{
    runLexer("'a'");
    expectToken(0, TokenCode::CHAR_LITERAL, "a", 1, 1);
}

TEST_F(LexerTest, HandlesCharNewlineEscape)
{
    runLexer("'\\n'");
    expectToken(0, TokenCode::CHAR_LITERAL, "\n", 1, 1);
}

TEST_F(LexerTest, HandlesCharQuoteEscape)
{
    runLexer("'\\''");
    expectToken(0, TokenCode::CHAR_LITERAL, "'", 1, 1);
}

TEST_F(LexerTest, HandlesCharNulEscape)
{
    runLexer("'\\0'");
    expectToken(0, TokenCode::CHAR_LITERAL, std::string(1, '\0'), 1, 1);
}

TEST_F(LexerTest, UnclosedStringReportsError)
{
    runLexer("\"abc");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "unclosed string must report an error";
    ASSERT_GT(context->tokenStream.size(), 0);
    EXPECT_EQ(context->tokenStream[0].code, TokenCode::STRING_LITERAL);
}

TEST_F(LexerTest, UnclosedCharReportsError)
{
    runLexer("'a");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "unclosed char must report an error";
}

TEST_F(LexerTest, CharAtImmediateEofReportsError)
{
    runLexer("'");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "bare quote at EOF must report an error";
}

TEST_F(LexerTest, CharEscapeAtEofReportsError)
{
    runLexer("'\\");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "escape at EOF must report an error";
}

// ── D: comments ────────────────────────────────────────────────────────────────

TEST_F(LexerTest, UnclosedBlockCommentReportsError)
{
    runLexer("/* no closing");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "unclosed block comment must report an error";
}

TEST_F(LexerTest, LineCommentAtEofNoError)
{
    runLexer("// comment with no newline at EOF");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
    EXPECT_EQ(context->tokenStream.size(), 0);
}

TEST_F(LexerTest, BlockCommentWithSpecialChars)
{
    // Block comments close at the first `*/`; a nested-looking `/*` is data.
    // `/* a /* b */` occupies cols 1-12, so `x` is at col 14.
    runLexer("/* a /* b */ x");
    expectToken(0, TokenCode::IDENTIFIER, "x", 1, 14);
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(LexerTest, NonAsciiInCommentSkipped)
{
    // Non-ASCII bytes in a comment must not trip ctype UB (P9 unsigned cast).
    runLexer("// \xe4\xb8\xad\xe6\x96\x87 comment\nx");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
    ASSERT_GT(context->tokenStream.size(), 0);
    EXPECT_EQ(context->tokenStream[0].code, TokenCode::IDENTIFIER);
    EXPECT_EQ(context->tokenStream[0].value, "x");
}

TEST_F(LexerTest, NonAsciiInStringLiteral)
{
    runLexer("\"\xe4\xb8\xad\xe6\x96\x87\"");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
    expectToken(0, TokenCode::STRING_LITERAL, "\xe4\xb8\xad\xe6\x96\x87", 1, 1);
}

// ── D: numeric literal edges ───────────────────────────────────────────────────

TEST_F(LexerTest, LeadingZeroInteger)
{
    runLexer("007");
    expectToken(0, TokenCode::INT_LITERAL, "007", 1, 1);
}

TEST_F(LexerTest, TrailingDotIsIntegerThenDot)
{
    // `5.` lexes as integer 5 followed by a dot (member-access start).
    runLexer("5.");
    expectToken(0, TokenCode::INT_LITERAL, "5", 1, 1);
    expectToken(1, TokenCode::DOT, ".", 1, 2);
}

TEST_F(LexerTest, DotThenNumberIsDotThenInteger)
{
    runLexer(".5");
    expectToken(0, TokenCode::DOT, ".", 1, 1);
    expectToken(1, TokenCode::INT_LITERAL, "5", 1, 2);
}

TEST_F(LexerTest, FloatWithExponent)
{
    runLexer("1.5e3");
    expectToken(0, TokenCode::FLOAT_LITERAL, "1.5e3", 1, 1);
}

TEST_F(LexerTest, FloatUpperExponent)
{
    runLexer("1E5");
    expectToken(0, TokenCode::FLOAT_LITERAL, "1E5", 1, 1);
}

TEST_F(LexerTest, FloatExponentPositiveSign)
{
    runLexer("2e+3");
    expectToken(0, TokenCode::FLOAT_LITERAL, "2e+3", 1, 1);
}

TEST_F(LexerTest, IntegerThenIdentifier)
{
    runLexer("123abc");
    expectToken(0, TokenCode::INT_LITERAL, "123", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "abc", 1, 4);
}

TEST_F(LexerTest, MemberAccessOnIntegerLiteral)
{
    runLexer("1.foo");
    expectToken(0, TokenCode::INT_LITERAL, "1", 1, 1);
    expectToken(1, TokenCode::DOT, ".", 1, 2);
    expectToken(2, TokenCode::IDENTIFIER, "foo", 1, 3);
}

// ── D: remaining keywords ──────────────────────────────────────────────────────

TEST_F(LexerTest, RecognizesControlKeywords)
{
    runLexer("in as self move break continue");
    expectToken(0, TokenCode::IN, "in", 1, 1);
    expectToken(1, TokenCode::AS, "as", 1, 4);
    expectToken(2, TokenCode::SELF, "self", 1, 7);
    expectToken(3, TokenCode::MOVE, "move", 1, 12);
    expectToken(4, TokenCode::BREAK, "break", 1, 17);
    expectToken(5, TokenCode::CONTINUE, "continue", 1, 23);
}

TEST_F(LexerTest, RecognizesDeclarationKeywords)
{
    runLexer("impl trait enum match");
    expectToken(0, TokenCode::IMPL, "impl", 1, 1);
    expectToken(1, TokenCode::TRAIT, "trait", 1, 6);
    expectToken(2, TokenCode::ENUM, "enum", 1, 12);
    expectToken(3, TokenCode::MATCH, "match", 1, 17);
}

TEST_F(LexerTest, RecognizesVoidKeyword)
{
    runLexer("void");
    expectToken(0, TokenCode::VOID, "void", 1, 1);
}

TEST_F(LexerTest, KeywordBoundaryNotSubstring)
{
    // `iff` is an identifier, not `if` + `f`.
    runLexer("iff ifx");
    expectToken(0, TokenCode::IDENTIFIER, "iff", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "ifx", 1, 5);
}

// ── D: operators ───────────────────────────────────────────────────────────────

TEST_F(LexerTest, ArrowVsMinusGreater)
{
    runLexer("-> - >");
    expectToken(0, TokenCode::ARROW, "->", 1, 1);
    expectToken(1, TokenCode::MINUS, "-", 1, 4);
    expectToken(2, TokenCode::GT, ">", 1, 6);
}

TEST_F(LexerTest, DoubleColonVsColons)
{
    runLexer(":: : :");
    expectToken(0, TokenCode::DOUBLE_COLON, "::", 1, 1);
    expectToken(1, TokenCode::COLON, ":", 1, 4);
    expectToken(2, TokenCode::COLON, ":", 1, 6);
}

TEST_F(LexerTest, LeVsLtEq)
{
    runLexer("<= < =");
    expectToken(0, TokenCode::LT_EQ, "<=", 1, 1);
    expectToken(1, TokenCode::LT, "<", 1, 4);
    expectToken(2, TokenCode::ASSIGN, "=", 1, 6);
}

TEST_F(LexerTest, ModuloOperator)
{
    runLexer("%");
    expectToken(0, TokenCode::MOD, "%", 1, 1);
}

TEST_F(LexerTest, NotEqAndNot)
{
    runLexer("!= !");
    expectToken(0, TokenCode::NOT_EQ, "!=", 1, 1);
    expectToken(1, TokenCode::NOT, "!", 1, 4);
}

TEST_F(LexerTest, DoubleArrow)
{
    runLexer("=> = >");
    expectToken(0, TokenCode::DOUBLE_ARROW, "=>", 1, 1);
    expectToken(1, TokenCode::ASSIGN, "=", 1, 4);
    expectToken(2, TokenCode::GT, ">", 1, 6);
}

// ── D: unknown characters ──────────────────────────────────────────────────────

TEST_F(LexerTest, UnknownCharacterReportsError)
{
    runLexer("$");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "unknown character must report an error";
}

TEST_F(LexerTest, UnknownCharacterAmongValidTokens)
{
    runLexer("a $ b");
    EXPECT_GT(Logger::GetErrorCount(), 0);
    ASSERT_GT(context->tokenStream.size(), 2);
    EXPECT_EQ(context->tokenStream[0].code, TokenCode::IDENTIFIER);
    EXPECT_EQ(context->tokenStream[2].code, TokenCode::IDENTIFIER);
}

TEST_F(LexerTest, MultipleUnknownCharsReportMultipleErrors)
{
    runLexer("$ # @");
    EXPECT_GE(Logger::GetErrorCount(), 3) << "each unknown character reports an error";
}

// ── D: identifiers & positions ─────────────────────────────────────────────────

TEST_F(LexerTest, IdentifierWithDigitsInside)
{
    runLexer("var2 x9y _1");
    expectToken(0, TokenCode::IDENTIFIER, "var2", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "x9y", 1, 6);
    expectToken(2, TokenCode::IDENTIFIER, "_1", 1, 10);
}

TEST_F(LexerTest, SingleCharPerLinePositions)
{
    runLexer("a\nbb\nccc");
    expectToken(0, TokenCode::IDENTIFIER, "a", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "bb", 2, 1);
    expectToken(2, TokenCode::IDENTIFIER, "ccc", 3, 1);
}

TEST_F(LexerTest, TabCountsAsOneColumn)
{
    runLexer("a\tb");
    expectToken(0, TokenCode::IDENTIFIER, "a", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "b", 1, 3);
}

TEST_F(LexerTest, CommentThenCodeOnNextLine)
{
    runLexer("// comment\nx");
    expectToken(0, TokenCode::IDENTIFIER, "x", 2, 1);
}

TEST_F(LexerTest, BlockCommentThenInlineCode)
{
    runLexer("/* c */ y");
    expectToken(0, TokenCode::IDENTIFIER, "y", 1, 9);
}

// ── D2: more literal edge cases ────────────────────────────────────────────────

TEST_F(LexerTest, StringWithEscapes)
{
    // Escapes \n \t \" \\ resolve to the real characters in the token value.
    // (`\0` is tested separately — it would truncate the C++ assertion string.)
    runLexer("\"a\\n\\t\\\"\\\\\"");
    expectToken(0, TokenCode::STRING_LITERAL, "a\n\t\"\\", 1, 1);
}

// (Non-ASCII in a string literal is covered by NonAsciiInStringLiteral above.)

TEST_F(LexerTest, CharInvalidEscapeKeepsBackslash)
{
    // An invalid escape like `\q` keeps the backslash in the value.
    runLexer("'\\q'");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
    expectToken(0, TokenCode::CHAR_LITERAL, "\\q", 1, 1);
}

TEST_F(LexerTest, ConsecutiveStringLiterals)
{
    runLexer("\"a\" \"b\" \"c\"");
    expectToken(0, TokenCode::STRING_LITERAL, "a", 1, 1);
    expectToken(1, TokenCode::STRING_LITERAL, "b", 1, 5);
    expectToken(2, TokenCode::STRING_LITERAL, "c", 1, 9);
}

TEST_F(LexerTest, ConsecutiveCharLiterals)
{
    runLexer("'a''b'");
    expectToken(0, TokenCode::CHAR_LITERAL, "a", 1, 1);
    expectToken(1, TokenCode::CHAR_LITERAL, "b", 1, 4);
}

TEST_F(LexerTest, MultiDigitExponent
)
{
    runLexer("1e123");
    expectToken(0, TokenCode::FLOAT_LITERAL, "1e123", 1, 1);
}

TEST_F(LexerTest, ExponentWithNegativeLarge
)
{
    runLexer("2.5e-10");
    expectToken(0, TokenCode::FLOAT_LITERAL, "2.5e-10", 1, 1);
}

TEST_F(LexerTest, ZeroFloat
)
{
    runLexer("0.0");
    expectToken(0, TokenCode::FLOAT_LITERAL, "0.0", 1, 1);
}

TEST_F(LexerTest, LargeIntegerRun
)
{
    runLexer("123456789");
    expectToken(0, TokenCode::INT_LITERAL, "123456789", 1, 1);
}

TEST_F(LexerTest, DecimalThenIdentifierBoundary
)
{
    runLexer("3abc");
    expectToken(0, TokenCode::INT_LITERAL, "3", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "abc", 1, 2);
}

// ── D2: more operator/delimiter edge cases ─────────────────────────────────────

TEST_F(LexerTest, AssignmentInExpression
)
{
    runLexer("= == != ==");
    expectToken(0, TokenCode::ASSIGN, "=", 1, 1);
    expectToken(1, TokenCode::EQ_EQ, "==", 1, 3);
    expectToken(2, TokenCode::NOT_EQ, "!=", 1, 6);
    expectToken(3, TokenCode::EQ_EQ, "==", 1, 9);
}

TEST_F(LexerTest, ReferenceThenBorrow
)
{
    runLexer("& && &");
    expectToken(0, TokenCode::REFERENCE, "&", 1, 1);
    expectToken(1, TokenCode::AND, "&&", 1, 3);
    expectToken(2, TokenCode::REFERENCE, "&", 1, 6);
}

TEST_F(LexerTest, ColonDoubleColonMix
)
{
    runLexer(": :: :::");
    expectToken(0, TokenCode::COLON, ":", 1, 1);
    expectToken(1, TokenCode::DOUBLE_COLON, "::", 1, 3);
    expectToken(2, TokenCode::DOUBLE_COLON, "::", 1, 6);
    expectToken(3, TokenCode::COLON, ":", 1, 8);
}

TEST_F(LexerTest, OperatorsNoSpaces
)
{
    runLexer("+=-*");
    expectToken(0, TokenCode::PLUS, "+", 1, 1);
    expectToken(1, TokenCode::ASSIGN, "=", 1, 2);
    expectToken(2, TokenCode::MINUS, "-", 1, 3);
    expectToken(3, TokenCode::STAR, "*", 1, 4);
}

TEST_F(LexerTest, BracketsBalanced
)
{
    runLexer("[](){}");
    expectToken(0, TokenCode::LBRACKET, "[", 1, 1);
    expectToken(1, TokenCode::RBRACKET, "]", 1, 2);
    expectToken(2, TokenCode::LPAREN, "(", 1, 3);
    expectToken(3, TokenCode::RPAREN, ")", 1, 4);
    expectToken(4, TokenCode::LBRACE, "{", 1, 5);
    expectToken(5, TokenCode::RBRACE, "}", 1, 6);
}

// ── D2: identifiers & keywords ─────────────────────────────────────────────────

TEST_F(LexerTest, IdentifierStartingWithUnderscore)
{
    runLexer("_private __hidden _");
    expectToken(0, TokenCode::IDENTIFIER, "_private", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "__hidden", 1, 10);
    expectToken(2, TokenCode::IDENTIFIER, "_", 1, 19);
}

TEST_F(LexerTest, KeywordFollowedByUnderscoreIsIdentifier
)
{
    runLexer("if_ else_ ret_");
    expectToken(0, TokenCode::IDENTIFIER, "if_", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "else_", 1, 5);
    expectToken(2, TokenCode::IDENTIFIER, "ret_", 1, 11);
}

TEST_F(LexerTest, TypeKeywordFollowedByDigit
)
{
    runLexer("i32x i64_");
    expectToken(0, TokenCode::IDENTIFIER, "i32x", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "i64_", 1, 6);
}

TEST_F(LexerTest, SingleCharacterIdentifiers
)
{
    runLexer("a b c");
    expectToken(0, TokenCode::IDENTIFIER, "a", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "b", 1, 3);
    expectToken(2, TokenCode::IDENTIFIER, "c", 1, 5);
}

TEST_F(LexerTest, MixedCaseKeywordsBoundary
)
{
    runLexer("IF If iF");
    expectToken(0, TokenCode::IDENTIFIER, "IF", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "If", 1, 4);
    expectToken(2, TokenCode::IDENTIFIER, "iF", 1, 7);
}

// ── D2: positions & whitespace ─────────────────────────────────────────────────

TEST_F(LexerTest, MultiLinePositionsWithContent
)
{
    runLexer("let x = 1;\nlet y = 2;");
    expectToken(0, TokenCode::LET, "let", 1, 1);
    expectToken(3, TokenCode::INT_LITERAL, "1", 1, 9);
    expectToken(5, TokenCode::LET, "let", 2, 1);
    expectToken(8, TokenCode::INT_LITERAL, "2", 2, 9);
}

TEST_F(LexerTest, WindowsNewlinePositions
)
{
    runLexer("a\r\nb");
    expectToken(0, TokenCode::IDENTIFIER, "a", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "b", 2, 1);
}

TEST_F(LexerTest, TabsAndSpacesMixed
)
{
    runLexer("  \t  let");
    expectToken(0, TokenCode::LET, "let", 1, 6);
}

TEST_F(LexerTest, CommentBetweenTokens
)
{
    runLexer("a /* mid */ b");
    expectToken(0, TokenCode::IDENTIFIER, "a", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "b", 1, 13);
}

TEST_F(LexerTest, MultipleLineComments
)
{
    runLexer("// one\n// two\nx");
    expectToken(0, TokenCode::IDENTIFIER, "x", 3, 1);
}

// ── D2: unknown chars & errors ─────────────────────────────────────────────────

TEST_F(LexerTest, UnknownCharsStillLexAround
)
{
    runLexer("a $ b");
    EXPECT_GT(Logger::GetErrorCount(), 0);
    ASSERT_GE(context->tokenStream.size(), 3);
    EXPECT_EQ(context->tokenStream[0].value, "a");
    EXPECT_EQ(context->tokenStream[2].value, "b");
}

TEST_F(LexerTest, TildeIsUnknownChar
)
{
    runLexer("~");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(LexerTest, CaretIsUnknownChar
)
{
    runLexer("^");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(LexerTest, UnclosedStringThenMoreTokens
)
{
    runLexer("\"abc let");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(LexerTest, CharLiteralExtraChars
)
{
    // `'ab'` is a malformed char literal — first char read, closing quote check
    // fails.
    runLexer("'ab'");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(LexerTest, BlockCommentAtStartThenCode
)
{
    runLexer("/*x*/y");
    expectToken(0, TokenCode::IDENTIFIER, "y", 1, 6);
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

// ── D3: final lexer breadth ────────────────────────────────────────────────────

TEST_F(LexerTest, StringThenCharMix)
{
    runLexer("\"s\"'c'");
    expectToken(0, TokenCode::STRING_LITERAL, "s", 1, 1);
    expectToken(1, TokenCode::CHAR_LITERAL, "c", 1, 4);
}

TEST_F(LexerTest, NumberThenOperators
)
{
    runLexer("5+5-5");
    expectToken(0, TokenCode::INT_LITERAL, "5", 1, 1);
    expectToken(1, TokenCode::PLUS, "+", 1, 2);
    expectToken(2, TokenCode::INT_LITERAL, "5", 1, 3);
    expectToken(3, TokenCode::MINUS, "-", 1, 4);
    expectToken(4, TokenCode::INT_LITERAL, "5", 1, 5);
}

TEST_F(LexerTest, AllTypeKeywords
)
{
    runLexer("i8 i16 i32 i64 f32 f64 bool char void");
    expectToken(0, TokenCode::I8, "i8", 1, 1);
    expectToken(8, TokenCode::VOID, "void", 1, 34);
}

TEST_F(LexerTest, DoubleColonBeforeIdent
)
{
    runLexer("::foo");
    expectToken(0, TokenCode::DOUBLE_COLON, "::", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "foo", 1, 3);
}

TEST_F(LexerTest, ArrowThenIdent
)
{
    runLexer("->x");
    expectToken(0, TokenCode::ARROW, "->", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "x", 1, 3);
}

TEST_F(LexerTest, FloatWithMultipleDigits
)
{
    runLexer("123.456");
    expectToken(0, TokenCode::FLOAT_LITERAL, "123.456", 1, 1);
}

TEST_F(LexerTest, LineCommentWithCodeAfter
)
{
    runLexer("// c\n\nlet x");
    expectToken(0, TokenCode::LET, "let", 3, 1);
}

TEST_F(LexerTest, CharAndStringAdjacent
)
{
    runLexer("'a'\"b\"");
    expectToken(0, TokenCode::CHAR_LITERAL, "a", 1, 1);
    expectToken(1, TokenCode::STRING_LITERAL, "b", 1, 4);
}

TEST_F(LexerTest, Semicolons
)
{
    runLexer(";;;");
    expectToken(0, TokenCode::SEMI, ";", 1, 1);
    expectToken(1, TokenCode::SEMI, ";", 1, 2);
    expectToken(2, TokenCode::SEMI, ";", 1, 3);
}

TEST_F(LexerTest, CommaAndColon
)
{
    runLexer(", : ,");
    expectToken(0, TokenCode::COMMA, ",", 1, 1);
    expectToken(1, TokenCode::COLON, ":", 1, 3);
    expectToken(2, TokenCode::COMMA, ",", 1, 5);
}

TEST_F(LexerTest, IdentifierAfterNumberNoSpace
)
{
    runLexer("42x");
    expectToken(0, TokenCode::INT_LITERAL, "42", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "x", 1, 3);
}

TEST_F(LexerTest, FloatExponentWithDecimal
)
{
    runLexer("3.14e2");
    expectToken(0, TokenCode::FLOAT_LITERAL, "3.14e2", 1, 1);
}

TEST_F(LexerTest, EmptyLinePosition
)
{
    runLexer("\n\nx");
    expectToken(0, TokenCode::IDENTIFIER, "x", 3, 1);
}

TEST_F(LexerTest, TabBeforeToken
)
{
    runLexer("\tx");
    expectToken(0, TokenCode::IDENTIFIER, "x", 1, 2);
}

TEST_F(LexerTest, MultipleBlockComments
)
{
    // /*a*/ = cols 1-5, x = 6, /*b*/ = 7-11, y = 12.
    runLexer("/*a*/x/*b*/y");
    expectToken(0, TokenCode::IDENTIFIER, "x", 1, 6);
    expectToken(1, TokenCode::IDENTIFIER, "y", 1, 12);
}

TEST_F(LexerTest, NestedBlockCommentContent
)
{
    runLexer("/* outer { */ x");
    expectToken(0, TokenCode::IDENTIFIER, "x", 1, 15);
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(LexerTest, NegativeNumberTokenizesAsMinusInt
)
{
    runLexer("-5");
    expectToken(0, TokenCode::MINUS, "-", 1, 1);
    expectToken(1, TokenCode::INT_LITERAL, "5", 1, 2);
}

TEST_F(LexerTest, UnderscoreIdentifier
)
{
    runLexer("_");
    expectToken(0, TokenCode::IDENTIFIER, "_", 1, 1);
}

TEST_F(LexerTest, KeywordSelfThenIdent
)
{
    runLexer("self selfx");
    expectToken(0, TokenCode::SELF, "self", 1, 1);
    expectToken(1, TokenCode::IDENTIFIER, "selfx", 1, 6);
}

TEST_F(LexerTest, ExclamationVariants
)
{
    runLexer("! != !");
    expectToken(0, TokenCode::NOT, "!", 1, 1);
    expectToken(1, TokenCode::NOT_EQ, "!=", 1, 3);
    expectToken(2, TokenCode::NOT, "!", 1, 6);
}