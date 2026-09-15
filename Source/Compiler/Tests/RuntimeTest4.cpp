// Split out of RuntimeTest.cpp: the fixture and the helpers live in
// RuntimeTestFixture.hpp, and each runtime test TU compiles in PARALLEL
// (see TestModule.py). Keep new tests in whichever file fits; the split is
// purely about compile time.

#include "RuntimeTestFixture.hpp"


// ── P4 regression: cast narrowing must use explicit bit widths ─────────────────
// The old check compared PrimKind enum ordinals, silently depending on the enum
// being declared in width order. These pin the widths explicitly.

TEST_F(RuntimeTest, CastI64ToI32NarrowingRejected)
{
    expectCompileFail("fn f(x: i64) -> i32 { ret x as i32; } fn main() -> i32 { ret f(1); }",
        "smaller integer type");
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

TEST_F(RuntimeTest, CastI8ToCharAllowed)
{
    // Byte read back as char (the cast the heap buffer's element reads use).
    expectRun("fn main() -> i32 { let x = 'x' as i8; let c = x as char; ret c as i32; }", 120);
}

TEST_F(RuntimeTest, SharedRefReceiverReadsThrough)
{
    expectRun("struct counter { pub value: i32 } impl counter {"
              " fn new(v: i32) -> counter { ret counter { value: v }; }"
              " fn get(self: &counter) -> i32 { ret self.value; } }"
              " fn main() -> i32 { let c = counter::new(7); let r = &c; ret r.get(); }",
        7);
}

TEST_F(RuntimeTest, PartialMoveDropNoDoubleFree)
{
    // Borrow field a, move sibling b, scope-end drop must not double-free.
    expectRun("struct Inner { pub v: i32 } impl Drop for Inner { fn drop(self) {} }"
              " struct Pair { pub a: Inner, pub b: Inner }"
              " fn main() -> i32 { let mut p = Pair { a: Inner{v:1}, b: Inner{v:2} };"
              " let r = &p.a; let v = p.b; let w = r.v; ret w; }",
        1);
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

TEST_F(RuntimeTest, EnumMatchUnitVariantWildcard)
{
    expectRun("enum flag { on, off } fn main() -> i32 {"
              " let o = flag::off;"
              " match o { on => { ret 1; }, _ => { ret 0; }, }"
              " }",
        0);
}

TEST_F(RuntimeTest, EnumMatchExpression)
{
    // `let y = match ...` — a value match with tail expressions.
    expectRunWithPrologue("enum Option<T> { Some(T), None } fn main() -> i32 {"
                          " let o = Option::Some(7);"
                          " let y = match o { Some(v) => v + 1, None => 0 };"
                          " ret y; }",
        "",
        8);
}

TEST_F(RuntimeTest, PrintStringAndChar)
{
    expectOutput("fn main() -> i32 { print_str(\"hi\"); print_char('!'); println(); ret 0; }",
        "hi!\n",
        0);
}

TEST_F(RuntimeTest, MathAbsClamp)
{
    expectRun("fn main() -> i32 { ret abs(0 - 5) + clamp(12, 0, 10); }", 15);
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

TEST_F(RuntimeTest, GenericOperatorFunctionStruct)
{
    // A generic `fn sum<T: Add>` monomorphized over a struct implementing Add.
    expectRunWithPrologue("struct Vec2 { pub x: i32, pub y: i32 }"
                          " impl Add for Vec2 { fn add(self, other: Self) -> Vec2 {"
                          "   ret Vec2 { x: self.x + other.x, y: self.y + other.y }; } }"
                          " fn sum<T: Add>(a: T, b: T) -> T { ret a + b; }"
                          " fn main() -> i32 { let v = sum(Vec2{x:1,y:2}, Vec2{x:3,y:4});"
                          " ret v.x + v.y; }",
        kMathPrologue,
        10);
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

TEST_F(RuntimeTest, IteratorLastNthProduct)
{
    expectRun("fn main() -> i32 {"
              " let last_ = unwrap_or(last(range(1, 5)), 0);"    // 4
              " let nth_ = unwrap_or(nth(range(10, 20), 3), 0);" // 13
              " let prod = product(range(1, 5));"                // 24
              " ret last_ + nth_ + prod; }",
        4 + 13 + 24);
}

TEST_F(RuntimeTest, ArrayElementWrite)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; a[1] = 9; ret a[1]; }", 9);
}

TEST_F(RuntimeTest, ArrayOfCharsAndLoop)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3, 4]; let mut s = 0;"
              " let mut i = 0; while i < 4 { s = s + a[i]; i = i + 1; }"
              " a[3] = 40; ret s + a[3]; }",
        50);
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

// The heap primitives ARE the compiler's unsafe core: they take raw pointers and
// lower straight to libc malloc/free/memcpy/strlen with no bounds, lifetime or
// aliasing checking. They are therefore callable only from the standard library,
// which is what makes the rest of the language's heap use auditable. The heap
// path itself is covered end-to-end by the String tests (alloc/grow/free).
TEST_F(RuntimeTest, HeapPrimitivesAreStdlibOnly)
{
    expectCompileFail("fn main() -> i32 { let p = __alloc(8); ret 0; }",
        "can only be called from the standard library");
    expectCompileFail("fn main() -> i32 { let p = __alloc(8); __free(p); ret 0; }",
        "can only be called from the standard library");
    expectCompileFail("fn f(p: *i8) -> i32 { ret __strlen(p); } fn main() -> i32 { ret 0; }",
        "can only be called from the standard library");
}

