#pragma once

/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * Runtime regression tests: compile a `.lis` snippet through the FULL pipeline
 * (Lexer → Parser → HIR → sema → MIR → monomorphization → LLVM → Emitter), link
 * the object with the MinGW toolchain, run it, and assert the process exit code.
 *
 * This catches runtime semantic bugs that sema-only tests miss — e.g. the
 * reference-typed method-receiver bug (`let m = &mut c; m.add(5);` not mutating
 * c), which compiles cleanly but behaves wrongly at runtime.
 */

#include <gtest/gtest.h>

#include "RuntimeJit.hpp"

#include "llvm/IR/IRBuilder.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// NOTE: do NOT include <windows.h> here — it defines `ERROR`/`TRUE`/... as
// macros, which breaks `Logger::LogLevel::ERROR` in the compiler headers.
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#define dup _dup
#define dup2 _dup2
#define close _close
extern char *_pgmptr; // full path of the running executable (MinGW CRT)
#else
#include <unistd.h>
#endif

#include "Core/Context.hpp"
#include "IR/Emitter.hpp"
#include "IR/HIRBuilder.hpp"
#include "IR/HIRSemanticAnalyzer.hpp"
#include "IR/LLVMIRBuilder.hpp"
#include "IR/MIRBorrowCheck.hpp"
#include "IR/MIRBuilder.hpp"
#include "IR/MIRMonomorphization.hpp"
#include "Lexer/Lexer.hpp"
#include "Logger/Logger.hpp"
#include "Parser/Parser.hpp"

namespace fs = std::filesystem;

// Shared across every test TU: the per-process artifact counter must not be
// duplicated per translation unit (two TUs would hand out the same path).
inline std::atomic<int> gRtCounter{0};

namespace
{

/// Import prologue prepended to every RuntimeTest snippet. The stdlib is no
/// longer auto-preloaded — selective imports promote the public API's bare
/// names (the internal names stay `math$max` etc.).
static const char *kStdlibPrologue =
    "impt math { min, max, clamp, abs, fabs, gcd, lcm, ipow, is_even, is_odd, sign, deg_to_rad, rad_to_deg, lerp, Numeric, Integer, Add, Sub, Mul, Div, Rem, PartialEq, PartialOrd, BitAnd, BitOr, BitXor, Shl, Shr };\n"
    "impt option { Option, is_some, is_none, unwrap_or, and, or };\n"
    "impt iterator { Iterator, Range, range, sum, count, first, last, nth, product };\n"
    "impt string { String };\n"
    "impt chars { is_digit, is_alpha, is_alphanumeric, is_whitespace, digit_to_int };\n"
    "impt drop { Drop };\n"
    // `result` is imported WITHOUT `unwrap_or`: option.lis exports that name too,
    // and two selective imports of the same bare name are a deliberate conflict.
    // Tests that want Result::unwrap_or by bare name use kResultPrologue, or call
    // it qualified (`impt result;` + `result::unwrap_or`).
    "impt result { Result, is_ok, is_err };\n";

/// Math-only prologue for snippets that DEFINE their own `fn sum` (which would
/// clash with the iterator module's promoted `sum`).
/// Prologue for snippets that need Result WITHOUT option (so the bare name
/// `unwrap_or` resolves to the result module) — plus the Drop/string modules the
/// helper snippets use.
static const char *kResultPrologue =
    "impt result { Result, is_ok, is_err, unwrap_or };\n"
    "impt string { String };\n"
    "impt drop { Drop };\n";

static const char *kMathPrologue =
    "impt math { min, max, clamp, abs, fabs, gcd, lcm, ipow, is_even, is_odd, sign, deg_to_rad, rad_to_deg, lerp, Numeric, Integer, Add, Sub, Mul, Div, Rem, PartialEq, PartialOrd, BitAnd, BitOr, BitXor, Shl, Shr };\n";

/// Exit status of a child process that reached the builtin panic: libc
/// abort(). UCRT maps its __fastfail(FAST_FAIL_FATAL_APP_EXIT) to 0xC0000409;
/// on POSIX abort() raises SIGABRT (128 + 6). Asserted rather than merely
/// "non-zero" so a runtime/toolchain change is noticed instead of silently
/// accepted.
#ifdef _WIN32
static const int kPanicExitCode = (int)0xC0000409;
#else
static const int kPanicExitCode = 134;
#endif

/// Locate the preloaded stdlib (`Build/Binaries/lstdlib`). test.exe lives at
/// `Build/Intermediate/`, so it is the exe dir's parent + `Binaries/lstdlib`.
fs::path findStdlibDir()
{
#ifdef _WIN32
    if (_pgmptr)
    {
        fs::path cand = fs::path(_pgmptr).parent_path().parent_path() / "Binaries" / "lstdlib";
        if (fs::exists(cand)) return cand;
    }
#else
    {
        char buf[4096];
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0)
        {
            buf[n] = '\0';
            fs::path cand = fs::path(buf).parent_path().parent_path() / "Binaries" / "lstdlib";
            if (fs::exists(cand)) return cand;
        }
    }
#endif
    fs::path cwdCand = fs::current_path() / "Build" / "Binaries" / "lstdlib";
    if (fs::exists(cwdCand)) return cwdCand;
    return {};
}
/// This process's id. Test artifacts are named per (pid, test) so several test
/// binaries can run concurrently (sharded runs) without colliding.
int rtProcessId()
{
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}
} // namespace

