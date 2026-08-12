/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include "Analysiser/SymbolTable.hpp"
#include "Analysiser/TypeContext.hpp"
#include "Core/Pass.hpp"
#include "IR/HIR.hpp"
#include "IR/HIRVisitor.hpp"
#include "Logger/ErrorID.hpp"
#include "Logger/Logger.hpp"

#include <cstdlib>
#include <unordered_set>

class HIRSemanticAnalyzer : public HIRVisitor, public Pass
{
private:
    // -----------------------------------------------------------------------
    struct FunctionInfo
    {
        std::shared_ptr<Type> declaredReturnType;
        std::unordered_map<std::string, std::shared_ptr<GenericParamType>> gParams;
        bool hasReturnValue = false;
        bool isInFunction = false;
    } functionInfo;

    std::shared_ptr<Type> currentStructType;
    std::string traitName;

    bool isInTraitMethod = false;
    bool isInStruct = false;
    std::unordered_map<std::string, std::shared_ptr<GenericParamType>> traitGParams;
    std::unordered_map<std::string, std::shared_ptr<GenericParamType>> structGParams;

    /**
     * When true, resolveType() suppresses its error diagnostics. Used during
     * the pre-registration function-signature pass, where types are resolved
     * best-effort (the authoritative checks happen in the full analysis pass).
     */
    bool suppressTypeErrors_ = false;

    /** Loop nesting depth, for validating break/continue placement. */
    size_t loopDepth_ = 0;

    // ── borrow-checker state (Stage 1: lexical temps, Stage 2: NLL) ────────────

    /// One active borrow of a place. Created by `&p` / `&mut p`, a method
    /// receiver, or a temporary call-argument borrow.
    struct Borrow
    {
        std::string root;              // borrowed binding name (e.g. "x")
        std::string holderName;        // borrow variable (`let r = &p` → "r")
        std::vector<std::string> path; // field path (empty = whole root)
        bool isMut;                    // &mut vs &
        bool isPromoted;               // variable borrow (`let r = &p`) survives statements
        size_t createStmt;             // statement ordinal at creation (NLL)
        SourcePosition pos;
    };

    /// Access kind of a place use, for the borrow-conflict rules.
    enum class BorrowUseKind
    {
        Read,         // Copy read — conflicts with active &mut borrows
        Write,        // mutation — conflicts with any active borrow
        Move,         // non-Copy consumption — conflicts with any active borrow
        BorrowShared, // creating `&p` — conflicts with active &mut borrows
        BorrowMut,    // creating `&mut p` — conflicts with any active borrow
    };

    /// Active borrows in the current statement/block scope chain (innermost last).
    std::vector<Borrow> activeBorrows_;

    /// Block-scope markers: size of activeBorrows_ at each block entry; a block's
    /// borrows (and its temporaries) are truncated when the block exits.
    std::vector<size_t> blockBorrowMarkers_;

    /// Statement marker: size of activeBorrows_ at the start of the current
    /// statement. Temporary (non-promoted) borrows created inside it are removed
    /// at statement end; promoted (variable) borrows survive.
    size_t stmtBorrowStart_ = SIZE_MAX;

    // ── NLL (non-lexical lifetimes) state ──────────────────────────────────────

    /// Statement ordinal: incremented per analyzeStmt, reset per function. Used
    /// to decide whether a promoted borrow is live at a conflicting use (the
    /// borrow is live from its createStmt up to its holder's last-use ordinal).
    size_t stmtOrdinal_ = 0;

    /// All promoted (variable) borrows in the current function — survives block
    /// truncation so the end-of-function NLL resolve can check liveness.
    std::vector<Borrow> promotedBorrows_;

    /// Borrow-holder name → last statement ordinal where it is used.
    std::unordered_map<std::string, size_t> holderLastUseStmt_;

    /// A place use that may conflict with a PROMOTED borrow; resolved at the end
    /// of the function once holder last-uses are known.
    struct PendingConflict
    {
        std::string root;
        std::vector<std::string> path;
        BorrowUseKind kind;
        size_t ordinal;
        SourcePosition pos;
        size_t length;
    };
    std::vector<PendingConflict> pendingBorrowConflicts_;

