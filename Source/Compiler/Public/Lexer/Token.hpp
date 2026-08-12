/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * This file defined TokenCode and Token
 */

#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/SourcePosition.hpp"

/**
 * TokenCode is the type of Token
 * For different tokens, we can define different TokenCode to make the following steps easier
 */
enum class TokenCode
{
    /* Undefined */
    UNDEFINED,

    /* Keywords */
    IMPT,     // "impt"
    STRUCT,   // "struct"
    IMPL,     // "impl"
    FN,       // "fn"
    LET,      // "let"
    MUT,      // "mut"
    PUB,      // "pub"
    RET,      // "ret"
    IF,       // "if"
    ELSE,     // "else"
    FOR,      // "for"
    WHILE,    // "while"
    IN,       // "in"
    AS,       // "as"
    SELF,     // "self"
    MOVE,     // "move"
    TRAIT,    // "trait"
    BREAK,    // "break"
    CONTINUE, // "continue"
    ENUM,     // "enum"
    MATCH,    // "match"

    /* Type keywords */
    I8,   // "i8"
    I16,  // "i16"
    I32,  // "i32"
    I64,  // "i64"
    F32,  // "f32"
    F64,  // "f64"
    BOOL, // "bool"
    CHAR, // "char"
    VOID, // "void"

    /* Literal */
    BOOLEAN_TRUE,   // "true"
    BOOLEAN_FALSE,  // "false"
    INT_LITERAL,    // [0-9]+
    FLOAT_LITERAL,  // [0-9]+.[0-9]+([eE][+-]?[0-9]+)?
    CHAR_LITERAL,   // '[^'\\]|\\[nrt'"\\]'
    STRING_LITERAL, // "[^"\\]*(\\.[^"\\]*)*"

    /* Idenfiter */
    IDENTIFIER, // [a-zA-Z_][a-zA-Z0-9_]*

    /* Operator */
    PLUS,   // "+"
    MINUS,  // "-"
    STAR,   // "*"
    SLASH,  // "/"
    MOD,    // "%"
    EQ_EQ,  // "=="
    NOT_EQ, // "!="
    LT,     // "<"
    GT,     // ">"
    LT_EQ,  // "<="
    GT_EQ,  // ">="
    ASSIGN, // "="
    NOT,    // !
    BOR,    // |
    OR,     // ||
    AND,    // &&

    /* Delimiter */
    LBRACE,       // "{"
    RBRACE,       // "}"
    LPAREN,       // "("
    RPAREN,       // ")"
    COMMA,        // ","
    COLON,        // ":"
    SEMI,         // ";"
    DOT,          // "."
    DOUBLE_COLON, // "::"
    ARROW,        // "->"
    DOUBLE_ARROW, // "=>"
    REFERENCE,    // &
    LBRACKET,     // "["
    RBRACKET,     // "]"
};

// The length of all keywords
const size_t KEYWORDS_LENGTH = (size_t)TokenCode::BOOLEAN_FALSE - (size_t)TokenCode::IMPT + 1;

// The length of all type keywords
const size_t TYPE_KEYWORD_BEGIN = (size_t)TokenCode::I8;
const size_t TYPE_KEYWORD_END = (size_t)TokenCode::VOID;

// The procedure keyword list
const std::array<std::string, KEYWORDS_LENGTH> keywords = {
    "impt",
    "struct",
    "impl",
    "fn",
    "let",
    "mut",
    "pub",
    "ret",
    "if",
    "else",
    "for",
    "while",
    "in",
    "as",
    "self",
    "move",
    "trait",
    "break",
    "continue",
    "enum",
    "match",
    "i8",
    "i16",
    "i32",
    "i64",
    "f32",
    "f64",
    "bool",
    "char",
    "void",
    "true",
    "false"};

std::unordered_map<std::string, size_t> buildKeywordIndexMap();

// init the hash map
static inline const std::unordered_map<std::string, size_t> keywordsMap = buildKeywordIndexMap();

/*
 * Token is the smallest meaningful unit in the compiler
 * Lexer convet the source code to tokens
 * Every tokens are unsplitable, split forcefully will change the semantics
 * Each token has a own type, likes keyword, literal ans so on
 * The tokens of the same type can have different values, such as the number literal token can be 123 or 456
 * And for the error report, we will save informations about file, line and col
 */
struct Token
{
    // The type of the token
    TokenCode code = TokenCode::UNDEFINED;
    // The value of this token
    std::string value = "";

    SourcePosition position;
};

/**
 * TokenStream is a stream (a vector, actually) filled with Token
 * Some useful functions are in the Parser.
 */
using TokenStream = std::vector<Token>;

// Here are some helper functions

inline std::unordered_map<std::string, size_t> buildKeywordIndexMap()
{
    std::unordered_map<std::string, size_t> map;

    for (size_t i = 0; i < keywords.size(); ++i)
    {
        map[keywords[i]] = i;
    }

    return map;
}

inline std::optional<size_t> getKeywordPoistion(const std::string &tokenValue)
{
    if (auto it = keywordsMap.find(tokenValue); it != keywordsMap.end())
    {
        return it->second;
    }
    return std::nullopt;
}

inline bool isLetter(char letter)
{
    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z');
}

inline bool isDigit(char digit)
{
    return (digit >= '0') && (digit <= '9');
}