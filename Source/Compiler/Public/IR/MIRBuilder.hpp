/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include "Core/Pass.hpp"
#include "IR/HIR.hpp"
#include "IR/MIR.hpp"
#include "IR/MIRPrinter.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>

/**
 * MIRBuilder
 *
 * Lowers a fully-typed, scope-resolved HIR tree into MIR.
 *
 * Pass structure (single pass over HIR):
 *   buildProgram(HIRProgram*)
 *     └─ for each top-level item:
 *           buildFunction(HIRFunction*)
 *           buildStruct  (HIRStruct*)   – recorded for field layout only
 *           buildImpl    (HIRImpl*)     – delegates to buildFunction
 *   buildFunction
 *     └─ creates MIRBody, seeds locals[0] = return slot
 *        then lowers the body block via buildBlock()
 *   buildBlock / buildStmt / buildExpr
 *     └─ recursive descent; every expression is lowered to a fresh
 *        temporary and returns the MIRPlace that holds its result.
 *
 * Naming conventions for generated locals:
 *   _0          – return slot (always)
 *   _1 .. _N    – compiler temporaries (isTemp = true)
 *   <name>      – user-visible variables (isTemp = false)
 */
class MIRBuilder : public Pass
{
public:
    MIRBuilder() = default;
    MIRBuilder(std::shared_ptr<Context> cnt)
    {
        context = cnt;
    }

    ~MIRBuilder() {}

    virtual void run() override
    {
        context->mirProgram = std::make_unique<MIRProgram>(buildProgram(context->hirProgram.get()));
    }

    /** Entry point. Consumes an HIRProgram and returns a fully built MIRProgram. */
    MIRProgram buildProgram(HIRProgram *prog);

private:
    // ── per-function state ────────────────────────────────────────────────────
    MIRBody *body_ = nullptr;
    size_t tempCtr_ = 0;
    BasicBlockId curBB_ = 0;

    /** Maps user variable name → local index inside the current function. */
    std::unordered_map<std::string, size_t> varMap_;

    // ── top-level builders ────────────────────────────────────────────────────
    MIRFunction buildFunction(HIRFunction *fn);
    MIRGlobal buildGlobal(HIRVarDecl *decl);

    // ── block / statement builders ────────────────────────────────────────────
    void buildBlock(HIRBlock *block);
    void buildStmt(HIRStmt *stmt);

    void buildVarDecl(HIRVarDecl *decl);
    void buildAssign(HIRAssign *assign);
    void buildIf(HIRIf *ifStmt);
    void buildLoop(HIRLoop *loop);
    void buildReturn(HIRReturn *ret);
    void buildExprStmt(HIRExprStmt *es);

    // ── expression builders ───────────────────────────────────────────────────
    // Every buildExpr* returns the MIRPlace that holds the result.
    MIRPlace buildExpr(HIRExpr *expr);
    MIRPlace buildLiteral(HIRLiteral *lit);
    MIRPlace buildNameRef(HIRNameRef *ref);
    MIRPlace buildBinaryOp(HIRBinaryOp *bin);
    MIRPlace buildCast(HIRCast *cast);
    MIRPlace buildCall(HIRCall *call);
    MIRPlace buildMemberAccess(HIRMemberAccess *ma);
    MIRPlace buildStructInit(HIRStructInit *si);
    MIRPlace buildRef(HIRRef *ref);

    // ── operand helpers ───────────────────────────────────────────────────────
    /**
     * Decide Copy vs Move for a place.
     * Types that implement Copy get MIRCopy; everything else gets MIRMove.
     * For now we treat all primitives as Copy.  Struct/trait types are Move.
     */
    MIROperand placeToOperand(MIRPlace place);
    MIROperand exprToOperand(HIRExpr *expr);

    // ── local / temp management ───────────────────────────────────────────────
    size_t newLocal(const std::string &name,
        std::shared_ptr<Type> type,
        bool isMutable,
        bool isTemp,
        bool isArg = false);

    MIRPlace makeTempPlace(std::shared_ptr<Type> type);
    MIRPlace localPlace(size_t index);

    // ── basic-block management ────────────────────────────────────────────────
    BasicBlockId newBlock(const std::string &label = "");
    void sealBlock(BasicBlockId id, MIRTerminator term);
    void switchTo(BasicBlockId id);
    MIRBasicBlock &currentBlock();

    // ── statement emitters ────────────────────────────────────────────────────
    void emit(MIRStatement stmt);
    void emitAssign(MIRPlace lhs, MIRRValue rhs);
    void emitDrop(MIRPlace place);

    // ── BinaryOp kind conversion ──────────────────────────────────────────────
    static MIRRValueBinaryOp::Op convertBinOp(HIRBinaryOp::OpKind kind);

    // ── copy-semantics predicate ──────────────────────────────────────────────
    static bool isCopyType(const std::shared_ptr<Type> &type);
};
