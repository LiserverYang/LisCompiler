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
        EXPECT_FALSE(ok) << "expected a compile error, got clean compile for:\n" << source;
        EXPECT_NE(diag.find(fragment), std::string::npos)
            << "expected message containing '" << fragment << "', got:\n" << diag;
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
            Lexer lexer(context); lexer.run();
            Parser parser(context); parser.parseAll();
        }

        // Main source.
        context->filePath = "test.lis";
        context->fileValue = source;
        Lexer lexer(context); lexer.run();
        Parser parser(context); parser.parseAll();
        // Parse errors (incl. lexer errors the Parser gate sees) are now
        // recoverable — report them as a failed compile instead of exit(1)
        // killing the whole test process.
        if (Logger::GetErrorCount() > 0) return false;

        HIRBuilder builder(context); builder.run();

        // Gate on semantic errors — do NOT call sema.run() (it calls exit(1)).
        Logger::ResetErrorCount();
        HIRSemanticAnalyzer sema(context);
        sema.visit(context->hirProgram.get());
        if (Logger::GetErrorCount() > 0) return false;

        MIRBuilder mir(context); mir.run();
        MIRMonomorphization mono(context); mono.run();
        LLVMIRBuilder llvm(context, context->llvmContext, "test.lis"); llvm.run();

        Emitter::Options opts;
        opts.outPath = objPath.string();
        Emitter emitter(context, opts); emitter.run();
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
        ASSERT_TRUE(compile(source)) << "compilation failed:\n" << source;
        int code = linkAndRun();
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n" << source;
    }

    /// Compile, link, run, and assert both the exit code AND the captured stdout.
    void expectOutput(const std::string &source, const std::string &expectedOut, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n" << source;
        std::string out;
        int code = linkAndRun(&out);
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n" << source;
        // Windows printf emits CRLF; normalize to LF so the comparison is
        // platform-independent.
        std::string normalized;
        for (char c : out)
            if (c != '\r') normalized += c;
        EXPECT_EQ(normalized, expectedOut) << "stdout mismatch for:\n" << source;
    }

    /// Compile, link, run feeding `input` to stdin, and assert exit code +
    /// captured stdout (CRLF-normalized).
    void expectOutputWithInput(const std::string &source, const std::string &input,
        const std::string &expectedOut, int expectedExit)
    {
        ASSERT_TRUE(compile(source)) << "compilation failed:\n" << source;
        std::string out;
        int code = linkAndRun(&out, &input);
        EXPECT_EQ(code, expectedExit) << "runtime exit code mismatch for:\n" << source;
        std::string normalized;
        for (char c : out)
            if (c != '\r') normalized += c;
        EXPECT_EQ(normalized, expectedOut) << "stdout mismatch for:\n" << source;
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
        expectRun(src, expectedExit);
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
    Lexer lexer(ctx); lexer.run();
    Parser parser(ctx); parser.parseAll();
    HIRBuilder builder(ctx); builder.run();

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
        << "value-match arm tail expression missing from HIR dump:\n" << out;
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
        "hi!\n", 0);
}

TEST_F(RuntimeTest, PrintFloatAndBool)
{
    expectOutput("fn main() -> i32 { print_float(3.5); println(); print_bool(true); println(); ret 0; }",
        "3.500000\n1\n", 0);
}

TEST_F(RuntimeTest, PrintMultiple)
{
    expectOutput("fn main() -> i32 { print_str(\"x=\"); print_int(7); println(); ret 0; }",
        "x=7\n", 0);
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
              " ret x + y; }", 7);
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
              " ret a + o + flag; }", 15);
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
              " ret 0; }", 1);
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
              " let last_ = unwrap_or(last(range(1, 5)), 0);"   // 4
              " let nth_ = unwrap_or(nth(range(10, 20), 3), 0);" // 13
              " let prod = product(range(1, 5));"               // 24
              " ret last_ + nth_ + prod; }",
        4 + 13 + 24);
}

// ── input builtins (read_line / read_int / read_f64) ─────────────────────────

TEST_F(RuntimeTest, ReadInt)
{
    expectOutputWithInput(
        "fn main() -> i32 { let n = read_int(); print_int(n); println(); ret 0; }",
        "42\n", "42\n", 0);
}

TEST_F(RuntimeTest, ReadLine)
{
    // read_line strips the trailing newline.
    expectOutputWithInput(
        "fn main() -> i32 { let line = read_line(); print_str(line); println(); ret 0; }",
        "hello\n", "hello\n", 0);
}

TEST_F(RuntimeTest, ReadF64)
{
    expectOutputWithInput(
        "fn main() -> i32 { let x = read_f64(); print_float(x); println(); ret 0; }",
        "3.5\n", "3.500000\n", 0);
}

TEST_F(RuntimeTest, ReadIntThenLine)
{
    // read_int consumes one line; the next read_line consumes the next.
    expectOutputWithInput(
        "fn main() -> i32 { let n = read_int(); let line = read_line();"
        " print_int(n); print_char(','); print_str(line); println(); ret 0; }",
        "42\nhello\n", "42,hello\n", 0);
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
              " a[3] = 40; ret s + a[3]; }", 50);
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
        "hi\n", 0);
}

TEST_F(RuntimeTest, StringPushAndGrow)
{
    // from_lit("hello") + push_char + push_str — exercises the buffer grow path.
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hello\"); let mut t = s;"
                 " t.push_char(' '); t.push_str(\"world\");"
                 " print_str(t.to_cstr()); println(); ret t.len; }",
        "hello world\n", 11);
}

TEST_F(RuntimeTest, StringIndexOption)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"hi\");"
              " let c = unwrap_or(s.index(0), '?');"
              " let d = unwrap_or(s.index(9), '?');"   // out of bounds → None → '?'
              " ret (c as i32) - (d as i32); }", 104 - 63);
}

TEST_F(RuntimeTest, StringIsEmpty)
{
    // is_empty() flips from true (String::new) to false after a push_char.
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " let mut e = 0;"
              " if s.is_empty() { e = e + 1; }"
              " s.push_char('a');"
              " if s.is_empty() { ret e; } else { ret e + 1; }"
              " }", 2);
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
              "   ret frees; }", 1);
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
        "42\n3.500000\n1\n", 0);
}

TEST_F(RuntimeTest, HeapAllocFree)
{
    expectRun("fn main() -> i32 { let p = __alloc(8); p[0] = 'x' as i8;"
              " let c = p[0] as char; __free(p); ret c as i32; }", 120);
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
        "xi\n", 0);
}

// E2 (runtime): write through a &mut reference to an array is allowed.
TEST_F(RuntimeTest, WriteThroughMutRefToArray)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; let r = &mut a;"
              " r[1] = 9; ret a[1]; }", 9);
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
              " while i < 3 { s = s + a[i]; i = i + 1; } ret s; }", 6);
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
              " ret s.len; }", 16);
}

// ── P12 regression: control flow nested in an if body must execute ────────────
// buildIf sealed the branch ENTRY block (thenId/elseId) with Goto(join), which
// OVERWROTE the nested control flow's own terminator — the inner if/while/for
// body became unreachable. Seal the branch's actual END block instead.

TEST_F(RuntimeTest, NestedIfInsideIfExecutes)
{
    expectRun("fn main() -> i32 { let x = 5; let mut r = 0;"
              " if x > 3 { if x > 4 { r = 1; } } ret r; }", 1);
}

