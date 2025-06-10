/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 此文件定义了 TokenCode 和 Token ，用于存储词法分析的结果
 */

#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * TokenCode 用来定义 Token 的类型
 * 对于不同类型的 Token ，我们可以定义不同的 TokenCode ，方便后续分析
 */
enum class TokenCode
{
    /* 未定义 */
    UNDEFINED,

    /* 关键字 */
    IMPT,   // "impt"
    STRUCT, // "struct"
    IMPL,   // "impl"
    FN,     // "fn"
    LET,    // "let"
    MUT,    // "mut"
    PUB,    // "pub"
    RET,    // "ret"
    IF,     // "if"
    ELSE,   // "else"
    FOR,    // "for"
    WHILE,  // "while"
    IN,     // "in"
    AS,     // "as"
    SELF,   // "self"
    MOVE,   // "move"

    /* 类型关键字 */
    I8,   // "i8"
    I16,  // "i16"
    I32,  // "i32"
    I64,  // "i64"
    F32,  // "f32"
    F64,  // "f64"
    BOOL, // "bool"
    CHAR, // "char"

    /* 字面量 */
    BOOLEAN_TRUE,   // "true"
    BOOLEAN_FALSE,  // "false"
    INT_LITERAL,    // [0-9]+
    FLOAT_LITERAL,  // [0-9]+.[0-9]+([eE][+-]?[0-9]+)?
    CHAR_LITERAL,   // '[^'\\]|\\[nrt'"\\]'
    STRING_LITERAL, // "[^"\\]*(\\.[^"\\]*)*"

    /* 标识符 */
    IDENTIFIER, // [a-zA-Z_][a-zA-Z0-9_]*

    /* 运算符 */
    PLUS,   // "+"
    MINUS,  // "-"
    STAR,   // "*"
    SLASH,  // "/"
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

    /* 分隔符 */
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

};

// 为了存储关键字的一些信息方便分析，下面定义一个常量

// 所有关键字的长度
const size_t KEYWORDS_LENGTH = (size_t)TokenCode::BOOLEAN_FALSE - (size_t)TokenCode::IMPT + 1;

// 所有类型关键字长度
const size_t TYPE_KEYWORD_BEGIN = (size_t)TokenCode::I8;
const size_t TYPE_KEYWORD_END = (size_t)TokenCode::CHAR;

// 所有关键字的对应字符串
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
    "i8",
    "i16",
    "i32",
    "i64",
    "f32",
    "f64",
    "bool",
    "char",
    "true",
    "false"};

// 初始化哈希表的函数
std::unordered_map<std::string, size_t> buildKeywordIndexMap();

// 生成一个哈希表优化查询时间复杂度
static const std::unordered_map<std::string, size_t> keywordsMap = buildKeywordIndexMap();

/*
 * Token 是在编译阶段的最小有意义的单元
 * 词法分析器 Lexer 将源代码转换为一个个的 Token
 * 单个 Token 每个字符都是不可分割的整体，强行分割会改变语义
 * 每个 Token 都有一个自己的类型，例如关键字、字面量等
 * 同类型的 Token 可以有不同的值，例如同样的数字字面量类型 Token 可以为 123 也可以是 456
 * 同时为了方便错误处理，还会保留这个 Token 的文件、行信息
 */
struct Token
{
    // 这个 Token 的类型
    TokenCode code = TokenCode::UNDEFINED;
    // Token 的值
    std::string value = "";
    // Token 的文件路径
    std::string filePath = "";
    // Token 的起始下标
    size_t col = 0, line = 0, pos = 0, lineStart = 0;
};

// TokenStream 存储了一个翻译单元的所有 Token
using TokenStream = std::vector<Token>;

// 下面定义所有关于 Token 的辅助函数

/**
 * 构建哈希表，用于以 O(1) 时间复杂度检查标识符是否是关键字
 */
inline std::unordered_map<std::string, size_t> buildKeywordIndexMap()
{
    std::unordered_map<std::string, size_t> map;

    for (size_t i = 0; i < keywords.size(); ++i)
    {
        map[keywords[i]] = i;
    }

    return map;
}

/**
 * 获取关键字的位置
 * 如果不存在，则返回值为 std::nullopt
 */
inline std::optional<size_t> getKeywordPoistion(const std::string &tokenValue)
{
    if (auto it = keywordsMap.find(tokenValue); it != keywordsMap.end())
    {
        return it->second;
    }
    return std::nullopt;
}

/**
 * 判断一个字符是否是字母
 */
inline bool isLetter(char letter)
{
    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z');
}

/**
 * 判断一个字符是否是数字
 */
inline bool isDigit(char digit)
{
    return digit >= '0' && digit <= '9';
}