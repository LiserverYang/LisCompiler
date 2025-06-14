/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 程序的入口文件
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

// 记录了LLVM的核心数据结构，比如类型和常量表，不过我们不太需要关心它的内部
llvm::LLVMContext g_llvm_context;
// 用于创建LLVM指令
llvm::IRBuilder<> g_ir_builder(g_llvm_context);
// 用于管理函数和全局变量，可以粗浅地理解为类c++的编译单元(单个cpp文件)
llvm::Module g_module("my cool jit", g_llvm_context);
// 用于记录函数的变量参数
std::map<std::string, llvm::Value *> g_named_values;

int main(int argc, const char **argv)
{
    std::shared_ptr<Context> context = std::make_shared<Context>();
    context->filePath = argv[1];

    CompilePipeline compilePipeline{context};
    compilePipeline.run();

    printAST(context->program);

    return 0;
}