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
        EXPECT_GT(Logger::GetErrorCount(), 0) << "expected a borrow error, got none\n" << out;
        EXPECT_NE(out.find(fragment), std::string::npos)
            << "expected message containing '" << fragment << "', got:\n" << out;
    }

    /// The snippet must be accepted (no errors).
    void expectOk(const std::string &source)
    {
        std::string out = analyze(source);
        EXPECT_EQ(Logger::GetErrorCount(), 0) << "expected clean compile, got errors:\n" << out;
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

// ── C: shadowing / single-name rule ────────────────────────────────────────────

TEST_F(BorrowCheckerTest, VariableShadowingRejected)
{
    expectError("fn main() { let x = 1; let x = 2; }", "already exists");
}

TEST_F(BorrowCheckerTest, ShadowingInInnerBlockRejected)
{
    expectError("fn main() { let x = 1; { let x = 2; } }", "already exists");
}

TEST_F(BorrowCheckerTest, ShadowingParamRejected)
{
    expectError("fn f(x: i32) -> i32 { let x = 5; ret x; } fn main() { ret f(1); }",
        "already exists");
}

TEST_F(BorrowCheckerTest, DistinctNamesInBlocksOk)
{
    expectOk("fn main() { let x = 1; { let y = 2; } ret 0; }");
}

// ── C: move semantics ──────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, UseAfterMoveRejected)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let y = x; let z = x.v; }",
        "moved");
}

TEST_F(BorrowCheckerTest, MoveThenReinitRejected)
{
    // Re-initialising a moved variable is rejected (single-owner, no revival).
    expectError("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let y = x;"
                " x = S { v: 2 }; ret x.v; }", "moved");
}

TEST_F(BorrowCheckerTest, MoveWholeAfterFieldMoveAllowed)
{
    // Moving one field then moving the whole value is allowed (the whole move
    // just carries the remaining fields; the partial-move bookkeeping permits it).
    expectOk("struct S { v: i32, w: i32 } fn main() { let x = S { v: 1, w: 2 };"
             " let a = x.v; let b = x; ret b.w; }");
}

TEST_F(BorrowCheckerTest, MoveFieldThenReinitFieldOk)
{
    expectOk("struct S { v: i32, w: i32 } fn main() { let mut x = S { v: 1, w: 2 };"
             " let a = x.v; x.v = 9; let b = x.w; ret a + b; }");
}

TEST_F(BorrowCheckerTest, MoveDisjointFieldsOk)
{
    expectOk("struct S { v: i32, w: i32 } fn main() { let x = S { v: 1, w: 2 };"
             " let a = x.v; let b = x.w; ret a + b; }");
}

TEST_F(BorrowCheckerTest, DoubleMoveRejected)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 };"
                " let a = x; let b = x; }", "moved");
}

TEST_F(BorrowCheckerTest, CopyPrimitivesDoNotMove)
{
    expectOk("fn main() { let a = 5; let b = a; let c = a; ret b + c; }");
}

TEST_F(BorrowCheckerTest, CopyCharDoesNotMove)
{
    expectOk("fn main() { let a = 'x'; let b = a; let c = a; ret 0; }");
}

TEST_F(BorrowCheckerTest, CopyBoolDoesNotMove)
{
    expectOk("fn main() { let a = true; let b = a; let c = a; ret 0; }");
}

TEST_F(BorrowCheckerTest, NonCopyStructMoveForbidsSecondUse)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let a = x;"
                " let b = x; }", "moved");
}

// ── C: deeper paths and mixed projections ──────────────────────────────────────

TEST_F(BorrowCheckerTest, DeepNestedDisjointFields)
{
    expectOk("struct A { x: i32 } struct B { a: A, b: A } fn main() {"
             " let v = B { a: A { x: 1 }, b: A { x: 2 } };"
             " let p = &v.a.x; let q = &v.b.x; ret 0; }");
}

TEST_F(BorrowCheckerTest, DeepNestedAncestorDescendantReadOk)
{
    // Reading a field of the borrowed ancestor after a descendant borrow is fine
    // (no conflict — the ancestor borrow is still readable).
    expectOk("struct A { x: i32 } struct B { a: A } fn main() {"
             " let v = B { a: A { x: 1 } }; let p = &v.a; let q = &v.a.x; let t = p.x; }");
}