TEST_F(RuntimeTest, PrivateFieldCannotBeConstructedOutsideItsType)
{
    expectCompileFail("struct S { v: i32 } fn main() -> i32 { let s = S { v: 1 }; ret 0; }",
        "field 'v' of 'S' is private");
}

TEST_F(RuntimeTest, PrivateFieldIsInaccessibleFromAFreeFunction)
{
    expectCompileFail("struct S { v: i32 } fn peek(s: &S) -> i32 { ret s.v; }"
                      " fn main() -> i32 { ret 0; }",
        "field 'v' of 'S' is private");
}

TEST_F(RuntimeTest, StringFieldsArePrivateButAccessorsWork)
{
    expectCompileFail("fn main() -> i32 { let s = String::from_lit(\"ab\"); ret s.len; }",
        "field 'len' of 'String' is private");
    expectRun("fn main() -> i32 { let s = String::from_lit(\"ab\"); ret s.len(); }", 2);
}

// B4: explicit `[T; 0]` is not a legal type.
TEST_F(RuntimeTest, ZeroSizeArrayTypeRejected)
{
    expectCompileFail("fn main() -> i32 { let a: [i32; 0] = [1]; ret 0; }",
        "array size must be a positive integer");
}

// C1: global initializers must be literals — else they'd silently zero-initialize.
TEST_F(RuntimeTest, GlobalArrayInitializerRejected)
{
    expectCompileFail("let g = [1, 2]; fn main() -> i32 { ret 0; }",
        "global variable initializer must be a literal");
}

// E5: builtin / libc names are reserved.
TEST_F(RuntimeTest, ReservedBuiltinNameRejected)
{
    expectCompileFail("fn __alloc(n: i32) -> i32 { ret 0; } fn main() -> i32 { ret 0; }",
        "reserved by the compiler");
}

// D1 (runtime): an out-of-bounds array index aborts the process.
TEST_F(RuntimeTest, ArrayOobAborts)
{
    ASSERT_TRUE(compile("fn main() -> i32 { let a = [1, 2, 3]; ret a[99]; }"))
        << "compilation failed";
    int code = linkAndRun();
    EXPECT_NE(code, 0) << "out-of-bounds array index must abort, got exit 0";
}

// A1: push_char grows exactly when the null terminator would overflow.
TEST_F(RuntimeTest, StringPushCharGrowsAtCap)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " let mut i = 0; while i < 16 { s.push_char('a'); i = i + 1; }"
              " ret s.len(); }",
        16);
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

TEST_F(RuntimeTest, ForInsideIfBodyExecutes)
{
    expectRun("fn main() -> i32 { let mut r = 0;"
              " if true { for x in range(1, 3) { r = r + x; } } ret r; }",
        3);
}

TEST_F(RuntimeTest, ElseIfElseBranchRuns)
{
    // The final `else` of an else-if chain must also be reachable.
    expectRun("fn main() -> i32 { let x = 50; let mut r = 0;"
              " if x < 5 { r = 1; } else if x < 20 { r = 2; } else { r = 3; }"
              " ret r; }",
        3);
}

TEST_F(RuntimeTest, IfInsideForBodyRegression)
{
    // Direction check: for body containing an if already worked; must stay.
    expectRun("fn main() -> i32 { let mut r = 0;"
              " for x in range(1, 4) { if x > 1 { r = r + x; } } ret r; }",
        5);
}

TEST_F(RuntimeTest, BoolFalseCastToIntIsZero)
{
    expectRun("fn main() -> i32 { let b = false; ret b as i32; }", 0);
}

TEST_F(RuntimeTest, BoolCastResultInArithmetic)
{
    // The cast result must be a usable 1/0 value, not -1 (which would give 9).
    expectRun("fn main() -> i32 { let b = true; ret 10 + (b as i32); }", 11);
}

TEST_F(RuntimeTest, ArithmeticParenthesesOverridePrecedence)
{
    expectRun("fn main() -> i32 { ret (2 + 3) * 4; }", 20);
}

TEST_F(RuntimeTest, ArithmeticModuloNegativeIsCstyle)
{
    // The result of `%` follows the dividend (C semantics): -17 % 5 = -2.
    // Take the magnitude so the exit code stays positive.
    expectRun("fn main() -> i32 { let a = 0 - 17; let r = a % 5;"
              " if r < 0 { ret 0 - r; } ret r; }",
        2);
}

TEST_F(RuntimeTest, I16AdditionFits)
{
    expectRun("fn main() -> i32 { let a = 'A' as i16; let b = 'A' as i16;"
              " let c = a + b; ret c as i32; }",
        130);
}

TEST_F(RuntimeTest, I64Multiplication)
{
    expectRun("fn main() -> i64 { let a = 100 as i64; let b = 20 as i64;"
              " let c = a * b; ret c; }",
        2000);
}

