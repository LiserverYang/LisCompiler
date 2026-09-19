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

    /**
     * The borrow / move / definite-assignment / dangling-return checks are NOT
     * here: they are CFG DATAFLOW (live ranges and per-point state), which the
     * HIR tree can only approximate. MIRBorrowCheck produces them on the MIR CFG
     * instead — see Source/Compiler/Private/IR/MIRBorrowCheck.cpp.
     *
     * What STAYS in this pass: type checking, mutability (E3004/E4006 — a
     * property of the place, no dataflow involved) and "a non-Copy binding needs
     * an initializer" (E3012 — a syntax rule).
     */

    /** Loop nesting depth, for validating break/continue placement. */
    size_t loopDepth_ = 0;

    /// Module path of the top-level item currently being analyzed (from
    /// Context::stmtAttributions). Reference-side lookups use it to resolve
    /// bare names to the module's internal names.
    std::string currentModule_;

    /** Module-level `let` declarations whose symbol slot the name pass created
     *  (preRegister). visit(HIRVarDecl) refreshes THOSE in place instead of
     *  reporting a redefinition — a second global of the same name is still a
     *  real error and is not in this set. */
    std::unordered_set<const HIRVarDecl *> preRegisteredGlobals_;

    /** Does a bare (unprefixed) local/param name conflict with the symbol it
     *  resolves to? Only a module-level `let` of the ROOT module shares the bare
     *  namespace (a non-root module's globals are keyed `mod$name`, so a bare
     *  lookup cannot see them); anything else is a same-scope conflict. */
    bool bareNameConflicts(const Symbol *existing) const;
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
    /// True while the statement sequence being analyzed is already unreachable
    /// (after `ret`/`break`/`continue`, a diverging call, or a branch that cannot
    /// fall through). Reachability is what tells a loop that `while true { ... }`
    /// with no break never falls through to the code after it.
    bool sequenceTerminated_ = false;

    /// Per-loop "its body contains a break" flags (innermost last), used to decide
    /// whether `while true { ... }` can ever fall through to the code after it.
    std::vector<bool> loopBodyBreaks_;

    /// Set while the ASSIGNMENT TARGET of an assign statement is being analyzed:
    /// `v[i] = x` resolves `IndexMut::set`, while reading the element in a
    /// compound assignment (`v[i] += x`) resolves `Index::at`.
    bool inAssignTarget_ = false;

    /// True while the assignment target being analyzed belongs to a COMPOUND
    /// assignment (`v[i] += x`). The read side of the element then needs its own
    /// resolution: an assignment target normally resolves IndexMut::set only,
    /// but the compound form READS the element first (Index::at) and writes it
    /// back, so visit(HIRIndexAccess) resolves both. */
    bool inCompoundAssignTarget_ = false;

    /// Set currentModule_ from Context::stmtAttributions for the item at `index`.
    void setModuleForItem(size_t index);

    // ── Stage 3 (dangling returns) moved to MIRBorrowCheck ──────────────────────
    //
    // Rejecting a `ret` of a reference that points into this function's frame is
    // E4007 in MIRBorrowCheck now: it reads the returned PLACE off the MIR (a
    // reference-typed param or a global outlives the call; a local slot, a
    // by-value param slot or a local struct field does not). The RefOrigin
    // bookkeeping that used to live on Symbol is no longer maintained.

    /// ReferenceType, or a SelfType with isRef (defensive). Used by type checking.
    static bool isReferenceType(const std::shared_ptr<Type> &ty);
    /// A CustomType with at least one reference-typed field.
    static bool structHasRefFields(const std::shared_ptr<Type> &ty);

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

    /** True when the current context may touch a PRIVATE field of `type`: we are
     *  inside a method of that very type (inherent impl or trait impl, static or
     *  not — the declaring type is what matters, not which impl block introduced
     *  the method). The comparison uses the ORIGIN name so a generic method body
     *  (`impl Box<T>`, whose `currentStructType` is the definition) can access
     *  private fields of an instantiation (`Box$i32`) and vice versa. */
    bool canAccessPrivateFieldsOf(const std::shared_ptr<CustomType> &type) const;

    /** Report a private-field access (E3015) unless it is legal here. */
    void checkFieldAccess(const CustomType::Field &field, const std::shared_ptr<CustomType> &type, HIRNode &errNode);

    /** Type-check a call's arguments against its parameter types — the one
     *  place every call form shares, so the rules cannot drift between a free
     *  function, an instance method, a static method and a trait method:
     *    - analyse the argument (its type is what the check needs),
     *    - typesCompatible() rather than equals(), so a `&mut T` argument
     *      satisfies a `&T` parameter,
     *    - a REFERENCE argument to a REFERENCE parameter is left for the MIR
     *      borrow checker, which registers the temporary reborrow.
     *  `paramOffset` is 1 for a method call (params[0] is the receiver, which
     *  is not an argument) and 0 for a free function or static method. When
     *  `explainUninferredGeneric` is set, a context-free generic value
     *  (`f(Option::None)` with an `Option<i32>` parameter) is explained as an
     *  inference failure instead of a bare mismatch. */
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

    /** Builtin `assert(cond)` / `assert(cond, msg)`. Unlike panic it RETURNS
     *  (void) when the condition holds, so it must not mark the sequence
     *  unreachable; the failure path is built by MIRBuilder (branch → the
     *  synthesized `assert_fail` call → abort). */
    bool handleAssertBuiltin(HIRCall *node, const std::string &name);

    /** Builtin C-string helpers: `str_len(s: &i8) -> i32` and
     *  `str_cmp(a: &i8, b: &i8) -> i32` (libc strlen/strcmp semantics). */
    bool handleStrBuiltin(HIRCall *node, const std::string &name);

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

    /** Resolve `obj[i]` on a user type through the index operator traits
     *  (`trait Index<T> { fn at(self: &Self, i: i32) -> T; }` and
     *  `trait IndexMut<T> { fn set(self: &mut Self, i: i32, v: T); }`).
     *  `forWrite` picks IndexMut (the node is an assignment target) over Index.
     *  Mirrors resolveOperatorMethod: the method symbol lives under the type's
     *  ORIGIN name, and a generic instantiation gets its struct arguments
     *  substituted into the signature before the operand check. Returns false
     *  when the type does not implement the trait. */
    bool resolveIndexMethod(HIRIndexAccess *node, const std::shared_ptr<CustomType> &ct, bool forWrite);

    /** Verify every constraint an `impl` put on its generic parameters against
     *  the instantiation a call site is using (`subst` maps the method's
     *  parameter names to the receiver/class instantiation's arguments).
     *  `impl<T: Copy> Index<T> for Vec<T>` indexed as `Vec<String>` is rejected
     *  here — the element would otherwise be handed out BY VALUE while the
     *  container still owned it (a double drop). Reports the first violation and
     *  returns false. MUST run before the signature is substituted:
     *  substituteType() rebuilds the FunctionType and drops its genericParams. */
    bool checkMethodGenericBounds(const std::shared_ptr<FunctionType> &fnType,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &subst,
        HIRNode &errNode,
        const std::string &owner);

    /** Bind one binary-operator operand to the trait method's parameter type.
     *
     * A parameter may be the operand's type directly, or a REFERENCE to it: the
     * comparison traits take `&Self` so that `a == b` never consumes either side
     * (a non-Copy operand such as String could not implement a by-value `eq` at
     * all — the operator would move both operands). When the parameter is a
     * reference, the operand is wrapped in a HIRRef that borrows the place, and
     * the caller must then NOT treat it as a move source. Returns false when the
     * operand does not fit the parameter. */
    bool bindOperatorOperand(std::unique_ptr<HIRExpr> &operand, const std::shared_ptr<Type> &paramTy);
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
    /** Best-effort type of a module-level `let` for the name pass: the explicit
     *  annotation, or the literal initializer's kind. Type errors are
     *  suppressed (pass 2 is authoritative). */
    std::shared_ptr<Type> bestEffortGlobalType(HIRVarDecl *decl);

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
    /** Resolve a function's/method's signature into a FunctionType (fills
     *  `f->type` and `f->returnType`; hands back the resolved parameter types).
     *  `inferredRet` supplies the return type when the body infers it. Shared by
     *  the function pre-pass and the impl-method pre-pass. */
    std::shared_ptr<Type> resolveFunctionSignature(HIRFunction *f,
        const std::shared_ptr<Type> &inferredRet,
        std::vector<std::shared_ptr<Type>> *paramTypesOut = nullptr);

    /** Resolve ONE method's best-effort signature and append its
     *  CustomType::Method entry (symbol keyed `Struct::name`). */
    void preRegisterMethodType(HIRImpl *impl,
        HIRFunction *method,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns,
        std::vector<CustomType::Method> &out);

    /** Pass 1c-3: attach every impl's method signatures to its type BEFORE any
     *  method body is analyzed, so `self.helper()` resolves. */
    void preRegisterImplMethods(HIRImpl *impl);

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
        // Borrow / move / definite assignment / dangling returns are checked by
        // MIRBorrowCheck on the MIR CFG, not by this pass.
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
    virtual void visit(HIRDeref *node) override;
    virtual void visit(HIRUnaryOp *node) override;
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
