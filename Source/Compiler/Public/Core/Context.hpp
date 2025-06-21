/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Lexer/Token.hpp"
#include "Parser/AST.hpp"
#include "Argparser/Args.hpp"

/**
 * Context stored all informations of compiler, these will be shared in different passes
 */
struct Context
{
    std::string filePath;
    std::string fileValue;

    std::shared_ptr<Args> args = std::make_shared<Args>();

    /**
     * When a source code be passed by Lexer，we will get TokenStream
     */
    TokenStream tokenStream;

    /**
     * Program is the root of AST
     * It will be generate after Parser parsed the tokenStream
     */
    Program program;
};