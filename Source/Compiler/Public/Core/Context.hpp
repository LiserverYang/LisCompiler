/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Analysiser/TypeContext.hpp"
#include "Argparser/Args.hpp"
#include "IR/HIR.hpp"
#include "IR/MIR.hpp"
#include "Lexer/Token.hpp"
#include "Parser/AST.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

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

    std::shared_ptr<TypeContext> typeContext = std::make_shared<TypeContext>();

    std::unique_ptr<HIRProgram> hirProgram;

    std::unique_ptr<MIRProgram> mirProgram;

    llvm::LLVMContext llvmContext;
    std::unique_ptr<llvm::Module> module;
};