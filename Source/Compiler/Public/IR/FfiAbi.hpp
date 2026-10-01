#pragma once

#include "Analysiser/Type.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>

#include <memory>
#include <string>
#include <vector>

namespace FfiAbi
{
/// How ONE source-level parameter reaches C.
enum class ArgKind
{
    Direct, ///< as itself (scalar / pointer / handle)
    Coerce, ///< split into register-sized parts (1 or 2); see argTys
    ByAddr, ///< a pointer to a COPY (the platform passes big aggregates by reference)
};

enum class RetKind
{
    Direct,
    Coerce, ///< 1 part: that type; 2 parts: a literal struct of both
    Sret,   ///< hidden first pointer; the function returns void
};

struct Plan
{
    std::vector<ArgKind> args;                     ///< one per source parameter
    std::vector<std::vector<llvm::Type *>> argTys; ///< the parts that parameter becomes
    RetKind ret = RetKind::Direct;
    std::vector<llvm::Type *> retParts; ///< Coerce: 1 or 2 register-sized types
    llvm::Type *retTy = nullptr;        ///< Coerce: the value returned; Sret: ptr
    bool valid = true;
    std::string why; ///< when !valid: the reason, for E3020
};

/// True when any parameter or the return type is a by-value aggregate (a struct).
bool needsPlan(const FunctionType &sig);

/// Classify \p sig for the target described by \p triple / \p dl -- exactly the
/// ones codegen uses (Context::targetTriple / Context::dataLayout).
Plan classify(const FunctionType &sig, const std::string &triple, const llvm::DataLayout &dl, llvm::LLVMContext &ctx);
} // namespace FfiAbi