TEST_F(BorrowCheckerTest, DeepNestedMutDisjoint)
{
    expectOk("struct A { x: i32 } struct B { a: A, b: A } fn main() {"
             " let mut v = B { a: A { x: 1 }, b: A { x: 2 } };"
             " let p = &mut v.a.x; let q = &mut v.b.x; ret 0; }");
}

// Arrays of non-Copy structs are rejected (locked semantics: array elements are
// Copy-only), so there is no `&a[i].field` path to test — covered by the
// ArrayOfNonCopyElementsRejected test instead.

// ── C: match arms ──────────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, BorrowInMatchArmOk)
{
    expectOk("enum O<T> { Some(T), None } fn main() { let o = O::Some(5);"
             " match o { Some(v) => { let r = &v; ret 0; }, None => { ret 0; } } }");
}

TEST_F(BorrowCheckerTest, MutateScrutineeAfterMatchOk)
{
    // The match consumes `o` (owned) — rebinding the name afterwards is a new
    // declaration, which the single-name rule rejects; use a fresh name.
    expectOk("enum O<T> { Some(T), None } fn main() { let o = O::Some(5);"
             " match o { Some(v) => { let _t = v; }, None => { } }"
             " let p = O::Some(1); let _q = p; ret 0; }");
}

TEST_F(BorrowCheckerTest, BorrowBindingInArmOk)
{
    expectOk("enum O<T> { Some(T), None } fn main() { let o = O::Some(5);"
             " match o { Some(v) => { let r = &v; let t = r; ret 0; }, None => { ret 0; } } }");
}

// ── C: loops ───────────────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, LoopMutBorrowDiesEachIteration)
{
    // A &mut borrow created and used within each iteration dies at the
    // iteration end, so reading x after the loop is fine.
    expectOk("fn main() { let mut x = 1; let mut i = 0; while i < 3 {"
             " let r = &mut x; let t = r; i = i + 1; } let y = x; ret y; }");
}

TEST_F(BorrowCheckerTest, FreshBorrowEachIterationOk)
{
    expectOk("fn main() { let mut x = 1; let mut i = 0; while i < 3 {"
             " let r = &x; let t = r; i = i + 1; } ret 0; }");
}

TEST_F(BorrowCheckerTest, MutBorrowThenReadAfterLoop)
{
    expectOk("fn main() { let mut x = 5; let mut i = 0; while i < 2 {"
             " let r = &x; let _t = r; i = i + 1; } let y = x; ret y; }");
}

// ── C: if / else branches ──────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, BorrowInBothIfBranchesOk)
{
    expectOk("fn main() { let x = 5; let b = true; if b { let r = &x; let _t = r; }"
             " else { let r = &x; let _t = r; } ret 0; }");
}

TEST_F(BorrowCheckerTest, MutBorrowInThenEndsAtBranch)
{
    expectOk("struct S { v: i32 } fn main() { let mut p = S { v: 5 }; let b = true;"
             " if b { let r = &mut p; r.v = 9; } let y = p.v; ret y; }");
}

TEST_F(BorrowCheckerTest, BorrowsInBothBranchesIndependent)
{
    // A borrow created in one branch is scoped to that branch; both branches can
    // borrow the same value without conflict.
    expectOk("fn main() { let x = 5; let b = true; if b { let r = &x; let t = r; }"
             " else { let r = &x; let t = r; } let y = x; ret y; }");
}

// ── C: reborrow chains ─────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, ReborrowViaRefBinding)
{
    // `let s = &r` reborrows the binding r (root = r); a chain of such bindings
    // copies the reference and is legal.
    expectOk("struct S { v: i32 } fn main() { let mut p = S { v: 1 };"
             " let r = &mut p; let s = &r; let u = s; ret 0; }");
}

TEST_F(BorrowCheckerTest, ReborrowChainSharedBinding)
{
    expectOk("struct S { v: i32 } fn main() { let p = S { v: 1 };"
             " let r = &p; let s = &r; let t = &s; let u = t; ret 0; }");
}

// ── C: write-through rules (locked semantics) ──────────────────────────────────

TEST_F(BorrowCheckerTest, WriteThroughSharedRefToFieldRejected)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x;"
                " r.v = 5; }", "immutable");
}

TEST_F(BorrowCheckerTest, WriteThroughMutRefToFieldAllowed)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let r = &mut x;"
             " r.v = 5; let t = r.v; ret t; }");
}

