/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * RuntimeJit.cpp — in-process execution of a compiled Lis module (MCJIT).
 *
 * Why MCJIT and not ORC: on this MinGW + static-LLVM build, LLJITBuilder::create()
 * dies with an access violation before it can return an error (the ORC JITLink
 * COFF path). MCJIT uses RuntimeDyld and has been the working Windows JIT for
 * years; for "compile a module, call main, read the result" it needs no more.
 *
 * The module the compiler produced declares libc functions (printf, malloc, ...)
 * as external symbols. They are mapped EXPLICITLY to this executable's own
 * thunks, so an unlisted symbol fails loudly instead of resolving to garbage.
 * Keep the list in sync with the getOrDeclare* helpers in LLVMIRBuilder.
 */

#include "RuntimeJit.hpp"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define LIS_DUP _dup
#define LIS_DUP2 _dup2
#define LIS_CLOSE _close
#define LIS_GETPID _getpid
#else
#include <unistd.h>
#define LIS_DUP dup
#define LIS_DUP2 dup2
#define LIS_CLOSE close
#define LIS_GETPID getpid
#endif

namespace
{
bool Trace()
{
    static const bool On = std::getenv("LIS_JIT_TRACE") != nullptr;
    return On;
}

void TraceStep(const char *What)
{
    if (Trace())
    {
        std::fprintf(stderr, "[jit] %s\n", What);
        std::fflush(stderr);
    }
}

/// The input builtins reach stdin through UCRT's __acrt_iob_func. Mapping that
/// symbol to this shim keeps the stream IDENTITY consistent with the freopen()
/// below: the JIT'd fgets then reads the very stream this file redirected, no
/// matter which CRT's FILE* the "real" __acrt_iob_func would have handed out.
FILE *LisJitIobFunc(int Index)
{
    if (Trace())
    {
        std::fprintf(stderr, "[jit] iob_func(%d)\n", Index);
        std::fflush(stderr);
    }
    switch (Index)
    {
    case 0:
        return stdin;
    case 1:
        return stdout;
    default:
        return stderr;
    }
}

void CaptureStdout(int &Saved, std::string &Path)
{
    Path = "lis_jit_out_" + std::to_string(LIS_GETPID()) + ".txt";
    std::fflush(stdout);
    Saved = LIS_DUP(1);
    FILE *F = std::freopen(Path.c_str(), "w", stdout);
    (void)F;
}

std::string RestoreStdout(int Saved, const std::string &Path)
{
    // Flush EVERY stream, not just stdout. The program under test can reach the
    // redirected descriptor through any FILE object (the CRT's stdout, a stream
    // the input shim returned, ...), and anything still buffered in one of them
    // would be written AFTER the file below is read — which is exactly how a
    // sharded run once lost the trailing newline of one test's output
    // (MathFabsNegative saw "3.500000" instead of "3.500000\n").
    std::fflush(nullptr);
    if (Saved >= 0)
    {
        LIS_DUP2(Saved, 1);
        LIS_CLOSE(Saved);
    }
    std::string Out;
    if (FILE *F = std::fopen(Path.c_str(), "rb"))
    {
        char Buf[4096];
        size_t N = 0;
        while ((N = std::fread(Buf, 1, sizeof(Buf), F)) > 0)
            Out.append(Buf, N);
        std::fclose(F);
    }
    std::remove(Path.c_str());
    return Out;
}
} // namespace

