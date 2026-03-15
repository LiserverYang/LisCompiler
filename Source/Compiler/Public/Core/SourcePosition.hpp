/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include <string>
#include <cctype>

/**
 * SourcePosition provides a unification api to store the location of something (Token, AST Node) in source code.
 */
struct SourcePosition
{
    std::string *filePath = nullptr;
    size_t col = 0, line = 0, pos = 0, lineStart = 0;
};