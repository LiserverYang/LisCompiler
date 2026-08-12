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
#include "IR/MIRBuilder.hpp"
#include "IR/MIRMonomorphization.hpp"
#include "Lexer/Lexer.hpp"
#include "Logger/Logger.hpp"
#include "Parser/Parser.hpp"

namespace fs = std::filesystem;

namespace
{
std::atomic<int> g_rtCounter{0};

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
} // namespace

class RuntimeTest : public ::testing::Test
{
protected:
    fs::path stdLibDir;
    fs::path objPath;
    fs::path exePath;
    fs::path diagPath;

    void SetUp() override
    {
        stdLibDir = findStdlibDir();
        ASSERT_FALSE(stdLibDir.empty()) << "cannot locate Build/Binaries/lstdlib";
        int id = g_rtCounter++;
        objPath = fs::temp_directory_path() / ("lis_rt_" + std::to_string(id) + ".o");
        exePath = fs::temp_directory_path() / ("lis_rt_" + std::to_string(id) + ".exe");
        diagPath = fs::temp_directory_path() / ("lis_rt_diag_" + std::to_string(id) + ".txt");
    }

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
    bool compile(const std::string &source)
    {
        auto context = std::make_shared<Context>();
        context->args->setArg("o", "2");
        context->args->setArg("filePath", "test.lis");

        // Preload the stdlib (Lexer + Parser only, mirroring CompilePipeline).
        for (auto &entry : fs::directory_iterator(stdLibDir))
        {
            if (entry.path().extension() != ".lis") continue;
            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            context->filePath = entry.path().string();
            context->fileValue = content;
            Lexer lexer(context);
            lexer.run();
            Parser parser(context);
            parser.parseAll();
        }

        // Main source.
        context->filePath = "test.lis";
        context->fileValue = source;
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
        mir.run();
        MIRMonomorphization mono(context);
        mono.run();
        LLVMIRBuilder llvm(context, context->llvmContext, "test.lis");
        llvm.run();

        Emitter::Options opts;
        opts.outPath = objPath.string();
        Emitter emitter(context, opts);
        emitter.run();
        return true;
    }

    /// Link objPath → exePath with the MinGW toolchain, then run it and return
    /// the process exit code (-1 if linking or launching failed). If `out` is
    /// non-null the child's stdout is captured into it; if `in` is non-null its
    /// bytes are fed to the child's stdin (via a pipe) before it runs.
    int linkAndRun(std::string *out = nullptr, const std::string *in = nullptr)
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
        if (in && savedIn != -1)
        {
            dup2(savedIn, _fileno(stdin));
            close(savedIn);
        }
        return code;
#else
        int st = std::system(exePath.string().c_str());
        return WEXITSTATUS(st);
#endif
    }

    /// Compile, link, run and assert the process exit code.
    void expectRun(const std::string &source, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n"
                                     << source;
        int code = linkAndRun();
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << source;
    }

    /// Compile, link, run, and assert both the exit code AND the captured stdout.
    void expectOutput(const std::string &source, const std::string &expectedOut, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n"
                                     << source;
        std::string out;
        int code = linkAndRun(&out);
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
        int code = linkAndRun(&out, &input);
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n"
                                      << source;
        std::string normalized;
        for (char c : out)
            if (c != '\r') normalized += c;
        EXPECT_EQ(normalized, expectedOut) << "stdout mismatch for:\n"
                                           << source;
    }
};

// ── basic codegen ──────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, Arithmetic)
{
    expectRun("fn main() -> i32 { ret 1 + 2 * 3; }", 7);
}

// P8: the HIR printer's tree walk must include value-match arm tail expressions.
// Before the fix getHIRChildren(HIRMatch) only walked arm.body, so
// `Some(v) => v + 1` arms were invisible in --print-hir.
TEST_F(RuntimeTest, HIRPrinterShowsMatchTailValue)
{
    auto ctx = std::make_shared<Context>();
    ctx->filePath = "test.lis";
    ctx->fileValue =
        "enum Option<T> { Some(T), None } fn main() -> i32 {"
        " let o = Option::Some(1); let y = match o { Some(v) => v + 1, None => 0 }; ret y; }";
    Lexer lexer(ctx);
    lexer.run();
    Parser parser(ctx);
    parser.parseAll();
    HIRBuilder builder(ctx);
    builder.run();

    // printHIR writes to std::cout (C++ iostream), which does NOT follow the
    // C-stdio freopen/dup2 dance that compileCapture relies on for the
    // printf-based logger — the dump came back empty on MinGW. Swap std::cout's
    // buffer into a local ostringstream instead (platform-independent).
    std::ostringstream oss;
    auto *oldBuf = std::cout.rdbuf(oss.rdbuf());
    printHIR((HIRNode *)ctx->hirProgram.get());
    std::cout.rdbuf(oldBuf);
    std::string out = oss.str();
    // The value arm `Some(v) => v + 1` must be walked and printed as a binary op.
    // The HIRPrinter labels the node `binary_op` (lowercase, see opKindToString),
    // so the assertion checks that exact label.
    EXPECT_NE(out.find("binary_op"), std::string::npos)
        << "value-match arm tail expression missing from HIR dump:\n"
        << out;
}