TEST_F(RuntimeTest, WhileInsideIfBodyExecutes)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut r = 0;"
              " if true { while i < 2 { r = r + 1; i = i + 1; } } ret r; }", 2);
}

TEST_F(RuntimeTest, ForInsideIfBodyExecutes)
{
    expectRun("fn main() -> i32 { let mut r = 0;"
              " if true { for x in range(1, 3) { r = r + x; } } ret r; }", 3);
}

TEST_F(RuntimeTest, NestedIfInElseBodyExecutes)
{
    expectRun("fn main() -> i32 { let x = 1; let mut r = 0;"
              " if x > 3 { r = 1; } else { if x > 0 { r = 2; } } ret r; }", 2);
}

TEST_F(RuntimeTest, ElseIfChainRunsWithoutCrash)
{
    // `else if` desugars to `else { if ... }` — the exact else-nested-if shape
    // that used to segfault lisc.exe (orphaned inner blocks). Must pick the
    // matching else-if branch and not crash.
    expectRun("fn main() -> i32 { let x = 10; let mut r = 0;"
              " if x < 5 { r = 1; } else if x < 20 { r = 2; } else { r = 3; }"
              " ret r; }", 2);
}

TEST_F(RuntimeTest, ElseIfElseBranchRuns)
{
    // The final `else` of an else-if chain must also be reachable.
    expectRun("fn main() -> i32 { let x = 50; let mut r = 0;"
              " if x < 5 { r = 1; } else if x < 20 { r = 2; } else { r = 3; }"
              " ret r; }", 3);
}

TEST_F(RuntimeTest, StatementAfterNestedIfExecutes)
{
    // Not only the nested if's own body — a statement AFTER it in the same block
    // must execute too (the pre-fix seal overwrote control flow past the if).
    expectRun("fn main() -> i32 { let mut r = 0;"
              " if true { if true { r = 1; } r = 5; } ret r; }", 5);
}

TEST_F(RuntimeTest, IfInsideWhileBodyRegression)
{
    // Direction check: while body containing an if already worked; must stay.
    expectRun("fn main() -> i32 { let mut i = 0; let mut r = 0;"
              " while i < 2 { if true { r = 1; } i = i + 1; } ret r; }", 1);
}

TEST_F(RuntimeTest, IfInsideForBodyRegression)
{
    // Direction check: for body containing an if already worked; must stay.
    expectRun("fn main() -> i32 { let mut r = 0;"
              " for x in range(1, 4) { if x > 1 { r = r + x; } } ret r; }", 5);
}

// ── P13 regression: a bare if as a branch must not crash the compiler ─────────
// HIRBuilder dynamic_cast'd every branch to HIRBlock; `else if ...` (else is a
// bare IfStmt) and `if a if b {...}` (then is a bare IfStmt) produced a null
// block and MIRBuilder's buildBlock(null) segfaulted. Branches are now wrapped
// in a synthetic block.

TEST_F(RuntimeTest, BareIfThenBranchRuns)
{
    expectRun("fn main() -> i32 { let mut r = 0; if true if true { r = 1; } ret r; }", 1);
}

TEST_F(RuntimeTest, BareIfThenElseBranchRuns)
{
    expectRun("fn main() -> i32 { let x = 1; let mut r = 0;"
              " if true if x > 0 { r = 2; } ret r; }", 2);
}

// ── P14 regression: bool→int cast must be a zero-extension ────────────────────
// bool is lowered to LLVM i1; the widening cast used SExt, so `true as i32`
// sign-extended to -1 instead of 1. bool is unsigned → ZExt.

TEST_F(RuntimeTest, BoolTrueCastToIntIsOne)
{
    expectRun("fn main() -> i32 { let b = true; ret b as i32; }", 1);
}

TEST_F(RuntimeTest, BoolFalseCastToIntIsZero)
{
    expectRun("fn main() -> i32 { let b = false; ret b as i32; }", 0);
}

TEST_F(RuntimeTest, BoolTrueCastToInt64IsOne)
{
    expectRun("fn main() -> i64 { let b = true; ret b as i64; }", 1);
}

TEST_F(RuntimeTest, BoolCastResultInArithmetic)
{
    // The cast result must be a usable 1/0 value, not -1 (which would give 9).
    expectRun("fn main() -> i32 { let b = true; ret 10 + (b as i32); }", 11);
}

// ── B: arithmetic semantics ─────────────────────────────────────────────────────

TEST_F(RuntimeTest, ArithmeticLargeProduct)
{
    expectRun("fn main() -> i32 { ret 1000000 * 1000; }", 1000000000);
}

TEST_F(RuntimeTest, ArithmeticLeftAssociativeSubtraction)
{
    // Equal precedence is left-associative: (100 - 50) - 25 = 25, not
    // 100 - (50 - 25) = 75.
    expectRun("fn main() -> i32 { ret 100 - 50 - 25; }", 25);
}

TEST_F(RuntimeTest, ArithmeticPrecedenceMulBeforeAdd)
{
    expectRun("fn main() -> i32 { ret 2 + 3 * 4; }", 14);
}

TEST_F(RuntimeTest, ArithmeticParenthesesOverridePrecedence)
{
    expectRun("fn main() -> i32 { ret (2 + 3) * 4; }", 20);
}

TEST_F(RuntimeTest, ArithmeticDivisionTruncates)
{
    expectRun("fn main() -> i32 { ret 7 / 2; }", 3);
}

TEST_F(RuntimeTest, ArithmeticModuloPositive)
{
    expectRun("fn main() -> i32 { ret 17 % 5; }", 2);
}

TEST_F(RuntimeTest, ArithmeticModuloNegativeIsCstyle)
{
    // The result of `%` follows the dividend (C semantics): -17 % 5 = -2.
    // Take the magnitude so the exit code stays positive.
    expectRun("fn main() -> i32 { let a = 0 - 17; let r = a % 5;"
              " if r < 0 { ret 0 - r; } ret r; }", 2);
}

TEST_F(RuntimeTest, ArithmeticModuloExactMultiple)
{
    expectRun("fn main() -> i32 { ret 20 % 5; }", 0);
}

TEST_F(RuntimeTest, ArithmeticSumChain)
{
    expectRun("fn main() -> i32 { ret 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10; }", 55);
}

TEST_F(RuntimeTest, ArithmeticMixedOps)
{
    // 2 + 6 * 7 / 3 - 4 = 2 + 14 - 4 = 12.
    expectRun("fn main() -> i32 { ret 2 + 6 * 7 / 3 - 4; }", 12);
}

// ── B: integer widths (i8/i16/i64) ─────────────────────────────────────────────
// i8/i16 values come from char casts (`'A' as i8`); i64 from widening casts.

TEST_F(RuntimeTest, I8AdditionWraps)
{
    // 65 + 65 = 130 overflows signed i8 → -126.
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'A' as i8;"
              " let c = a + b; ret c as i32; }", -126);
}

TEST_F(RuntimeTest, I8SubtractionNegative)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'B' as i8;"
              " let c = a - b; ret c as i32; }", -1);
}

TEST_F(RuntimeTest, I8WidenToI32)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; ret a as i32; }", 65);
}

TEST_F(RuntimeTest, I8WidenToI64SignExtends)
{
    // -1 as i8 widens to i64 as -1 (sign extension, not zero extension).
    expectRun("fn main() -> i64 { let a = 'A' as i8; let b = 'B' as i8;"
              " let c = a - b; ret c as i64; }", -1);
}

