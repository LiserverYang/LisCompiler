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
            Parser parser(context); parser.run();
        }

        // Main source.
        context->filePath = "test.lis";
        context->fileValue = source;
        Lexer lexer(context); lexer.run();
        Parser parser(context); parser.run();

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
};

// ── basic codegen ──────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, Arithmetic)
{
    expectRun("fn main() -> i32 { ret 1 + 2 * 3; }", 7);
}

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
              "   fn next(self: &mut Self) -> option<i32> {"
              "     if self.current > 0 { let v = self.current; self.current = self.current - 1;"
              "                          ret option::some(v); }"
              "     ret option::none; } }"
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
    expectRun("enum option<T> { some(T), none } fn main() -> i32 {"
              " let o = option::some(7);"
              " match o { some(v) => { ret v; }, none => { ret 0; }, }"
              " }",
        7);
}

TEST_F(RuntimeTest, EnumMatchWildcard)
{
    expectRun("enum option<T> { some(T), none } fn main() -> i32 {"
              " let o = option::some(3);"
              " match o { some(v) => { ret v; }, _ => { ret 99; }, }"
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
    expectRun("enum option<T> { some(T), none } fn main() -> i32 {"
              " let o = option::some(5);"
              " match o { some(v) => { ret v; }, none => { ret 0; }, }"
              " }",
        5);
}

TEST_F(RuntimeTest, EnumMatchNonCopyPayload)
{
    // A non-Copy payload is MOVED into the binding; reading it works.
    expectRun("enum option<T> { some(T), none } struct Inner { v: i32 }"
              " fn main() -> i32 { let o = option::some(Inner{v: 5}); let mut got = 0;"
              " match o { some(x) => { got = x.v; }, none => { got = 99; }, }"
              " ret got; }",
        5);
}

TEST_F(RuntimeTest, EnumMatchNonCopyNoDoubleFree)
{
    // The moved payload is dropped exactly once (drop glue counter).
    expectRun("enum option<T> { some(T), none } struct Inner { v: i32 }"
              " let ctr = 0; impl Drop for Inner { fn drop(self) { ctr = ctr + 1; } }"
              " fn main() -> i32 { { let o = option::some(Inner{v: 5});"
              " match o { some(x) => { }, none => { }, } }"
              " ret ctr; }",
        1);
}

TEST_F(RuntimeTest, EnumMatchExpression)
{
    // `let y = match ...` — a value match with tail expressions.
    expectRun("enum option<T> { some(T), none } fn main() -> i32 {"
              " let o = option::some(7);"
              " let y = match o { some(v) => v + 1, none => 0 };"
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
    // A bare `option::none` can't infer T in a generic-arg position, so the
    // test builds a concrete none via a local helper (return-position inference).
    expectRun("fn mk_none() -> option<i32> { ret option::none; }"
              " fn main() -> i32 {"
              " let a = option::some(7);"
              " let x = unwrap_or(a, 0); let y = unwrap_or(mk_none(), 0);"
              " ret x + y; }", 7);
}

TEST_F(RuntimeTest, OptionIsSomeAndOr)
{
    expectRun("fn mk_none() -> option<i32> { ret option::none; }"
              " fn main() -> i32 {"
              " let s = is_some(option::some(1));"
              " let n = is_none(mk_none());"
              " let a = unwrap_or(and(option::some(1), option::some(5)), 0);"
              " let o = unwrap_or(or(mk_none(), option::some(9)), 0);"
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
