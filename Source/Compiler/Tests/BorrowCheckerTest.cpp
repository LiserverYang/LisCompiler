/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * Borrow-checker regression tests (Stage 1: lexical lifetimes).
 *
 * Each case compiles a small `.lis` snippet through Lexer → Parser →
 * HIRBuilder → HIRSemanticAnalyzer and asserts either that a borrow error is
 * reported (compile-fail) or that the code is accepted (compile-pass). The
 * sema's run() exit-gate is bypassed by calling visit() directly; parse errors
 * would exit via the Parser gate, so all snippets below are syntactically valid.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#else
#include <unistd.h>
#endif

#include "Core/Context.hpp"
#include "IR/HIRBuilder.hpp"
#include "IR/HIRSemanticAnalyzer.hpp"
#include "Lexer/Lexer.hpp"
#include "Logger/Logger.hpp"
#include "Parser/Parser.hpp"

namespace
{
// Save/restore C stdout so we can read the Logger's diagnostics.
int g_savedStdout = -1;

// P12: a fixed "bc_test_out.txt" in the CWD is a collision hazard: two test
// binaries running from the same directory (or a leftover from a killed run)
// silently overwrite each other. Key the temp file by process id so every
// process gets its own path; the `_getpid`/`getpid` pair is already covered by
// the platform <process.h>/<unistd.h> includes above.
const char *captureFileName()
{
#ifdef _WIN32
    static std::string name = "bc_test_out_" + std::to_string(_getpid()) + ".txt";
#else
    static std::string name = "bc_test_out_" + std::to_string(getpid()) + ".txt";
#endif
    return name.c_str();
}

void captureStdout()
{
    fflush(stdout);
    g_savedStdout = dup(1);
    FILE *f = freopen(captureFileName(), "w", stdout);
    (void)f;
}

void restoreStdout()
{
    fflush(stdout);
    if (g_savedStdout >= 0)
    {
        dup2(g_savedStdout, 1);
        close(g_savedStdout);
        g_savedStdout = -1;
    }
}

std::string readCaptured()
{
    std::ifstream f(captureFileName());
    std::string out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::remove(captureFileName());
    return out;
}
} // namespace

class BorrowCheckerTest : public ::testing::Test
{
protected:
    std::shared_ptr<Context> context;

    void SetUp() override
    {
        context = std::make_shared<Context>();
        context->filePath = "test.lis";
    }

    /// Run the pipeline to the sema (bypassing the sema exit gate) and return
    /// the captured stderr-style diagnostics.
    std::string analyze(const std::string &source)
    {
        context->fileValue = source;
        context->tokenStream.clear();
        context->program.globalStatements.clear();
        context->hirProgram.reset();

        Lexer lexer(context);
        lexer.run();
        Parser parser(context);
        // parseAll() not run(): a parse error would make run() exit(1) and kill
        // the whole test process. The error count below then reflects parse +
        // sema errors (this file only feeds syntactically valid snippets, but
        // the gate-free entry keeps the harness robust).
        parser.parseAll();
        HIRBuilder builder(context);
        builder.run();

        Logger::ResetErrorCount();
        captureStdout();
        HIRSemanticAnalyzer sema(context);
        sema.visit(context->hirProgram.get());
        restoreStdout();
        std::string out = readCaptured();
        return out;
    }

    /// The snippet must be rejected with at least one diagnostic containing
    /// `fragment`.
    void expectError(const std::string &source, const std::string &fragment)
    {
        std::string out = analyze(source);
        EXPECT_GT(Logger::GetErrorCount(), 0) << "expected a borrow error, got none\n"
                                              << out;
        EXPECT_NE(out.find(fragment), std::string::npos)
            << "expected message containing '" << fragment << "', got:\n"
            << out;
    }

    /// The snippet must be accepted (no errors).
    void expectOk(const std::string &source)
    {
        std::string out = analyze(source);
        EXPECT_EQ(Logger::GetErrorCount(), 0) << "expected clean compile, got errors:\n"
                                              << out;
    }
};