TEST_F(RuntimeTest, I16AdditionFits)
{
    expectRun("fn main() -> i32 { let a = 'A' as i16; let b = 'A' as i16;"
              " let c = a + b; ret c as i32; }", 130);
}

TEST_F(RuntimeTest, I16WidenToI64)
{
    expectRun("fn main() -> i64 { let a = 'A' as i16; ret a as i64; }", 65);
}

TEST_F(RuntimeTest, I64Addition)
{
    expectRun("fn main() -> i64 { let a = 5 as i64; let b = 7 as i64;"
              " let c = a + b; ret c; }", 12);
}

TEST_F(RuntimeTest, I64Multiplication)
{
    expectRun("fn main() -> i64 { let a = 100 as i64; let b = 20 as i64;"
              " let c = a * b; ret c; }", 2000);
}

TEST_F(RuntimeTest, I64Division)
{
    expectRun("fn main() -> i64 { let a = 100 as i64; let b = 25 as i64;"
              " let c = a / b; ret c; }", 4);
}

TEST_F(RuntimeTest, IntegerWidthChainWidening)
{
    // i8 → i16 → i32 → i64, value preserved at each step.
    expectRun("fn main() -> i64 { let a = 'A' as i8; let b = a as i16;"
              " let c = b as i32; let d = c as i64; ret d; }", 65);
}

TEST_F(RuntimeTest, I8Comparison)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'B' as i8;"
              " if a < b { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, I8BitwiseAnd)
{
    // 'A'=65 (0b1000001) & 'B'=66 (0b1000010) = 0b1000000 = 64.
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'B' as i8;"
              " let c = a & b; ret c as i32; }", 64);
}

// ── B: float (f64) semantics ───────────────────────────────────────────────────

TEST_F(RuntimeTest, FloatAddition)
{
    expectOutput("fn main() -> i32 { print_float(1.5 + 2.5); println(); ret 0; }",
        "4.000000\n", 0);
}

TEST_F(RuntimeTest, FloatMultiplication)
{
    expectOutput("fn main() -> i32 { print_float(2.5 * 2.0); println(); ret 0; }",
        "5.000000\n", 0);
}

TEST_F(RuntimeTest, FloatDivision)
{
    expectOutput("fn main() -> i32 { print_float(7.0 / 2.0); println(); ret 0; }",
        "3.500000\n", 0);
}

TEST_F(RuntimeTest, FloatComparison)
{
    expectRun("fn main() -> i32 { if 3.5 > 3.0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, FloatEquality)
{
    expectRun("fn main() -> i32 { if 1.0 == 1.0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, IntToFloatCast)
{
    expectOutput("fn main() -> i32 { let x = 5 as f64; print_float(x); println(); ret 0; }",
        "5.000000\n", 0);
}

TEST_F(RuntimeTest, I64ToFloatCast)
{
    expectOutput("fn main() -> i32 { let a = 3 as i64; let b = a as f64;"
                 " print_float(b); println(); ret 0; }", "3.000000\n", 0);
}

TEST_F(RuntimeTest, FloatModuloVariables)
{
    expectOutput("fn main() -> i32 { let x = 10.0; let y = 4.0; let z = x - y * 2.0;"
                 " print_float(z); println(); ret 0; }", "2.000000\n", 0);
}

TEST_F(RuntimeTest, FloatScientificLiteral)
{
    // 1e2 is 100.0.
    expectOutput("fn main() -> i32 { print_float(1e2); println(); ret 0; }",
        "100.000000\n", 0);
}

TEST_F(RuntimeTest, FloatNaNComparisonIsFalse)
{
    // NaN is not less than, greater than, or equal to anything.
    expectRun("fn main() -> i32 { let x = 0.0 / 0.0; if x > 0.0 { ret 1; } ret 0; }", 0);
}

// ── B: casts ───────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, CharToIntCast)
{
    expectRun("fn main() -> i32 { let c = 'A'; ret c as i32; }", 65);
}

TEST_F(RuntimeTest, IntToCharCast)
{
    expectRun("fn main() -> i32 { let x = 66 as i32; let c = x as char; ret c as i32; }", 66);
}

TEST_F(RuntimeTest, I8ToCharCast)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let c = a as char; ret c as i32; }", 65);
}

TEST_F(RuntimeTest, BoolToCharCastRejected)
{
    expectCompileFail("fn main() -> i32 { let b = true; let c = b as char; ret c as i32; }",
        "only be cast to integer");
}

TEST_F(RuntimeTest, FloatToIntCastRejected)
{
    expectCompileFail("fn main() -> i32 { let x = 3.7 as i32; ret x; }", "cannot be cast");
}

TEST_F(RuntimeTest, CharToI64Widening)
{
    expectRun("fn main() -> i64 { let c = 'A'; ret c as i64; }", 65);
}

TEST_F(RuntimeTest, I64ToI32NarrowingRejected)
{
    expectCompileFail("fn main() -> i32 { let a = 5 as i64; ret a as i32; }",
        "smaller integer type");
}

TEST_F(RuntimeTest, IntToBoolCastRejected)
{
    // bool is not an integer target → "integer can only be cast to float or integer".
    expectCompileFail("fn main() -> i32 { let x = 1 as bool; ret 0; }",
        "can only be cast to float or integer");
}

TEST_F(RuntimeTest, SameTypeCastUselessInfo)
{
    // i32 → i32 is a useless cast (INFO, not an error) — compile succeeds.
    expectRun("fn main() -> i32 { ret 5 as i32; }", 5);
}

TEST_F(RuntimeTest, I16ToI64Widening)
{
    expectRun("fn main() -> i64 { let a = 'A' as i16; ret a as i64; }", 65);
}

TEST_F(RuntimeTest, CastInExpression)
{
    // A cast can appear anywhere an expression can.
    expectRun("fn main() -> i32 { ret ('B' as i32) + 1; }", 67);
}

TEST_F(RuntimeTest, CharToBoolCastRejected)
{
    expectCompileFail("fn main() -> i32 { let c = 'A'; let b = c as bool; ret 0; }",
        "only be cast to integer");
}

TEST_F(RuntimeTest, I8ToI16Widening)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = a as i16; ret b as i32; }", 65);
}

// ── B: control flow (while / for / break / continue) ───────────────────────────

TEST_F(RuntimeTest, WhileSum0To9)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 10 { s = s + i; i = i + 1; } ret s; }", 45);
}

TEST_F(RuntimeTest, WhileEvenSumUpTo20)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 20 { i = i + 1; if (i % 2) == 0 { s = s + i; } } ret s; }", 110);
}

TEST_F(RuntimeTest, WhileCountDown)
{
    expectRun("fn main() -> i32 { let mut n = 5; let mut c = 0;"
              " while n > 0 { n = n - 1; c = c + 1; } ret c; }", 5);
}

TEST_F(RuntimeTest, WhileTrueWithBreak)
{
    expectRun("fn main() -> i32 { let mut i = 0; while true { i = i + 1;"
              " if i > 5 { break; } } ret i; }", 6);
}

TEST_F(RuntimeTest, WhileBreakEarlyExitCode)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 100 { i = i + 1; if i == 10 { break; } s = s + i; } ret s; }", 45);
}

TEST_F(RuntimeTest, WhileContinueSkipsEven)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 10 { i = i + 1; if (i % 2) == 0 { continue; } s = s + i; } ret s; }", 25);
}

TEST_F(RuntimeTest, WhileNestedBlocks)
{
    expectRun("fn main() -> i32 { let mut s = 0; let mut i = 0;"
              " while i < 3 { let mut j = 0; while j < 3 { s = s + 1; j = j + 1; }"
              " i = i + 1; } ret s; }", 9);
}