TEST_F(BorrowCheckerTest, WriteThroughMutRefToArrayIndexAllowed)
{
    expectOk("fn main() { let mut a = [1, 2, 3]; let r = &mut a; r[1] = 9; let t = r[1]; ret t; }");
}

TEST_F(BorrowCheckerTest, WriteThroughSharedRefToArrayIndexRejected)
{
    // Indexing through a shared array reference is read-only.
    expectError("fn main() { let a = [1, 2, 3]; let r = &a; r[1] = 9; }",
        "cannot assign through a shared reference");
}

TEST_F(BorrowCheckerTest, WriteThroughMutRefToNestedFieldAllowed)
{
    expectOk("struct S { v: i32 } struct W { s: S } fn main() { let mut w = W { s: S { v: 1 } };"
             " let r = &mut w; r.s.v = 9; let t = r.s.v; ret t; }");
}

TEST_F(BorrowCheckerTest, AssignThroughSharedRefWholeRejected)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let r = &x;"
                " r = &x; }", "immutable");
}

// ── C: NLL across calls and expressions ────────────────────────────────────────

TEST_F(BorrowCheckerTest, BorrowDiesBeforeCallOk)
{
    expectOk("fn id(x: i32) -> i32 { ret x; } fn main() { let mut x = 5;"
             " { let r = &x; let t = r; } let y = id(x); ret y; }");
}

// ── C: array element borrows ───────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, ArrayElementSharedBorrowOk)
{
    expectOk("fn main() { let a = [1, 2, 3]; let r = &a[1]; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, ArrayElementMutBorrowsConflict)
{
    // `&mut a[i]` borrows the whole array root `a` (index is a `[*]`
    // projection), so two element mut borrows conflict even on distinct indices.
    expectError("fn main() { let mut a = [1, 2, 3]; let r0 = &mut a[0]; let r1 = &mut a[1];"
                " let t0 = r0; }", "borrowed as mutable");
}

TEST_F(BorrowCheckerTest, ArraySameElementMutBorrowConflict)
{
    expectError("fn main() { let mut a = [1, 2, 3]; let r0 = &mut a[0]; let r1 = &mut a[0];"
                " let t = r0; }", "borrowed as mutable");
}

TEST_F(BorrowCheckerTest, ArrayIndexBorrowAndMutateConflict)
{
    expectError("fn main() { let mut a = [1, 2, 3]; let r = &a[0]; a[0] = 9; let t = r; }",
        "borrowed");
}

TEST_F(BorrowCheckerTest, ArrayOfNonCopyElementsRejected)
{
    expectError("struct S { v: i32 } fn main() -> i32 { let a = [S { v: 1 }]; ret 0; }",
        "must be Copy");
}

// ── C: method receivers ────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, MutReceiverBorrowEndsAfterCall)
{
    // The &mut receiver borrow dies after the method call, so a later shared
    // borrow of the same value is legal.
    expectOk("struct S { v: i32 } impl S { fn add(self: &mut S) { self.v = self.v + 1; } }"
             " fn main() { let mut c = S { v: 1 }; let m = &mut c; m.add(); let r = &c; ret 0; }");
}

TEST_F(BorrowCheckerTest, MethodCallEndsReceiverBorrow)
{
    expectOk("struct S { v: i32 } impl S { fn get(self: &S) -> i32 { ret self.v; } }"
             " fn main() { let c = S { v: 1 }; let r = &c; let x = r.get(); ret x; }");
}

TEST_F(BorrowCheckerTest, SelfMutWriteAllowed)
{
    expectOk("struct S { v: i32 } impl S { fn set(self: &mut S, d: i32) { self.v = d; } }"
             " fn main() { let mut c = S { v: 1 }; c.set(9); ret 0; }");
}

TEST_F(BorrowCheckerTest, SelfSharedWriteRejected)
{
    expectError("struct S { v: i32 } impl S { fn bad(self: &S) { self.v = 9; } }"
                " fn main() { let c = S { v: 1 }; c.bad(); ret 0; }", "immutable");
}

TEST_F(BorrowCheckerTest, BorrowDoesNotBlockWriteToOtherVar)
{
    // A borrow of x does not block writes to an unrelated variable.
    expectOk("fn main() { let mut x = 5; let mut y = 1; let r = &x; y = 9; let t = r; ret 0; }");
}

// ── C: misc safety ─────────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, BorrowOfGlobalOk)
{
    expectOk("let g = 5; fn main() { let r = &g; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, MutateGlobalWhileBorrowedRejected)
{
    // Writing a global while it is borrowed is blocked. Globals use `let g` (no
    // `mut` keyword at global scope).
    expectError("let g = 5; fn main() { let r = &g; g = 6; let t = r; ret 0; }", "borrowed");
}

TEST_F(BorrowCheckerTest, NestedStructInitFieldBorrowOk)
{
    // Reading a field through a reference to a nested struct.
    expectOk("struct A { x: i32 } struct B { a: A } fn main() { let v = B { a: A { x: 1 } };"
             " let r = &v.a; let t = r.x; ret t; }");
}

TEST_F(BorrowCheckerTest, BorrowAfterFinalUseOfMovedSiblingOk)
{
    // Move one field, then borrow a disjoint field (the moved one is done).
    expectOk("struct S { v: i32, w: i32 } fn main() { let x = S { v: 1, w: 2 };"
             " let a = x.v; let r = &x.w; let t = r; ret a; }");
}

// ── C2: more NLL last-use edges ────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, NllMutBorrowEndsBeforeFieldRead)
{
    // &mut borrow used, then a field read through a fresh path — no conflict.
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 };"
             " let r = &mut x; let t = r.v; let u = x.v; ret t + u; }");
}

TEST_F(BorrowCheckerTest, NllSharedBorrowEndsAtCopy)
{
    expectOk("fn main() { let x = 5; let r = &x; let a = r; let b = x; }");
}

TEST_F(BorrowCheckerTest, NllMutBorrowReborrowedDies)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 };"
             " { let r = &mut x; let s = &r; let t = s; } let u = x.v; ret u; }");
}

