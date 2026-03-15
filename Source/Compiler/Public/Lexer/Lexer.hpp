/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The implementation of Lexer
 */

#pragma once

#include "Core/Pass.hpp"
#include "Token.hpp"

#include <cctype>
#include <memory>
#include <stdexcept>

/**
 * `Lexer` is a part of compiler
 * The task of it is to split the code, a kind of string into `Token`s, a type with more information
 * This will be helpful to next passes
 * The result of lexer is `TokenStream`, a vector of `Token`
 */
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