// ── P2 regression: SelfType interning must not drop receiver mutability ───────
// createSelf was keyed by trait name only, so a trait mixing `&self` and
// `&mut self` shared ONE SelfType: the `&mut self` method's receiver lost its
// mutability, and the conformance check compared its `&mut S` param against a
// `&S` expected type → false "param type mismatch" on valid code.

TEST_F(BorrowCheckerTest, TraitWithMixedReceiverKinds)
{
    expectOk("trait Mixed { fn read(self: &Self) -> i32; fn write(self: &mut Self, v: i32); }"
             " struct S { v: i32 } impl Mixed for S {"
             " fn read(self: &S) -> i32 { ret self.v; }"
             " fn write(self: &mut S, v: i32) { self.v = v; } }"
             " fn main() -> i32 { let mut s = S { v: 1 }; s.write(5); ret s.read(); }");
}

TEST_F(BorrowCheckerTest, TraitSharedReceiverCannotWrite)
{
    // After the P2 fix the `&self` receiver is genuinely shared — writing
    // through it must still be rejected (locked semantics: write through a
    // shared reference is forbidden).
    expectError("trait Mixed { fn read(self: &Self) -> i32; fn write(self: &mut Self, v: i32); }"
                " struct S { v: i32 } impl Mixed for S {"
                " fn read(self: &S) -> i32 { self.v = 5; ret self.v; }"
                " fn write(self: &mut S, v: i32) { self.v = v; } }"
                " fn main() -> i32 { ret 0; }",
        "immutable");
}

// ── aliasing rules ─────────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, SharedThenMutBorrow)
{
    // Under NLL an UNUSED first borrow is dead, so `&mut x` after it is legal;
    // using the first borrow after the conflict keeps it live → conflict.
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let a = &x; let b = &mut x; let t = a.v; }",
        "already borrowed");
}

TEST_F(BorrowCheckerTest, MutThenSharedBorrow)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let a = &mut x; let b = &x; let t = a.v; }",
        "borrowed as mutable");
}

TEST_F(BorrowCheckerTest, TwoMutBorrows)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let a = &mut x; let b = &mut x; let t = a.v; }",
        "already borrowed");
}

TEST_F(BorrowCheckerTest, UnusedSharedBorrowDoesNotBlock)
{
    // NLL: an unused borrow's lifetime ends at its creation.
    expectOk("struct S { v: i32 } fn main() { let x = S { v: 1 }; let a = &x; let b = &mut x; ret 0; }");
}

TEST_F(BorrowCheckerTest, TwoSharedBorrows)
{
    expectOk("struct S { v: i32 } fn main() { let x = S { v: 1 }; let a = &x; let b = &x; ret 0; }");
}

// ── mutate / move while borrowed ───────────────────────────────────────────────

TEST_F(BorrowCheckerTest, AssignWholeWhileBorrowed)
{
    // The borrow is live because r is used AFTER the write → the write is blocked.
    expectError("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &x; x = S { v: 2 }; let t = r.v; }",
        "because it is borrowed");
}

TEST_F(BorrowCheckerTest, AssignWholeAfterBorrowDead)
{
    // NLL: r's borrow is dead (r never used), so the write is legal.
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &x; x = S { v: 2 }; ret x.v; }");
}

TEST_F(BorrowCheckerTest, AssignFieldWhileBorrowed)
{
    expectError("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &x; x.v = 5; let t = r.v; }",
        "because it is borrowed");
}

TEST_F(BorrowCheckerTest, MoveWhileBorrowed)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x; let y = x; }",
        "cannot move out of 'x' because it is borrowed");
}

TEST_F(BorrowCheckerTest, MutateThroughMutBorrow)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &mut x; r.v = 5; ret r.v; }");
}

TEST_F(BorrowCheckerTest, ReadThroughSharedBorrow)
{
    expectOk("struct S { v: i32 } fn main() { let x = S { v: 7 }; let r = &x; ret r.v; }");
}

TEST_F(BorrowCheckerTest, WriteThroughSharedBorrow)
{
    // Writing through a shared reference is forbidden by mutability.
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x; r.v = 5; }",
        "immutable");
}

