#include "Core/CompilePipeline.hpp"

#include "Lexer/Lexer.hpp"
#include "Parser/Parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

CompilePipeline::CompilePipeline(std::shared_ptr<Context> cnt)
{
    context = std::move(cnt);

    std::ifstream f{context->filePath, std::ios::binary};
    std::stringstream ss;
    ss << f.rdbuf();
    context->fileValue = ss.str();

    passes.emplace_back(std::make_unique<Lexer>(context));
    passes.emplace_back(std::make_unique<Parser>(context));
}