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

// Scratch buffer capacity for to_string_*: wide enough for the longest
// `%f` rendering of a double (DBL_MAX prints ~320 chars; 512 leaves headroom
// for the sign, decimal point, and locale quirks). Used for BOTH the malloc
// size and the String.cap field so they can never drift apart.
constexpr size_t TO_STRING_BUF_CAP = 512;

// ─── Type bridge ─────────────────────────────────────────────────────────────
// Implement these four functions in a TypeHelper.cpp (or inline here if your
// Type class is simple enough). The builder only calls these; it never touches
// Type internals directly.

/// Map a semantic Type to the corresponding llvm::Type*.
/// e.g. Int32 → i32, Bool → i1, Void → void, Struct "Foo" → %Foo (opaque ptr).
llvm::Type *semanticTypeToLLVM(const std::shared_ptr<Type> &ty,
    llvm::LLVMContext &ctx);

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

    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::shared_ptr<Type>>>>
        structFields_;

    // ── Per-function state ───────────────────────────────────────────────────
    struct FunctionState
    {
        llvm::Function *fn = nullptr;
        const MIRBody *body = nullptr;

        /// A VOID `fn main()` is emitted as the standard `i32 @main()` (see
        /// declareFunctions): the C runtime turns the return value into the
        /// process status, and a void main hands it whatever the last libc
        /// call left in eax. Every `ret` in such a function returns 0.
        bool implicitZeroRet = false;

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

    /// Generate __drop_<T> glue function bodies for every struct type,
    /// recursively dropping non-Copy fields. Must run before function bodies
    /// are lowered — lowerDrop() skips any glue that is still a declaration.
    void generateDropGlue();

    // ── Body lowering ────────────────────────────────────────────────────────

    void createAllocas(FunctionState &fs);
    void lowerBlock(FunctionState &fs, const MIRBasicBlock &bb);
    void lowerStatement(FunctionState &fs, const MIRStatement &stmt);
    void lowerTerminator(FunctionState &fs, const MIRTerminator &term);

    // ── Statement variants ───────────────────────────────────────────────────

    void lowerAssign(FunctionState &fs, const MIRStmtAssign &s);

    /// Array literal -> destination, element by element (a store loop for the
    /// `[v; N]` repeat form). Avoids materialising the whole `[N x T]` as an SSA
    /// aggregate, which is quadratic in N (see lowerAssign).
    void emitArrayInto(FunctionState &fs, const MIRPlace &dest, const MIRRValueArrayInit &init);

    /// The same lowering against an already-computed destination pointer.
    void emitArrayIntoPtr(FunctionState &fs, llvm::Value *dst, const MIRRValueArrayInit &init);

    /// The place behind a copy/move operand; null for a constant operand.
    static const MIRPlace *operandPlaceOf(const MIROperand &op);

    /// Pointer to a place, or null for a LOCAL whose alloca is absent (the void
    /// return slot) — the same guard storePlace() applies.
    llvm::Value *placePtrOrNull(FunctionState &fs, const MIRPlace &p);
    void lowerCall(FunctionState &fs, const MIRStmtCall &s, std::optional<llvm::BasicBlock *> normalDest = std::nullopt, std::optional<llvm::BasicBlock *> unwindDest = std::nullopt);
    void lowerDrop(FunctionState &fs, const MIRStmtDrop &s);

    // ── RValue / Operand / Place lowering ────────────────────────────────────

    llvm::Value *lowerRValue(FunctionState &fs, const MIRRValue &rv);
    llvm::Value *lowerOperand(FunctionState &fs, const MIROperand &op);
    llvm::Value *lowerConst(const MIRConst &c);

    /// Returns a pointer to the place (always a pointer — callers load/store).
    llvm::Value *lowerPlaceAsPtr(
        FunctionState &fs,
        const MIRPlace &p,
        std::shared_ptr<Type> *outFinalTy = nullptr);

    /// Returns the loaded value at the place.
    llvm::Value *loadPlace(FunctionState &fs, const MIRPlace &p);

    /// Stores `val` into the place.
    void storePlace(FunctionState &fs, const MIRPlace &p, llvm::Value *val);

    // ── Helpers ──────────────────────────────────────────────────────────────

    llvm::Type *toLLVMType(const std::shared_ptr<Type> &ty);
    llvm::Function *getOrDeclareDropGlue(const std::string &structName);
    llvm::Function *getOrDeclareFn(const std::string &name);
    /// An extern "C" symbol: declared with the caller's signature (parameters,
    /// return type and the C variadic flag), never called indirectly.
    llvm::Function *getOrDeclareExternFn(const std::string &name, const std::shared_ptr<FunctionType> &sig);
    /// The LLVM type of a C-side parameter/return. Only `bool` differs: a Lis bool
    /// is one bit, C's _Bool is one byte, so it crosses as i8 and the call site
    /// zero-extends in / truncates out (coerceBoolToC below).
    llvm::Type *toLLVMTypeForFfi(const std::shared_ptr<Type> &ty);
    /// i1 -> i8 for a bool argument of an extern call (other types pass through).
    llvm::Value *coerceBoolToC(llvm::Value *v, const std::shared_ptr<Type> &ty);
    /// i8 -> i1 for a bool RESULT of an extern call (a non-bool result, or no
    /// extern signature, passes through unchanged).
    llvm::Value *coerceBoolFromC(llvm::Value *v, const std::shared_ptr<FunctionType> &sig);
    std::string mangleName(const MIRFunction &fn) const;

    /// True for the only two things C can reach in the object: `main` (the C
    /// runtime) and every `export fn` (Context::exportedSymbols holds the C
    /// symbol each one emits -- the #[link_name] name when there is one).
    /// Everything else is module-private and gets internal linkage.
    bool isCEntryPoint(const std::string &name) const;

    /// Is this type (or anything reachable inside it) a `&T`/`&mut T`/`*T`?
    /// Used to decide whether ANOTHER parameter could alias the one exclusive
    /// reference a function takes -- a reference smuggled inside a by-value
    /// aggregate aliases just as well as one passed directly.
    static bool containsIndirection(const std::shared_ptr<Type> &ty, int depth = 0);

    /// Does the body read or write a global variable? A global is reachable
    /// without any parameter, so a `&mut` argument bound to that same global
    /// (`f(&mut G)` where the body pushes to `G`) would alias it.
    static bool bodyReferencesGlobals(const MIRFunction &mirFn);

    /// State what the borrow checker proved: a `&mut T` parameter is the ONLY
    /// way into its object, so accesses through it cannot alias anything else
    /// (`noalias`). See the definition for the four conditions that keep the
    /// claim true -- each one has a counterexample in IrFactsTest.cpp.
    void applyParameterAttributes(llvm::Function *fn, const MIRFunction &mirFn);

    /// Declare `printf(i32(ptr, ...))` once. Still needed by the Display
    /// lowering for the primitives (`__show_*`); the print_* builtins that used
    /// it are gone — printing is stdlib code now.
    llvm::Function *getOrDeclarePrintf();
    /// MinGW/UCRT defines the standard streams as `__acrt_iob_func(fd)` (a
    /// function, not a data symbol) — get a FILE* through it there; fall back to
    /// the `@stdin` / `@stdout` external globals on other libcs.
    llvm::Function *getOrDeclareAcrtIobFunc();
    llvm::GlobalVariable *getOrDeclareStdin();

    /// Builtin heap: declare `malloc`/`free`/`memcpy`/`strlen` with real
    /// signatures and lower `__alloc`/`__free`/`__memcpy`/`__strlen` to them.
    llvm::Function *getOrDeclareMalloc();
    llvm::Function *getOrDeclareFree();
    llvm::Function *getOrDeclareMemcpy();
    llvm::Function *getOrDeclareStrlen();
    /// int strcmp(const char* a, const char* b) — the `str_cmp` builtin and the
    /// lowering of `&i8 == &i8` (content comparison, not addresses).
    llvm::Function *getOrDeclareStrcmp();
    void emitHeapCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);
    bool isHeapBuiltin(const std::string &name);

    /// Builtin raw-pointer → reference conversion: `__deref` / `__deref_mut`.
    /// Both a raw pointer and a reference ARE an address at this level, so the
    /// conversion is the identity — only the result slot has to be written.
    void emitPtrBuiltin(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);
    bool isPtrBuiltin(const std::string &name);

    /// Builtin to_string: declare libc `sprintf` and lower
    /// `to_string_i32/i64/f64/bool/char` to malloc + sprintf + strlen, wrapping
    /// the result in a stdlib `String` struct.
    llvm::Function *getOrDeclareSprintf();
    void emitToStringCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);
    bool isToStringBuiltin(const std::string &name);

    /// Declare libc `void abort(void)` — the runtime trap for out-of-bounds
    /// array indexing and the second half of a panic.
    llvm::Function *getOrDeclareAbort();

    /// Builtin panic: `fprintf(stderr, "panicked: %s\n", msg)` then `abort()`.
    /// Declares libc `fprintf` and resolves the stderr FILE* (MinGW/UCRT has no
    /// `stderr` data symbol — it is `__acrt_iob_func(2)`, the same mechanism the
    /// read builtins use for stdin).
    llvm::Function *getOrDeclareFprintf();
    llvm::Value *getStderrFilePtr();
    /// Emit the panic message + abort. `msgPtr` is an i8* message.
    void emitPanicMessage(llvm::Value *msgPtr);
    /// Lower the builtin `panic(&i8)` call. Never returns (the MIR block is
    /// sealed with MIRTermDiverge, so the terminator emits `unreachable`).
    void emitPanicCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);
    bool isPanicBuiltin(const std::string &name);

    /// The block a failed `assert` branches to: writes `"<file>:<line>: assertion
    /// failed: <msg>"` to stderr and aborts. `args[0]` is the location string,
    /// `args[1]` the user message (both i8*). Never returns — the MIR block is
    /// sealed with MIRTermDiverge by MIRBuilder.
    void emitAssertFailCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);
    bool isAssertFailBuiltin(const std::string &name);

    /// Builtin C-string helpers: `str_len` -> libc strlen, `str_cmp` -> libc
    /// strcmp. `str_eq` (the `&i8 == &i8` lowering) is strcmp(a, b) == 0.
    bool isStrBuiltin(const std::string &name);
    void emitStrBuiltinCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);

    /// IO primitives (`__read_byte` / `__write` / `__flush`): the byte stream the
    /// standard library's `io` module is built on, lowered to libc fgetc / fwrite
    /// / fflush over stdin / stdout. Everything user-visible (tokens, numbers,
    /// lines, print/println) is Lis code in Source/Std/io.lis.
    bool isIoBuiltin(const std::string &name);
    void emitIoCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);
    llvm::Function *getOrDeclareFgetc();
    llvm::Function *getOrDeclareFwrite();
    llvm::Function *getOrDeclareFflush();
    /// FILE* for stdin / stdout (MinGW/UCRT resolves them through
    /// __acrt_iob_func, other libcs export data symbols).
    llvm::Value *getStdinFilePtr();
    llvm::Value *getStdoutFilePtr();
    /// Flush stdout. Called before every abort, so a panic or a bounds check does
    /// not swallow the output a program already produced.
    void emitFlushStdout();

    /// The compiler-generated `Display` lowering for the primitives. A primitive
    /// cannot carry a method body, so monomorphization retargets `<i32>::show`
    /// to `__show_i32` and the backend prints it (printf with the type's
    /// format). User types implement `Display` in Lis instead.
    bool isShowBuiltin(const std::string &name);
    void emitShowCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args);

    /// Get-or-declare an external libc function. If `name` already exists in
    /// the module but with a DIFFERENT type, report an internal error (a user
    /// function with a libc name is rejected in sema, so this is defensive).
    llvm::Function *getOrDeclareLibcFunction(const std::string &name,
        llvm::FunctionType *fty);

    /// Emit a single alloca at the entry block for a given local.
    llvm::AllocaInst *emitEntryAlloca(llvm::Function *fn,
        llvm::Type *ty,
        const std::string &name);

    unsigned fieldIndexOf(const std::string &structName,
        const std::string &fieldName) const;

    std::shared_ptr<Type>
    getElementType(const std::shared_ptr<Type> &ty);

    std::shared_ptr<Type>
    fieldTypeOf(const std::string &structName,
        const std::string &fieldName);
};