TEST_F(RuntimeTest, WhileBodyUsesLoopVar)
{
    expectRun("fn main() -> i32 { let mut s = 0; let mut i = 1;"
              " while i <= 5 { s = s + i * 10; i = i + 1; } ret s; }", 150);
}

TEST_F(RuntimeTest, ForRangeSum1To10)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 11) { s = s + x; } ret s; }", 55);
}

TEST_F(RuntimeTest, ForRangeEmpty)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(5, 5) { s = s + x; } ret s; }", 0);
}

TEST_F(RuntimeTest, ForRangeBackwardEmpty)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(10, 1) { s = s + x; } ret s; }", 0);
}

TEST_F(RuntimeTest, ForBreak)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 100) {"
              " if x > 5 { break; } s = s + x; } ret s; }", 15);
}

TEST_F(RuntimeTest, ForContinueSkipsThree)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 6) {"
              " if x == 3 { continue; } s = s + x; } ret s; }", 12);
}

TEST_F(RuntimeTest, ForNestedProductSum)
{
    expectRun("fn main() -> i32 { let mut s = 0; for i in range(1, 4) {"
              " for j in range(1, 4) { s = s + i * j; } } ret s; }", 36);
}

TEST_F(RuntimeTest, ForVarNotLeakedOutside)
{
    // The loop variable is scoped to the loop — reusing the name outside fails
    // (single-name rule), but a DIFFERENT name works.
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 3) { s = s + x; }"
              " let y = 100; ret s + y; }", 103);
}

TEST_F(RuntimeTest, ForOnCustomIterator)
{
    // A struct implementing Iterator<i32> is usable in for.
    expectRun("struct R { cur: i32, end: i32 } impl Iterator<i32> for R {"
              " fn next(self: &mut Self) -> Option<i32> {"
              " if self.cur < self.end { let v = self.cur; self.cur = self.cur + 1;"
              " ret Option::Some(v); } ret Option::None; } }"
              " fn mk() -> R { ret R { cur: 1, end: 4 }; }"
              " fn main() -> i32 { let mut s = 0; for x in mk() { s = s + x; } ret s; }", 6);
}

// ── B: recursion ───────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, RecursionFactorial)
{
    expectRun("fn fact(n: i32) -> i32 { if n <= 1 { ret 1; } ret n * fact(n - 1); }"
              " fn main() -> i32 { ret fact(5); }", 120);
}

TEST_F(RuntimeTest, RecursionFibonacci)
{
    expectRun("fn fib(n: i32) -> i32 { if n < 2 { ret n; }"
              " ret fib(n - 1) + fib(n - 2); } fn main() -> i32 { ret fib(7); }", 13);
}

TEST_F(RuntimeTest, RecursionCountDown)
{
    // `down` (not `count` — that name collides with the preloaded stdlib's
    // `count<T: Iterator>`).
    expectRun("fn down(n: i32, acc: i32) -> i32 { if n == 0 { ret acc; }"
              " ret down(n - 1, acc + 1); } fn main() -> i32 { ret down(10, 0); }", 10);
}

TEST_F(RuntimeTest, RecursionSumUpTo)
{
    // `sumto` (not `sum` — collides with stdlib). Sum(20)=210 fits the 8-bit
    // exit code; sum(100)=5050 would truncate to 186.
    expectRun("fn sumto(n: i32) -> i32 { if n == 0 { ret 0; } ret n + sumto(n - 1); }"
              " fn main() -> i32 { ret sumto(20); }", 210);
}

TEST_F(RuntimeTest, RecursionNestedDepth)
{
    // Count nesting depth via two mutually-independent recursive helpers.
    expectRun("fn down(n: i32) -> i32 { if n == 0 { ret 0; } ret 1 + down(n - 1); }"
              " fn main() -> i32 { ret down(3) + down(4); }", 7);
}

// ── B: function pointers ───────────────────────────────────────────────────────

TEST_F(RuntimeTest, FunctionPointerMultipleCalls)
{
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn main() -> i32 { let f = dbl;"
              " ret f(5) + f(7); }", 24);
}

TEST_F(RuntimeTest, FunctionPointerReassignment)
{
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn id(x: i32) -> i32 { ret x; }"
              " fn main() -> i32 { let mut f = dbl; f = id; ret f(5); }", 5);
}

TEST_F(RuntimeTest, FunctionPointerChained)
{
    expectRun("fn inc(x: i32) -> i32 { ret x + 1; } fn main() -> i32 { let f = inc;"
              " let a = f(10); let b = f(a); ret b; }", 12);
}

TEST_F(RuntimeTest, FunctionPointerMoveSemantics)
{
    // A function reference is a value: `let b = a` MOVES it, leaving `a`
    // unusable (single-owner). Calling through the moved binding works.
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn main() -> i32 { let a = dbl;"
              " let b = a; ret b(5); }", 10);
}

// ── B: bitwise & and | (infix) ─────────────────────────────────────────────────

TEST_F(RuntimeTest, BitAndBasic)
{
    expectRun("fn main() -> i32 { ret 6 & 3; }", 2);
}

TEST_F(RuntimeTest, BitOrBasic)
{
    expectRun("fn main() -> i32 { ret 6 | 3; }", 7);
}

TEST_F(RuntimeTest, BitAndZero)
{
    expectRun("fn main() -> i32 { ret 7 & 0; }", 0);
}

TEST_F(RuntimeTest, BitOrMaxByte)
{
    expectRun("fn main() -> i32 { ret 0 | 255; }", 255);
}

TEST_F(RuntimeTest, BitOpsCombined)
{
    // (12 & 10) | 3 = 8 | 3 = 11.
    expectRun("fn main() -> i32 { ret (12 & 10) | 3; }", 11);
}

TEST_F(RuntimeTest, BitAndAssociativity)
{
    // 15 & 12 = 12, 12 & 10 = 8.
    expectRun("fn main() -> i32 { ret 15 & 12 & 10; }", 8);
}

TEST_F(RuntimeTest, BitOrAssociativity)
{
    // 1 | 2 = 3, 3 | 4 = 7.
    expectRun("fn main() -> i32 { ret 1 | 2 | 4; }", 7);
}

TEST_F(RuntimeTest, BitAndPrecedenceLowerThanCompare)
{
    // Comparison binds tighter than & : (6 > 3) is true; 6 & 3 = 2. But `6 > 3 & 1`
    // parses as (6 > 3) & 1 = true & 1... type-mismatch. Instead verify & binds
    // looser than * : 2 * 3 & 5 = 6 & 5 = 4.
    expectRun("fn main() -> i32 { ret 2 * 3 & 5; }", 4);
}