int RunModuleInJit(std::unique_ptr<llvm::Module> Mod,
    std::unique_ptr<llvm::LLVMContext> Ctx,
    std::string *Out,
    const std::string *In)
{
    static std::once_flag InitOnce;
    std::call_once(InitOnce, []()
        {
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();
        });

    if (!Mod || !Ctx)
        return -1000;

    // How does main return? A Lis main is usually i32, but the language allows
    // i64 (and void). A real link truncates the value into the process exit
    // status; call it with the matching C type so the same value comes back.
    llvm::Function *Entry = Mod->getFunction("main");
    if (!Entry)
        return -1001;
    // NOTE: bind by NAME (addGlobalMapping(StringRef, uint64_t)). Going through
    // ExecutionEngine::FindFunctionNamed returned nullptr for every symbol,
    // which silently left MCJIT to its own process-symbol search — the symbols
    // still resolved, but stdin ended up as a different CRT's FILE*.
    const llvm::Type *RetTy = Entry->getReturnType();
    const unsigned RetBits = RetTy->isIntegerTy() ? RetTy->getIntegerBitWidth() : 0;

    TraceStep("creating MCJIT engine");
    std::string EngineError;
    llvm::EngineBuilder Builder(std::move(Mod));
    Builder.setEngineKind(llvm::EngineKind::JIT);
    Builder.setErrorStr(&EngineError);
    std::unique_ptr<llvm::ExecutionEngine> Engine(Builder.create());
    if (!Engine)
    {
        if (Trace())
            std::fprintf(stderr, "[jit] engine error: %s\n", EngineError.c_str());
        return -1002;
    }

    TraceStep("mapping libc symbols");
    struct SymbolBinding
    {
        const char *Name;
        void *Address;
    };
    const SymbolBinding LibcSymbols[] = {
        {"printf", reinterpret_cast<void *>(&printf)},
        {"fprintf", reinterpret_cast<void *>(&fprintf)},
        {"sprintf", reinterpret_cast<void *>(&sprintf)},
        {"fgets", reinterpret_cast<void *>(&fgets)},
        {"strcspn", reinterpret_cast<void *>(&strcspn)},
        {"atoi", reinterpret_cast<void *>(&atoi)},
        {"strtod", reinterpret_cast<void *>(&strtod)},
        {"malloc", reinterpret_cast<void *>(&malloc)},
        {"free", reinterpret_cast<void *>(&free)},
        {"memcpy", reinterpret_cast<void *>(&memcpy)},
        {"strlen", reinterpret_cast<void *>(&strlen)},
        {"abort", reinterpret_cast<void *>(&abort)},
#ifdef _WIN32
        {"__acrt_iob_func", reinterpret_cast<void *>(&LisJitIobFunc)},
#endif
    };
    for (const SymbolBinding &Binding : LibcSymbols)
    {
        if (Trace())
        {
            std::fprintf(stderr, "[jit] mapping %s\n", Binding.Name);
            std::fflush(stderr);
        }
        Engine->addGlobalMapping(Binding.Name,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Binding.Address)));
    }

    TraceStep("resolving main");
    const uint64_t MainAddress = Engine->getFunctionAddress("main");
    if (MainAddress == 0)
        return -1005;

    // stdin: the JIT'd program reads this process's own stdin.
    std::string InPath;
    bool RedirectedIn = false;
    if (In)
    {
        InPath = "lis_jit_in_" + std::to_string(LIS_GETPID()) + ".txt";
        FILE *Written = std::fopen(InPath.c_str(), "wb");
        if (Written)
        {
            std::fwrite(In->data(), 1, In->size(), Written);
            std::fclose(Written);
        }
        FILE *Reopened = std::freopen(InPath.c_str(), "rb", stdin);
        if (Reopened)
            std::clearerr(stdin); // drop any EOF flag cached by an earlier test
        RedirectedIn = (Reopened != nullptr);
        if (Trace())
        {
            std::fprintf(stderr, "[jit] stdin file %s written=%d reopened=%d\n",
                InPath.c_str(), Written != nullptr, RedirectedIn);
            std::fflush(stderr);
        }
    }

    int SavedOut = -1;
    std::string OutPath;
    if (Out)
        CaptureStdout(SavedOut, OutPath);

    TraceStep("calling main");
    int Code = 0;
    if (RetBits == 64)
        Code = static_cast<int>(reinterpret_cast<int64_t (*)()>(MainAddress)());
    else if (RetBits > 0)
        Code = reinterpret_cast<int (*)()>(MainAddress)();
    else
        reinterpret_cast<void (*)()>(MainAddress)();
    TraceStep("main returned");

    if (Out)
        *Out = RestoreStdout(SavedOut, OutPath);
    if (RedirectedIn)
    {
        std::fclose(stdin);
        std::remove(InPath.c_str());
    }

    Engine.reset();
    return Code;
}