// P1 regression: an integer literal too large for int64 used as a VALUE used to
// crash HIRBuilder's unguarded std::stoll (std::terminate). It must now be
// rejected with a clean diagnostic instead.
TEST_F(RuntimeTest, ValueIntegerLiteralOverflowRejected)
{
    expectCompileFail("fn main() -> i32 { let a = 99999999999999999999; ret a as i32; }",
        "overflows");
}

// ── P4 regression: cast narrowing must use explicit bit widths ─────────────────
// The old check compared PrimKind enum ordinals, silently depending on the enum
// being declared in width order. These pin the widths explicitly.

TEST_F(RuntimeTest, CastI64ToI32NarrowingRejected)
{
    expectCompileFail("fn f(x: i64) -> i32 { ret x as i32; } fn main() -> i32 { ret f(1); }",
        "smaller integer type");
}

TEST_F(RuntimeTest, CastI32ToI8NarrowingRejected)
{
    expectCompileFail("fn f(x: i32) -> i8 { ret x as i8; } fn main() -> i32 { ret f(1) as i32; }",
        "smaller integer type");
}

TEST_F(RuntimeTest, CastI16ToI8NarrowingRejected)
{
    expectCompileFail("fn f(x: i16) -> i8 { ret x as i8; } fn main() -> i32 { ret f(1) as i32; }",
        "smaller integer type");
}

TEST_F(RuntimeTest, CastI32ToI64WideningAllowed)
{
    // i32 → i64 is a widening cast — must compile and run.
    expectRun("fn main() -> i64 { ret 5 as i64; }", 5);
}

TEST_F(RuntimeTest, CastI8ToI64WideningAllowed)
{
    // i8 → i64 is a widening cast. Literals are i32, so build the i8 input via
    // char → i8 (the language's byte path, as string.lis does). NOTE: `'\\0'`
    // in the C++ string is the two chars backslash-zero (a NUL char literal in
    // .lis) — a bare `'\0'` would embed an actual NUL byte into the source.
    expectRun("fn f(x: i8) -> i64 { ret x as i64; }"
              " fn main() -> i64 { let a = 'A' as i8; let b = '\\0' as i8; ret f(a) + f(b); }",
        65);
}

TEST_F(RuntimeTest, CastI16ToI32WideningAllowed)
{
    // i16 → i32 widening. Build the i16 input via char → i16 (char may be cast
    // to any integer width).
    expectRun("fn f(x: i16) -> i32 { ret x as i32; }"
              " fn main() -> i32 { let a = 'A' as i16; let b = '\\0' as i16; ret f(a) + f(b); }",
        65);
}

// ── P5 regression: i64 → char must not silently truncate ──────────────────────
// The old cast check `if (target == CHAR) break;` let ANY integer cast to char
// through; i64 → char truncated to char's runtime i32 width silently. Now only
// widths ≤ 32 bits may cast to char.

TEST_F(RuntimeTest, CastI64ToCharRejected)
{
    expectCompileFail("fn f(x: i64) -> char { ret x as char; } fn main() -> i32 { ret 0; }",
        "smaller integer type");
}

TEST_F(RuntimeTest, CastI32ToCharAllowed)
{
    // 65 as char → 'A' (char is i32 at runtime; identity cast).
    expectRun("fn main() -> i32 { let c = 65 as char; ret c as i32; }", 65);
}

TEST_F(RuntimeTest, CastI8ToCharAllowed)
{
    // Byte read back as char (the documented s.data[i] as char pattern).
    expectRun("fn main() -> i32 { let x = 'x' as i8; let c = x as char; ret c as i32; }", 120);
}

// NOTE: malformed-exponent literals (`1e`, `1e+`) are NOT testable here — the
// lexer error trips the Parser gate which exit(1)s the whole test process. They
// are covered by LexerTest.MalformedFloatExponent* instead.

TEST_F(RuntimeTest, IndirectCall)
{
    // Function pointers (Step 6): `let fp = dbl; fp(21)`.
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn main() -> i32 { let fp = dbl; ret fp(21); }", 42);
}

// ── reference-typed method receivers (regression) ─────────────────────────────

TEST_F(RuntimeTest, MutRefReceiverMutatesThrough)
{
    // `let m = &mut c; m.add(4)` must mutate c (receiver passed by value).
    expectRun("struct counter { pub value: i32 } impl counter {"
              " fn new(v: i32) -> counter { ret counter { value: v }; }"
              " fn add(self: &mut counter, d: i32) { self.value = self.value + d; }"
              " fn get(self: &counter) -> i32 { ret self.value; } }"
              " fn main() -> i32 { let mut c = counter::new(1); let m = &mut c;"
              " m.add(4); let g = m.get(); ret c.value + g; }",
        10);
}

