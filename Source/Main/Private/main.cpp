/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The entrypoint of compiler
 */

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "Core/CompilePipeline.hpp"
#include "Parser/ASTPrinter.hpp"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

// LLVM global varialbes
llvm::LLVMContext g_llvm_context;
llvm::IRBuilder<> g_ir_builder(g_llvm_context);
llvm::Module g_module("my cool jit", g_llvm_context);
std::map<std::string, llvm::Value *> g_named_values;

int main(int argc, const char **argv)
{
    std::shared_ptr<Context> context = std::make_shared<Context>();

    CompilePipeline compilePipeline{context, argc, argv};
    compilePipeline.run();

    printAST(context->program);

    return 0;
}