TEST_F(RuntimeTest, I8BitwiseAnd)
{
    // 'A'=65 (0b1000001) & 'B'=66 (0b1000010) = 0b1000000 = 64.
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'B' as i8;"
              " let c = a & b; ret c as i32; }",
        64);
}

TEST_F(RuntimeTest, FloatComparison)
{
    expectRun("fn main() -> i32 { if 3.5 > 3.0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, FloatModuloVariables)
{
    expectOutput("fn main() -> i32 { let x = 10.0; let y = 4.0; let z = x - y * 2.0;"
                 " print_float(z); println(); ret 0; }",
        "2.000000\n",
        0);
}

TEST_F(RuntimeTest, I8ToCharCast)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let c = a as char; ret c as i32; }", 65);
}

TEST_F(RuntimeTest, CharToI64Widening)
{
    expectRun("fn main() -> i64 { let c = 'A'; ret c as i64; }", 65);
}

TEST_F(RuntimeTest, IntToBoolCastRejected)
{
    // bool is not an integer target → "integer can only be cast to float or integer".
    expectCompileFail("fn main() -> i32 { let x = 1 as bool; ret 0; }",
        "can only be cast to float or integer");
}

TEST_F(RuntimeTest, WhileEvenSumUpTo20)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 20 { i = i + 1; if (i % 2) == 0 { s = s + i; } } ret s; }",
        110);
}

TEST_F(RuntimeTest, WhileBreakEarlyExitCode)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 100 { i = i + 1; if i == 10 { break; } s = s + i; } ret s; }",
        45);
}

TEST_F(RuntimeTest, ForRangeSum1To10)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 11) { s = s + x; } ret s; }", 55);
}

TEST_F(RuntimeTest, ForContinueSkipsThree)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 6) {"
              " if x == 3 { continue; } s = s + x; } ret s; }",
        12);
}

// ── B: recursion ───────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, RecursionFactorial)
{
    expectRun("fn fact(n: i32) -> i32 { if n <= 1 { ret 1; } ret n * fact(n - 1); }"
              " fn main() -> i32 { ret fact(5); }",
        120);
}

TEST_F(RuntimeTest, RecursionNestedDepth)
{
    // Count nesting depth via two mutually-independent recursive helpers.
    expectRun("fn down(n: i32) -> i32 { if n == 0 { ret 0; } ret 1 + down(n - 1); }"
              " fn main() -> i32 { ret down(3) + down(4); }",
        7);
}

TEST_F(RuntimeTest, FunctionPointerMoveSemantics)
{
    // A function reference is a value: `let b = a` MOVES it, leaving `a`
    // unusable (single-owner). Calling through the moved binding works.
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn main() -> i32 { let a = dbl;"
              " let b = a; ret b(5); }",
        10);
}

TEST_F(RuntimeTest, BitOrAssociativity)
{
    // 1 | 2 = 3, 3 | 4 = 7.
    expectRun("fn main() -> i32 { ret 1 | 2 | 4; }", 7);
}

// ── B: enums and match ─────────────────────────────────────────────────────────

TEST_F(RuntimeTest, EnumUnitDispatchValue)
{
    expectRun("enum E { A, B, C } fn main() -> i32 { let e = E::B;"
              " let y = match e { A => 1, B => 2, C => 3 }; ret y; }",
        2);
}

TEST_F(RuntimeTest, EnumValueArmNoBlock)
{
    expectRun("enum E { A, B } fn main() -> i32 { let e = E::A;"
              " let y = match e { A => 10, B => 20 }; ret y; }",
        10);
}

TEST_F(RuntimeTest, EnumWithPartialEqImpl)
{
    expectRun("enum E { A, B } impl PartialEq for E { fn eq(self, o: Self) -> bool {"
              " ret true; } fn ne(self, o: Self) -> bool { ret false; } }"
              " fn main() -> i32 { let e = E::A; if e == E::A { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, GlobalWrite)
{
    expectRun("let g = 1; fn main() -> i32 { g = g + 5; ret g; }", 6);
}

TEST_F(RuntimeTest, GlobalMutatedInFunction)
{
    expectRun("let g = 0; fn bump() { g = g + 1; } fn main() -> i32 {"
              " bump(); bump(); bump(); ret g; }",
        3);
}

// ── B: generics ────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GenericIdentityFunction)
{
    expectRun("fn id<T>(x: T) -> T { ret x; } fn main() -> i32 { ret id(42); }", 42);
}

TEST_F(RuntimeTest, GenericStructMethod)
{
    expectRun("struct W<T> { pub v: T } impl W<T> { fn get(self) -> T { ret self.v; } }"
              " fn main() -> i32 { let w = W { v: 8 }; ret w.get(); }",
        8);
}

// ── B: arrays ──────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, ArrayBasicIndex)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3]; ret a[0] + a[2]; }", 4);
}

TEST_F(RuntimeTest, ArraySingleElement)
{
    expectRun("fn main() -> i32 { let a = [42]; ret a[0]; }", 42);
}

TEST_F(RuntimeTest, StringFromLiteral)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"hi\"); ret s.len(); }", 2);
}

TEST_F(RuntimeTest, StringGrowPastCap)
{
    // 30 pushes forces multiple buffer reallocations (cap starts at 16).
    expectRun("fn main() -> i32 { let mut s = String::new(); let mut i = 0;"
              " while i < 30 { s.push_char('a'); i = i + 1; } ret s.len(); }",
        30);
}