TEST_F(RuntimeTest, SharedRefReceiverReadsThrough)
{
    expectRun("struct counter { pub value: i32 } impl counter {"
              " fn new(v: i32) -> counter { ret counter { value: v }; }"
              " fn get(self: &counter) -> i32 { ret self.value; } }"
              " fn main() -> i32 { let c = counter::new(7); let r = &c; ret r.get(); }",
        7);
}

// P2 regression (runtime): a trait mixing `&self` and `&mut self` methods used
// to share one SelfType (createSelf keyed by name), dropping the `&mut self`
// receiver's mutability — valid impls were rejected, and this call pattern is
// the same one that would then behave wrongly. It must compile and run.
TEST_F(RuntimeTest, TraitMixedReceiverKindsRuntime)
{
    expectRun("trait Mixed { fn read(self: &Self) -> i32; fn write(self: &mut Self, v: i32); }"
              " struct S { v: i32 } impl Mixed for S {"
              " fn read(self: &S) -> i32 { ret self.v; }"
              " fn write(self: &mut S, v: i32) { self.v = v; } }"
              " fn main() -> i32 { let mut s = S { v: 1 }; s.write(7); ret s.read(); }",
        7);
}

// ── ownership / drop glue at runtime ──────────────────────────────────────────

TEST_F(RuntimeTest, DropGlueRunsAtBlockEnd)
{
    // A block-end drop must invoke the user Drop impl once (observable side effect).
    expectRun("let counter = 0; struct X { v: i32 } impl Drop for X { fn drop(self) { counter = counter + 1; } }"
              " fn main() -> i32 { { let x = X { v: 1 }; } ret counter; }",
        1);
}

TEST_F(RuntimeTest, PartialMoveDropNoDoubleFree)
{
    // Borrow field a, move sibling b, scope-end drop must not double-free.
    expectRun("struct Inner { v: i32 } impl Drop for Inner { fn drop(self) {} }"
              " struct Pair { a: Inner, b: Inner }"
              " fn main() -> i32 { let mut p = Pair { a: Inner{v:1}, b: Inner{v:2} };"
              " let r = &p.a; let v = p.b; let w = r.v; ret w; }",
        1);
}

// ── iterators / for-loops ──────────────────────────────────────────────────────

TEST_F(RuntimeTest, ForLoopOverCustomIterator)
{
    expectRun("struct Countdown { pub start: i32, pub current: i32 }"
              " impl Countdown { fn new(n: i32) -> Countdown { ret Countdown { start: n, current: n }; } }"
              " impl Iterator<i32> for Countdown {"
              "   fn next(self: &mut Self) -> Option<i32> {"
              "     if self.current > 0 { let v = self.current; self.current = self.current - 1;"
              "                          ret Option::Some(v); }"
              "     ret Option::None; } }"
              " fn main() -> i32 { let mut total = 0; for x in Countdown::new(3) { total = total + x; } ret total; }",
        6);
}

// ── generics tech-debt (0c) ────────────────────────────────────────────────────
// These two shapes were broken (or worked only by a cache accident) before the
// fix: a static method of a generic struct whose symbol carries the struct's
// generic param.

TEST_F(RuntimeTest, GenericStructStaticMethodNoOwnGenerics)
{
    // `fn new(_v: T)` uses the STRUCT's T without redeclaring it.
    expectRun("struct box<T> { pub v: T } impl box { fn new(_v: T) { ret box { v: _v }; } }"
              " fn main() -> i32 { let b = box::new(10); ret b.v; }",
        10);
}

TEST_F(RuntimeTest, GenericStructStaticMethodOwnGenerics)
{
    expectRun("struct box<T> { pub v: T } impl box { fn new<T>(_v: T) { ret box { v: _v }; } }"
              " fn main() -> i32 { let b = box::new(10); ret b.v; }",
        10);
}

// P7: a generic struct with a trait constraint runs buildStructType twice
// (pass-1b pre-registration + pass-2 full analysis) on the SAME generic param;
// updateContraints must stay idempotent (no duplicate implTrait entries).
TEST_F(RuntimeTest, GenericStructWithTraitConstraint)
{
    expectRun("struct box<T: Numeric> { pub v: T } impl box { fn make(_v: T) -> box { ret box { v: _v }; } }"
              " fn main() -> i32 { let b = box::make(10); ret b.v; }",
        10);
}

// ── match / enum runtime ──────────────────────────────────────────────────────

TEST_F(RuntimeTest, EnumMatchUnitVariant)
{
    expectRun("enum color { red, green, blue } fn main() -> i32 {"
              " let c = color::green;"
              " match c { red => { ret 1; }, green => { ret 2; }, blue => { ret 3; }, }"
              " }",
        2);
}

TEST_F(RuntimeTest, EnumMatchPayloadBinding)
{
    expectRun("enum Option<T> { Some(T), None } fn main() -> i32 {"
              " let o = Option::Some(7);"
              " match o { Some(v) => { ret v; }, None => { ret 0; }, }"
              " }",
        7);
}

