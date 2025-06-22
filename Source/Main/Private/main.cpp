/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The entrypoint of compiler
 */

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "Core/CompilePipeline.hpp"
#include "Parser/ASTPrinter.hpp"\

int main(int argc, const char **argv)
{
    std::shared_ptr<Context> context = std::make_shared<Context>();

    CompilePipeline compilePipeline{context, argc, argv};
    compilePipeline.run();

    printAST(context->program);

    return 0;
}