class RuntimeTest : public ::testing::Test
{
protected:
    fs::path stdLibDir;
    fs::path objPath;
    fs::path exePath;
    fs::path diagPath;

    /// Context of the last successful compile(): it owns the LLVM module and its
    /// LLVM context, which the in-process execution path takes over.
    std::shared_ptr<Context> lastCompile_;

    void SetUp() override
    {
        stdLibDir = findStdlibDir();
        ASSERT_FALSE(stdLibDir.empty()) << "cannot locate Build/Binaries/lstdlib";
        int id = gRtCounter++;
        const std::string base = "lis_rt_" + std::to_string(rtProcessId()) + "_" + std::to_string(id);
        objPath = fs::temp_directory_path() / (base + ".o");
        exePath = fs::temp_directory_path() / (base + ".exe");
        diagPath = fs::temp_directory_path() / (base + "_diag.txt");
    }

    /// Extra CLI-style arguments applied to the Context built by the next
    /// compile() (e.g. {{"max_depth", "16"}}). Tests set it before calling a
    /// helper; it is not reset automatically (assign an empty vector to clear).
    std::vector<std::pair<std::string, std::string>> extraArgs;

    void TearDown() override
    {
        std::error_code ec;
        fs::remove(objPath, ec);
        fs::remove(exePath, ec);
        fs::remove(diagPath, ec);
    }

