/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * LLVMIRBuilder — lowers MIRProgram to an llvm::Module.
 *
 * Usage:
 *   LLVMIRBuilder builder(context, "my_module");
 *   builder.lowerProgram(mirProgram);
 *   // builder.getModule() now holds the completed llvm::Module
 *
 * Assumptions about your Type class (Analysiser/Type.hpp).
 * The builder calls free functions declared at the bottom of this header;
 * implement them in a TypeHelper.cpp that knows your actual Type internals.
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

#include "Analysiser/SymbolTable.hpp"
#include "Core/Debugging.hpp"
#include "Core/Pass.hpp"
#include "IR/MIR.hpp"

// ─── Type bridge ─────────────────────────────────────────────────────────────
// Implement these four functions in a TypeHelper.cpp (or inline here if your
// Type class is simple enough). The builder only calls these; it never touches
// Type internals directly.

/// Map a semantic Type to the corresponding llvm::Type*.
/// e.g. Int32 → i32, Bool → i1, Void → void, Struct "Foo" → %Foo (opaque ptr).
llvm::Type *semanticTypeToLLVM(const std::shared_ptr<Type> &ty,
    llvm::LLVMContext &ctx);

/// True if values of this type are trivially copyable (i.e. use MIRCopy freely).
/// Primitives + raw pointers = true.  Structs with drop glue = false.
bool isCopyType(const std::shared_ptr<Type> &ty);

/// True if this type is a pointer or reference at the semantic level.
bool isPointerLike(const std::shared_ptr<Type> &ty);

/// Return the struct name for struct types (used for GEP field indexing).
/// Returns "" for non-struct types.
std::string getStructName(const std::shared_ptr<Type> &ty);

// ─── MIRToLLVM ───────────────────────────────────────────────────────────────

class LLVMIRBuilder : public Pass
{
public:
    /// Lower an entire MIRProgram.  Call once per compilation unit.
    void lowerProgram(const MIRProgram &prog);

    LLVMIRBuilder() = default;

    /// @param ctx   Caller-owned LLVMContext. Must outlive this object.
    /// @param name  Module name (typically your source file name).
    LLVMIRBuilder(std::shared_ptr<Context> cnt, llvm::LLVMContext &ctx, const std::string &name);

    ~LLVMIRBuilder()
    {
    }

    virtual void run() override
    {
        lowerProgram(*context->mirProgram.get());

        if (context->args->getArg("print_llvmir").compare("true") == 0)
        {
            context->module->print(llvm::outs(), nullptr);
        }
    }

private:
    // ── Struct layout cache ──────────────────────────────────────────────────
    // Maps struct name → llvm::StructType* (created during the first pass).
    std::unordered_map<std::string, llvm::StructType *> structTypes_;

    // Field name → index inside a struct (name → (fieldName → idx)).
    std::unordered_map<std::string, std::unordered_map<std::string, unsigned>>
        fieldIndex_;

    // ── Per-function state ───────────────────────────────────────────────────
    struct FunctionState
    {
        llvm::Function *fn = nullptr;
        const MIRBody *body = nullptr;

        // local index → alloca (for mutable / address-taken locals)
        std::unordered_map<size_t, llvm::AllocaInst *> allocas;

        // local index → SSA value (for immutable temps that never need alloca)
        // Populated lazily as assignments are encountered.
        std::unordered_map<size_t, llvm::Value *> ssaValues;

        // MIR BasicBlockId → llvm::BasicBlock*
        std::unordered_map<BasicBlockId, llvm::BasicBlock *> blocks;
    };

    // ── LLVM handles ────────────────────────────────────────────────────────
    llvm::LLVMContext &ctx_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    // ── Top-level passes ────────────────────────────────────────────────────

    /// First pass: declare all struct types (so forward references work).
    void declareStructTypes(const MIRProgram &prog);

    /// Second pass: declare all function signatures (so mutual recursion works).
    void declareFunctions(const MIRProgram &prog);

    /// Third pass: lower globals.
    void lowerGlobals(const MIRProgram &prog);

    /// Fourth pass: lower function bodies.
    void lowerFunctionBody(const MIRFunction &mirFn);

    // ── Body lowering ────────────────────────────────────────────────────────

    void createAllocas(FunctionState &fs);
    void lowerBlock(FunctionState &fs, const MIRBasicBlock &bb);
    void lowerStatement(FunctionState &fs, const MIRStatement &stmt);
    void lowerTerminator(FunctionState &fs, const MIRTerminator &term);

    // ── Statement variants ───────────────────────────────────────────────────

    void lowerAssign(FunctionState &fs, const MIRStmtAssign &s);
    void lowerCall(FunctionState &fs, const MIRStmtCall &s, std::optional<llvm::BasicBlock *> normalDest = std::nullopt, std::optional<llvm::BasicBlock *> unwindDest = std::nullopt);
    void lowerDrop(FunctionState &fs, const MIRStmtDrop &s);

    // ── RValue / Operand / Place lowering ────────────────────────────────────

    llvm::Value *lowerRValue(FunctionState &fs, const MIRRValue &rv);
    llvm::Value *lowerOperand(FunctionState &fs, const MIROperand &op);
    llvm::Value *lowerConst(const MIRConst &c);

    /// Returns a pointer to the place (always a pointer — callers load/store).
    llvm::Value *lowerPlaceAsPtr(FunctionState &fs, const MIRPlace &p);

    /// Returns the loaded value at the place.
    llvm::Value *loadPlace(FunctionState &fs, const MIRPlace &p);

    /// Stores `val` into the place.
    void storePlace(FunctionState &fs, const MIRPlace &p, llvm::Value *val);

    // ── Helpers ──────────────────────────────────────────────────────────────

    llvm::Type *toLLVMType(const std::shared_ptr<Type> &ty);
    llvm::Function *getOrDeclareDropGlue(const std::string &structName);
    llvm::Function *getOrDeclareFn(const std::string &name);
    std::string mangleName(const MIRFunction &fn) const;

    /// Emit a single alloca at the entry block for a given local.
    llvm::AllocaInst *emitEntryAlloca(llvm::Function *fn,
        llvm::Type *ty,
        const std::string &name);

    unsigned fieldIndexOf(const std::string &structName,
        const std::string &fieldName) const;
};