// ── borrow of a moved value ────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, BorrowMovedValue)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let y = x; let r = &x; }",
        "cannot borrow moved value");
}

// ── borrow lifetimes: statement / block ────────────────────────────────────────

TEST_F(BorrowCheckerTest, TemporaryBorrowsDoNotConflict)
{
    // Two temporary borrows in separate statements must not conflict.
    expectOk("struct S { v: i32 } fn f(s: &S) -> i32 { ret s.v; } fn g(s: &mut S) { s.v = 9; }"
             " fn main() { let mut x = S { v: 1 }; let a = f(&x); g(&mut x); ret x.v; }");
}

TEST_F(BorrowCheckerTest, VariableBorrowLivesToBlockEnd)
{
    // A promoted variable borrow lives until the block ends.
    expectOk("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x; let t = r.v; }");
}

TEST_F(BorrowCheckerTest, NllBorrowEndsAtLastUse)
{
    // `r`'s borrow ends at its last use (`r.v`), so the later `&mut x` is fine.
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &x; let v = r.v; let s = &mut x; s.v = 9; ret v; }");
}

TEST_F(BorrowCheckerTest, NllMutBorrowEndsBeforeRead)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &mut x; r.v = 5; ret x.v; }");
}

TEST_F(BorrowCheckerTest, NllBorrowLiveWhenHolderUsedAfter)
{
    // r is used AFTER the conflict → its borrow is live → conflict.
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x; let s = &mut x; let t = r.v; }",
        "already borrowed");
}

TEST_F(BorrowCheckerTest, NllReadThenWrite)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &x; let v = r.v; x.v = 5; ret v; }");
}

TEST_F(BorrowCheckerTest, NllMoveStaysConservative)
{
    // Moves are not NLL-ed: moving a borrowed place is forbidden even after the
    // holder's last use (would leave the reference dangling).
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x; let v = r.v; let y = x; }",
        "cannot move out of 'x' because it is borrowed");
}

TEST_F(BorrowCheckerTest, NllMoveOfUnusedBorrowStaysBlocked)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x; let y = x; }",
        "cannot move out of 'x' because it is borrowed");
}

TEST_F(BorrowCheckerTest, BorrowEndsAtBlockExit)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 };"
             " { let r = &x; let t = r.v; } x.v = 5; ret x.v; }");
}

TEST_F(BorrowCheckerTest, BorrowInIfBranchEnds)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 };"
             " let c: bool = false; if c == true { let r = &x; let t = r.v; } x.v = 5; ret x.v; }");
}

// ── method receivers ───────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, MutSelfOnImmutable)
{
    expectError("struct S { v: i32 } impl S { fn set(self: &mut S, v: i32) { self.v = v; } }"
                " fn main() { let x = S { v: 1 }; x.set(5); }",
        "as mutable because it is not mutable");
}

TEST_F(BorrowCheckerTest, MutSelfOnMutable)
{
    expectOk("struct S { v: i32 } impl S { fn set(self: &mut S, v: i32) { self.v = v; } fn get(self: &S) -> i32 { ret self.v; } }"
             " fn main() { let mut x = S { v: 1 }; x.set(5); ret x.get(); }");
}

TEST_F(BorrowCheckerTest, ReceiverBorrowEndsAtStatement)
{
    // `x.get()` borrows x for the statement; `x.set(5)` afterwards is legal.
    expectOk("struct S { v: i32 } impl S { fn set(self: &mut S, v: i32) { self.v = v; } fn get(self: &S) -> i32 { ret self.v; } }"
             " fn main() { let mut x = S { v: 1 }; let a = x.get(); x.set(5); ret x.v; }");
}

// ── loops ──────────────────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, LoopBodyBorrowEndsAtIterationEnd)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let c: bool = false;"
             " while c == true { let r = &x; let t = r.v; } x.v = 5; ret x.v; }");
}

