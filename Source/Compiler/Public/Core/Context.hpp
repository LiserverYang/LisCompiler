/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Lexer/Token.hpp"
#include "Parser/AST.hpp"
#include "Argparser/Args.hpp"

/**
 * Context 存储了所有有关于编译的信息，这些信息在不同的 Pass 之间共享
 */
struct Context
{
    std::string filePath;
    std::string fileValue;

    std::shared_ptr<Args> args = std::make_shared<Args>();

    /**
     * 当源文件被 Lexer 解析后，就会得到 TokenStream
     */
    TokenStream tokenStream;

    /**
     * Parser 通过解析 TokenStream 得到抽象语法树（AST）
     * Program 可以认为是 AST 的根节点
     */
    Program program;
};