    /// Resolve deferred promoted-borrow conflicts once holder last-uses are known.
    void resolvePromotedBorrows();

    /// Log a borrow error at an absolute source position (used by the NLL resolve).
    void logAtPosition(const SourcePosition &pos, size_t length, const std::string &msg, size_t errorId);

    /// Shared conflict message + error-id builder (inline and NLL resolve).
    static void borrowConflictInfo(std::string &msg, size_t &errorId, const std::string &name, BorrowUseKind kind);

    /// Register a borrow of `(root, path)`, checking aliasing conflicts first.
    /// `isPromoted` marks a borrow-variable (`let r = &p`) that survives the
    /// statement. Returns true on success.
    bool registerBorrow(const std::string &root, const std::vector<std::string> &path, bool isMut, bool isPromoted, HIRNode &errNode);

    /// Check a place use against active borrows; logs a conflict and returns
    /// false if the access is forbidden.
    bool checkBorrowUse(const std::string &root, const std::vector<std::string> &path, BorrowUseKind kind, HIRNode &errNode);

    /// Remove non-promoted (temporary) borrows created after `marker`.
    void endTemporaryBorrowsSince(size_t marker);

    /// True if two place paths overlap (one is a prefix of the other).
    static bool pathsOverlap(const std::vector<std::string> &a, const std::vector<std::string> &b);

    // ── Stage 3: dangling / escape analysis (RefOrigin) ─────────────────────────
    //
    // A returned reference is dangling iff it (transitively) points into this
    // function's stack frame. Every reference-typed expression resolves to a
    // RefOrigin; Local → reject on return, everything else → allow.

    /// ReferenceType, or a SelfType with isRef (defensive).
    static bool isReferenceType(const std::shared_ptr<Type> &ty);
    /// A CustomType with at least one reference-typed field.
    static bool structHasRefFields(const std::shared_ptr<Type> &ty);
    /// Origin of the reference VALUE held by a reference-typed binding.
    RefOrigin originOfBinding(Symbol *sym);
    /// Origin of the STORAGE denoted by the place (root, path) — what `&place` points at.
    RefOrigin placeStorageOrigin(const std::string &root, const std::vector<std::string> &path);
    /// Origin of a reference-typed expression's value (the returned-reference check).
    RefOrigin originOfReferenceValue(HIRExpr *expr);
    /// Origin of the reference value stored at the struct place (root, path).
    RefOrigin fieldValueOrigin(const std::string &root, const std::vector<std::string> &path);
    /// Mark a reference-typed / ref-fielded PARAM as Param (safe) at function entry.
    void setupParamOrigin(Symbol *sym);
    /// Populate a LOCAL binding's origins from its initializer (decl).
    void setupBindingOrigins(HIRVarDecl *node);
    /// Refresh origins after an assignment (re-assignment must re-derive them).
    void updateAssignOrigins(HIRAssign *node);
    /// Reject `ret` of a reference that points into this function's frame.
    void checkDanglingReturn(HIRReturn *node);
    /// Reject `ret` of a struct whose reference fields point into this frame.
    void checkStructReturn(HIRExpr *value, const std::shared_ptr<CustomType> &declaredStruct, HIRNode &errNode);

    // -----------------------------------------------------------------------
    void log(HIRNode &node, const std::string &msg, size_t errorId = E_SemanticError, Logger::LogLevel level = Logger::LogLevel::ERROR, bool exit = false)
    {
        Logger::LogInfo info{};
        info.codePath = context->filePath;
        info.code = &context->fileValue;
        info.col = node.position.col;
        info.line = node.position.line;
        info.length = node.length;
        info.beginPosition = node.position.lineStart;
        info.msg = msg;
        info.errorId = errorId;
        info.exit = exit;
        Logger::Log(level, info);
    }

    std::shared_ptr<Type> resolveType(const HIRRawType &raw, HIRNode &errorNode);

    /** Dispatch a method call on a generic param receiver via its trait bounds. */
    void dispatchGenericParamMethod(HIRCall *node, std::shared_ptr<GenericParamType> gp);