TEST_F(BorrowCheckerTest, NllBorrowInExpressionDies)
{
    expectOk("fn main() { let mut x = 5; { let r = &x; let t = r; } x = 6; ret x; }");
}

TEST_F(BorrowCheckerTest, NllMutBorrowThenWholeReassign)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 };"
             " { let r = &mut x; let t = r.v; } x = S { v: 2 }; ret x.v; }");
}

TEST_F(BorrowCheckerTest, BorrowDiesBeforeReturn)
{
    expectOk("struct S { v: i32 } fn main() -> i32 { let x = S { v: 7 };"
             " let r = &x; let t = r.v; ret t; }");
}

TEST_F(BorrowCheckerTest, MutBorrowThenReturnValue)
{
    expectOk("struct S { v: i32 } fn main() -> i32 { let mut x = S { v: 3 };"
             " let r = &mut x; let t = r.v; ret t; }");
}

// ── C2: field-path read/write through refs ─────────────────────────────────────

TEST_F(BorrowCheckerTest, ReadNestedFieldThroughRef)
{
    expectOk("struct A { x: i32 } struct B { a: A } fn main() { let v = B { a: A { x: 5 } };"
             " let r = &v; let t = r.a.x; ret t; }");
}

TEST_F(BorrowCheckerTest, WriteNestedFieldThroughMutRef)
{
    expectOk("struct A { x: i32 } struct B { a: A } fn main() { let mut v = B { a: A { x: 1 } };"
             " let r = &mut v; r.a.x = 9; let t = r.a.x; ret t; }");
}

TEST_F(BorrowCheckerTest, ReadFieldThenMutBorrowSibling)
{
    expectOk("struct S { v: i32, w: i32 } fn main() { let mut x = S { v: 1, w: 2 };"
             " let a = x.v; let r = &mut x.w; let t = r; ret a; }");
}

TEST_F(BorrowCheckerTest, MutBorrowFieldThenReadSibling)
{
    expectOk("struct S { v: i32, w: i32 } fn main() { let mut x = S { v: 1, w: 2 };"
             " let r = &mut x.v; let t = x.w; let u = r; ret t; }");
}

TEST_F(BorrowCheckerTest, MutBorrowWholeThenFieldConflict)
{
    // &mut x (whole) then &x.v (field) — field is inside the whole borrow.
    expectError("struct S { v: i32 } fn main() { let mut x = S { v: 1 };"
                " let r = &mut x; let q = &x.v; let t = r.v; }", "borrowed");
}

