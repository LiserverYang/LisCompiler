/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 词法分析器实现
 */

#pragma once

#include "Core/Pass.hpp"
#include "Token.hpp"

#include <cctype>
#include <stdexcept>
#include <memory>

class Lexer : public Pass
{
public:
    Lexer() = default;
    Lexer(std::shared_ptr<Context> cnt)
    {
        context = cnt;
    }

    ~Lexer() {}

    virtual void run() override;

private:
    size_t index = 0;
    size_t line = 1;
    size_t column = 1;
    size_t lineStart = 0;
    void skipWhitespace();
    void skipLineComment();
    void skipBlockComment();
    Token lexStringLiteral();
    Token lexCharLiteral();
    Token lexNumber();
    Token lexIdentifier();
    Token lexOperatorOrDelimiter();
};