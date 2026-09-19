/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include "Core/Pass.hpp"
#include "IR/HIR.hpp"
#include "IR/MIR.hpp"
#include "Logger/Logger.hpp"
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

        // MIR-level diagnostics (a `-> never` function whose body never
        // diverges) must stop the pipeline HERE: the later stages lower the
        // well-formed-but-wrong body without complaint and the compiler would
        // exit 0 on an invalid program. Tests call buildProgram() instead (this
        // gate exits the process, mirroring HIRSemanticAnalyzer::run()).
        if (Logger::GetErrorCount() > 0)
            exit(1);
    }

    /** Entry point. Consumes an HIRProgram and returns a fully built MIRProgram. */
    MIRProgram buildProgram(HIRProgram *prog);

private:
    // ── per-function state ────────────────────────────────────────────────────
    MIRBody *body_ = nullptr;
    size_t tempCtr_ = 0;
    BasicBlockId curBB_ = 0;

    /** Source FILE of the top-level item currently being lowered (from
     *  Context::stmtAttributions). MIR-level diagnostics use it so a module
     *  (stdlib) function is reported against its own file — Context::filePath is
     *  the main file by the time MIR runs. */
    std::string currentItemFilePath_;

    /** Emit a diagnostic at `pos` (in the current item file) and count it. */
    void logAtItem(const SourcePosition &pos, size_t length, const std::string &msg);

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
     * --- Drop slots and drop flags (conditional drops) -------------------
     *
     * A DROP SLOT is one leaf of a local's drop decomposition: the place a
     * single drop statement targets -- the local itself for a type with no
     * droppable fields (String, an enum, ...), or a Field path inside it for
     * a struct of droppable fields. collectDropSlots() walks exactly the
     * recursion emitDropPartial() does, so one slot is precisely one drop
     * statement the static decomposition would have emitted.
     */
    struct DropSlot
    {
        std::vector<std::string> path; // Field names from the root ('' = root)
        std::shared_ptr<Type> type;    // type of the place at `path`
    };

    /** local index -> its drop slots, in drop order (filled on registration). */
    std::unordered_map<size_t, std::vector<DropSlot>> dropSlots_;

    /**
     * Slot key (local + path, see slotKey) -> the i1 local holding that
     * slot's RUNTIME ownership bit: true while the slot still owns a value
     * that must be released, false once it was moved out (or dropped).
     *
     * Rust's Drop trait needs exactly this. Ownership is flow-sensitive: a
     * value moved out on ONE path is still owned on the others, so the drop
     * cannot be placed statically without either freeing it early or leaking
     * it. Freeing early is the worse half -- it is observable (destructor
     * order) and it is a use-after-free for every alias the borrow checker
     * does not track (a raw pointer out of String::to_cstr(), say). Each slot
     * therefore carries a bit: set when the slot becomes live, cleared by the
     * move that ends its life, and tested by the drop.
     */
    std::unordered_map<std::string, size_t> dropFlags_;

    /**
     * Slots whose ownership DIFFERS between two control-flow paths (a
     * conditional move). Their drop is emitted as `if (flag) drop(slot)` at
     * the scope end instead of being decided statically.
     */
    std::unordered_map<size_t, std::unordered_set<std::string>> dynamicSlots_;

    /**
     * Enclosing loop targets, innermost last.
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

    /** On the edge of a two-way branch, record every owned local whose drop
     *  slot is owned here but moved out on the SIBLING edge (or vice versa).
     *  Such a slot is a CONDITIONAL move: its drop is emitted under its
     *  runtime flag at the scope end. Nothing is dropped here — see
     *  emitDropDynamic for why placing the drop on this edge is wrong. */
    void markConditionalMoves(const OwnershipState &self, const OwnershipState &sibling);

    // ── drop slots / drop flags ─────────────────────────────────
    /** Stable key for a slot: the local index, then each Field name. */
    static std::string slotKey(size_t local, const std::vector<std::string> &path);

    /** Collect the drop slots of `type` under `prefix`, walking the SAME
     *  recursion emitDropPartial() uses (a field is decomposed when it needs
     *  a drop and is not an array). A type that decomposes into nothing is
     *  its own slot, so a slot set is never empty. */
    static void collectDropSlots(const std::shared_ptr<Type> &type,
        std::vector<std::string> &prefix,
        std::vector<DropSlot> &out);

    /** Register `place` as an owned local/temp: remember its drop slots and
     *  give each one a drop flag initialised to 'owns its value'. Called for
     *  every place pushed onto ownedLocalsStack_. */
    void registerDropSlots(const MIRPlace &place);

    /** Set the drop flags of every slot at or under `path` (empty path = all
     *  of them) to `owned`. No-op for a place that has no slots. */
    void writeSlotFlags(size_t local, const std::vector<std::string> &path, bool owned);

    /** Is slot `path` of `local` moved out in this ownership state? A slot is
     *  dead when the root is moved, or when a recorded move path is a prefix
     *  of it (moving `s.a` kills the slot `s.a.b` too). */
    static bool slotMoved(const std::unordered_set<size_t> &moved,
        const std::unordered_map<size_t, std::vector<std::vector<std::string>>> &partial,
        size_t local,
        const std::vector<std::string> &path);

    /** Drop a local that has conditional-move slots: emit `if (flag) drop`
     *  for each dynamic slot and the plain drop for the rest, then clear the
     *  flags (a drop consumes whatever it released, so a second drop site on
     *  the same path releases nothing). */
    void emitDropDynamic(const MIRPlace &place);

    // ── expression builders ───────────────────────────────────────────────────
    // Every buildExpr* returns the MIRPlace that holds the result.
    MIRPlace buildExpr(HIRExpr *expr);
    MIRPlace buildLiteral(HIRLiteral *lit);
    MIRPlace buildNameRef(HIRNameRef *ref);
    MIRPlace buildBinaryOp(HIRBinaryOp *bin);
    MIRPlace buildCast(HIRCast *cast);
    MIRPlace buildCall(HIRCall *call);
    MIRPlace buildMemberAccess(HIRMemberAccess *ma);
    MIRPlace buildIndexAccess(HIRIndexAccess *ia);
    /// `*p` — a place: the operand's place with a Deref projection appended.
    MIRPlace buildDeref(HIRDeref *d);
    MIRPlace buildArrayLiteral(HIRArrayLiteral *al);
    MIRPlace buildStructInit(HIRStructInit *si);
    MIRPlace buildVariantInit(HIRVariantInit *vi);
    MIRPlace buildRef(HIRRef *ref);
    MIRPlace buildTry(HIRTry *tryExpr);

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

    /** True if `type` is the `never` (uninhabited) type — the return type of a
     *  diverging call like `panic("...")`. buildCall seals such a block with
     *  MIRTermDiverge, and buildMatch skips the result-slot write for a
     *  diverging arm (there is no value to write). */
    static bool isNeverType(const std::shared_ptr<Type> &type);
};