TEST_F(RuntimeTest, EnumMatchWildcard)
{
    expectRun("enum Option<T> { Some(T), None } fn main() -> i32 {"
              " let o = Option::Some(3);"
              " match o { Some(v) => { ret v; }, _ => { ret 99; }, }"
              " }",
        3);
}

TEST_F(RuntimeTest, EnumMatchUnitVariantWildcard)
{
    expectRun("enum flag { on, off } fn main() -> i32 {"
              " let o = flag::off;"
              " match o { on => { ret 1; }, _ => { ret 0; }, }"
              " }",
        0);
}

TEST_F(RuntimeTest, EnumVariantIsMoved)
{
    // After matching, the original enum binding should be "moved".
    expectRun("enum Option<T> { Some(T), None } fn main() -> i32 {"
              " let o = Option::Some(5);"
              " match o { Some(v) => { ret v; }, None => { ret 0; }, }"
              " }",
        5);
}

TEST_F(RuntimeTest, EnumMatchNonCopyPayload)
{
    // A non-Copy payload is MOVED into the binding; reading it works.
    expectRun("enum Option<T> { Some(T), None } struct Inner { v: i32 }"
              " fn main() -> i32 { let o = Option::Some(Inner{v: 5}); let mut got = 0;"
              " match o { Some(x) => { got = x.v; }, None => { got = 99; }, }"
              " ret got; }",
        5);
}

TEST_F(RuntimeTest, EnumMatchNonCopyNoDoubleFree)
{
    // The moved payload is dropped exactly once (drop glue counter).
    expectRun("enum Option<T> { Some(T), None } struct Inner { v: i32 }"
              " let ctr = 0; impl Drop for Inner { fn drop(self) { ctr = ctr + 1; } }"
              " fn main() -> i32 { { let o = Option::Some(Inner{v: 5});"
              " match o { Some(x) => { }, None => { }, } }"
              " ret ctr; }",
        1);
}

TEST_F(RuntimeTest, EnumMatchExpression)
{
    // `let y = match ...` — a value match with tail expressions.
    expectRun("enum Option<T> { Some(T), None } fn main() -> i32 {"
              " let o = Option::Some(7);"
              " let y = match o { Some(v) => v + 1, None => 0 };"
              " ret y; }",
        8);
}

// ── print builtins ─────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, PrintInt)
{
    expectOutput("fn main() -> i32 { print_int(42); println(); ret 0; }", "42\n", 0);
}

TEST_F(RuntimeTest, PrintStringAndChar)
{
    expectOutput("fn main() -> i32 { print_str(\"hi\"); print_char('!'); println(); ret 0; }",
        "hi!\n",
        0);
}

TEST_F(RuntimeTest, PrintFloatAndBool)
{
    expectOutput("fn main() -> i32 { print_float(3.5); println(); print_bool(true); println(); ret 0; }",
        "3.500000\n1\n",
        0);
}

TEST_F(RuntimeTest, PrintMultiple)
{
    expectOutput("fn main() -> i32 { print_str(\"x=\"); print_int(7); println(); ret 0; }",
        "x=7\n",
        0);
}

// ── math helpers ───────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, MathMinMax)
{
    expectRun("fn main() -> i32 { ret min(3, 7) + max(3, 7); }", 10);
}

TEST_F(RuntimeTest, MathAbsClamp)
{
    expectRun("fn main() -> i32 { ret abs(0 - 5) + clamp(12, 0, 10); }", 15);
}