TEST_F(RuntimeTest, BitAndWithComparisons)
{
    expectRun("fn main() -> i32 { let a = 6 & 3; let b = 8 | 1;"
              " if a < b { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, BitwiseOnI64)
{
    expectRun("fn main() -> i64 { let a = 12 as i64; let b = 10 as i64;"
              " let c = a & b; ret c; }", 8);
}

// ── B: enums and match ─────────────────────────────────────────────────────────

TEST_F(RuntimeTest, EnumUnitDispatchValue)
{
    expectRun("enum E { A, B, C } fn main() -> i32 { let e = E::B;"
              " let y = match e { A => 1, B => 2, C => 3 }; ret y; }", 2);
}

TEST_F(RuntimeTest, EnumPayloadMatch)
{
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 { let o = O::Some(7);"
              " match o { Some(v) => { ret v; }, None => { ret 0; } } }", 7);
}

TEST_F(RuntimeTest, EnumMultiPayload)
{
    expectRun("enum E { P(i32, i32), Q } fn main() -> i32 { let e = E::P(3, 4);"
              " match e { P(a, b) => { ret a + b; }, Q => { ret 0; } } }", 7);
}

TEST_F(RuntimeTest, EnumWildcardArm)
{
    expectRun("enum E { A, B, C } fn main() -> i32 { let e = E::C;"
              " match e { A => { ret 1; }, _ => { ret 9; } } }", 9);
}

TEST_F(RuntimeTest, EnumValueArmNoBlock)
{
    expectRun("enum E { A, B } fn main() -> i32 { let e = E::A;"
              " let y = match e { A => 10, B => 20 }; ret y; }", 10);
}

TEST_F(RuntimeTest, EnumMatchNonExhaustiveRejected)
{
    expectCompileFail("enum E { A, B } fn main() -> i32 { let e = E::A;"
                      " match e { A => { ret 0; } } }", "exhaustive");
}

TEST_F(RuntimeTest, EnumMatchNonEnumRejected)
{
    expectCompileFail("fn main() -> i32 { let x = 5; match x { 1 => { ret 0; },"
                      " _ => { ret 1; } } }", "");
}

TEST_F(RuntimeTest, EnumEqualityWithoutTraitRejected)
{
    expectCompileFail("enum E { A, B } fn main() -> i32 { let e = E::A;"
                      " if e == E::A { ret 1; } ret 0; }", "implement");
}

TEST_F(RuntimeTest, EnumWithPartialEqImpl)
{
    expectRun("enum E { A, B } impl PartialEq for E { fn eq(self, o: Self) -> bool {"
              " ret true; } fn ne(self, o: Self) -> bool { ret false; } }"
              " fn main() -> i32 { let e = E::A; if e == E::A { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, EnumNestedMatch)
{
    expectRun("enum A { X(i32), Y } enum B { M(i32), N } fn main() -> i32 {"
              " let a = A::X(3); match a { X(v) => { match B::M(v) {"
              " M(w) => { ret w; }, N => { ret 0; } } }, Y => { ret 0; } } }", 3);
}

TEST_F(RuntimeTest, EnumGenericTwoInstantiations)
{
    // The same generic enum instantiated with two different payload types.
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 {"
              " let a = O::Some(5); let b = O::Some('A');"
              " let mut r = 0; match a { Some(v) => { r = v; }, None => { r = 0; } }"
              " match b { Some(c) => { ret r + (c as i32); }, None => { ret r; } } }", 70);
}

// ── B: globals ─────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GlobalRead)
{
    expectRun("let g = 10; fn main() -> i32 { ret g; }", 10);
}

TEST_F(RuntimeTest, GlobalWrite)
{
    expectRun("let g = 1; fn main() -> i32 { g = g + 5; ret g; }", 6);
}

TEST_F(RuntimeTest, GlobalMultipleSum)
{
    expectRun("let a = 1; let b = 2; let c = 3; fn main() -> i32 { ret a + b + c; }", 6);
}

TEST_F(RuntimeTest, GlobalReadFromFunction)
{
    expectRun("let g = 5; fn get() -> i32 { ret g; } fn main() -> i32 { ret get(); }", 5);
}

TEST_F(RuntimeTest, GlobalMutatedInFunction)
{
    expectRun("let g = 0; fn bump() { g = g + 1; } fn main() -> i32 {"
              " bump(); bump(); bump(); ret g; }", 3);
}

TEST_F(RuntimeTest, GlobalExprInitializerRejected)
{
    // A binary-expression initializer is not a literal → rejected (would
    // silently zero-initialize in static storage).
    expectCompileFail("let g = 1 + 2; fn main() -> i32 { ret g; }",
        "global variable initializer must be a literal");
}

TEST_F(RuntimeTest, GlobalFloatLiteral)
{
    expectOutput("let g = 2.5; fn main() -> i32 { print_float(g); println(); ret 0; }",
        "2.500000\n", 0);
}

TEST_F(RuntimeTest, GlobalBoolLiteral)
{
    expectRun("let g = true; fn main() -> i32 { if g { ret 1; } ret 0; }", 1);
}

// ── B: generics ────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GenericIdentityFunction)
{
    expectRun("fn id<T>(x: T) -> T { ret x; } fn main() -> i32 { ret id(42); }", 42);
}

TEST_F(RuntimeTest, GenericPickLarger)
{
    expectRun("fn pick<T: Numeric>(a: T, b: T) -> T { if a > b { ret a; } ret b; }"
              " fn main() -> i32 { ret pick(3, 9); }", 9);
}

TEST_F(RuntimeTest, GenericStructPair)
{
    expectRun("struct Pair<T> { a: T, b: T } fn main() -> i32 {"
              " let p = Pair { a: 3, b: 4 }; ret p.a + p.b; }", 7);
}

TEST_F(RuntimeTest, GenericStructNested)
{
    expectRun("struct Box<T> { v: T } fn main() -> i32 { let b = Box { v: Box { v: 5 } };"
              " ret b.v.v; }", 5);
}

TEST_F(RuntimeTest, GenericStructMethod)
{
    expectRun("struct W<T> { v: T } impl W<T> { fn get(self) -> T { ret self.v; } }"
              " fn main() -> i32 { let w = W { v: 8 }; ret w.get(); }", 8);
}

TEST_F(RuntimeTest, GenericFloatInstantiation)
{
    expectOutput("fn id<T>(x: T) -> T { ret x; } fn main() -> i32 {"
                 " let f = id(3.5); print_float(f); println(); ret 0; }", "3.500000\n", 0);
}

TEST_F(RuntimeTest, GenericUnboundedAddRejected)
{
    expectCompileFail("fn f<T>(x: T) -> T { ret x + x; } fn main() -> i32 { ret 0; }",
        "Numeric");
}

TEST_F(RuntimeTest, GenericBoundedAddWorks)
{
    expectRun("fn f<T: Numeric>(x: T) -> T { ret x + x; }"
              " fn main() -> i32 { ret f(21); }", 42);
}

TEST_F(RuntimeTest, GenericOperatorOverload)
{
    expectRun("struct V { x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } fn main() -> i32 {"
              " let a = V { x: 3 }; let b = V { x: 4 }; ret (a + b).x; }", 7);
}

// ── B: arrays ──────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, ArrayBasicIndex)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3]; ret a[0] + a[2]; }", 4);
}

TEST_F(RuntimeTest, ArrayMutableElement)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; a[1] = 9; ret a[1]; }", 9);
}

TEST_F(RuntimeTest, ArrayReferenceIndex)
{
    expectRun("fn main() -> i32 { let a = [5, 6, 7]; let r = &a; ret r[0] + r[2]; }", 12);
}

TEST_F(RuntimeTest, ArrayLoopSum)
{
    expectRun("fn main() -> i32 { let a = [10, 20, 30]; let mut s = 0; let mut i = 0;"
              " while i < 3 { s = s + a[i]; i = i + 1; } ret s; }", 60);
}

TEST_F(RuntimeTest, ArraySingleElement)
{
    expectRun("fn main() -> i32 { let a = [42]; ret a[0]; }", 42);
}

TEST_F(RuntimeTest, ArrayIndexLastElement)
{
    expectRun("fn main() -> i32 { let a = [7, 8, 9]; ret a[2]; }", 9);
}

// ── B: strings ─────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, StringNewIsEmpty)
{
    expectRun("fn main() -> i32 { let s = String::new(); ret s.len; }", 0);
}

TEST_F(RuntimeTest, StringFromLiteral)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"hi\"); ret s.len; }", 2);
}