TEST_F(BorrowCheckerTest, ConflictingBorrowsInsideLoop)
{
    // a is used AFTER b → a's borrow is live at the &mut x → conflict.
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let c: bool = false;"
                " while c == true { let a = &x; let b = &mut x; let t = a.v; } }",
        "already borrowed");
}

TEST_F(BorrowCheckerTest, OuterBorrowSurvivesLoop)
{
    // r is used after the loop → its borrow is live at the write → conflict.
    expectError("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &mut x;"
                " let c: bool = false; while c == true { r.v = 1; } x.v = 5; let t = r.v; }",
        "because it is borrowed");
}

// ── regression: the borrow checker must not reject valid existing patterns ─────

TEST_F(BorrowCheckerTest, NoBorrowsCompiles)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; x.v = 2; ret x.v; }");
}

TEST_F(BorrowCheckerTest, GlobalCounterStillWorks)
{
    expectOk("let counter = 0; fn bump() { counter = counter + 1; } fn main() -> i32 { ret counter; }");
}

// ── Stage 3: dangling / escape analysis ────────────────────────────────────────

TEST_F(BorrowCheckerTest, RetLocalRef)
{
    expectError("fn f() -> &i32 { let x = 5; ret &x; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, RetLocalRefViaVar)
{
    expectError("fn f() -> &i32 { let x = 5; let r = &x; ret r; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, RetLocalStructField)
{
    expectError("struct S { v: i32 } fn f() -> &i32 { let s = S { v: 1 }; ret &s.v; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, RetByValueParamRef)
{
    // A by-value param is a callee-frame copy; a reference into it dangles.
    expectError("struct S { v: i32 } fn f(s: S) -> &i32 { ret &s.v; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, RetStructInitWithLocalRef)
{
    expectError("struct H { r: &i32 } fn f() -> H { let x = 5; ret H { r: &x }; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, RetLocalStructWithLocalRef)
{
    expectError("struct H { r: &i32 } fn f() -> H { let x = 5; let h = H { r: &x }; ret h; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, RetLocalStructRefFieldValue)
{
    expectError("struct H { r: &i32 } fn f() -> &i32 { let x = 5; let h = H { r: &x }; ret h.r; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, ReassignedRefVarEscapes)
{
    // r initially points at a global, then is re-pointed at a local; the return
    // must be rejected (a re-assignment refreshes the origin).
    expectError("let G = 1; fn f() -> &i32 { let x = 5; let mut r: &i32 = &G; r = &x; ret r; } fn main() { ret 0; }",
        "does not live long enough");
}

TEST_F(BorrowCheckerTest, ReassignedRefFieldEscapes)
{
    expectError("let G = 1; struct H { r: &i32 } fn f() -> H { let x = 5; let mut h = H { r: &G }; h.r = &x; ret h; } fn main() { ret 0; }",
        "does not live long enough");
}

// ── Stage 3: legal patterns that must keep compiling ───────────────────────────

TEST_F(BorrowCheckerTest, RetParamRef)
{
    expectOk("fn f(p: &i32) -> &i32 { ret p; } fn main() { ret 0; }");
}

TEST_F(BorrowCheckerTest, RetThroughParamRef)
{
    expectOk("struct S { v: i32 } fn f(p: &S) -> &i32 { ret &p.v; } fn main() { ret 0; }");
}

TEST_F(BorrowCheckerTest, RetSelfField)
{
    expectOk("struct S { v: i32 } impl S { fn get(self: &S) -> &i32 { ret &self.v; } }"
             " fn main() { let s = S { v: 1 }; let r = s.get(); ret 0; }");
}

TEST_F(BorrowCheckerTest, RetGlobalRef)
{
    expectOk("let G = 5; fn f() -> &i32 { ret &G; } fn main() { ret 0; }");
}

TEST_F(BorrowCheckerTest, RetCopiedParamRef)
{
    expectOk("fn f(p: &i32) -> &i32 { let q = p; ret q; } fn main() { ret 0; }");
}

TEST_F(BorrowCheckerTest, RetStringLiteral)
{
    // String literals live in static storage → safe to return.
    expectOk("fn f() -> &i8 { ret \"hi\"; } fn main() { ret 0; }");
}

TEST_F(BorrowCheckerTest, RetStructRefFieldFromParam)
{
    expectOk("struct H { r: &i32 } fn f(p: &i32) -> H { ret H { r: p }; } fn main() { ret 0; }");
}

TEST_F(BorrowCheckerTest, RetThroughParamRefField)
{
    expectOk("struct H { r: &i32 } fn f(p: &H) -> &i32 { ret p.r; } fn main() { ret 0; }");
}

TEST_F(BorrowCheckerTest, RetStructRefFieldFromGlobal)
{
    expectOk("let G = 1; struct H { r: &i32 } fn f() -> H { let h = H { r: &G }; ret h; } fn main() { ret 0; }");
}

// ── Stage 4: field precision (disjoint fields coexist) ─────────────────────────

TEST_F(BorrowCheckerTest, DisjointSharedThenMut)
{
    // `&p.a` + `&mut p.b`: disjoint fields coexist.
    expectOk("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
             " let r = &p.a; let s = &mut p.b; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, DisjointTwoMut)
{
    expectOk("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
             " let r = &mut p.a; let s = &mut p.b; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, SameFieldMutConflict)
{
    expectError("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
                " let r = &p.a; let s = &mut p.a; let t = r; ret 0; }",
        "already borrowed");
}

TEST_F(BorrowCheckerTest, WholeVsFieldConflict)
{
    // `&p` (whole) borrows every field → `&mut p.a` conflicts (ancestor/descendant).
    expectError("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
                " let r = &p; let s = &mut p.a; let t = r.a; ret 0; }",
        "already borrowed");
}

TEST_F(BorrowCheckerTest, WriteDisjointFieldOk)
{
    expectOk("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
             " let r = &p.a; p.b = 5; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, WriteSameFieldBlocked)
{
    expectError("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
                " let r = &p.a; p.a = 5; let t = r; ret 0; }",
        "because it is borrowed");
}

TEST_F(BorrowCheckerTest, WriteWholeBlocked)
{
    expectError("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
                " let r = &p; p = S { a: 9, b: 9 }; let t = r.a; ret 0; }",
        "because it is borrowed");
}

TEST_F(BorrowCheckerTest, MoveDisjointFieldWhileBorrowed)
{
    // Moving a non-Copy sibling field while p.a is borrowed is allowed (disjoint).
    expectOk("struct Inner { v: i32 } struct S { a: Inner, b: Inner } fn main() {"
             " let mut p = S { a: Inner{v:1}, b: Inner{v:2} };"
             " let r = &p.a; let v = p.b; let t = r; ret 0; }");
}

// ── NLL field precision ────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, NllBorrowEndsAtCopy)
{
    // r's borrow ends at `let v = r`, so `&mut p.a` afterwards is fine.
    expectOk("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
             " let r = &p.a; let v = r; let s = &mut p.a; ret 0; }");
}

TEST_F(BorrowCheckerTest, NllDescendantAfterDeath)
{
    // r (borrow of p.inner) dies at `let v = r`; the descendant `&mut p.inner.v` is fine.
    expectOk("struct I { a: i32, v: i32 } struct S { inner: I, other: i32 } fn main() {"
             " let mut p = S { inner: I { a: 1, v: 2 }, other: 3 };"
             " let r = &p.inner; let v = r; let s = &mut p.inner.v; ret 0; }");
}

TEST_F(BorrowCheckerTest, NllAncestorLiveConflict)
{
    // r (borrow of p.inner) is used AFTER the descendant borrow → still live → conflict.
    expectError("struct I { a: i32, v: i32 } struct S { inner: I, other: i32 } fn main() {"
                " let mut p = S { inner: I { a: 1, v: 2 }, other: 3 };"
                " let r = &p.inner; let s = &mut p.inner.v; let t = r.a; ret 0; }",
        "already borrowed");
}

// ── deep field paths ───────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, DeepDisjointFields)
{
    expectOk("struct I { x: i32, y: i32 } struct S { inner: I, other: i32 } fn main() {"
             " let mut p = S { inner: I { x: 1, y: 2 }, other: 3 };"
             " let r = &p.inner.x; let s = &mut p.inner.y; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, CrossLevelDisjoint)
{
    expectOk("struct I { x: i32, y: i32 } struct S { inner: I, other: i32 } fn main() {"
             " let mut p = S { inner: I { x: 1, y: 2 }, other: 3 };"
             " let r = &p.inner.x; let s = &mut p.other; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, DescendantVsAncestor)
{
    expectError("struct I { x: i32, y: i32 } struct S { inner: I, other: i32 } fn main() {"
                " let mut p = S { inner: I { x: 1, y: 2 }, other: 3 };"
                " let r = &p.inner.x; let s = &mut p.inner; let t = r; ret 0; }",
        "already borrowed");
}

// ── re-borrow precision ────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, ReborrowDisjointOk)
{
    // `&r` borrows the binding r (root=r); it does not touch p.b.
    expectOk("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
             " let r = &mut p.a; let s = &r; let u = s; let t = &mut p.b; ret 0; }");
}

TEST_F(BorrowCheckerTest, ReborrowLiveConflict)
{
    // r's p.a borrow is live (r used after `&mut p.a`) → conflict.
    expectError("struct S { a: i32, b: i32 } fn main() { let mut p = S { a: 1, b: 2 };"
                " let r = &mut p.a; let s = &r; let t = &mut p.a; let u = r; ret 0; }",
        "already borrowed");
}

// ── borrow-after-move field precision ──────────────────────────────────────────

TEST_F(BorrowCheckerTest, BorrowSiblingAfterMove)
{
    // Moving p.b then borrowing the DISJOINT p.a is legal.
    expectOk("struct Inner { v: i32 } struct S { a: Inner, b: Inner } fn main() {"
             " let mut p = S { a: Inner{v:1}, b: Inner{v:2} };"
             " let v = p.b; let r = &p.a; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, BorrowMovedFieldRejected)
{
    expectError("struct Inner { v: i32 } struct S { a: Inner, b: Inner } fn main() {"
                " let mut p = S { a: Inner{v:1}, b: Inner{v:2} };"
                " let v = p.b; let r = &p.b; ret 0; }",
        "cannot borrow moved value");
}

TEST_F(BorrowCheckerTest, BorrowWholeAfterFieldMove)
{
    // Borrowing the whole struct after a field move includes the moved field.
    expectError("struct Inner { v: i32 } struct S { a: Inner, b: Inner } fn main() {"
                " let mut p = S { a: Inner{v:1}, b: Inner{v:2} };"
                " let v = p.b; let r = &p; ret 0; }",
        "cannot borrow moved value");
}

// ── reference-typed method receivers (regression) ──────────────────────────────
// `m.add(5)` with `m: &mut S` must pass m's VALUE (the referent pointer), not
// `&m` (the address of the reference slot). Sema-only tests guard compilation;
// the runtime behaviour is locked in by Examples/method_ref.lis.

TEST_F(BorrowCheckerTest, MethodCallThroughMutRefReceiver)
{
    expectOk("struct S { v: i32 } impl S { fn add(self: &mut S, d: i32) { self.v = self.v + d; } }"
             " fn main() { let mut c = S { v: 1 }; let m = &mut c; m.add(4); ret 0; }");
}

TEST_F(BorrowCheckerTest, MethodCallThroughSharedRefReceiver)
{
    expectOk("struct S { v: i32 } impl S { fn get(self: &S) -> i32 { ret self.v; } }"
             " fn main() { let c = S { v: 7 }; let r = &c; let x = r.get(); ret 0; }");
}

TEST_F(BorrowCheckerTest, MethodCallMutRefAsSharedReceiver)
{
    expectOk("struct S { v: i32 } impl S { fn get(self: &S) -> i32 { ret self.v; } }"
             " fn main() { let mut c = S { v: 7 }; let m = &mut c; let x = m.get(); ret 0; }");
}