TEST_F(RuntimeTest, MathFabs)
{
    expectRun("fn main() -> i32 { if fabs(0.0 - 2.5) > 2.0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathMinMaxFloat)
{
    expectRun("fn main() -> i32 { if max(3.5, 4.5) == 4.5 { ret 1; } ret 0; }", 1);
}

// ── operator type-check / Numeric constraint (soundness) ───────────────────────

TEST_F(RuntimeTest, GenericMaxOnStructRejected)
{
    // `max<T: Numeric>` with a struct arg must be rejected at the call site,
    // NOT crash LLVM on an invalid ICmp.
    expectCompileFail("struct box { pub a: i32, pub b: i32 }"
                      " fn main() -> i32 { max(box{a:1,b:2}, box{a:3,b:4}); ret 0; }",
        "Numeric");
}

TEST_F(RuntimeTest, StructComparisonRejected)
{
    expectCompileFail("struct box { pub a: i32 } fn main() -> i32 {"
                      " let x = box{a:1}; let y = box{a:2}; ret (x < y) as i32; }",
        "cannot be applied");
}

TEST_F(RuntimeTest, UnconstrainedGenericOpRejected)
{
    // A generic function using `<` on T must declare `T: Numeric`.
    expectCompileFail("fn m<T>(a: T, b: T) -> T { if a < b { ret a; } ret b; }"
                      " fn main() -> i32 { ret m(1, 2); }",
        "Numeric");
}

TEST_F(RuntimeTest, StructLogicalOpRejected)
{
    expectCompileFail("struct box { pub a: i32 } fn main() -> i32 {"
                      " let x = box{a:1}; if x && x { ret 1; } ret 0; }",
        "bool");
}

TEST_F(RuntimeTest, ImplNumericOnStructRejected)
{
    expectCompileFail("struct box { pub a: i32 } impl Numeric for box {}"
                      " fn main() -> i32 { ret 0; }",
        "operator overloading");
}

// ── operator overloading ───────────────────────────────────────────────────────

TEST_F(RuntimeTest, OperatorOverloadAdd)
{
    // `impl Add for Vec2` + `v1 + v2` lowers to `v1.add(v2)`.
    expectRun("struct Vec2 { pub x: i32, pub y: i32 }"
              " impl Add for Vec2 { fn add(self, other: Self) -> Vec2 {"
              "   ret Vec2 { x: self.x + other.x, y: self.y + other.y }; } }"
              " fn main() -> i32 { let a = Vec2{x:1,y:2}; let b = Vec2{x:3,y:4};"
              " let c = a + b; ret c.x + c.y; }",
        10);
}

TEST_F(RuntimeTest, OperatorOverloadComparison)
{
    // `impl PartialOrd for Vec2` + `v1 < v2` → `v1.lt(v2)` returns bool.
    expectRun("struct Vec2 { pub x: i32, pub y: i32 }"
              " impl PartialOrd for Vec2 { fn lt(self, other: Self) -> bool {"
              "   ret (self.x + self.y) < (other.x + other.y); }"
              "   fn gt(self, other: Self) -> bool {"
              "   ret (self.x + self.y) > (other.x + other.y); }"
              "   fn le(self, other: Self) -> bool {"
              "   ret (self.x + self.y) <= (other.x + other.y); }"
              "   fn ge(self, other: Self) -> bool {"
              "   ret (self.x + self.y) >= (other.x + other.y); } }"
              " fn main() -> i32 { let a = Vec2{x:1,y:1}; let b = Vec2{x:5,y:5};"
              " if a < b { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, GenericOperatorFunctionStruct)
{
    // A generic `fn sum<T: Add>` monomorphized over a struct implementing Add.
    expectRun("struct Vec2 { pub x: i32, pub y: i32 }"
              " impl Add for Vec2 { fn add(self, other: Self) -> Vec2 {"
              "   ret Vec2 { x: self.x + other.x, y: self.y + other.y }; } }"
              " fn sum<T: Add>(a: T, b: T) -> T { ret a + b; }"
              " fn main() -> i32 { let v = sum(Vec2{x:1,y:2}, Vec2{x:3,y:4});"
              " ret v.x + v.y; }",
        10);
}

TEST_F(RuntimeTest, GenericOperatorFunctionPrimitive)
{
    // The same generic `sum<T: Add>` monomorphized over i32 — the generic body's
    // `+` falls back to a direct binary op (primitives have no `add` method).
    expectRun("fn sum<T: Add>(a: T, b: T) -> T { ret a + b; }"
              " fn main() -> i32 { ret sum(2, 3) + sum(4, 5); }",
        14);
}

TEST_F(RuntimeTest, OperatorOverloadChained)
{
    // `a + b + c` → `(a.add(b)).add(c)`.
    expectRun("struct Vec2 { pub x: i32, pub y: i32 }"
              " impl Add for Vec2 { fn add(self, other: Self) -> Vec2 {"
              "   ret Vec2 { x: self.x + other.x, y: self.y + other.y }; } }"
              " fn main() -> i32 { let a = Vec2{x:1,y:0}; let b = Vec2{x:2,y:0};"
              " let c = Vec2{x:3,y:0}; let d = a + b + c; ret d.x; }",
        6);
}

TEST_F(RuntimeTest, StructWithoutOpTraitRejected)
{
    // A struct NOT implementing `Add` still rejects `+` with a clean error.
    expectCompileFail("struct box { pub a: i32 } fn main() -> i32 {"
                      " let x = box{a:1}; let y = box{a:2}; let z = x + y; ret z.a; }",
        "Add");
}

// ── stdlib breadth (option / math / char / iterator helpers) ───────────────────

TEST_F(RuntimeTest, OptionHelpers)
{
    // A bare `Option::None` can't infer T in a generic-arg position, so the
    // test builds a concrete none via a local helper (return-position inference).
    expectRun("fn mk_none() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 {"
              " let a = Option::Some(7);"
              " let x = unwrap_or(a, 0); let y = unwrap_or(mk_none(), 0);"
              " ret x + y; }",
        7);
}

TEST_F(RuntimeTest, OptionIsSomeAndOr)
{
    expectRun("fn mk_none() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 {"
              " let s = is_some(Option::Some(1));"
              " let n = is_none(mk_none());"
              " let a = unwrap_or(and(Option::Some(1), Option::Some(5)), 0);"
              " let o = unwrap_or(or(mk_none(), Option::Some(9)), 0);"
              " let mut flag = 0;"
              " if s && n { flag = 1; }"
              " ret a + o + flag; }",
        15);
}

TEST_F(RuntimeTest, MathGcdLcmIpow)
{
    expectRun("fn main() -> i32 { ret gcd(12, 18) + lcm(4, 6) + ipow(2, 10); }",
        6 + 12 + 1024);
}

TEST_F(RuntimeTest, MathSignEven)
{
    expectRun("fn main() -> i32 {"
              " let s = sign(0 - 5); let e = is_even(10); let o = is_odd(7);"
              " let mut total = s;"
              " if e { total = total + 1; }"
              " if o { total = total + 1; }"
              " ret total; }",
        1); // -1 + 1 + 1
}

TEST_F(RuntimeTest, MathDegLerp)
{
    expectRun("fn main() -> i32 {"
              " if lerp(0.0, 10.0, 0.5) == 5.0 { ret 1; }"
              " if deg_to_rad(180.0) > 3.14 { ret 1; }"
              " ret 0; }",
        1);
}

TEST_F(RuntimeTest, CharClassification)
{
    expectRun("fn main() -> i32 {"
              " let a = is_digit('5'); let b = is_alpha('g');"
              " let c = is_alphanumeric('Z'); let d = is_whitespace(' ');"
              " let e = digit_to_int('7');"
              " let mut flag = 0;"
              " if a && b && c && d { flag = 1; }"
              " ret e + flag; }",
        8);
}

TEST_F(RuntimeTest, IteratorLastNthProduct)
{
    expectRun("fn main() -> i32 {"
              " let last_ = unwrap_or(last(range(1, 5)), 0);"    // 4
              " let nth_ = unwrap_or(nth(range(10, 20), 3), 0);" // 13
              " let prod = product(range(1, 5));"                // 24
              " ret last_ + nth_ + prod; }",
        4 + 13 + 24);
}

// ── input builtins (read_line / read_int / read_f64) ─────────────────────────

TEST_F(RuntimeTest, ReadInt)
{
    expectOutputWithInput(
        "fn main() -> i32 { let n = read_int(); print_int(n); println(); ret 0; }",
        "42\n",
        "42\n",
        0);
}

TEST_F(RuntimeTest, ReadLine)
{
    // read_line strips the trailing newline.
    expectOutputWithInput(
        "fn main() -> i32 { let line = read_line(); print_str(line); println(); ret 0; }",
        "hello\n",
        "hello\n",
        0);
}

TEST_F(RuntimeTest, ReadF64)
{
    expectOutputWithInput(
        "fn main() -> i32 { let x = read_f64(); print_float(x); println(); ret 0; }",
        "3.5\n",
        "3.500000\n",
        0);
}

TEST_F(RuntimeTest, ReadIntThenLine)
{
    // read_int consumes one line; the next read_line consumes the next.
    expectOutputWithInput(
        "fn main() -> i32 { let n = read_int(); let line = read_line();"
        " print_int(n); print_char(','); print_str(line); println(); ret 0; }",
        "42\nhello\n",
        "42,hello\n",
        0);
}

// ── arrays / heap / String (Step 22) ─────────────────────────────────────────

TEST_F(RuntimeTest, ArrayLiteralAndIndex)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3, 4]; ret a[0] + a[3]; }", 5);
}

TEST_F(RuntimeTest, ArrayElementWrite)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; a[1] = 9; ret a[1]; }", 9);
}