TEST_F(RuntimeTest, StringPushChars)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " s.push_char('a'); s.push_char('b'); ret s.len; }", 2);
}

TEST_F(RuntimeTest, StringPushStr)
{
    expectRun("fn main() -> i32 { let mut s = String::new(); s.push_str(\"hello\");"
              " ret s.len; }", 5);
}

TEST_F(RuntimeTest, StringGrowPastCap)
{
    // 30 pushes forces multiple buffer reallocations (cap starts at 16).
    expectRun("fn main() -> i32 { let mut s = String::new(); let mut i = 0;"
              " while i < 30 { s.push_char('a'); i = i + 1; } ret s.len; }", 30);
}

TEST_F(RuntimeTest, StringIndexInBounds)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"abc\");"
              " match s.index(1) { Some(c) => { ret c as i32; }, None => { ret 0; } } }", 98);
}

TEST_F(RuntimeTest, StringIndexOutOfBounds)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"abc\");"
              " match s.index(99) { Some(c) => { ret c as i32; }, None => { ret 0; } } }", 0);
}

TEST_F(RuntimeTest, StringEmptyIndex)
{
    expectRun("fn main() -> i32 { let s = String::new();"
              " match s.index(0) { Some(c) => { ret 1; }, None => { ret 0; } } }", 0);
}

TEST_F(RuntimeTest, StringToCstrPrints)
{
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hello\");"
                 " let p = s.to_cstr(); print_str(p); println(); ret 0; }", "hello\n", 0);
}

TEST_F(RuntimeTest, StringMutateByte)
{
    expectRun("fn main() -> i32 { let mut s = String::from_lit(\"hi\");"
              " s.data[0] = 'x' as i8; ret s.data[0] as i32; }", 120);
}

TEST_F(RuntimeTest, StringPushStrThenIndex)
{
    expectRun("fn main() -> i32 { let mut s = String::from_lit(\"ab\");"
              " s.push_str(\"cde\"); match s.index(4) {"
              " Some(c) => { ret c as i32; }, None => { ret 0; } } }", 101);
}

// ── B: operator overloading ────────────────────────────────────────────────────

TEST_F(RuntimeTest, OpOverloadSubtraction)
{
    expectRun("struct V { x: i32 } impl Sub for V { fn sub(self, o: Self) -> V {"
              " ret V { x: self.x - o.x }; } } fn main() -> i32 {"
              " let a = V { x: 10 }; let b = V { x: 3 }; ret (a - b).x; }", 7);
}

TEST_F(RuntimeTest, OpOverloadMultiplication)
{
    expectRun("struct V { x: i32 } impl Mul for V { fn mul(self, o: Self) -> V {"
              " ret V { x: self.x * o.x }; } } fn main() -> i32 {"
              " let a = V { x: 6 }; let b = V { x: 7 }; ret (a * b).x; }", 42);
}

TEST_F(RuntimeTest, OpOverloadDivision)
{
    expectRun("struct V { x: i32 } impl Div for V { fn div(self, o: Self) -> V {"
              " ret V { x: self.x / o.x }; } } fn main() -> i32 {"
              " let a = V { x: 20 }; let b = V { x: 4 }; ret (a / b).x; }", 5);
}

TEST_F(RuntimeTest, OpOverloadRemainder)
{
    expectRun("struct V { x: i32 } impl Rem for V { fn rem(self, o: Self) -> V {"
              " ret V { x: self.x % o.x }; } } fn main() -> i32 {"
              " let a = V { x: 17 }; let b = V { x: 5 }; ret (a % b).x; }", 2);
}

TEST_F(RuntimeTest, OpOverloadChainedPrecedence)
{
    // a + b * c: * binds tighter than + even under overloading → 1 + 6 = 7.
    expectRun("struct V { x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } impl Mul for V { fn mul(self, o: Self) -> V {"
              " ret V { x: self.x * o.x }; } } fn main() -> i32 {"
              " let a = V { x: 1 }; let b = V { x: 2 }; let c = V { x: 3 };"
              " let r = a + b * c; ret r.x; }", 7);
}