    /// Compile while capturing the Logger's diagnostics to `diagnostics`.
    bool compileCapture(const std::string &source, std::string &diagnostics)
    {
        fflush(stdout);
        int saved = dup(_fileno(stdout));
        FILE *f = freopen(diagPath.string().c_str(), "w", stdout);
        (void)f;
        bool ok = compile(source);
        fflush(stdout);
        dup2(saved, _fileno(stdout));
        close(saved);
        std::ifstream fi(diagPath);
        diagnostics.assign((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
        return ok;
    }

    /// The snippet must be REJECTED (semantic error, not a codegen crash) with a
    /// diagnostic containing `fragment`.
    void expectCompileFail(const std::string &source, const std::string &fragment)
    {
        std::string diag;
        bool ok = compileCapture(source, diag);
        EXPECT_FALSE(ok) << "expected a compile error, got clean compile for:\n"
                         << source;
        EXPECT_NE(diag.find(fragment), std::string::npos)
            << "expected message containing '" << fragment << "', got:\n"
            << diag;
    }

    /// Compile `source` through the full pipeline to objPath. Returns false on
    /// any semantic error (diagnostics are logged to stdout by the Logger).
    /// The stdlib is NOT auto-preloaded; a prologue imports its public API
    /// (selective imports promote the bare names the snippets use). Snippets
    /// that DEFINE their own stdlib-named types (e.g. their own `enum Option`)
    /// pass an empty or reduced prologue to avoid the (correct) import clash.
    bool compile(const std::string &source, const std::string &prologue = kStdlibPrologue)
    {
        auto context = std::make_shared<Context>();
        context->args->setArg("o", "2");
        context->args->setArg("filePath", "test.lis");
        // CLI-style overrides a test wants the compiler to see (e.g.
        // extraArgs = {{"max_depth", "16"}} to exercise --max-depth).
        for (const auto &[key, value] : extraArgs)
            context->args->setArg(key, value);
        context->searchPaths.push_back(stdLibDir.string());
        // The real lstdlib is also the unsafe-core boundary: the stdlib modules
        // imported below are the only files allowed to use the heap primitives.
        context->stdLibDirs.push_back(stdLibDir.string());

        // Main source (stdlib imports prepended).
        context->filePath = "test.lis";
        context->fileValue = prologue + source;
        Lexer lexer(context);
        lexer.run();
        Parser parser(context);
        parser.parseAll();
        // Parse errors (incl. lexer errors the Parser gate sees) are now
        // recoverable — report them as a failed compile instead of exit(1)
        // killing the whole test process.
        if (Logger::GetErrorCount() > 0) return false;

        HIRBuilder builder(context);
        builder.run();

        // Gate on semantic errors — do NOT call sema.run() (it calls exit(1)).
        Logger::ResetErrorCount();
        HIRSemanticAnalyzer sema(context);
        sema.visit(context->hirProgram.get());
        if (Logger::GetErrorCount() > 0) return false;

        MIRBuilder mir(context);
        // buildProgram() rather than run(): run() gates MIR-level diagnostics
        // (a `-> never` function that never diverges) with exit(1), which would
        // kill the whole test process. Mirror the sema handling above.
        context->mirProgram = std::make_unique<MIRProgram>(mir.buildProgram(context->hirProgram.get()));
        if (Logger::GetErrorCount() > 0) return false;
        // Borrow / move / definite-assignment checking on MIR: a no-op unless
        // LIS_BORROW_CHECK=mir selects it (then HIRSemanticAnalyzer above has
        // skipped its own implementations of the same rules).
        {
            MIRBorrowCheck borrowCheck(context);
            borrowCheck.run();
            if (Logger::GetErrorCount() > 0) return false;
        }
        MIRMonomorphization mono(context);
        mono.run();
        LLVMIRBuilder llvm(context, *context->llvmContext, "test.lis");
        llvm.run();

        Emitter::Options opts;
        opts.outPath = objPath.string();
        Emitter emitter(context, opts);
        emitter.run();
        // The module (and its context) stay available for in-process execution.
        lastCompile_ = context;
        return true;
    }

    /// Compile `source` with extra MODULE files. Each module entry is
    /// (module-path, content); files are written under a fresh temp dir that is
    /// prepended to Context::searchPaths, so `impt foo.bar;` finds foo/bar.lis.
    /// The stdlib is still preloaded into the root module (as compile() does).
    bool compileMulti(const std::string &source,
        const std::vector<std::pair<std::string, std::string>> &modules,
        std::string *diagnostics = nullptr)
    {
        auto context = std::make_shared<Context>();
        context->args->setArg("o", "2");
        context->args->setArg("filePath", "test.lis");

        // Write the module files and expose them via searchPaths.
        // Per (process, test): the counter alone is per-process, so two sharded
        // runners would otherwise share a directory name and delete each
        // other's module files mid-test (seen as "cannot find module 'baz.qux'").
        fs::path modDir = fs::temp_directory_path()
                          / ("lis_mods_" + std::to_string(rtProcessId()) + "_" + std::to_string(gRtCounter++));
        fs::create_directories(modDir);
        for (auto &[name, src] : modules)
        {
            // Dot-separated module path → filesystem path: "foo.bar" → foo/bar
            fs::path relPath;
            std::string seg;
            for (char c : name)
            {
                if (c == '.')
                {
                    relPath /= seg;
                    seg.clear();
                }
                else
                    seg += c;
            }
            relPath /= seg;
            fs::path p = modDir / relPath;
            p += ".lis";
            fs::create_directories(p.parent_path());
            std::ofstream f(p);
            f << src;
        }
        context->searchPaths.push_back(modDir.string());
        context->searchPaths.push_back(stdLibDir.string());
        context->stdLibDirs.push_back(stdLibDir.string());

        // Main source (stdlib imports prepended).
        context->filePath = "test.lis";
        context->fileValue = std::string(kStdlibPrologue) + source;
        Lexer lexer(context);
        lexer.run();
        Parser parser(context);
        parser.parseAll();
        if (Logger::GetErrorCount() > 0)
        {
            fs::remove_all(modDir);
            return false;
        }

        HIRBuilder builder(context);
        builder.run();

        // Gate on semantic errors — do NOT call sema.run() (it calls exit(1)).
        Logger::ResetErrorCount();
        HIRSemanticAnalyzer sema(context);
        sema.visit(context->hirProgram.get());
        if (Logger::GetErrorCount() > 0)
        {
            fs::remove_all(modDir);
            return false;
        }

        MIRBuilder mir(context);
        // buildProgram() rather than run(): run() gates MIR-level diagnostics
        // (a `-> never` function that never diverges) with exit(1), which would
        // kill the whole test process. Mirror the sema handling above.
        context->mirProgram = std::make_unique<MIRProgram>(mir.buildProgram(context->hirProgram.get()));
        if (Logger::GetErrorCount() > 0) return false;
        // Borrow / move / definite-assignment checking on MIR: a no-op unless
        // LIS_BORROW_CHECK=mir selects it (then HIRSemanticAnalyzer above has
        // skipped its own implementations of the same rules).
        {
            MIRBorrowCheck borrowCheck(context);
            borrowCheck.run();
            if (Logger::GetErrorCount() > 0) return false;
        }
        MIRMonomorphization mono(context);
        mono.run();
        LLVMIRBuilder llvm(context, *context->llvmContext, "test.lis");
        llvm.run();

        Emitter::Options opts;
        opts.outPath = objPath.string();
        Emitter emitter(context, opts);
        emitter.run();
        fs::remove_all(modDir);
        (void)diagnostics;
        lastCompile_ = context;
        return true;
    }

    /// Link objPath → exePath with the MinGW toolchain, then run it and return
    /// the process exit code (-1 if linking or launching failed). If `out` is
    /// non-null the child's stdout is captured into it; if `err` is non-null its
    /// stderr is captured (the builtin `panic` writes its message there); if
    /// `in` is non-null its bytes are fed to the child's stdin (via a pipe)
    /// before it runs.
    int linkAndRun(std::string *out = nullptr, const std::string *in = nullptr,
        std::string *err = nullptr)
    {
        std::string linkCmd = "g++ -o \"" + exePath.string() + "\" \"" + objPath.string() + "\"";
        if (std::system(linkCmd.c_str()) != 0)
            return -1;
#ifdef _WIN32
        // Redirect the child's stdout to a pipe so the caller can read it back.
        int saved = -1;
        int fds[2] = {-1, -1};
        if (out)
        {
            fflush(stdout);
            saved = dup(_fileno(stdout));
            if (_pipe(fds, 65536, _O_BINARY) == 0)
            {
                dup2(fds[1], _fileno(stdout));
                close(fds[1]); // child inherits the write end; we close ours
            }
        }

        // Same for stderr — SEPARATE pipe: draining one pipe to EOF blocks
        // until the child exits, so a shared pipe would lose whichever stream
        // was still unread. The builtin panic writes its message here.
        int savedErr = -1;
        int fdsErr[2] = {-1, -1};
        if (err)
        {
            fflush(stderr);
            savedErr = dup(_fileno(stderr));
            if (_pipe(fdsErr, 65536, _O_BINARY) == 0)
            {
                dup2(fdsErr[1], _fileno(stderr));
                close(fdsErr[1]);
            }
        }

        // Feed the child's stdin from a pipe. The input is written BEFORE the
        // spawn so it sits in the pipe buffer (test inputs are small); closing
        // the write end gives the child EOF after it reads all of it.
        int savedIn = -1;
        int fdsIn[2] = {-1, -1};
        if (in)
        {
            fflush(stdin);
            savedIn = dup(_fileno(stdin));
            if (_pipe(fdsIn, 65536, _O_BINARY) == 0)
            {
                dup2(fdsIn[0], _fileno(stdin));
                if (fdsIn[0] != _fileno(stdin)) close(fdsIn[0]);
                _write(fdsIn[1], in->data(), (unsigned)in->size());
                close(fdsIn[1]);
            }
        }

        int code = _spawnl(_P_WAIT, exePath.string().c_str(), exePath.string().c_str(), nullptr);
        if (out && saved != -1)
        {
            dup2(saved, _fileno(stdout));
            close(saved);
            char buf[4096];
            ssize_t n;
            while ((n = read(fds[0], buf, sizeof(buf))) > 0)
                out->append(buf, (size_t)n);
            close(fds[0]);
        }
        if (err && savedErr != -1)
        {
            dup2(savedErr, _fileno(stderr));
            close(savedErr);
            char buf[4096];
            ssize_t n;
            while ((n = read(fdsErr[0], buf, sizeof(buf))) > 0)
                err->append(buf, (size_t)n);
            close(fdsErr[0]);
        }
        if (in && savedIn != -1)
        {
            dup2(savedIn, _fileno(stdin));
            close(savedIn);
        }
        return code;
#else
        if (!err)
        {
            int st = std::system(exePath.string().c_str());
            return WEXITSTATUS(st);
        }
        // POSIX: no fd juggling needed for one stream — redirect stderr to a
        // sibling temp file and read it back. (A signal death reports status 0
        // here, exactly as it already does for stdout-only runs.)
        fs::path errPath = exePath;
        errPath += ".err";
        std::string cmd = "\"" + exePath.string() + "\" 2> \"" + errPath.string() + "\"";
        int st = std::system(cmd.c_str());
        {
            std::ifstream fe(errPath);
            err->assign((std::istreambuf_iterator<char>(fe)), std::istreambuf_iterator<char>());
        }
        {
            std::error_code ec;
            fs::remove(errPath, ec);
        }
        return WEXITSTATUS(st);
#endif
    }

    /// Run the module produced by the last successful compile() IN THIS PROCESS:
    /// no g++ link, no child process (that is ~350 ms per case). Takes the module
    /// and its LLVM context; only valid immediately after a compile().
    int jitRun(std::string *out = nullptr, const std::string *in = nullptr)
    {
        if (!lastCompile_ || !lastCompile_->module)
            return -2000;
        std::shared_ptr<Context> ctx = lastCompile_;
        lastCompile_.reset();
        return RunModuleInJit(std::move(ctx->module), std::move(ctx->llvmContext), out, in);
    }

    /// Compile, run IN-PROCESS, and assert the exit code.
    ///
    /// Use expectRunProc instead for any snippet that can reach abort() (panic,
    /// out-of-bounds array index): abort() cannot be caught, so an in-process
    /// abort would take the whole test runner down with it.
    void expectRun(const std::string &source, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n"
                                     << source;
        int code = jitRun();
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << source;
    }

    /// Compile, LINK, run as a child process, and assert the exit code. The slow
    /// path, kept for snippets that may abort() the process.
    void expectRunProc(const std::string &source, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n"
                                     << source;
        int code = linkAndRun();
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << source;
    }

    /// Like expectRun but with a custom (or empty) stdlib prologue — for
    /// snippets that define their own stdlib-named types (`enum Option`).
    void expectRunWithPrologue(const std::string &source, const std::string &prologue, int expectedExit)
    {
        ASSERT_TRUE(compile(source, prologue)) << "compilation failed:\n"
                                               << source;
        int code = jitRun();
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << source;
    }
    /// Compile, link, run, and assert the process reached the builtin panic:
    /// it must abort (kPanicExitCode) and its stderr must contain
    /// `expectedStderr`.
    ///
    /// stderr — not stdout — is the panic channel, and stdout must NOT be
    /// asserted around a panic: abort() does not flush stdio, so anything the
    /// program printed before diverging is simply lost.
    void expectPanic(const std::string &source, const std::string &expectedStderr)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n"
                                     << source;
        std::string err;
        int code = linkAndRun(nullptr, nullptr, &err);
        EXPECT_EQ(code, kPanicExitCode)
            << "expected the process to abort on panic (exit " << kPanicExitCode
            << "), got " << code << " for:\n"
            << source;
        EXPECT_NE(err.find(expectedStderr), std::string::npos)
            << "expected stderr containing \"" << expectedStderr << "\", got:\n"
            << err;
    }

    /// Compile, link, run, and assert both the exit code AND the captured stdout.
    void expectOutput(const std::string &source, const std::string &expectedOut, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n"
                                     << source;
        std::string out;
        int code = jitRun(&out);
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << source;
        // Windows printf emits CRLF; normalize to LF so the comparison is
        // platform-independent.
        std::string normalized;
        for (char c : out)
            if (c != '\r') normalized += c;
        EXPECT_EQ(normalized, expectedOut) << "stdout mismatch for:\n"
                                           << source;
    }

    /// Compile, link, run feeding `input` to stdin, and assert exit code +
    /// captured stdout (CRLF-normalized).
    void expectOutputWithInput(const std::string &source, const std::string &input, const std::string &expectedOut, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n"
                                     << source;
        std::string out;
        int code = jitRun(&out, &input);
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << source;
        std::string normalized;
        for (char c : out)
            if (c != '\r') normalized += c;
        EXPECT_EQ(normalized, expectedOut) << "stdout mismatch for:\n"
                                           << source;
    }

    /// Compile, link and run `Examples/<name>.lis` (located via the exe path),
    /// asserting the baseline exit code. Guards the canonical examples against
    /// silent rot.
    void expectExample(const std::string &name, int expectedExit)
    {
        fs::path root = stdLibDir.parent_path().parent_path().parent_path(); // .../Build/Binaries/lstdlib → repo root
        fs::path ex = root / "Examples" / (name + ".lis");
        std::ifstream f(ex);
        ASSERT_TRUE(f.good()) << "cannot open " << ex;
        std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // Examples carry their own `impt` statements (no auto-prologue — the
        // examples' legacy names like `max`/`Option`/`sum` would clash).
        ASSERT_TRUE(compile(src, "")) << "compilation failed:\n"
                                      << src;
        int code = linkAndRun();
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << src;
    }
};

// ── basic codegen ──────────────────────────────────────────────────────────────