TEST_F(RuntimeTest, ArrayMovedNotCopied)
{
    // Arrays are Move: `let b = a` invalidates a (single ownership).
    expectCompileFail("fn main() -> i32 { let a = [1, 2]; let b = a; ret a[0]; }",
        "moved");
}

TEST_F(RuntimeTest, ArrayOfCharsAndLoop)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3, 4]; let mut s = 0;"
              " let mut i = 0; while i < 4 { s = s + a[i]; i = i + 1; }"
              " a[3] = 40; ret s + a[3]; }",
        50);
}

TEST_F(RuntimeTest, ArrayNonCopyElementRejected)
{
    expectCompileFail("struct S { v: i32 } fn main() -> i32 { let a = [S{v:1}]; ret 0; }",
        "must be Copy");
}

TEST_F(RuntimeTest, StringFromLitAndPrint)
{
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hi\");"
                 " print_str(s.to_cstr()); println(); ret 0; }",
        "hi\n",
        0);
}

TEST_F(RuntimeTest, StringPushAndGrow)
{
    // from_lit("hello") + push_char + push_str — exercises the buffer grow path.
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hello\"); let mut t = s;"
                 " t.push_char(' '); t.push_str(\"world\");"
                 " print_str(t.to_cstr()); println(); ret t.len; }",
        "hello world\n",
        11);
}