TEST_F(RuntimeTest, OpOverloadNotEqual)
{
    expectRun("struct V { x: i32 } impl PartialEq for V { fn eq(self, o: Self) -> bool {"
              " ret self.x == o.x; } fn ne(self, o: Self) -> bool { ret self.x != o.x; } }"
              " fn main() -> i32 { let a = V { x: 1 }; let b = V { x: 2 };"
              " if a != b { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, OpOverloadEqual)
{
    expectRun("struct V { x: i32 } impl PartialEq for V { fn eq(self, o: Self) -> bool {"
              " ret self.x == o.x; } fn ne(self, o: Self) -> bool { ret self.x != o.x; } }"
              " fn main() -> i32 { let a = V { x: 5 }; let b = V { x: 5 };"
              " if a == b { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, OpOverloadLessThan)
{
    expectRun("struct V { x: i32 } impl PartialOrd for V { fn lt(self, o: Self) -> bool {"
              " ret self.x < o.x; } fn gt(self, o: Self) -> bool { ret self.x > o.x; }"
              " fn le(self, o: Self) -> bool { ret self.x <= o.x; }"
              " fn ge(self, o: Self) -> bool { ret self.x >= o.x; } }"
              " fn main() -> i32 { let a = V { x: 1 }; let b = V { x: 2 };"
              " if a < b { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, OpOverloadOnGeneric)
{
    expectRun("struct V { x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } fn sum<T: Add>(a: T, b: T) -> T { ret a + b; }"
              " fn main() -> i32 { let r = sum(V { x: 2 }, V { x: 5 }); ret r.x; }", 7);
}

// ── E: option.lis ──────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, OptionIsSomeOnSome)
{
    expectRun("fn main() -> i32 { let o = Option::Some(7); if is_some(o) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, OptionIsSomeOnNone)
{
    // `Option::None` standalone can't infer T — pin it via a return-type helper.
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let o = none_i(); if is_some(o) { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, OptionIsNoneOnNone)
{
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let o = none_i(); if is_none(o) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, OptionIsNoneOnSome)
{
    expectRun("fn main() -> i32 { let o = Option::Some(3); if is_none(o) { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, OptionUnwrapOrSome)
{
    expectRun("fn main() -> i32 { ret unwrap_or(Option::Some(7), 0); }", 7);
}

TEST_F(RuntimeTest, OptionUnwrapOrNone)
{
    expectRun("fn main() -> i32 { ret unwrap_or(Option::None, 0); }", 0);
}

TEST_F(RuntimeTest, OptionUnwrapOrFallbackUsed)
{
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let o = none_i(); ret unwrap_or(o, 42); }", 42);
}

TEST_F(RuntimeTest, OptionAndSomeSome)
{
    // and(Some(1), Some(5)) → Some(5).
    expectRun("fn main() -> i32 { let a = and(Option::Some(1), Option::Some(5));"
              " match a { Some(v) => { ret v; }, None => { ret 0; } } }", 5);
}

TEST_F(RuntimeTest, OptionAndNoneSome)
{
    // and(None, Some(5)) → None.
    expectRun("fn main() -> i32 { let a = and(Option::None, Option::Some(5));"
              " match a { Some(v) => { ret 1; }, None => { ret 0; } } }", 0);
}

TEST_F(RuntimeTest, OptionOrSomeNone)
{
    // or(Some(1), Some(9)) → Some(1) — first Some wins.
    expectRun("fn main() -> i32 { let a = or(Option::Some(1), Option::Some(9));"
              " match a { Some(v) => { ret v; }, None => { ret 0; } } }", 1);
}

TEST_F(RuntimeTest, OptionOrNoneSome)
{
    // or(None, Some(9)) → Some(9).
    expectRun("fn main() -> i32 { let a = or(Option::None, Option::Some(9));"
              " match a { Some(v) => { ret v; }, None => { ret 0; } } }", 9);
}

TEST_F(RuntimeTest, OptionOrNoneNone)
{
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let a = or(none_i(), none_i());"
              " match a { Some(v) => { ret 1; }, None => { ret 0; } } }", 0);
}

TEST_F(RuntimeTest, OptionFunctionsChain)
{
    expectRun("fn main() -> i32 { let o = Option::Some(3);"
              " let u = unwrap_or(o, 100); let s = is_some(Option::Some(u));"
              " if s { ret u; } ret 0; }", 3);
}

// ── E: math.lis — min/max/clamp/abs ───────────────────────────────────────────

TEST_F(RuntimeTest, MathMinBasic)
{
    expectRun("fn main() -> i32 { ret min(3, 7); }", 3);
}

TEST_F(RuntimeTest, MathMinEqual)
{
    expectRun("fn main() -> i32 { ret min(5, 5); }", 5);
}

TEST_F(RuntimeTest, MathMinNegative)
{
    // min(-7, 3): -7 < 3 → -7. Take the magnitude to keep the exit code clean.
    expectRun("fn main() -> i32 { let m = min(0 - 7, 3); if m < 0 { ret 0 - m; } ret m; }", 7);
}

TEST_F(RuntimeTest, MathMaxBasic)
{
    expectRun("fn main() -> i32 { ret max(3, 7); }", 7);
}

TEST_F(RuntimeTest, MathMaxEqual)
{
    expectRun("fn main() -> i32 { ret max(9, 9); }", 9);
}

TEST_F(RuntimeTest, MathMaxNegative)
{
    expectRun("fn main() -> i32 { ret max(0 - 5, 2); }", 2);
}

TEST_F(RuntimeTest, MathClampWithin)
{
    expectRun("fn main() -> i32 { ret clamp(5, 0, 10); }", 5);
}

TEST_F(RuntimeTest, MathClampBelow)
{
    expectRun("fn main() -> i32 { ret clamp(0 - 1, 0, 10); }", 0);
}

TEST_F(RuntimeTest, MathClampAbove)
{
    expectRun("fn main() -> i32 { ret clamp(11, 0, 10); }", 10);
}

TEST_F(RuntimeTest, MathClampEqual)
{
    expectRun("fn main() -> i32 { ret clamp(10, 0, 10); }", 10);
}

TEST_F(RuntimeTest, MathAbsPositive)
{
    expectRun("fn main() -> i32 { ret abs(7); }", 7);
}

TEST_F(RuntimeTest, MathAbsNegative)
{
    expectRun("fn main() -> i32 { ret abs(0 - 7); }", 7);
}

TEST_F(RuntimeTest, MathAbsZero)
{
    expectRun("fn main() -> i32 { ret abs(0); }", 0);
}

TEST_F(RuntimeTest, MathFabsNegative)
{
    expectOutput("fn main() -> i32 { print_float(fabs(0.0 - 3.5)); println(); ret 0; }",
        "3.500000\n", 0);
}

TEST_F(RuntimeTest, MathFabsPositive)
{
    expectOutput("fn main() -> i32 { print_float(fabs(2.25)); println(); ret 0; }",
        "2.250000\n", 0);
}

// ── E: math.lis — gcd/lcm/ipow ─────────────────────────────────────────────────

TEST_F(RuntimeTest, MathGcdBasic)
{
    expectRun("fn main() -> i32 { ret gcd(12, 18); }", 6);
}

TEST_F(RuntimeTest, MathGcdCoprime)
{
    expectRun("fn main() -> i32 { ret gcd(7, 13); }", 1);
}

TEST_F(RuntimeTest, MathGcdWithZero)
{
    // gcd(0, b) == b.
    expectRun("fn main() -> i32 { ret gcd(0, 5); }", 5);
}

TEST_F(RuntimeTest, MathGcdSame)
{
    expectRun("fn main() -> i32 { ret gcd(6, 6); }", 6);
}

TEST_F(RuntimeTest, MathGcdOneIsOne)
{
    expectRun("fn main() -> i32 { ret gcd(1, 100); }", 1);
}

TEST_F(RuntimeTest, MathLcmBasic)
{
    expectRun("fn main() -> i32 { ret lcm(4, 6); }", 12);
}

TEST_F(RuntimeTest, MathLcmCoprime)
{
    expectRun("fn main() -> i32 { ret lcm(3, 5); }", 15);
}

TEST_F(RuntimeTest, MathLcmWithZero)
{
    expectRun("fn main() -> i32 { ret lcm(0, 5); }", 0);
}

TEST_F(RuntimeTest, MathLcmSame)
{
    expectRun("fn main() -> i32 { ret lcm(7, 7); }", 7);
}

TEST_F(RuntimeTest, MathIpowBasic)
{
    expectRun("fn main() -> i32 { ret ipow(2, 10); }", 1024);
}

TEST_F(RuntimeTest, MathIpowZeroExponent)
{
    expectRun("fn main() -> i32 { ret ipow(5, 0); }", 1);
}

TEST_F(RuntimeTest, MathIpowOneExponent)
{
    expectRun("fn main() -> i32 { ret ipow(9, 1); }", 9);
}

TEST_F(RuntimeTest, MathIpowSmallBase)
{
    expectRun("fn main() -> i32 { ret ipow(3, 3); }", 27);
}

// ── E: math.lis — is_even/is_odd/sign ──────────────────────────────────────────

TEST_F(RuntimeTest, MathIsEvenTrue)
{
    expectRun("fn main() -> i32 { if is_even(10) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathIsEvenFalse)
{
    expectRun("fn main() -> i32 { if is_even(7) { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, MathIsEvenNegative)
{
    expectRun("fn main() -> i32 { if is_even(0 - 4) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathIsOddTrue)
{
    expectRun("fn main() -> i32 { if is_odd(7) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathIsOddFalse)
{
    expectRun("fn main() -> i32 { if is_odd(10) { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, MathIsOddNegative)
{
    // (0-5) % 2 = -1 != 0 → odd.
    expectRun("fn main() -> i32 { if is_odd(0 - 5) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathSignPositive)
{
    expectRun("fn main() -> i32 { ret sign(5); }", 1);
}

TEST_F(RuntimeTest, MathSignNegative)
{
    // sign(-5) = -1.
    expectRun("fn main() -> i32 { let s = sign(0 - 5); if s < 0 { ret 0 - s; } ret s; }", 1);
}

TEST_F(RuntimeTest, MathSignZero)
{
    expectRun("fn main() -> i32 { ret sign(0); }", 0);
}

// ── E: math.lis — float helpers ────────────────────────────────────────────────

TEST_F(RuntimeTest, MathDegToRadHalfPi)
{
    // deg_to_rad(90) = π/2 ≈ 1.5707963...
    expectOutput("fn main() -> i32 { let r = deg_to_rad(90.0);"
                 " print_float(r); println(); ret 0; }", "1.570796\n", 0);
}

TEST_F(RuntimeTest, MathRadToDegFullCircle)
{
    // rad_to_deg(6.28318530718) ≈ 360.
    expectOutput("fn main() -> i32 { let d = rad_to_deg(6.28318530718);"
                 " print_float(d); println(); ret 0; }", "360.000000\n", 0);
}

TEST_F(RuntimeTest, MathLerpMidpoint)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 0.5)); println(); ret 0; }",
        "5.000000\n", 0);
}

TEST_F(RuntimeTest, MathLerpStart)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 0.0)); println(); ret 0; }",
        "0.000000\n", 0);
}

TEST_F(RuntimeTest, MathLerpEnd)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 1.0)); println(); ret 0; }",
        "10.000000\n", 0);
}

TEST_F(RuntimeTest, MathLerpBeyondRange)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 2.0)); println(); ret 0; }",
        "20.000000\n", 0);
}

// ── E: char.lis ────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, CharIsDigitTrue)
{
    expectRun("fn main() -> i32 { if is_digit('5') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsDigitFalse)
{
    expectRun("fn main() -> i32 { if is_digit('a') { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, CharIsAlphaLower)
{
    expectRun("fn main() -> i32 { if is_alpha('a') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsAlphaUpper)
{
    expectRun("fn main() -> i32 { if is_alpha('Z') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsAlphaNonLetter)
{
    expectRun("fn main() -> i32 { if is_alpha('1') { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, CharIsAlphanumericDigit)
{
    expectRun("fn main() -> i32 { if is_alphanumeric('7') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsAlphanumericLetter)
{
    expectRun("fn main() -> i32 { if is_alphanumeric('x') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsAlphanumericPunct)
{
    expectRun("fn main() -> i32 { if is_alphanumeric('!') { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, CharIsWhitespaceSpace)
{
    expectRun("fn main() -> i32 { if is_whitespace(' ') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsWhitespaceTab)
{
    expectRun("fn main() -> i32 { if is_whitespace('\\t') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsWhitespaceNewline)
{
    expectRun("fn main() -> i32 { if is_whitespace('\\n') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsWhitespaceLetter)
{
    expectRun("fn main() -> i32 { if is_whitespace('a') { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, CharDigitToInt)
{
    expectRun("fn main() -> i32 { ret digit_to_int('7'); }", 7);
}

TEST_F(RuntimeTest, CharDigitToIntZero)
{
    expectRun("fn main() -> i32 { ret digit_to_int('0'); }", 0);
}

TEST_F(RuntimeTest, CharDigitToIntNonDigit)
{
    expectRun("fn main() -> i32 { ret digit_to_int('x'); }", 0);
}

TEST_F(RuntimeTest, CharDigitToIntNine)
{
    expectRun("fn main() -> i32 { ret digit_to_int('9'); }", 9);
}

// ── E: iterator.lis ────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, IteratorRangeSum1To5)
{
    expectRun("fn main() -> i32 { ret sum(range(1, 5)); }", 10);
}

TEST_F(RuntimeTest, IteratorRangeSumSingle)
{
    expectRun("fn main() -> i32 { ret sum(range(4, 5)); }", 4);
}

TEST_F(RuntimeTest, IteratorRangeSumEmpty)
{
    expectRun("fn main() -> i32 { ret sum(range(5, 5)); }", 0);
}

TEST_F(RuntimeTest, IteratorRangeCount)
{
    expectRun("fn main() -> i32 { ret count(range(1, 5)); }", 4);
}

TEST_F(RuntimeTest, IteratorRangeCountEmpty)
{
    expectRun("fn main() -> i32 { ret count(range(3, 3)); }", 0);
}

TEST_F(RuntimeTest, IteratorFirstSome)
{
    expectRun("fn main() -> i32 { let f = first(range(1, 5));"
              " match f { Some(v) => { ret v; }, None => { ret 0; } } }", 1);
}

TEST_F(RuntimeTest, IteratorFirstEmpty)
{
    expectRun("fn main() -> i32 { let f = first(range(5, 5));"
              " match f { Some(v) => { ret 1; }, None => { ret 0; } } }", 0);
}

TEST_F(RuntimeTest, IteratorLastSome)
{
    expectRun("fn main() -> i32 { let l = last(range(1, 5));"
              " match l { Some(v) => { ret v; }, None => { ret 0; } } }", 4);
}

TEST_F(RuntimeTest, IteratorLastEmpty)
{
    expectRun("fn main() -> i32 { let l = last(range(2, 2));"
              " match l { Some(v) => { ret 1; }, None => { ret 0; } } }", 0);
}

TEST_F(RuntimeTest, IteratorNthValid)
{
    expectRun("fn main() -> i32 { let n = nth(range(10, 20), 3);"
              " match n { Some(v) => { ret v; }, None => { ret 0; } } }", 13);
}

TEST_F(RuntimeTest, IteratorNthOutOfBounds)
{
    expectRun("fn main() -> i32 { let n = nth(range(10, 20), 99);"
              " match n { Some(v) => { ret 1; }, None => { ret 0; } } }", 0);
}

TEST_F(RuntimeTest, IteratorNthZero)
{
    expectRun("fn main() -> i32 { let n = nth(range(5, 10), 0);"
              " match n { Some(v) => { ret v; }, None => { ret 0; } } }", 5);
}

TEST_F(RuntimeTest, IteratorProductRange)
{
    expectRun("fn main() -> i32 { ret product(range(1, 5)); }", 24);
}

TEST_F(RuntimeTest, IteratorProductEmpty)
{
    expectRun("fn main() -> i32 { ret product(range(3, 3)); }", 1);
}

TEST_F(RuntimeTest, IteratorSumThenCount)
{
    expectRun("fn main() -> i32 { let s = sum(range(1, 6)); let c = count(range(1, 6));"
              " ret s + c; }", 20);
}

// ── F: Examples regression ─────────────────────────────────────────────────────
// Every Examples/*.lis must keep compiling and producing its baseline exit code
// (the documented outputs: borrow 55 / iterator 23 / match 8 / method_ref 10 /
// operator 41 / ...). Prevents silent rot of the canonical examples.

TEST_F(RuntimeTest, ExampleBorrow)
{
    expectExample("borrow", 55);
}

TEST_F(RuntimeTest, ExampleDrop)
{
    expectExample("drop", 0);
}

TEST_F(RuntimeTest, ExampleExample)
{
    expectExample("example", 0);
}

TEST_F(RuntimeTest, ExampleGenericType)
{
    expectExample("generic_type", 0);
}

TEST_F(RuntimeTest, ExampleIo)
{
    // io.lis reads from stdin; the baseline run with empty input exits 0.
    expectExample("io", 0);
}

TEST_F(RuntimeTest, ExampleIterator)
{
    expectExample("iterator", 23);
}

TEST_F(RuntimeTest, ExampleMatch)
{
    expectExample("match", 8);
}

TEST_F(RuntimeTest, ExampleMethodRef)
{
    expectExample("method_ref", 10);
}

TEST_F(RuntimeTest, ExampleOperator)
{
    expectExample("operator", 41);
}

TEST_F(RuntimeTest, ExampleOwnership)
{
    expectExample("ownership", 0);
}

TEST_F(RuntimeTest, ExamplePrint)
{
    expectExample("print", 0);
}

TEST_F(RuntimeTest, ExampleString)
{
    expectExample("string", 0);
}
