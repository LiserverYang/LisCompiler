/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Lexer/TokenStreamPrinter.hpp"
#include "magic_enum.hpp"

void PrintTokenStream(TokenStream &stream)
{
    printf("type            value   <file:line:col:length>\n");

    for (auto &token : stream)
    {
        printf("%-5s    \t%-5s  \t<%s:%d:%d:%d>\n",
            magic_enum::enum_name(token.code).data(),
            token.value.c_str(),
            (*token.position.filePath).c_str(),
            token.position.line,
            token.position.col,
            token.value.length());
    }
}