TEST_F(RuntimeTest, StringIndexOption)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"hi\");"
              " let c = unwrap_or(s.index(0), '?');"
              " let d = unwrap_or(s.index(9), '?');" // out of bounds → None → '?'
              " ret (c as i32) - (d as i32); }",
        104 - 63);
}

TEST_F(RuntimeTest, StringIsEmpty)
{
    // is_empty() flips from true (String::new) to false after a push_char.
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " let mut e = 0;"
              " if s.is_empty() { e = e + 1; }"
              " s.push_char('a');"
              " if s.is_empty() { ret e; } else { ret e + 1; }"
              " }",
        2);
}

TEST_F(RuntimeTest, StringFreedExactlyOnce)
{
    // A heap-owning struct with a Drop counter: moving it transfers ownership,
    // so the scope-end drop (which frees the buffer) runs exactly once — never
    // twice. This is the same single-ownership machinery String uses.
    expectRun("let frees = 0;"
              " struct Buf { pub data: &mut i8, pub len: i32 }"
              " impl Drop for Buf { fn drop(self) { frees = frees + 1; __free(self.data); } }"
              " fn main() -> i32 {"
              "   let p = __alloc(4);"
              "   { let b = Buf { data: p, len: 0 }; let c = b; }" // b moved into c, c drops → frees 1
              "   ret frees; }",
        1);
}

TEST_F(RuntimeTest, StringMoveTransfersOwnership)
{
    // After `let t = s;`, s is unusable (single ownership).
    expectCompileFail("fn main() -> i32 { let s = String::from_lit(\"a\");"
                      " let t = s; let x = s.len; ret x; }",
        "moved");
}

TEST_F(RuntimeTest, ToStringBuiltins)
{
    expectOutput("fn main() -> i32 { let a = to_string_i32(42); print_str(a.to_cstr()); println();"
                 " let b = to_string_f64(3.5); print_str(b.to_cstr()); println();"
                 " let c = to_string_bool(true); print_str(c.to_cstr()); println(); ret 0; }",
        "42\n3.500000\n1\n",
        0);
}

TEST_F(RuntimeTest, HeapAllocFree)
{
    expectRun("fn main() -> i32 { let p = __alloc(8); p[0] = 'x' as i8;"
              " let c = p[0] as char; __free(p); ret c as i32; }",
        120);
}

// ── Step 23 robustness fixes ──────────────────────────────────────────────────

// B1: array-size literal that doesn't fit int64 → clean sema error, not a
// crash (the parser clamps to a sentinel; sema reports "exceeds the limit").
TEST_F(RuntimeTest, ArraySizeLiteralTooLargeRejected)
{
    expectCompileFail("fn main() -> i32 { let a: [i32; 99999999999999999999] = [1]; ret a[0]; }",
        "exceeds the limit");
}

// B2: array size above the element cap → clean error, not an LLVM assert.
TEST_F(RuntimeTest, ArraySizeOverLimitRejected)
{
    expectCompileFail("fn main() -> i32 { let a: [i32; 5000000000] = [1]; ret a[0]; }",
        "exceeds the limit");
}

// B4: empty array literal has no element type to infer.
TEST_F(RuntimeTest, EmptyArrayLiteralRejected)
{
    expectCompileFail("fn main() -> i32 { let a = []; ret 0; }",
        "empty array literal");
}

// B4: explicit `[T; 0]` is not a legal type.
TEST_F(RuntimeTest, ZeroSizeArrayTypeRejected)
{
    expectCompileFail("fn main() -> i32 { let a: [i32; 0] = [1]; ret 0; }",
        "array size must be a positive integer");
}

// B3: arrays are not first-class — reject as function params / returns before
// the LLVM backend asserts on a non-first-class ArrayType.
TEST_F(RuntimeTest, ArrayAsFunctionParamRejected)
{
    expectCompileFail("fn f(a: [i32; 2]) -> i32 { ret a[0]; } fn main() -> i32 { ret 0; }",
        "array type cannot be a function parameter");
}

TEST_F(RuntimeTest, ArrayAsFunctionReturnRejected)
{
    expectCompileFail("fn f() -> [i32; 2] { ret [1, 2]; } fn main() -> i32 { ret 0; }",
        "array type cannot be a function return");
}

// B5: indexing a reference to a struct is illegal (codegen has no element type).
TEST_F(RuntimeTest, IndexRefToStructRejected)
{
    expectCompileFail("struct Foo { v: i32 } fn main() -> i32 { let mut f = Foo{v:1};"
                      " let r = &mut f; ret r[0].v; }",
        "cannot index a reference to type");
}

// C1: global initializers must be literals — else they'd silently zero-initialize.
TEST_F(RuntimeTest, GlobalArrayInitializerRejected)
{
    expectCompileFail("let g = [1, 2]; fn main() -> i32 { ret 0; }",
        "global variable initializer must be a literal");
}

TEST_F(RuntimeTest, GlobalCallInitializerRejected)
{
    expectCompileFail("let s = String::new(); fn main() -> i32 { ret 0; }",
        "global variable initializer must be a literal");
}