    /** Analyze the move semantics of consuming `source` (a whole variable or a
     *  field path). Rejects whole-value and field use-after-move and records
     *  partial (field) moves on the root symbol. */
    void handleMoveSource(HIRExpr *source, HIRNode &errNode);

    /** Recognize a builtin print call (`print_str/int/float/bool/char`, `println`)
     *  by callee name, validate its args, set the call's type to VOID, and return
     *  true if `node` is such a builtin call (skipping normal call resolution).
     *  These lower to libc `printf` in LLVMIRBuilder. */
    bool handlePrintBuiltin(HIRCall *node, const std::string &name);

    /** Recognize a builtin input call (`read_line` → &i8, `read_int` → i32,
     *  `read_f64` → f64) by callee name, set the call's return type, and return
     *  true if `node` is such a builtin (skipping normal call resolution).
     *  These lower to libc `fgets` + parse in LLVMIRBuilder. */
    bool handleInputBuiltin(HIRCall *node, const std::string &name);

    /** Recognize a builtin heap call (`__alloc` → &mut i8, `__free`,
     *  `__memcpy`, `__strlen`) by callee name, validate args, and return true
     *  if `node` is such a builtin. These lower to libc malloc/free/memcpy/
     *  strlen in LLVMIRBuilder. */
    bool handleHeapBuiltin(HIRCall *node, const std::string &name);

    /** Recognize a builtin to_string call (`to_string_i32/i64/f64/bool/char` →
     *  String) by callee name, set the return type to the stdlib String struct,
     *  and return true. These lower to malloc + sprintf + strlen in
     *  LLVMIRBuilder. */
    bool handleToStringBuiltin(HIRCall *node, const std::string &name);

    // ── operator overloading ──────────────────────────────────────────────
    /// Trait name for a binary op (`+`→"Add"), or nullptr for logical ops
    /// (`&&`/`||`) which never overload.
    static const char *operatorTraitName(HIRBinaryOp::OpKind op);
    /// Method name for a binary op (`+`→"add"), or nullptr for logical ops.
    static const char *operatorMethodName(HIRBinaryOp::OpKind op);
    /// True if `name` is one of the builtin operator traits that primitives
    /// auto-implement (Add/Sub/Mul/Div/Rem/PartialEq/PartialOrd/BitAnd/.../Shr).
    static bool isOperatorTrait(const std::string &name);
    /// Resolve `node` (`a op b` on a struct/enum) to the trait-method call
    /// `a.method(b)`, filling the operator fields and node->type. Returns false
    /// (logging) if the struct's impl signature does not match.
    bool resolveOperatorMethod(HIRBinaryOp *node, const std::shared_ptr<CustomType> &ct, const char *opMethod, const char *opTrait);
    /// Resolve `a op b` where both operands are a generic param `T: <opTrait>`
    /// inside a generic function body. Emits the placeholder callee
    /// `<T>::method`; MIRMonomorphization retargets it to the concrete struct
    /// method (or falls back to a direct binary op for primitives).
    bool resolveGenericOperatorMethod(HIRBinaryOp *node, const std::shared_ptr<GenericParamType> &gp, const char *opMethod, const char *opTrait);

    // Dispatch helpers
    void analyzeExpr(HIRExpr *expr);
    void analyzeStmt(HIRStmt *stmt);

    // First pass: register top-level names so forward refs work
    void preRegister(HIRNode *item);
    /** Build the CustomType for a struct (used by preRegister and full analysis). */
    std::shared_ptr<Type> buildStructType(HIRStruct *node);
    /** Build the CustomType for an enum (fat tagged union) — used by the
     *  pre-registration pass and the full analysis. */
    std::shared_ptr<Type> buildEnumType(HIREnum *node);
    /** Best-effort function-signature resolution for the pre-registration pass. */
    void preRegisterFunctionType(HIRFunction *f, const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns = {});

