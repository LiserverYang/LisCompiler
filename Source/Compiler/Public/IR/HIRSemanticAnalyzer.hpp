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

    /// Module path of the top-level item currently being analyzed (from
    /// Context::stmtAttributions). Reference-side lookups use it to resolve
    /// bare names to the module's internal names.
    std::string currentModule_;
    /// Source FILE of the top-level item currently being analyzed (same
    /// attribution). Diagnostics attach to this path — Context::filePath is
    /// the main file by the time sema runs, so module items would otherwise
    /// report the wrong file.
    std::string currentFilePath_;

    /** Module-aware symbol lookup: bare names resolve against (1) the local
     *  scope chain (locals/params are always bare), (2) the current module's
     *  internal top-level name, (3) the root module's bare name. Names already
     *  carrying a `$` prefix (module-qualified) are looked up directly. */
    Symbol *lookupModuleAware(const std::string &name);
    /** True when monomorphization will substitute generic parameters in the body
     *  currently being analyzed — a generic function, or a method of a generic
     *  struct. A bare generic type there is still substituted later, so it is not
     *  the un-inferrable-definition error the same code is outside such a
     *  context. */
    bool inGenericContext() const;
    // ── definite assignment (`let x;` has no initializer) ─────────────────────

    /// Symbol → initialized, for every LOCAL binding visible at one point.
    using InitState = std::unordered_map<Symbol *, bool>;

    /// Snapshot the visible local bindings (locals/params only — the global
    /// scope holds top-level symbols, which are always initialized).
    InitState snapshotInitState();

    /// Re-read the SAME key set. Re-walking the scope chain would be wrong here:
    /// bindings declared inside a branch are out of scope once it exits, so their
    /// Symbol objects are already destroyed.
    InitState captureInitState(const InitState &keys) const;

    /// Write a state back onto its symbols.
    void restoreInitState(const InitState &state);

    /// dst[k] = dst[k] && other[k] — the AND rule for a branch join: a binding is
    /// definitely initialized after the join only if BOTH paths initialized it.
    static void mergeInitState(InitState &dst, const InitState &other);

    /// Reject a USE of a binding that is not definitely initialized on this path.
    /// `placeExpr` is the whole place expression (`x`, `x.f`, `a[i]`); only a plain
    /// variable root can be uninitialized.
    void checkInitializedUse(HIRExpr *placeExpr, HIRNode &errNode);

    /// True while the statement sequence being analyzed is already unreachable
    /// (after `ret`/`break`/`continue`, a diverging call, or a branch that cannot
    /// fall through). Definite-assignment diagnostics are suppressed there — the
    /// code cannot run — mirroring MIRBuilder's dead-block handling. Only the new
    /// check consults it; all other diagnostics keep reporting as before.
    bool sequenceTerminated_ = false;

    /// Per-loop "its body contains a break" flags (innermost last), used to decide
    /// whether `while true { ... }` can ever fall through to the code after it.
    std::vector<bool> loopBodyBreaks_;

    /// Set while the ASSIGNMENT TARGET of an assign statement is being analyzed, so
    /// visit(HIRNameRef) does not report the write itself as a read of an
    /// uninitialized value (`let x: i32; x = 1;`).
    bool inAssignTarget_ = false;

    /// Set currentModule_ from Context::stmtAttributions for the item at `index`.
    void setModuleForItem(size_t index);

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
        // currentFilePath_ carries the ITEM's source file (module items keep
        // their own path); context->filePath is the main file by now.
        info.codePath = currentFilePath_.empty() ? context->filePath : currentFilePath_;
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

    /** True when the current context may touch a PRIVATE field of `type`: we are
     *  inside a method of that very type (inherent impl or trait impl, static or
     *  not — the declaring type is what matters, not which impl block introduced
     *  the method). The comparison uses the ORIGIN name so a generic method body
     *  (`impl Box<T>`, whose `currentStructType` is the definition) can access
     *  private fields of an instantiation (`Box$i32`) and vice versa. */
    bool canAccessPrivateFieldsOf(const std::shared_ptr<CustomType> &type) const;

    /** Report a private-field access (E3015) unless it is legal here. */
    void checkFieldAccess(const CustomType::Field &field, const std::shared_ptr<CustomType> &type, HIRNode &errNode);

    /** Implicit reborrow for a reference argument. Returns true when `arg` is a
     *  reference-typed PLACE passed to a REFERENCE parameter: the callee borrows
     *  the referent for the duration of the call (a temporary borrow, exactly
     *  like a method receiver) and the argument is NOT moved — otherwise
     *  forwarding a `&mut T` would consume it. Returns false for by-value
     *  parameters and non-place arguments, where the normal copy/move rules
     *  apply. */
    bool tryReborrowArg(HIRExpr *arg, const std::shared_ptr<Type> &paramTy, HIRNode &errNode);

    /** Type-check a call's arguments against its parameter types — the one
     *  place every call form shares, so the rules cannot drift between a free
     *  function, an instance method, a static method and a trait method:
     *    - analyse the argument (its type is what the check needs),
     *    - typesCompatible() rather than equals(), so a `&mut T` argument
     *      satisfies a `&T` parameter,
     *    - a REFERENCE argument to a REFERENCE parameter is reborrowed and the
     *      caller keeps it; anything else follows the by-value copy/move rules
     *      and the source is consumed (handleMoveSource).
     *  `paramOffset` is 1 for a method call (params[0] is the receiver, which
     *  is not an argument) and 0 for a free function or static method. When
     *  `explainUninferredGeneric` is set, a context-free generic value
     *  (`f(Option::None)` with an `Option<i32>` parameter) is explained as an
     *  inference failure instead of a bare mismatch. */
    /** Every place a move out of the member-access chain `source` would take a
     *  field OUT of: the receiver of the first projection, then each
     *  intermediate projection (`s.a.b` → the types of `s` and of `s.a`). Empty
     *  when `source` is not a field access. A move leaves all of them behind,
     *  which is what the two rules below test. */
    std::vector<std::shared_ptr<Type>> moveOutContainers(HIRExpr *source);

    /** Rust's E0507 as a language decision (2026-09-15): a field cannot be moved
     *  out of a place the function only BORROWS. A borrow owns nothing, so the
     *  value would go to the receiver while the referent keeps releasing it —
     *  two owners of one buffer. Returns the reference the move would go
     *  through, or nullptr. (Reading a Copy field through a reference is a copy,
     *  not a move, and never reaches this.) */
    std::shared_ptr<ReferenceType> referenceMovedOutOf(HIRExpr *source);

    /** Rust's E0509 as a language decision (2026-09-15): a non-Copy field may
     *  not be moved OUT of a value whose type implements Drop, because that
     *  leaves the value partially initialized while its own destructor owns all
     *  of its fields. Returns the type that would be left behind, or nullptr.
     *  Whole-value moves leave nothing behind and stay legal. */
    std::shared_ptr<CustomType> dropTypePartiallyMovedBy(HIRExpr *source);

    void checkCallArgs(const std::vector<std::unique_ptr<HIRExpr>> &args,
        const std::vector<std::shared_ptr<Type>> &params,
        size_t paramOffset,
        HIRCall &call,
        bool explainUninferredGeneric);
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

    /** Recognize a builtin heap call (`__alloc` → `*mut i8`, `__free`,
     *  `__memcpy`, `__strlen`) by callee name, validate args, and return true
     *  if `node` is such a builtin. These lower to libc malloc/free/memcpy/
     *  strlen in LLVMIRBuilder. STDLIB ONLY: they take and return raw pointers
     *  and perform no bounds/lifetime/aliasing checking. */
    bool handleHeapBuiltin(HIRCall *node, const std::string &name);

    /** True when the top-level item being analyzed comes from a file inside one
     *  of Context::stdLibDirs — the compiler's unsafe core. Gates the heap
     *  primitives, the raw-pointer conversions and raw-pointer indexing. */
    bool inStdLib() const;

    /** Recognize a raw-pointer → reference conversion (`__deref` → `&T`,
     *  `__deref_mut` → `&mut T`) by callee name. The result type is derived
     *  from the argument's pointee, so no generics machinery is involved. This
     *  is the explicit, stdlib-only way to turn an address into a borrow; the
     *  opposite direction (`&T` → `*T`) is an implicit coercion. */
    bool handlePtrBuiltin(HIRCall *node, const std::string &name);

    /** Recognize a builtin to_string call (`to_string_i32/i64/f64/bool/char` →
     *  String) by callee name, set the return type to the stdlib String struct,
     *  and return true. These lower to malloc + sprintf + strlen in
     *  LLVMIRBuilder. */
    bool handleToStringBuiltin(HIRCall *node, const std::string &name);

    /** Recognize the builtin `panic(&i8)` call by callee name, validate its
     *  arg, set the call's type to `never` (the diverging/uninhabited type),
     *  and return true. Lowers to `fprintf(stderr, ...)` + `abort()` in
     *  LLVMIRBuilder; the MIR block that contains the call is sealed with
     *  MIRTermDiverge so everything after it is unreachable. */
    bool handlePanicBuiltin(HIRCall *node, const std::string &name);

    /** True if `ty` is the `never` primitive (the return type of `panic`). A
     *  `never`-typed expression coerces to ANY expected type — it produces no
     *  value, so nothing about the expected type is violated. Central place
     *  so match-arm unification and typesCompatible can't drift apart. */
    static bool isNeverType(const std::shared_ptr<Type> &ty);

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
    /** Reject types that contain themselves BY VALUE (`struct A { pub a: A }`,
     *  `enum E { A(E) }`, or a cycle through several types). Such a type has
     *  infinite size: the LLVM lowering gets an opaque/unsized type, the module
     *  verifier rejects it (`GEP into unsized type!`) and the compiler used to
     *  abort with that message instead of a diagnostic. Runs once, after every
     *  struct/enum body has been analyzed, over the whole program. */
    void checkForRecursiveTypes(HIRProgram *program);

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
    virtual void visit(HIRTry *node) override;
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