TEST_F(RuntimeTest, StringPushStrThenIndex)
{
    expectRun("fn main() -> i32 { let mut s = String::from_lit(\"ab\");"
              " s.push_str(\"cde\"); match s.index(4) {"
              " Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        101);
}

TEST_F(RuntimeTest, OpOverloadDivision)
{
    expectRun("struct V { pub x: i32 } impl Div for V { fn div(self, o: Self) -> V {"
              " ret V { x: self.x / o.x }; } } fn main() -> i32 {"
              " let a = V { x: 20 }; let b = V { x: 4 }; ret (a / b).x; }",
        5);
}

TEST_F(RuntimeTest, OpOverloadEqual)
{
    expectRun("struct V { pub x: i32 } impl PartialEq for V { fn eq(self, o: Self) -> bool {"
              " ret self.x == o.x; } fn ne(self, o: Self) -> bool { ret self.x != o.x; } }"
              " fn main() -> i32 { let a = V { x: 5 }; let b = V { x: 5 };"
              " if a == b { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, OptionIsSomeOnNone)
{
    // `Option::None` standalone can't infer T — pin it via a return-type helper.
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let o = none_i(); if is_some(o) { ret 1; } ret 0; }",
        0);
}

TEST_F(RuntimeTest, OptionUnwrapOrSome)
{
    expectRun("fn main() -> i32 { ret unwrap_or(Option::Some(7), 0); }", 7);
}

TEST_F(RuntimeTest, OptionAndSomeSome)
{
    // and(Some(1), Some(5)) → Some(5).
    expectRun("fn main() -> i32 { let a = and(Option::Some(1), Option::Some(5));"
              " match a { Some(v) => { ret v; }, None => { ret 0; } } }",
        5);
}

TEST_F(RuntimeTest, OptionFunctionsChain)
{
    expectRun("fn main() -> i32 { let o = Option::Some(3);"
              " let u = unwrap_or(o, 100); let s = is_some(Option::Some(u));"
              " if s { ret u; } ret 0; }",
        3);
}

TEST_F(RuntimeTest, MathMaxBasic)
{
    expectRun("fn main() -> i32 { ret max(3, 7); }", 7);
}

TEST_F(RuntimeTest, MathClampWithin)
{
    expectRun("fn main() -> i32 { ret clamp(5, 0, 10); }", 5);
}

TEST_F(RuntimeTest, MathAbsPositive)
{
    expectRun("fn main() -> i32 { ret abs(7); }", 7);
}

TEST_F(RuntimeTest, MathFabsNegative)
{
    expectOutput("fn main() -> i32 { print_float(fabs(0.0 - 3.5)); println(); ret 0; }",
        "3.500000\n",
        0);
}

TEST_F(RuntimeTest, MathGcdOneIsOne)
{
    expectRun("fn main() -> i32 { ret gcd(1, 100); }", 1);
}

TEST_F(RuntimeTest, MathLcmWithZero)
{
    expectRun("fn main() -> i32 { ret lcm(0, 5); }", 0);
}

TEST_F(RuntimeTest, MathIpowSmallBase)
{
    expectRun("fn main() -> i32 { ret ipow(3, 3); }", 27);
}

TEST_F(RuntimeTest, MathIsEvenNegative)
{
    expectRun("fn main() -> i32 { if is_even(0 - 4) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathSignPositive)
{
    expectRun("fn main() -> i32 { ret sign(5); }", 1);
}

TEST_F(RuntimeTest, MathSignZero)
{
    expectRun("fn main() -> i32 { ret sign(0); }", 0);
}

TEST_F(RuntimeTest, MathLerpStart)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 0.0)); println(); ret 0; }",
        "0.000000\n",
        0);
}

TEST_F(RuntimeTest, MathLerpBeyondRange)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 2.0)); println(); ret 0; }",
        "20.000000\n",
        0);
}

