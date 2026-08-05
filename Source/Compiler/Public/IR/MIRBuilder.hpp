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
#include <unordered_set>
#include <vector>

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

    /**
     * Stack of "owned local" vectors, one per lexical block currently being
     * lowered. buildVarDecl records non-Copy locals here; buildBlock drops them
     * in reverse declaration order (LIFO) when the block ends.
     */
    std::vector<std::vector<MIRPlace>> ownedLocalsStack_;

    /**
     * Locals whose value has been moved out (via a MIRMove operand). These
     * must not be dropped at block end — the receiver now owns the value.
     */
    std::unordered_set<size_t> movedLocals_;

    /**
     * Root local index → full Field projection paths moved out by partial
     * (field) moves, e.g. `let x = pair.a` → [["a"]], `let x = p.a.b` →
     * [["a","b"]]. The root still owns the remaining fields, so emitDrop()
     * must not drop the whole struct (that would double-free the moved field);
     * instead it decomposes the root recursively via emitDropPartial(),
     * dropping every still-owned non-Copy field down the tree.
     */
    std::unordered_map<size_t, std::vector<std::vector<std::string>>> partiallyMovedFields_;

    /**
     * Enclosing loop targets, innermost last. break lowers to
     * Goto{breakTarget} (the loop exit), continue to Goto{continueTarget}
     * (the loop header). ownedFrameBase is the index into ownedLocalsStack_
     * of the loop-body block's frame: a break/continue must drop exactly the
     * frames from the current innermost one down to (and including) the loop
     * body frame — NOT enclosing scopes, whose locals outlive the loop.
     */
    struct LoopTarget
    {
        BasicBlockId breakTarget;
        BasicBlockId continueTarget;
        size_t ownedFrameBase;
    };
    std::vector<LoopTarget> loopTargets_;

    // ── top-level builders ────────────────────────────────────────────────────
    MIRFunction buildFunction(HIRFunction *fn);
    MIRGlobal buildGlobal(HIRVarDecl *decl);

    // ── block / statement builders ────────────────────────────────────────────
    void buildBlock(HIRBlock *block);
    void buildStmt(HIRStmt *stmt);

    void buildVarDecl(HIRVarDecl *decl);
    void buildAssign(HIRAssign *assign);
    void buildIf(HIRIf *ifStmt);
    MIRPlace buildMatch(HIRMatch *match);
    void buildLoop(HIRLoop *loop);
    void buildReturn(HIRReturn *ret);
    void buildBreak(HIRBreak *brk);
    void buildContinue(HIRContinue *cont);
    void buildJump(const char *keyword, bool toExit);
    void buildExprStmt(HIRExprStmt *es);

    /** Emit drop statements for owned locals in scopes from the innermost
     *  active one down to (and including) frame `frameBase`, innermost-first
     *  (reverse LIFO). Used on early-exit paths to correctly drop locals that
     *  would otherwise only be dropped at block-end fall-through:
     *    - return:      frameBase = 0 (the whole function dies)
     *    - break/continue: frameBase = the loop body's frame — enclosing
     *      scopes outlive the loop and must NOT be dropped here. */
    void dropOwnedLocalsFrom(size_t frameBase);

    /// Ownership state at one CFG point: which locals are fully moved out and
    /// which field paths are moved out. Used for per-branch flow-sensitivity.
    struct OwnershipState
    {
        std::unordered_set<size_t> moved;
        std::unordered_map<size_t, std::vector<std::vector<std::string>>> partial;
    };

    /** On THIS branch's edge, drop outer-scope locals that are owned here but
     *  dead on the SIBLING branch (moved out there) — otherwise they leak on
     *  this path while the join suppresses the drop. Sets the global state to
     *  `self` so emitDrop's movedLocals_ check reflects this path. */
    void emitPathDrops(const OwnershipState &self, const OwnershipState &sibling);

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
    MIRPlace buildVariantInit(HIRVariantInit *vi);
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

    /** Drop `place` but skip the moved-out sub-paths (relative to `place`).
     *  Recurses into struct fields; used to decompose a partially-moved root
     *  into per-field drops so sibling fields are dropped but the moved ones
     *  are not (double-free). */
    void emitDropPartial(MIRPlace place,
        const std::vector<std::vector<std::string>> &movedPaths);

    // ── BinaryOp kind conversion ──────────────────────────────────────────────
    static MIRRValueBinaryOp::Op convertBinOp(HIRBinaryOp::OpKind kind);

    // ── copy-semantics predicate ──────────────────────────────────────────────
    static bool isCopyType(const std::shared_ptr<Type> &type);
};