TEST_F(BorrowCheckerTest, SharedBorrowWholeThenMutFieldConflict)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 };"
                " let r = &x; let q = &mut x.v; let t = r.v; }", "borrowed");
}

TEST_F(BorrowCheckerTest, DeepDisjointMutThreeLevels)
{
    expectOk("struct A { x: i32, y: i32 } struct B { a: A, b: A } fn main() {"
             " let mut v = B { a: A { x: 1, y: 2 }, b: A { x: 3, y: 4 } };"
             " let r = &mut v.a.x; let s = &mut v.b.y; let t = &mut v.a.y; ret 0; }");
}

// ── C2: borrowing params and locals ────────────────────────────────────────────

TEST_F(BorrowCheckerTest, BorrowParamField)
{
    expectOk("struct S { v: i32 } fn f(s: &S) -> i32 { let r = s; let t = r.v; ret t; }"
             " fn main() { let x = S { v: 1 }; ret f(&x); }");
}

TEST_F(BorrowCheckerTest, MutBorrowParamField)
{
    expectOk("struct S { v: i32 } fn f(s: &mut S) { let r = s; r.v = 5; }"
             " fn main() { let mut x = S { v: 1 }; f(&mut x); ret 0; }");
}

TEST_F(BorrowCheckerTest, ReborrowParamAsLocal)
{
    expectOk("struct S { v: i32 } fn f(s: &S) -> i32 { let r = &s; let t = r; ret 0; }"
             " fn main() { let x = S { v: 1 }; ret f(&x); }");
}

TEST_F(BorrowCheckerTest, BorrowOfStructLiteralField)
{
    expectOk("struct S { v: i32 } fn main() { let r = &(S { v: 1 }); let t = r.v; ret t; }");
}

TEST_F(BorrowCheckerTest, BorrowTempResultOfCall)
{
    expectOk("fn make() -> i32 { ret 5; } fn main() { let r = &make(); let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, ParamBorrowLifetimeIndependent)
{
    expectOk("struct S { v: i32 } fn f(s: &S) -> i32 { ret s.v; }"
             " fn main() { let a = S { v: 1 }; let b = S { v: 2 };"
             " let x = f(&a); let y = f(&b); ret x + y; }");
}

// ── C2: arrays and refs ────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, ArrayWholeRefThenMutateOtherVar)
{
    expectOk("fn main() { let a = [1, 2, 3]; let mut b = 0; let r = &a; b = 9; let t = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, ArraySharedThenMutArrayConflict)
{
    expectError("fn main() { let mut a = [1, 2, 3]; let r = &a; let q = &mut a; let t = r; }",
        "borrowed");
}

TEST_F(BorrowCheckerTest, ArrayMutThenSharedConflict)
{
    expectError("fn main() { let mut a = [1, 2, 3]; let r = &mut a; let q = &a; let t = r; }",
        "borrowed as mutable");
}

TEST_F(BorrowCheckerTest, ArrayElementSharedThenWholeMutConflict)
{
    // `&a[0]` borrows the array root, so a whole-array &mut conflicts.
    expectError("fn main() { let mut a = [1, 2, 3]; let r = &a[0]; let q = &mut a; let t = r; }",
        "borrowed");
}

TEST_F(BorrowCheckerTest, ArrayTwoSharedElementBorrowsOk)
{
    expectOk("fn main() { let a = [1, 2, 3]; let r0 = &a[0]; let r1 = &a[1]; let t = r0; ret 0; }");
}

TEST_F(BorrowCheckerTest, ArrayRefReadElementThroughIndex)
{
    expectOk("fn main() { let a = [4, 5, 6]; let r = &a; let t = r[2]; ret t; }");
}

// ── C2: if/else and moves ──────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, MoveInOneBranchUseOtherRejected)
{
    // A move in the then-branch makes the else-branch's use of x unsound
    // (flow-sensitive move tracking rejects it even though the branches are
    // exclusive at runtime).
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let b = true;"
                " if b { let y = x; ret 0; } else { let t = x.v; ret t; } }", "moved");
}

TEST_F(BorrowCheckerTest, MoveInBranchThenUseAfterRejected)
{
    // A move in the then-branch consumes x on that path; using x after the if
    // is unsound if the branch was taken.
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let b = true;"
                " if b { let y = x; } let t = x.v; }", "moved");
}

TEST_F(BorrowCheckerTest, BorrowInThenMoveInElse)
{
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 }; let b = true;"
                " if b { let r = &x; let t = r; } else { let y = x; }"
                " let u = x; }", "moved");
}