// E4: reference elements would escape origin tracking — reject the array.
TEST_F(RuntimeTest, ArrayOfReferencesRejected)
{
    expectCompileFail("fn main() -> i32 { let mut x = 1; let a = [&mut x]; ret 0; }",
        "cannot be a reference");
}

// E5: builtin / libc names are reserved.
TEST_F(RuntimeTest, ReservedBuiltinNameRejected)
{
    expectCompileFail("fn __alloc(n: i32) -> i32 { ret 0; } fn main() -> i32 { ret 0; }",
        "reserved by the compiler");
}

TEST_F(RuntimeTest, ReservedLibcNameRejected)
{
    expectCompileFail("fn strlen(s: &i8) -> i32 { ret 0; } fn main() -> i32 { ret 0; }",
        "reserved by the compiler");
}

// E2: writing through a SHARED reference to an array is rejected.
TEST_F(RuntimeTest, WriteThroughSharedRefRejected)
{
    expectCompileFail("fn main() -> i32 { let a = [1, 2]; let r: &[i32; 2] = &a;"
                      " r[0] = 5; ret 0; }",
        "cannot assign through a shared reference");
}

// E2 (runtime): write THROUGH a &mut field of an immutable binding is allowed.
TEST_F(RuntimeTest, WriteThroughMutFieldIndex)
{
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hi\"); s.data[0] = 'x' as i8;"
                 " print_str(s.to_cstr()); println(); ret 0; }",
        "xi\n",
        0);
}

// E2 (runtime): write through a &mut reference to an array is allowed.
TEST_F(RuntimeTest, WriteThroughMutRefToArray)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; let r = &mut a;"
              " r[1] = 9; ret a[1]; }",
        9);
}

// D1 (runtime): an out-of-bounds array index aborts the process.
TEST_F(RuntimeTest, ArrayOobAborts)
{
    ASSERT_TRUE(compile("fn main() -> i32 { let a = [1, 2, 3]; ret a[99]; }"))
        << "compilation failed";
    int code = linkAndRun();
    EXPECT_NE(code, 0) << "out-of-bounds array index must abort, got exit 0";
}

// D1 (runtime): in-bounds indexing does NOT abort.
TEST_F(RuntimeTest, ArrayInBoundsNoAbort)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3]; let mut s = 0; let mut i = 0;"
              " while i < 3 { s = s + a[i]; i = i + 1; } ret s; }",
        6);
}

// A2: %f of 1e100 is a ~108-char string — would overflow the old 64-byte
// buffer; the 512-byte cap must render it completely.
TEST_F(RuntimeTest, ToStringF64LargeNoOverflow)
{
    ASSERT_TRUE(compile("fn main() -> i32 { let s = to_string_f64(1e100);"
                        " print_str(s.to_cstr()); println(); ret 0; }"))
        << "compilation failed";
    std::string out;
    int code = linkAndRun(&out);
    EXPECT_EQ(code, 0) << "runtime exit code mismatch";
    EXPECT_GT(out.size(), (size_t)64) << "to_string_f64(1e100) was truncated";
}

// A1: push_char grows exactly when the null terminator would overflow.
TEST_F(RuntimeTest, StringPushCharGrowsAtCap)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " let mut i = 0; while i < 16 { s.push_char('a'); i = i + 1; }"
              " ret s.len; }",
        16);
}

// ── 2026-08-12 spec decisions ────────────────────────────────────────────
// 1. Default parameter values (`a: i32 = 5`) were dead syntax (parsed then
//    ignored by every later pass) — removed; must now be a parse error.
TEST_F(RuntimeTest, DefaultParameterValuesRejected)
{
    expectCompileFail("fn f(a: i32 = 5) -> i32 { ret a; } fn main() -> i32 { ret f(1); }",
        "default parameter values are not supported");
}

// 2. #[i_know] statement attribute: relaxes the integer-narrowing cast ERROR
//    to a warning (data may still truncate — the user takes responsibility).
TEST_F(RuntimeTest, IKnowAttributeAllowsNarrowingCast)
{
    expectRun("fn main() -> i32 { let big: i64 = 1 as i64;"
              " #[i_know = \"i know what I'm doing\"] let t: i32 = big as i32;"
              " ret t; }",
        1);
}

// Same, but the attribute in front of an ASSIGNMENT statement.
TEST_F(RuntimeTest, IKnowAttributeOnAssignment)
{
    expectRun("fn main() -> i32 { let big: i64 = 5 as i64; let mut t: i32 = 0;"
              " #[i_know] t = big as i32;"
              " ret t; }",
        5);
}

// Without #[i_know], i64 -> i32 narrowing stays a hard error (regression).
TEST_F(RuntimeTest, NarrowingCastWithoutIKnowRejected)
{
    expectCompileFail("fn main() -> i32 { let big: i64 = 1 as i64;"
                      " let t: i32 = big as i32; ret t; }",
        "cannot cast integer to a smaller integer type");
}