TEST_F(RuntimeTest, CharIsAlphaUpper)
{
    expectRun("fn main() -> i32 { if is_alpha('Z') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsAlphanumericPunct)
{
    expectRun("fn main() -> i32 { if is_alphanumeric('!') { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, CharIsWhitespaceLetter)
{
    expectRun("fn main() -> i32 { if is_whitespace('a') { ret 1; } ret 0; }", 0);
}

// ── E: iterator.lis ────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, IteratorRangeSum1To5)
{
    expectRun("fn main() -> i32 { ret sum(range(1, 5)); }", 10);
}

TEST_F(RuntimeTest, IteratorFirstSome)
{
    expectRun("fn main() -> i32 { let f = first(range(1, 5));"
              " match f { Some(v) => { ret v; }, None => { ret 0; } } }",
        1);
}

TEST_F(RuntimeTest, IteratorNthValid)
{
    expectRun("fn main() -> i32 { let n = nth(range(10, 20), 3);"
              " match n { Some(v) => { ret v; }, None => { ret 0; } } }",
        13);
}

TEST_F(RuntimeTest, IteratorProductEmpty)
{
    expectRun("fn main() -> i32 { ret product(range(3, 3)); }", 1);
}

TEST_F(RuntimeTest, ExampleGenericType)
{
    expectExample("generic_type", 0);
}

TEST_F(RuntimeTest, ExampleMatch)
{
    expectExample("match", 8);
}

TEST_F(RuntimeTest, ExampleOperator)
{
    expectExample("operator", 41);
}

TEST_F(RuntimeTest, ExamplePrint)
{
    expectExample("print", 0);
}

TEST_F(RuntimeTest, DropGenericType)
{
    expectRun("struct Box<T> { pub v: T } impl Drop for Box<i32> { fn drop(self) { } }"
              " fn main() -> i32 { let b = Box { v: 5 }; ret b.v; }",
        5);
}

TEST_F(RuntimeTest, DropFieldWithNestedDrop)
{
    expectRun("struct Inner { pub v: i32 } impl Drop for Inner { fn drop(self) { } }"
              " struct Outer { pub i: Inner, pub v: i32 } impl Drop for Outer { fn drop(self) { } }"
              " fn main() -> i32 { let o = Outer { i: Inner { v: 1 }, v: 2 }; ret o.v; }",
        2);
}

TEST_F(RuntimeTest, EnumMatchExpressionValue)
{
    expectRun("enum E { A, B } fn main() -> i32 { let e = E::B;"
              " ret match e { A => 1, B => 2 }; }",
        2);
}

TEST_F(RuntimeTest, EnumPayloadTupleMatch)
{
    expectRun("enum E { P(i32, i32), Q } fn main() -> i32 { let e = E::P(2, 3);"
              " match e { P(a, b) => { ret a * b; }, Q => { ret 0; } } }",
        6);
}

// ── B2: more strings ───────────────────────────────────────────────────────────

TEST_F(RuntimeTest, StringEmptyNewIndex)
{
    expectRun("fn main() -> i32 { let s = String::new();"
              " match s.index(0) { Some(c) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, StringDoubleAppend)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " s.push_str(\"ab\"); s.push_str(\"cd\"); ret s.len(); }",
        4);
}

TEST_F(RuntimeTest, StringNewThenPushStrThenIndex)
{
    expectRun("fn main() -> i32 { let mut s = String::new(); s.push_str(\"xyz\");"
              " match s.index(1) { Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        121);
}

TEST_F(RuntimeTest, OpOverloadBitOr)
{
    expectRun("struct M { pub x: i32 } impl BitOr for M { fn bitor(self, o: Self) -> M {"
              " ret M { x: self.x | o.x }; } } fn main() -> i32 {"
              " let a = M { x: 6 }; let b = M { x: 3 }; ret (a | b).x; }",
        7);
}

TEST_F(RuntimeTest, OpOverloadMixedWithPrimitive)
{
    expectRun("struct V { pub x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } fn main() -> i32 {"
              " let a = V { x: 10 }; let b = V { x: 5 }; let v = a + b;"
              " ret v.x + 100; }",
        115);
}

TEST_F(RuntimeTest, I64ReturnFromCastChain)
{
    expectRun("fn main() -> i64 { let a = 'A' as i8; let b = a as i64; ret b; }", 65);
}

TEST_F(RuntimeTest, FloatCompareMixed)
{
    expectRun("fn main() -> i32 { if 2.5 < 3.5 && 3.5 < 4.5 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, GlobalModifiedInLoop)
{
    expectRun("let g = 0; fn main() -> i32 { let mut i = 0;"
              " while i < 5 { g = g + 1; i = i + 1; } ret g; }",
        5);
}

// ── B2: more generics ──────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GenericSwapLikeViaStruct)
{
    expectRun("struct Pair<T> { pub a: T, pub b: T } fn main() -> i32 {"
              " let p = Pair { a: 1, b: 2 }; let q = Pair { a: p.b, b: p.a }; ret q.a; }",
        2);
}

TEST_F(RuntimeTest, WhileNestedContinue)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 10 { i = i + 1; if (i % 3) == 0 { continue; } s = s + i; } ret s; }",
        37);
}

TEST_F(RuntimeTest, ForIteratingComputedRange)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(2, 7) { s = s + x; } ret s; }", 20);
}

TEST_F(RuntimeTest, ArrayReferenceReassignElement)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; let r = &mut a;"
              " r[0] = 9; ret r[0]; }",
        9);
}

TEST_F(RuntimeTest, DeepArithmeticNesting)
{
    expectRun("fn main() -> i32 { ret ((1 + 2) * (3 + 4)) - 5; }", 16);
}

TEST_F(RuntimeTest, MultiStatementFunction)
{
    expectRun("fn main() -> i32 { let a = 1; let b = 2; let c = a + b;"
              " let d = c * 3; ret d; }",
        9);
}

TEST_F(RuntimeTest, ArithmeticUnaryViaSubtract)
{
    expectRun("fn main() -> i32 { let a = 0 - 10; ret 0 - a; }", 10);
}

TEST_F(RuntimeTest, ComparisonChainVariables)
{
    expectRun("fn main() -> i32 { let a = 3; let b = 5; let c = 7;"
              " if a < b && b < c { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, FloatCompareGreater)
{
    expectRun("fn main() -> i32 { if 7.5 >= 7.5 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharRangeCheck)
{
    expectRun("fn main() -> i32 { let c = 'm';"
              " if c >= 'a' && c <= 'z' { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, EnumValueArmWithComputation)
{
    expectRun("enum E { A, B } fn main() -> i32 { let e = E::B;"
              " ret match e { A => 10 * 2, B => 20 + 1 }; }",
        21);
}

TEST_F(RuntimeTest, EnumMatchConsumesScrutinee)
{
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 {"
              " let o = O::Some(5); let y = match o { Some(v) => v, None => 0 }; ret y; }",
        5);
}

TEST_F(RuntimeTest, StringMutateThenReadBack)
{
    expectRun("fn main() -> i32 { let mut s = String::from_lit(\"abc\");"
              " s.push_str(\"z\"); match s.index(3) { Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        122);
}

TEST_F(RuntimeTest, GenericDoubleParam)
{
    expectRun("fn add2<T: Numeric>(a: T, b: T) -> T { ret a + b; }"
              " fn main() -> i32 { ret add2(3, 4); }",
        7);
}

TEST_F(RuntimeTest, GenericFunctionOnI64)
{
    expectRun("fn dbl<T: Numeric>(x: T) -> T { ret x + x; }"
              " fn main() -> i64 { let a = 4 as i64; ret dbl(a); }",
        8);
}

TEST_F(RuntimeTest, GlobalCharUsed)
{
    expectRun("let letter = 'q'; fn main() -> i32 { ret letter as i32; }", 113);
}

TEST_F(RuntimeTest, ArrayComparisonElement)
{
    expectRun("fn main() -> i32 { let a = [1, 5]; if a[0] < a[1] { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, OpOverloadFloat)
{
    expectOutput("struct V { pub x: f64 } impl Add for V { fn add(self, o: Self) -> V {"
                 " ret V { x: self.x + o.x }; } } fn main() -> i32 {"
                 " let a = V { x: 1.5 }; let b = V { x: 2.5 };"
                 " print_float((a + b).x); println(); ret 0; }",
        "4.000000\n",
        0);
}

TEST_F(RuntimeTest, WhileCountingDown)
{
    expectRun("fn main() -> i32 { let mut n = 10; let mut s = 0;"
              " while n > 0 { s = s + n; n = n - 1; } ret s; }",
        55);
}

TEST_F(RuntimeTest, NestedWhileTwoCounters)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut pairs = 0;"
              " while i < 3 { let mut j = 0; while j < 2 { pairs = pairs + 1; j = j + 1; }"
              " i = i + 1; } ret pairs; }",
        6);
}

TEST_F(RuntimeTest, CompareStringsViaLen)
{
    expectRun("fn main() -> i32 { let a = String::from_lit(\"aa\");"
              " let b = String::from_lit(\"bbb\"); if a.len() < b.len() { ret 1; } ret 0; }",
        1);
}

// NOTE: matching a struct FIELD that holds an enum (`match s.e`) is not
// expressible — the match scrutinee must be a value binding, not a field access.
// No test for it.

TEST_F(RuntimeTest, GlobalCounterInMatch)
{
    expectRun("let hits = 0; enum E { A, B } fn main() -> i32 {"
              " let e = E::A; match e { A => { hits = hits + 1; }, B => { hits = hits + 2; } }"
              " ret hits; }",
        1);
}

// NOTE: arrays of structs are rejected (locked semantics: array elements are
// Copy-only, and structs are not Copy) — covered by ArrayOfNonCopyElementsRejected.

TEST_F(RuntimeTest, FloatLerpComposition)
{
    expectOutput("fn main() -> i32 { print_float(lerp(lerp(0.0, 10.0, 0.5), 100.0, 0.5));"
                 " println(); ret 0; }",
        "52.500000\n",
        0);
}

TEST_F(RuntimeTest, GlobalIncrementFunction)
{
    expectRun("let g = 0; fn bump() { g = g + 1; } fn main() -> i32 {"
              " bump(); bump(); bump(); ret g; }",
        3);
}

TEST_F(RuntimeTest, FloatDivisionPrecision)
{
    expectOutput("fn main() -> i32 { print_float(10.0 / 4.0); println(); ret 0; }",
        "2.500000\n",
        0);
}

TEST_F(RuntimeTest, ArraySumThroughRef)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3]; let r = &a;"
              " ret r[0] + r[1] + r[2]; }",
        6);
}

TEST_F(RuntimeTest, RecursionWithAccumulator)
{
    expectRun("fn sumto(n: i32, acc: i32) -> i32 { if n == 0 { ret acc; }"
              " ret sumto(n - 1, acc + n); } fn main() -> i32 { ret sumto(10, 0); }",
        55);
}

// (Enum-as-struct-field match is not expressible — see the note above.)

TEST_F(RuntimeTest, WhileWithCompoundCondition)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 6 && s < 12 { i = i + 1; s = s + i; } ret s; }",
        15);
}

TEST_F(RuntimeTest, GenericEnumMethodOnValue)
{
    // Inline construction pins T for the generic unwrap_or.
    expectRun("fn main() -> i32 { ret unwrap_or(Option::Some(9), 0); }", 9);
}

TEST_F(RuntimeTest, BoolLiteralInMatch)
{
    expectRun("enum E { A, B } fn main() -> i32 { let e = E::B;"
              " let b = match e { A => true, B => false }; if b { ret 1; } ret 0; }",
        0);
}

TEST_F(RuntimeTest, StringEmptyLenAfterNew)
{
    expectRun("fn main() -> i32 { let s = String::new(); if s.len() == 0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleArithmeticSub)
{
    expectRun("fn main() -> i32 { ret 50 - 12; }", 38);
}

TEST_F(RuntimeTest, SimpleComparisonEq)
{
    expectRun("fn main() -> i32 { if 5 == 5 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleIfTrue)
{
    expectRun("fn main() -> i32 { if true { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleFunctionCall)
{
    expectRun("fn add(a: i32, b: i32) -> i32 { ret a + b; } fn main() -> i32 { ret add(2, 3); }", 5);
}

TEST_F(RuntimeTest, SimpleCharLiteral)
{
    expectRun("fn main() -> i32 { ret 'A' as i32; }", 65);
}

TEST_F(RuntimeTest, SimpleNegativeViaSubtract)
{
    expectRun("fn main() -> i32 { let a = 0 - 3; if a < 0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleDoubleNestedLoops)
{
    expectRun("fn main() -> i32 { let mut n = 0; for i in range(1, 3) {"
              " for j in range(1, 3) { n = n + 1; } } ret n; }",
        4);
}

TEST_F(RuntimeTest, ModuleAliasImport)
{
    ASSERT_TRUE(compileMulti(
        "impt math_lib as m;\n"
        "fn main() -> i32 { ret m::double_it(21); }",
        {{"math_lib", "fn double_it(x: i32) -> i32 { ret x * 2; }"}}));
    EXPECT_EQ(linkAndRun(), 42);
}

TEST_F(RuntimeTest, ModuleTypeIsolation)
{
    // Two modules each define their own `struct Vec2` and `fn make` — the
    // module prefix keeps them from colliding.
    ASSERT_TRUE(compileMulti(
        "impt a;\n"
        "impt b;\n"
        "fn main() -> i32 { ret a::make(3) + b::make(4); }",
        {{"a", "struct Vec2 { pub x: i32 }\nfn make(v: i32) -> i32 { ret v; }"},
            {"b", "struct Vec2 { pub x: i32 }\nfn make(v: i32) -> i32 { ret v * 10; }"}}));
    EXPECT_EQ(linkAndRun(), 43);
}

// ── G: panic / never (the uninhabited type) ───────────────────────────────────
//
// panic(&i8) is a compiler builtin: it writes "panicked: <msg>" to stderr and
// calls libc abort(). Its result type is never, the uninhabited type, so it
// type-checks in ANY position (an argument, a return value, a match arm) while
// never producing a value. Everything after a diverging call in the same
// statement list is unreachable, and MIRBuilder::emit() drops it.
//
// ASSERTION NOTE: stdout must not be compared around a panic. abort() does not
// flush stdio, so whatever the program printed before diverging is lost; stderr
// is the observable channel (see expectPanic).

TEST_F(RuntimeTest, PanicAbortsWithMessage)
{
    expectPanic("fn main() -> i32 { panic(\"boom\"); ret 0; }", "panicked: boom");
}

// When EVERY value arm diverges the match itself is never and still coerces to
// the declared return type. (This used to store a valueless operand and trap
// inside LLVM.)
TEST_F(RuntimeTest, AllDivergingArmsMakeTheMatchNever)
{
    expectPanic("fn pick(o: Option<i32>) -> i32 {\n"
                "    ret match o { Some(v) => panic(\"some\"), None => panic(\"none\"), };\n"
                "}\n"
                "fn main() -> i32 { let o = Option::Some(1); ret pick(o); }",
        "panicked: some");
}

// Bottom coercion is uniform across the value positions: a never-valued
// initialiser, assignment and array element all satisfy the surrounding type.
TEST_F(RuntimeTest, NeverCoercesInLetInitializer)
{
    expectPanic("fn main() -> i32 { let x: i32 = panic(\"let-init\"); ret 0; }",
        "panicked: let-init");
}

TEST_F(RuntimeTest, NeverFunctionDivergingInsideWhileTrue)
{
    expectPanic("fn spin() -> never {\n"
                "    let mut i = 0;\n"
                "    while true { i = i + 1; if i > 3 { panic(\"stop\"); } }\n"
                "}\n"
                "fn main() -> i32 { spin(); ret 0; }",
        "panicked: stop");
}

TEST_F(RuntimeTest, UninferredGenericArgumentInLetRejected)
{
    expectCompileFail("fn main() -> i32 { let n: Option<i32> = Option::None; ret 0; }",
        "cannot infer the generic argument(s) of 'Option'");
    expectCompileFail("fn main() -> i32 { let n = Option::None; ret 0; }",
        "cannot infer the generic argument(s) of 'Option'");
}

TEST_F(RuntimeTest, PanicNameIsReserved)
{
    expectCompileFail("fn panic() { }\nfn main() -> i32 { ret 0; }",
        "is reserved by the compiler");
}

TEST_F(RuntimeTest, OptionUnwrapOnNoneAborts)
{
    expectPanic("fn nothing() -> Option<i32> { ret Option::None; }\n"
                "fn main() -> i32 { let n = nothing(); ret n.unwrap(); }",
        "panicked: called unwrap on a None value");
}

// A non-Copy payload is moved out of the enum and owned by the caller
// (exactly one drop of the String buffer).
TEST_F(RuntimeTest, OptionUnwrapNonCopyPayload)
{
    expectOutput("fn main() -> i32 {\n"
                 "    let o = Option::Some(String::from_lit(\"payload\"));\n"
                 "    let s = o.unwrap();\n"
                 "    print_str(s.to_cstr());\n"
                 "    println();\n"
                 "    ret 0;\n"
                 "}",
        "payload\n", 0);
}

TEST_F(RuntimeTest, GenericEnumTwoTypesThroughGenericFunction)
{
    expectRun("enum Pair<A, B> { Both(A, B), Neither }\n"
              "fn pick<X, Y>(p: Pair<X, Y>) -> X {\n"
              "    match p { Both(a, b) => { ret a; }, Neither => { panic(\"empty\"); } }\n"
              "}\n"
              "fn main() -> i32 { let p = Pair::Both(9, 'y'); ret pick(p); }",
        9);
}

// ── J: definite assignment (a let without an initializer) ────────────────────
//
// Option B of the spec decision: a Move binding must have an initializer (the
// scope-exit drop would otherwise release an unconstructed value); a Copy binding
// may be declared without one, but every use must be definitely assigned —
// flow-merged with AND across if/match, conservatively reset across a loop.

TEST_F(RuntimeTest, UninitCopyBindingAssignThenRead)
{
    expectRun("fn main() -> i32 { let mut x: i32; x = 5; ret x; }", 5);
}

// The loop body may run zero times, so an assignment inside it does not make the
// binding definitely assigned afterwards (conservative, like Rust).
TEST_F(RuntimeTest, UninitAssignedOnlyInsideLoopRejected)
{
    expectCompileFail("fn main() -> i32 { let mut x: i32; let mut i = 0;\n"
                      "    while i < 3 { x = 1; i = i + 1; }\n"
                      "    ret x; }",
        "use of uninitialized value");
}

// Writing a FIELD through an uninitialized reference needs the binding first.
// Since `&mut T` stopped being Copy the code is rejected even earlier: the
// binding itself cannot be declared without an initializer (E3012).
TEST_F(RuntimeTest, UninitFieldWriteThroughUninitRefRejected)
{
    expectCompileFail("struct P { pub v: i32 }\n"
                      "fn main() -> i32 { let mut r: &mut P; r.v = 1; ret 0; }",
        "without an initializer");
}

// The Err path returns Err(e) from the enclosing function immediately: the
// statements after the operator must not run (the second one would otherwise add).
TEST_F(RuntimeTest, TryOperatorPropagatesErrAndSkipsRest)
{
    expectRun("fn bad() -> Result<i32, i32> { ret Result::Err(7); }\n"
              "fn f() -> Result<i32, i32> {\n"
              "    let v = bad()?;\n"
              "    let w = bad()?;\n"
              "    ret Result::Ok(v + w);\n"
              "}\n"
              "fn main() -> i32 { let r = f();\n"
              "    match r { Ok(v) => { ret v; }, Err(e) => { ret e; } } }",
        7);
}

// Non-Copy payloads travel through the operator on both paths exactly once.
TEST_F(RuntimeTest, TryOperatorNonCopyPayloads)
{
    expectOutput("fn make() -> Result<String, String> { ret Result::Ok(String::from_lit(\"payload\")); }\n"
                 "fn f() -> Result<String, String> { let a = make()?; ret Result::Ok(a); }\n"
                 "fn main() -> i32 { let r = f();\n"
                 "    match r { Ok(v) => { print_str(v.to_cstr()); }, Err(e) => { print_str(e.to_cstr()); } }\n"
                 "    println();\n"
                 "    ret 0;\n"
                 "}",
        "payload\n", 0);
    expectOutput("fn bad() -> Result<i32, String> { ret Result::Err(String::from_lit(\"inner\")); }\n"
                 "fn f() -> Result<i32, String> { let v = bad()?; ret Result::Ok(v); }\n"
                 "fn main() -> i32 { let r = f();\n"
                 "    match r { Ok(v) => { print_int(v); }, Err(e) => { print_str(e.to_cstr()); } }\n"
                 "    println();\n"
                 "    ret 0;\n"
                 "}",
        "inner\n", 0);
}

// Minimal JIT sanity check: a hand-built module that returns 42, with no
// dependency on the Lis compiler. If THIS crashes, the JIT setup itself is the
// problem rather than the module the compiler produced.
TEST_F(RuntimeTest, JitTrivialModule)
{
    auto ctx = std::make_unique<llvm::LLVMContext>();
    auto mod = std::make_unique<llvm::Module>("probe", *ctx);
    llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*ctx), {}, false);
    llvm::Function *fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "main", mod.get());
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(*ctx, "entry", fn);
    llvm::IRBuilder<> ir(bb);
    ir.CreateRet(ir.getInt32(42));

    int code = RunModuleInJit(std::move(mod), std::move(ctx), nullptr, nullptr);
    EXPECT_EQ(code, 42);
}
