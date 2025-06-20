#include "Core/CompilePipeline.hpp"
#include "Argparser/Argparser.hpp"
#include "Argparser/Behaviors.hpp"
#include "Core/LambdaPass.hpp"
#include "Lexer/Lexer.hpp"
#include "Parser/Parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

CompilePipeline::CompilePipeline(std::shared_ptr<Context> cnt, int argc, const char **argv)
{
    context = std::move(cnt);

    ArgparserCreateInfo createInfo{argc, argv, true, context->args};
    auto argParser = std::make_unique<Argparser>(context, createInfo);

    argParser->registRule(ArgParseRule{{"filePath"}, nullptr, "./a.lis", "test"});
    argParser->registRule(ArgParseRule{{"--test"}, setAsTrue, "false", "test"});

    passes.emplace_back(argParser.release());
    passes.emplace_back(std::make_unique<LambdaPass>(context, [](std::shared_ptr<Context> ctx)
        {
            // This pass is to read file values
            ctx->filePath = ctx->args->getArg("filePath");
            ctx->fileValue = (std::stringstream{} << std::ifstream{ctx->filePath, std::ios::binary}.rdbuf()).str(); }));
    passes.emplace_back(std::make_unique<Lexer>(context));
    passes.emplace_back(std::make_unique<Parser>(context));
}