TEST_F(BorrowCheckerTest, MutBorrowInBothBranchesIndependent)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let b = true;"
             " if b { let r = &mut x; r.v = 2; } else { let r = &mut x; r.v = 3; }"
             " let t = x.v; ret t; }");
}

// NOTE: `let r;` (uninitialized) is not supported in this language, so a borrow
// "merged from both branches into an outer binding" is not expressible — each
// branch's borrows are scoped to the branch. No test for it.

// ── C2: misc safety ────────────────────────────────────────────────────────────

TEST_F(BorrowCheckerTest, CopyDoesNotMoveStruct)
{
    // Structs are NOT Copy — a second use is a move, rejected.
    expectError("struct S { v: i32 } fn main() { let x = S { v: 1 };"
                " let a = x; let b = x; }", "moved");
}

TEST_F(BorrowCheckerTest, CloneViaCopyPrimitive)
{
    expectOk("fn main() { let a = 5; let b = a; let c = a; let d = b + c; ret d; }");
}

TEST_F(BorrowCheckerTest, BorrowAfterCopyPrimitive)
{
    expectOk("fn main() { let a = 5; let b = a; let r = &a; let t = r; ret b; }");
}

TEST_F(BorrowCheckerTest, MutBorrowThenPrimitiveCopy)
{
    expectOk("fn main() { let mut a = 5; let r = &mut a; let t = r; let b = a; ret 0; }");
}

TEST_F(BorrowCheckerTest, BorrowChainThroughFunctionArg)
{
    expectOk("fn take(s: &S) -> i32 { ret s.v; } struct S { v: i32 }"
             " fn main() { let x = S { v: 7 }; let r = &x; let t = take(r); ret t; }");
}

TEST_F(BorrowCheckerTest, ReassignBorrowedVarConflict)
{
    expectError("fn main() { let mut x = 5; let r = &x; x = 6; let t = r; }", "borrowed");
}

TEST_F(BorrowCheckerTest, UnusedMutBorrowDoesNotBlockRead)
{
    expectOk("fn main() { let mut x = 5; let r = &mut x; let t = x; ret t; }");
}

TEST_F(BorrowCheckerTest, MutBorrowThenMoveOtherVar
)
{
    expectOk("struct S { v: i32 } fn main() { let mut x = S { v: 1 }; let y = S { v: 2 };"
             " let r = &mut x; let z = y; ret 0; }");
}

TEST_F(BorrowCheckerTest, TwoStructsIndependentBorrows
)
{
    expectOk("struct S { v: i32 } fn main() { let mut a = S { v: 1 }; let mut b = S { v: 2 };"
             " let r = &mut a; let s = &mut b; ret 0; }");
}

TEST_F(BorrowCheckerTest, BorrowWholeThenUseFieldAfterDeath
)
{
    expectOk("struct S { v: i32 } fn main() { let x = S { v: 1 };"
             " { let r = &x; let t = r.v; } let u = x.v; ret u; }");
}

// NOTE: field access through a REF-TO-REF (`&&S`) is not supported — the member
// access checker rejects "member access on non-struct type '&S'". A ref-to-ref
// can only be copied (`let s = &r; let u = s;`), which the reborrow tests cover.

TEST_F(BorrowCheckerTest, ParamBorrowWriteThroughMut
)
{
    expectOk("struct S { v: i32 } fn f(s: &mut S) { s.v = 9; }"
             " fn main() { let mut x = S { v: 1 }; f(&mut x); ret x.v; }");
}

TEST_F(BorrowCheckerTest, ParamSharedWriteRejected
)
{
    expectError("struct S { v: i32 } fn f(s: &S) { s.v = 9; }"
                " fn main() { let x = S { v: 1 }; f(&x); ret 0; }", "immutable");
}

TEST_F(BorrowCheckerTest, BorrowLocalRefToCopy
)
{
    expectOk("fn main() { let x = 5; let r = &x; let s = r; ret 0; }");
}

TEST_F(BorrowCheckerTest, MutBorrowThenReadDifferentField
)
{
    expectOk("struct S { v: i32, w: i32 } fn main() { let mut x = S { v: 1, w: 2 };"
             " let r = &mut x.v; let t = x.w; let u = r; ret t; }");
}
