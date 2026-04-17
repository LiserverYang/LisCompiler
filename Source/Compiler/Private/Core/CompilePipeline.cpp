/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 * Implement the compile pipeline
 */

#include "Core/CompilePipeline.hpp"
#include "Argparser/Argparser.hpp"
#include "Argparser/Behaviors.hpp"
#include "Core/Debugging.hpp"
#include "Core/LambdaPass.hpp"
#include "IR/Emitter.hpp"
#include "IR/HIRBuilder.hpp"
#include "IR/HIRSemanticAnalyzer.hpp"
#include "IR/LLVMIRBuilder.hpp"
#include "IR/MIRBuilder.hpp"
#include "IR/MIRMonomorphization.hpp"
#include "Lexer/Lexer.hpp"
#include "Parser/Parser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

CompilePipeline::CompilePipeline(std::shared_ptr<Context> cnt, int argc, const char **argv)
{
    context = cnt;

    // create the argparser and regist the arguments
    ArgparserCreateInfo createInfo{argc, argv, true, context->args, "Usage: lisc [options] file"};
    auto argParser = std::make_unique<Argparser>(context, createInfo);

    argParser->registRule(ArgParseRule{{"filePath"}, nullptr, "./a.lis", "test"});
    argParser->registRule(ArgParseRule{{"--print-ast"}, setAsTrue, "false", "Print the parsed ast."});
    argParser->registRule(ArgParseRule{{"--print-tokenstream"}, setAsTrue, "false", "Print the tokenstream."});
    argParser->registRule(ArgParseRule{{"--print-typetable"}, setAsTrue, "false", "Print the types of TypeContext."});
    argParser->registRule(ArgParseRule{{"--print-hir"}, setAsTrue, "false", "Print the parsed HIR."});
    argParser->registRule(ArgParseRule{{"--print-mir"}, setAsTrue, "false", "Print the parsed MIR."});
    argParser->registRule(ArgParseRule{{"--print-llvmir"}, setAsTrue, "false", "Print the parsed LLVM IR."});
    argParser->registRule(ArgParseRule{{"-o"}, setAsValue, "2", "The optimise level(0-3), default is 2."});

    // here we load the standard library definations
    // the standard library will export into %binary_path%/lstdlib/*.lis

    namespace fs = std::filesystem;

    fs::path stdLibDir = fs::path(argv[0]).parent_path() / "lstdlib";

    if (!fs::exists(stdLibDir))
    {
        std::runtime_error("could not find the standard library, please check the binary_path/lstdlib!");
    }

    std::string originalFilePath = context->filePath;
    std::string originalFileValue = context->fileValue;

    for (auto &entry : fs::directory_iterator(stdLibDir))
    {
        if (entry.path().extension() == ".lis")
        {
            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            context->filePath = entry.path().string();
            context->fileValue = content;

            Lexer lexer(context);
            Parser parser(context);
            lexer.run();
            parser.run();
        }
    }

    context->filePath = originalFilePath;
    context->fileValue = originalFileValue;

    passes.emplace_back(argParser.release());
    passes.emplace_back(std::make_unique<LambdaPass>(context, [](std::shared_ptr<Context> ctx)
        {
            // this pass is to read file
            ctx->filePath = ctx->args->getArg("filePath");
            ctx->fileValue = (std::stringstream{} << std::ifstream{ctx->filePath, std::ios::binary}.rdbuf()).str(); }));
    passes.emplace_back(std::make_unique<Lexer>(context));
    passes.emplace_back(std::make_unique<Parser>(context));
    passes.emplace_back(std::make_unique<HIRBuilder>(context));
    passes.emplace_back(std::make_unique<HIRSemanticAnalyzer>(context));
    passes.emplace_back(std::make_unique<MIRBuilder>(context));
    passes.emplace_back(std::make_unique<MIRMonomorphization>(context));
    passes.emplace_back(std::make_unique<LLVMIRBuilder>(context, context->llvmContext, context->args->getArg("filePath")));

    Emitter::Options emitOpts;
    int optLevel = std::stoi(context->args->getArg("o"));
    emitOpts.optLevel = optLevel;
    emitOpts.runOptimiser = (optLevel > 0);
    passes.emplace_back(std::make_unique<Emitter>(context, emitOpts));
}