    /** Best-effort type of a `ret <expr>` in the pre-pass (before the body is analyzed). */
    std::shared_ptr<Type> bestEffortRetType(HIRExpr *expr,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &paramTypes,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns);
    /** Scan a body for the first concrete `ret` value type (best-effort). */
    std::shared_ptr<Type> scanInferredReturn(HIRBlock *body,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &paramTypes,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns);
    /**
     * Pre-register a struct's trait conformance (implTrait) from an impl,
     * BEFORE pass 2, so bound checks are order-independent.
     */
    void preRegisterImplTrait(HIRImpl *node);
    /** Resolve an impl's trait generic args (shared by pre-pass and conformance). */
    std::vector<std::shared_ptr<Type>> resolveTraitArgs(HIRImpl *node, const std::shared_ptr<TraitType> &traitType, std::unordered_map<std::string, std::shared_ptr<Type>> *outSubst);

    /** Resolve one trait bound (`T: Iterator<i32>`) to an instantiated TraitType,
     *  or nullptr (optionally logging) if the trait is unknown. Shared by struct
     *  and function generic-param resolution so they can't diverge. */
    std::shared_ptr<TraitType> resolveTraitConstraint(const HIRGenericConstraint &con, HIRNode &errNode, bool silent);

public:
    HIRSemanticAnalyzer() = default;
    HIRSemanticAnalyzer(std::shared_ptr<Context> cnt)
    {
        context = cnt;
        SymbolTable::getInstance().initGlobalScope();
    }

    virtual void run() override
    {
        // Count only semantic errors from THIS compilation unit (the process
        // may be reused, e.g. for a REPL or multiple files).
        Logger::ResetErrorCount();

        visit(context->hirProgram.get());

        if (context->args->getArg("print_hir") == "true")
            printHIR((HIRNode *)context->hirProgram.get());

        if (context->args->getArg("print_typetable") == "true")
            context->typeContext->printTypeTable();

        // Semantic errors are reported non-fatally so that all of them surface
        // in one run. If any were logged, the program is invalid — abort cleanly
        // here rather than letting the MIR/LLVM stages run on broken input
        // (which used to crash with an unhelpful runtime_error).
        if (Logger::GetErrorCount() > 0)
            exit(1);
    }

    virtual void visit(HIRProgram *node) override;
    virtual void visit(HIRStruct *node) override;
    virtual void visit(HIREnum *node) override;
    virtual void visit(HIRTrait *node) override;
    virtual void visit(HIRImpl *node) override;
    virtual void visit(HIRFunction *node) override;
    virtual void visit(HIRBlock *node) override;
    virtual void visit(HIRVarDecl *node) override;
    virtual void visit(HIRAssign *node) override;
    virtual void visit(HIRIf *node) override;
    virtual void visit(HIRMatch *node) override;
    virtual void visit(HIRLoop *node) override;
    virtual void visit(HIRReturn *node) override;
    virtual void visit(HIRBreak *node) override;
    virtual void visit(HIRContinue *node) override;
    virtual void visit(HIRExprStmt *node) override;
    virtual void visit(HIRNameRef *node) override;
    virtual void visit(HIRLiteral *node) override;
    virtual void visit(HIRBinaryOp *node) override;
    virtual void visit(HIRCast *node) override;
    virtual void visit(HIRCall *node) override;
    virtual void visit(HIRMemberAccess *node) override;
    virtual void visit(HIRIndexAccess *node) override;
    virtual void visit(HIRArrayLiteral *node) override;
    virtual void visit(HIRStructInit *node) override;
    virtual void visit(HIRVariantInit *node) override;
    virtual void visit(HIRRef *node) override;
    virtual void visit(HIRImport *node) override;

    std::vector<std::shared_ptr<Type>> inferGenericArguments(
        const std::vector<std::shared_ptr<Type>> &genericParams,
        const std::vector<std::shared_ptr<Type>> &paramTypes,
        const std::vector<std::unique_ptr<HIRExpr>> &args);

    void matchGenericType(
        std::shared_ptr<Type> paramTy,
        std::shared_ptr<Type> argTy,
        std::unordered_map<std::string, std::shared_ptr<Type>> &genericMap);

    std::shared_ptr<FunctionType> instantiateGenericFunction(
        std::shared_ptr<FunctionType> genericFunc,
        const std::vector<std::shared_ptr<Type>> &genericArgs);

    std::shared_ptr<Type> substituteType(
        std::shared_ptr<Type> ty,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &subst);
};