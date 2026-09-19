// Split out of RuntimeTest.cpp: the fixture and the helpers live in
// RuntimeTestFixture.hpp, and each runtime test TU compiles in PARALLEL
// (see TestModule.py). Keep new tests in whichever file fits; the split is
// purely about compile time.

#include "RuntimeTestFixture.hpp"

TEST_F(RuntimeTest, Arithmetic)
{
    expectRun("fn main() -> i32 { ret 1 + 2 * 3; }", 7);
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

TEST_F(RuntimeTest, CastI16ToI32WideningAllowed)
{
    // i16 → i32 widening. Build the i16 input via char → i16 (char may be cast
    // to any integer width).
    expectRun("fn f(x: i16) -> i32 { ret x as i32; }"
              " fn main() -> i32 { let a = 'A' as i16; let b = '\\0' as i16; ret f(a) + f(b); }",
        65);
}

TEST_F(RuntimeTest, CastI32ToCharAllowed)
{
    // 65 as char → 'A' (char is i32 at runtime; identity cast).
    expectRun("fn main() -> i32 { let c = 65 as char; ret c as i32; }", 65);
}

// NOTE: malformed-exponent literals (`1e`, `1e+`) are NOT testable here — the
// lexer error trips the Parser gate which exit(1)s the whole test process. They
// are covered by LexerTest.MalformedFloatExponent* instead.

TEST_F(RuntimeTest, IndirectCall)
{
    // Function pointers (Step 6): `let fp = dbl; fp(21)`.
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn main() -> i32 { let fp = dbl; ret fp(21); }", 42);
}

// P2 regression (runtime): a trait mixing `&self` and `&mut self` methods used
// to share one SelfType (createSelf keyed by name), dropping the `&mut self`
// receiver's mutability — valid impls were rejected, and this call pattern is
// the same one that would then behave wrongly. It must compile and run.
TEST_F(RuntimeTest, TraitMixedReceiverKindsRuntime)
{
    expectRun("trait Mixed { fn read(self: &Self) -> i32; fn write(self: &mut Self, v: i32); }"
              " struct S { pub v: i32 } impl Mixed for S {"
              " fn read(self: &S) -> i32 { ret self.v; }"
              " fn write(self: &mut S, v: i32) { self.v = v; } }"
              " fn main() -> i32 { let mut s = S { v: 1 }; s.write(7); ret s.read(); }",
        7);
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

TEST_F(RuntimeTest, EnumVariantIsMoved)
{
    // After matching, the original enum binding should be "moved".
    expectRunWithPrologue("enum Option<T> { Some(T), None } fn main() -> i32 {"
                          " let o = Option::Some(5);"
                          " match o { Some(v) => { ret v; }, None => { ret 0; }, }"
                          " }",
        "",
        5);
}

// ── print builtins ─────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, PrintInt)
{
    expectOutput("fn main() -> i32 { print_int(42); println(); ret 0; }", "42\n", 0);
}

TEST_F(RuntimeTest, PrintMultiple)
{
    expectOutput("fn main() -> i32 { print_str(\"x=\"); print_int(7); println(); ret 0; }",
        "x=7\n",
        0);
}

TEST_F(RuntimeTest, MathFabs)
{
    expectRun("fn main() -> i32 { if fabs(0.0 - 2.5) > 2.0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, StructComparisonRejected)
{
    expectCompileFail("struct box { pub a: i32 } fn main() -> i32 {"
                      " let x = box{a:1}; let y = box{a:2}; ret (x < y) as i32; }",
        "cannot be applied");
}

TEST_F(RuntimeTest, ImplNumericOnStructRejected)
{
    expectCompileFail("struct box { pub a: i32 } impl Numeric for box {}"
                      " fn main() -> i32 { ret 0; }",
        "operator overloading");
}

TEST_F(RuntimeTest, GenericOperatorFunctionPrimitive)
{
    // The same generic `sum<T: Add>` monomorphized over i32 — the generic body's
    // `+` falls back to a direct binary op (primitives have no `add` method).
    expectRunWithPrologue("fn sum<T: Add>(a: T, b: T) -> T { ret a + b; }"
                          " fn main() -> i32 { ret sum(2, 3) + sum(4, 5); }",
        kMathPrologue,
        14);
}

TEST_F(RuntimeTest, StructWithoutOpTraitRejected)
{
    // A struct NOT implementing `Add` still rejects `+` with a clean error.
    expectCompileFail("struct box { pub a: i32 } fn main() -> i32 {"
                      " let x = box{a:1}; let y = box{a:2}; let z = x + y; ret z.a; }",
        "Add");
}

TEST_F(RuntimeTest, MathGcdLcmIpow)
{
    expectRun("fn main() -> i32 { ret gcd(12, 18) + lcm(4, 6) + ipow(2, 10); }",
        6 + 12 + 1024);
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

// ── arrays / heap / String (Step 22) ─────────────────────────────────────────

TEST_F(RuntimeTest, ArrayLiteralAndIndex)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3, 4]; ret a[0] + a[3]; }", 5);
}

TEST_F(RuntimeTest, ArrayNonCopyElementRejected)
{
    expectCompileFail("struct S { pub v: i32 } fn main() -> i32 { let a = [S{v:1}]; ret 0; }",
        "must be Copy");
}

TEST_F(RuntimeTest, StringIndexOption)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"hi\");"
              " let c = unwrap_or(s.index(0), '?');"
              " let d = unwrap_or(s.index(9), '?');" // out of bounds → None → '?'
              " ret (c as i32) - (d as i32); }",
        104 - 63);
}

TEST_F(RuntimeTest, StringMoveTransfersOwnership)
{
    // After `let t = s;`, s is unusable (single ownership).
    expectCompileFail("fn main() -> i32 { let s = String::from_lit(\"a\");"
                      " let t = s; let x = s.len(); ret x; }",
        "moved");
}

// Raw-pointer indexing is unchecked C pointer arithmetic — also stdlib-only, and
// the old "a reference to a primitive is a C buffer" special case is gone.
TEST_F(RuntimeTest, RawPointerOpsAreStdlibOnly)
{
    expectCompileFail("fn f(p: *mut i8) -> i32 { ret p[0] as i32; } fn main() -> i32 { ret 0; }",
        "only allowed inside the standard library");
    expectCompileFail("fn f(p: *mut i8) -> &i8 { ret __deref(p); } fn main() -> i32 { ret 0; }",
        "can only be used inside the standard library");
    expectCompileFail("fn f(p: &mut i8) -> i32 { ret p[0] as i32; } fn main() -> i32 { ret 0; }",
        "is not indexable");
}

// ── field visibility (`pub`) is ENFORCED ───────────────────────────────────────
// A private field is reachable only from a method of the declaring type. That is
// what lets a type keep an invariant: String's data/len/cap stay consistent
// because no other code can touch them.

TEST_F(RuntimeTest, PrivateFieldIsInaccessibleOutsideItsType)
{
    expectCompileFail("struct S { v: i32 } fn main() -> i32 { let s = S { v: 1 }; ret s.v; }",
        "field 'v' of 'S' is private");
}

// ── Step 23 robustness fixes ──────────────────────────────────────────────────

// B1: array-size literal that doesn't fit int64 → clean sema error, not a
// crash (the parser clamps to a sentinel; sema reports "exceeds the limit").
TEST_F(RuntimeTest, ArraySizeLiteralTooLargeRejected)
{
    expectCompileFail("fn main() -> i32 { let a: [i32; 99999999999999999999] = [1]; ret a[0]; }",
        "exceeds the limit");
}

// B5: indexing a reference to a struct is illegal (codegen has no element type).
// Since the heap-safety work the rejection is stated in terms of the rule that
// replaced the old "a reference to a primitive is a C buffer" special case:
// C-style indexing requires a RAW pointer, and only the stdlib may do it.
TEST_F(RuntimeTest, IndexRefToStructRejected)
{
    expectCompileFail("struct Foo { pub v: i32 } fn main() -> i32 { let mut f = Foo{v:1};"
                      " let r = &mut f; ret r[0].v; }",
        "is not indexable");
}

// E2: writing through a SHARED reference to an array is rejected.
TEST_F(RuntimeTest, WriteThroughSharedRefRejected)
{
    expectCompileFail("fn main() -> i32 { let a = [1, 2]; let r: &[i32; 2] = &a;"
                      " r[0] = 5; ret 0; }",
        "cannot assign through a shared reference");
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

// Without #[i_know], i64 -> i32 narrowing stays a hard error (regression).
TEST_F(RuntimeTest, NarrowingCastWithoutIKnowRejected)
{
    expectCompileFail("fn main() -> i32 { let big: i64 = 1 as i64;"
                      " let t: i32 = big as i32; ret t; }",
        "cannot cast integer to a smaller integer type");
}

TEST_F(RuntimeTest, ElseIfChainRunsWithoutCrash)
{
    // `else if` desugars to `else { if ... }` — the exact else-nested-if shape
    // that used to segfault lisc.exe (orphaned inner blocks). Must pick the
    // matching else-if branch and not crash.
    expectRun("fn main() -> i32 { let x = 10; let mut r = 0;"
              " if x < 5 { r = 1; } else if x < 20 { r = 2; } else { r = 3; }"
              " ret r; }",
        2);
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

TEST_F(RuntimeTest, ArithmeticPrecedenceMulBeforeAdd)
{
    expectRun("fn main() -> i32 { ret 2 + 3 * 4; }", 14);
}

TEST_F(RuntimeTest, ArithmeticDivisionTruncates)
{
    expectRun("fn main() -> i32 { ret 7 / 2; }", 3);
}

TEST_F(RuntimeTest, ArithmeticSumChain)
{
    expectRun("fn main() -> i32 { ret 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10; }", 55);
}

TEST_F(RuntimeTest, I8SubtractionNegative)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'B' as i8;"
              " let c = a - b; ret c as i32; }",
        -1);
}

TEST_F(RuntimeTest, I16WidenToI64)
{
    expectRun("fn main() -> i64 { let a = 'A' as i16; ret a as i64; }", 65);
}

TEST_F(RuntimeTest, I64Addition)
{
    expectRun("fn main() -> i64 { let a = 5 as i64; let b = 7 as i64;"
              " let c = a + b; ret c; }",
        12);
}

TEST_F(RuntimeTest, I8Comparison)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'B' as i8;"
              " if a < b { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, FloatMultiplication)
{
    expectOutput("fn main() -> i32 { print_float(2.5 * 2.0); println(); ret 0; }",
        "5.000000\n",
        0);
}

TEST_F(RuntimeTest, IntToFloatCast)
{
    expectOutput("fn main() -> i32 { let x = 5 as f64; print_float(x); println(); ret 0; }",
        "5.000000\n",
        0);
}

TEST_F(RuntimeTest, FloatNaNComparisonIsFalse)
{
    // NaN is not less than, greater than, or equal to anything.
    expectRun("fn main() -> i32 { let x = 0.0 / 0.0; if x > 0.0 { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, BoolToCharCastRejected)
{
    expectCompileFail("fn main() -> i32 { let b = true; let c = b as char; ret c as i32; }",
        "only be cast to integer");
}

TEST_F(RuntimeTest, I16ToI64Widening)
{
    expectRun("fn main() -> i64 { let a = 'A' as i16; ret a as i64; }", 65);
}

TEST_F(RuntimeTest, CharToBoolCastRejected)
{
    expectCompileFail("fn main() -> i32 { let c = 'A'; let b = c as bool; ret 0; }",
        "only be cast to integer");
}

TEST_F(RuntimeTest, WhileCountDown)
{
    expectRun("fn main() -> i32 { let mut n = 5; let mut c = 0;"
              " while n > 0 { n = n - 1; c = c + 1; } ret c; }",
        5);
}

TEST_F(RuntimeTest, WhileNestedBlocks)
{
    expectRun("fn main() -> i32 { let mut s = 0; let mut i = 0;"
              " while i < 3 { let mut j = 0; while j < 3 { s = s + 1; j = j + 1; }"
              " i = i + 1; } ret s; }",
        9);
}

TEST_F(RuntimeTest, ForBreak)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 100) {"
              " if x > 5 { break; } s = s + x; } ret s; }",
        15);
}

TEST_F(RuntimeTest, ForOnCustomIterator)
{
    // A struct implementing Iterator<i32> is usable in for.
    expectRun("struct R { pub cur: i32, pub end: i32 } impl Iterator<i32> for R {"
              " fn next(self: &mut Self) -> Option<i32> {"
              " if self.cur < self.end { let v = self.cur; self.cur = self.cur + 1;"
              " ret Option::Some(v); } ret Option::None; } }"
              " fn mk() -> R { ret R { cur: 1, end: 4 }; }"
              " fn main() -> i32 { let mut s = 0; for x in mk() { s = s + x; } ret s; }",
        6);
}

// ── B: function pointers ───────────────────────────────────────────────────────

TEST_F(RuntimeTest, FunctionPointerMultipleCalls)
{
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn main() -> i32 { let f = dbl;"
              " ret f(5) + f(7); }",
        24);
}

TEST_F(RuntimeTest, BitOrBasic)
{
    expectRun("fn main() -> i32 { ret 6 | 3; }", 7);
}

TEST_F(RuntimeTest, BitOrMaxByte)
{
    expectRun("fn main() -> i32 { ret 0 | 255; }", 255);
}

TEST_F(RuntimeTest, BitAndPrecedenceLowerThanCompare)
{
    // Comparison binds tighter than & : (6 > 3) is true; 6 & 3 = 2. But `6 > 3 & 1`
    // parses as (6 > 3) & 1 = true & 1... type-mismatch. Instead verify & binds
    // looser than * : 2 * 3 & 5 = 6 & 5 = 4.
    expectRun("fn main() -> i32 { ret 2 * 3 & 5; }", 4);
}

TEST_F(RuntimeTest, EnumWildcardArm)
{
    expectRun("enum E { A, B, C } fn main() -> i32 { let e = E::C;"
              " match e { A => { ret 1; }, _ => { ret 9; } } }",
        9);
}

TEST_F(RuntimeTest, EnumEqualityWithoutTraitRejected)
{
    expectCompileFail("enum E { A, B } fn main() -> i32 { let e = E::A;"
                      " if e == E::A { ret 1; } ret 0; }",
        "implement");
}

// ── B: globals ─────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GlobalRead)
{
    expectRun("let g = 10; fn main() -> i32 { ret g; }", 10);
}

TEST_F(RuntimeTest, GlobalMultipleSum)
{
    expectRun("let a = 1; let b = 2; let c = 3; fn main() -> i32 { ret a + b + c; }", 6);
}

TEST_F(RuntimeTest, GlobalExprInitializerRejected)
{
    // A binary-expression initializer is not a literal → rejected (would
    // silently zero-initialize in static storage).
    expectCompileFail("let g = 1 + 2; fn main() -> i32 { ret g; }",
        "global variable initializer must be a literal");
}

TEST_F(RuntimeTest, GenericStructNested)
{
    expectRun("struct Box<T> { pub v: T } fn main() -> i32 { let b = Box { v: Box { v: 5 } };"
              " ret b.v.v; }",
        5);
}

TEST_F(RuntimeTest, GenericBoundedAddWorks)
{
    // DECISION (2026-09-19): a generic parameter obeys the ownership rules, and
    // the arithmetic traits take their operands BY VALUE — so `x + x` needs the
    // parameter to be Copy (Rust reports E0382 for the same code).
    expectRun("fn f<T: Numeric + Copy>(x: T) -> T { ret x + x; }"
              " fn main() -> i32 { ret f(21); }",
        42);
}

TEST_F(RuntimeTest, ArrayReferenceIndex)
{
    expectRun("fn main() -> i32 { let a = [5, 6, 7]; let r = &a; ret r[0] + r[2]; }", 12);
}

TEST_F(RuntimeTest, ArrayIndexLastElement)
{
    expectRun("fn main() -> i32 { let a = [7, 8, 9]; ret a[2]; }", 9);
}

TEST_F(RuntimeTest, StringPushChars)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " s.push_char('a'); s.push_char('b'); ret s.len(); }",
        2);
}

TEST_F(RuntimeTest, StringIndexOutOfBounds)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"abc\");"
              " match s.index(99) { Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        0);
}

// ── B: operator overloading ────────────────────────────────────────────────────

TEST_F(RuntimeTest, OpOverloadSubtraction)
{
    expectRun("struct V { pub x: i32 } impl Sub for V { fn sub(self, o: Self) -> V {"
              " ret V { x: self.x - o.x }; } } fn main() -> i32 {"
              " let a = V { x: 10 }; let b = V { x: 3 }; ret (a - b).x; }",
        7);
}

TEST_F(RuntimeTest, OpOverloadRemainder)
{
    expectRun("struct V { pub x: i32 } impl Rem for V { fn rem(self, o: Self) -> V {"
              " ret V { x: self.x % o.x }; } } fn main() -> i32 {"
              " let a = V { x: 17 }; let b = V { x: 5 }; ret (a % b).x; }",
        2);
}

TEST_F(RuntimeTest, OpOverloadLessThan)
{
    expectRun("struct V { pub x: i32 } impl PartialOrd for V { fn lt(self: &Self, o: &Self) -> bool {"
              " ret self.x < o.x; } fn gt(self: &Self, o: &Self) -> bool { ret self.x > o.x; }"
              " fn le(self: &Self, o: &Self) -> bool { ret self.x <= o.x; }"
              " fn ge(self: &Self, o: &Self) -> bool { ret self.x >= o.x; } }"
              " fn main() -> i32 { let a = V { x: 1 }; let b = V { x: 2 };"
              " if a < b { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, OptionIsNoneOnSome)
{
    expectRun("fn main() -> i32 { let o = Option::Some(3); if is_none(o) { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, OptionUnwrapOrFallbackUsed)
{
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let o = none_i(); ret unwrap_or(o, 42); }",
        42);
}

TEST_F(RuntimeTest, OptionOrNoneSome)
{
    // or(None, Some(9)) → Some(9).
    expectRun("fn main() -> i32 { let a = or(Option::None, Option::Some(9));"
              " match a { Some(v) => { ret v; }, None => { ret 0; } } }",
        9);
}

TEST_F(RuntimeTest, MathMinEqual)
{
    expectRun("fn main() -> i32 { ret min(5, 5); }", 5);
}

TEST_F(RuntimeTest, MathMaxNegative)
{
    expectRun("fn main() -> i32 { ret max(0 - 5, 2); }", 2);
}

TEST_F(RuntimeTest, MathClampAbove)
{
    expectRun("fn main() -> i32 { ret clamp(11, 0, 10); }", 10);
}

TEST_F(RuntimeTest, MathAbsZero)
{
    expectRun("fn main() -> i32 { ret abs(0); }", 0);
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

TEST_F(RuntimeTest, MathLcmSame)
{
    expectRun("fn main() -> i32 { ret lcm(7, 7); }", 7);
}

TEST_F(RuntimeTest, MathIpowOneExponent)
{
    expectRun("fn main() -> i32 { ret ipow(9, 1); }", 9);
}

TEST_F(RuntimeTest, MathIsOddTrue)
{
    expectRun("fn main() -> i32 { if is_odd(7) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathIsOddNegative)
{
    // (0-5) % 2 = -1 != 0 → odd.
    expectRun("fn main() -> i32 { if is_odd(0 - 5) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathRadToDegFullCircle)
{
    // rad_to_deg(6.28318530718) ≈ 360.
    expectOutput("fn main() -> i32 { let d = rad_to_deg(6.28318530718);"
                 " print_float(d); println(); ret 0; }",
        "360.000000\n",
        0);
}

// ── E: chars.lis ────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, CharIsDigitTrue)
{
    expectRun("fn main() -> i32 { if is_digit('5') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsAlphanumericDigit)
{
    expectRun("fn main() -> i32 { if is_alphanumeric('7') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsWhitespaceTab)
{
    expectRun("fn main() -> i32 { if is_whitespace('\\t') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharDigitToIntZero)
{
    expectRun("fn main() -> i32 { ret digit_to_int('0'); }", 0);
}

TEST_F(RuntimeTest, IteratorRangeSumSingle)
{
    expectRun("fn main() -> i32 { ret sum(range(4, 5)); }", 4);
}

TEST_F(RuntimeTest, IteratorRangeCountEmpty)
{
    expectRun("fn main() -> i32 { ret count(range(3, 3)); }", 0);
}

TEST_F(RuntimeTest, IteratorLastEmpty)
{
    expectRun("fn main() -> i32 { let l = last(range(2, 2));"
              " match l { Some(v) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, IteratorProductRange)
{
    expectRun("fn main() -> i32 { ret product(range(1, 5)); }", 24);
}

TEST_F(RuntimeTest, IteratorSumThenCount)
{
    expectRun("fn main() -> i32 { let s = sum(range(1, 6)); let c = count(range(1, 6));"
              " ret s + c; }",
        20);
}

TEST_F(RuntimeTest, ExampleIterator)
{
    expectExample("iterator", 23);
}

TEST_F(RuntimeTest, ExampleMethodRef)
{
    expectExample("method_ref", 10);
}

TEST_F(RuntimeTest, ExampleString)
{
    expectExample("string", 0);
    // Result + the `?` operator (exit code = the first successful parse, 42).
    expectExample("result", 42);
}

TEST_F(RuntimeTest, DropMultipleValues)
{
    // Two values of the same Drop type → the counter reaches 2.
    expectRun("let dropped = 0; struct D { pub v: i32 } impl Drop for D {"
              " fn drop(self) { dropped = dropped + 1; } }"
              " fn main() -> i32 { { let a = D { v: 1 }; let b = D { v: 2 }; } ret dropped; }",
        2);
}

TEST_F(RuntimeTest, EnumWildcardWithPayload)
{
    // Scrutinee must carry a concrete payload type so the wildcard arm's value
    // has a type to unify against.
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 { let o = O::Some(5);"
              " ret match o { Some(v) => v, _ => 7 }; }",
        5);
}

TEST_F(RuntimeTest, EnumMovePayloadThenRebind)
{
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 { let o = O::Some(4);"
              " match o { Some(v) => { let w = v; ret w * 2; }, None => { ret 0; } } }",
        8);
}

TEST_F(RuntimeTest, StringPushStrToEmpty)
{
    expectRun("fn main() -> i32 { let mut s = String::new(); s.push_str(\"abc\"); ret s.len(); }", 3);
}

TEST_F(RuntimeTest, StringMutateAndGrow)
{
    // Grows the buffer (capacity doubling + memcpy + free of the old block) and
    // keeps the appended content readable at the far end.
    expectRun("fn main() -> i32 { let mut s = String::from_lit(\"hi\");"
              " let mut i = 0; while i < 30 { s.push_char('!'); i = i + 1; }"
              " match s.index(31) { Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        33);
}

TEST_F(RuntimeTest, OpOverloadBitAnd)
{
    expectRun("struct M { pub x: i32 } impl BitAnd for M { fn bitand(self, o: Self) -> M {"
              " ret M { x: self.x & o.x }; } } fn main() -> i32 {"
              " let a = M { x: 6 }; let b = M { x: 3 }; ret (a & b).x; }",
        2);
}

TEST_F(RuntimeTest, OpOverloadChainThree)
{
    expectRun("struct V { pub x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } fn main() -> i32 {"
              " let a = V { x: 1 }; let b = V { x: 2 }; let c = V { x: 3 };"
              " let r = a + b + c; ret r.x; }",
        6);
}

TEST_F(RuntimeTest, I8AndI16MixViaI32)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'B' as i16;"
              " ret (a as i32) + (b as i32); }",
        131);
}

TEST_F(RuntimeTest, FloatZeroAndNegative)
{
    expectOutput("fn main() -> i32 { print_float(0.0); println();"
                 " print_float(0.0 - 1.5); println(); ret 0; }",
        "0.000000\n-1.500000\n",
        0);
}

TEST_F(RuntimeTest, GlobalCrossFunctionState)
{
    expectRun("let g = 0; fn inc() { g = g + 1; } fn main() -> i32 {"
              " inc(); inc(); inc(); inc(); ret g; }",
        4);
}

TEST_F(RuntimeTest, GenericFunctionTwoParams)
{
    // `>` on T requires a Numeric bound.
    expectRun("fn pair<T: Numeric>(a: T, b: T) -> T { if a > b { ret a; } ret b; }"
              " fn main() -> i32 { ret pair(3, 9); }",
        9);
}

TEST_F(RuntimeTest, ForWithContinueAndBreak)
{
    // 1+2+3+4, skip 5, 6+7, break at 8 → 23.
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 20) {"
              " if x == 5 { continue; } if x == 8 { break; } s = s + x; } ret s; }",
        23);
}

// NOTE: `if (x = 5) > 3` is not expressible — assignment is a statement, not an
// expression, in this language. No test for it.

// ── B2: more arrays ────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, ArrayIndexFromVariable)
{
    expectRun("fn main() -> i32 { let a = [10, 20, 30]; let i = 2; ret a[i]; }", 30);
}

TEST_F(RuntimeTest, FunctionComposition)
{
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn inc(x: i32) -> i32 { ret x + 1; }"
              " fn main() -> i32 { ret dbl(inc(4)); }",
        10);
}

TEST_F(RuntimeTest, ArithmeticManyOperands)
{
    expectRun("fn main() -> i32 { ret 1 + 2 + 3 + 4 + 5 + 6; }", 21);
}

TEST_F(RuntimeTest, BoolResultInArithmetic)
{
    expectRun("fn main() -> i32 { let b = 5 > 3; if b { ret 10; } ret 20; }", 10);
}

TEST_F(RuntimeTest, FloatNegativeZero)
{
    expectOutput("fn main() -> i32 { print_float(0.0 - 0.0); println(); ret 0; }",
        "0.000000\n",
        0);
}

TEST_F(RuntimeTest, CharCompareNotEqual)
{
    expectRun("fn main() -> i32 { if 'a' != 'b' { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, I8ToI64ViaChain)
{
    expectRun("fn main() -> i64 { let a = 'A' as i8; let b = a as i32; let c = b as i64; ret c; }", 65);
}

TEST_F(RuntimeTest, EnumNestedVariantConstruction)
{
    expectRun("enum O<T> { Some(T), None } enum R { Ok(i32), Err }"
              " fn main() -> i32 { let o = O::Some(R::Ok(5));"
              " match o { Some(r) => { match r { Ok(v) => { ret v; }, Err => { ret 0; } } },"
              " None => { ret 0; } } }",
        5);
}

TEST_F(RuntimeTest, StringLengthAfterPushStr)
{
    expectRun("fn main() -> i32 { let mut s = String::from_lit(\"he\");"
              " s.push_str(\"llo\"); ret s.len(); }",
        5);
}

// ── B3: generics ───────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GenericIdentityChain)
{
    expectRun("fn id<T>(x: T) -> T { ret x; } fn main() -> i32 {"
              " let a = id(5); let b = id(a); ret b; }",
        5);
}

// ── B3: globals ────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GlobalFloatArithmetic)
{
    expectOutput("let g = 2.0; fn main() -> i32 { print_float(g * 3.0); println(); ret 0; }",
        "6.000000\n",
        0);
}

// ── B3: arrays ─────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, ArrayAllElementsSum)
{
    expectRun("fn main() -> i32 { let a = [2, 4, 6, 8]; let mut s = 0; let mut i = 0;"
              " while i < 4 { s = s + a[i]; i = i + 1; } ret s; }",
        20);
}

TEST_F(RuntimeTest, OpOverloadMethodCallSyntax)
{
    // The trait method is directly callable too.
    expectRun("struct V { pub x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } fn main() -> i32 {"
              " let a = V { x: 3 }; let b = V { x: 4 }; let c = a.add(b); ret c.x; }",
        7);
}

TEST_F(RuntimeTest, ContinueInnerLoop)
{
    expectRun("fn main() -> i32 { let mut s = 0; for i in range(1, 4) {"
              " for j in range(1, 4) { if j == 2 { continue; } s = s + 1; } } ret s; }",
        6);
}

TEST_F(RuntimeTest, ForBreakExitCode)
{
    // (`lastv`, not `last` — the stdlib has a `last` function.)
    expectRun("fn main() -> i32 { let mut lastv = 0; for x in range(1, 100) {"
              " lastv = x; if x == 7 { break; } } ret lastv; }",
        7);
}

TEST_F(RuntimeTest, EnumThreeWayDispatch)
{
    expectRun("enum D { Up, Down, Left, Right } fn main() -> i32 {"
              " let d = D::Right; ret match d { Up => 1, Down => 2, Left => 3, Right => 4 }; }",
        4);
}

TEST_F(RuntimeTest, ForAccumulateProduct)
{
    expectRun("fn main() -> i32 { let mut p = 1; for x in range(1, 5) { p = p * x; } ret p; }", 24);
}

TEST_F(RuntimeTest, WhileNegativeCondition)
{
    expectRun("fn main() -> i32 { let mut x = 0 - 3; let mut n = 0;"
              " while x < 0 { x = x + 1; n = n + 1; } ret n; }",
        3);
}

TEST_F(RuntimeTest, BoolNotViaCompare)
{
    expectRun("fn main() -> i32 { let b = false; if b == false { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MultiReturnFunctions)
{
    expectRun("fn f(x: i32) -> i32 { if x > 0 { ret x; } ret 0 - x; }"
              " fn main() -> i32 { ret f(5) + f(0 - 3); }",
        8);
}

TEST_F(RuntimeTest, WhileTrueWithContinue)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while true { i = i + 1; if (i % 2) == 0 { continue; }"
              " if i > 5 { break; } s = s + i; } ret s; }",
        9);
}

// NOTE: an owned enum scrutinee is MOVED into the first match, so matching the
// same value twice is rejected (single-owner). No test for reuse.

TEST_F(RuntimeTest, StringComparisonLengths)
{
    expectRun("fn main() -> i32 { let a = String::from_lit(\"a\");"
              " let b = String::from_lit(\"abc\");"
              " if a.len() < b.len() && b.len() == 3 { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, StringIndexNegative)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"abc\");"
              " match s.index(0 - 1) { Some(c) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, MatchArmWithGlobalSideEffect)
{
    expectRun("let n = 0; enum E { A, B } fn main() -> i32 {"
              " let e = E::A; match e { A => { n = n + 100; }, B => { n = n + 1; } } ret n; }",
        100);
}

TEST_F(RuntimeTest, ChainedMethodCalls)
{
    expectRun("struct S { pub v: i32 } impl S { fn add(self: &mut S, d: i32) { self.v = self.v + d; }"
              " fn mul(self: &mut S, m: i32) { self.v = self.v * m; } }"
              " fn main() -> i32 { let mut c = S { v: 1 }; c.add(2); c.mul(3); ret c.v; }",
        9);
}

TEST_F(RuntimeTest, GlobalAndLocalShadowFree)
{
    expectRun("let g = 5; fn main() -> i32 { let h = 3; ret g + h; }", 8);
}

TEST_F(RuntimeTest, SimpleArithmeticMul)
{
    expectRun("fn main() -> i32 { ret 6 * 7; }", 42);
}

TEST_F(RuntimeTest, SimpleComparisonNe)
{
    expectRun("fn main() -> i32 { if 5 != 6 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleIfFalse)
{
    expectRun("fn main() -> i32 { if false { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, SimpleGlobal)
{
    expectRun("let g = 9; fn main() -> i32 { ret g; }", 9);
}

TEST_F(RuntimeTest, SimpleArrayIndex)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3]; ret a[2]; }", 3);
}

TEST_F(RuntimeTest, SimpleModulo)
{
    expectRun("fn main() -> i32 { ret 20 % 6; }", 2);
}

TEST_F(RuntimeTest, SimpleNestedIf)
{
    expectRun("fn main() -> i32 { if true { if false { ret 1; } ret 2; } ret 0; }", 2);
}

TEST_F(RuntimeTest, SimpleCharCompare)
{
    expectRun("fn main() -> i32 { if 'a' == 'a' { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleBoolPrint)
{
    expectOutput("fn main() -> i32 { print_bool(true); println(); ret 0; }", "1\n", 0);
}

TEST_F(RuntimeTest, ModuleNotFoundRejected)
{
    std::string diag;
    bool ok = compileMulti("impt no_such_module;\nfn main() -> i32 { ret 0; }", {}, &diag);
    EXPECT_FALSE(ok) << "importing a missing module must fail";
}

TEST_F(RuntimeTest, ModuleQualifiedEnumVariant)
{
    ASSERT_TRUE(compileMulti(
        "impt shapes;\n"
        "fn main() -> i32 { let s = shapes::Shape::Circle(7); let r = match s { Circle(r) => r, Square(_) => 0 }; ret r; }",
        {{"shapes", "enum Shape { Circle(i32), Square(i32) }"}}));
    EXPECT_EQ(linkAndRun(), 7);
}

TEST_F(RuntimeTest, ModuleCrossReference)
{
    // Module a calls into module b (both imported by the main file).
    ASSERT_TRUE(compileMulti(
        "impt a;\nimpt b;\n"
        "fn main() -> i32 { ret a::call_b(); }",
        {{"a", "impt b;\nfn call_b() -> i32 { ret b::seven(); }"},
            {"b", "fn seven() -> i32 { ret 7; }"}}));
    EXPECT_EQ(linkAndRun(), 7);
}

TEST_F(RuntimeTest, NeverFunctionDiverges)
{
    expectPanic("fn boom() -> never { panic(\"never returns\"); }\n"
                "fn main() -> i32 { boom(); ret 0; }",
        "panicked: never returns");
}

// panic takes a &i8, so a never function can forward its own parameter.
TEST_F(RuntimeTest, NeverFunctionForwardsItsMessage)
{
    expectPanic("fn die(m: &i8) -> never { panic(m); }\n"
                "fn main() -> i32 { die(\"via a param\"); ret 0; }",
        "panicked: via a param");
}

// A diverging VALUE arm must not fix the match's type: 'None => panic(..)' is
// never, so the match is still i32 (fixed by the Some arm).
TEST_F(RuntimeTest, DivergingValueArmDoesNotFixMatchType)
{
    expectRun("fn main() -> i32 {\n"
              "    let o = Option::Some(5);\n"
              "    let x = match o { Some(v) => v, None => panic(\"unreachable\"), };\n"
              "    ret x;\n"
              "}",
        5);
    expectPanic("fn nothing() -> Option<i32> { ret Option::None; }\n"
                "fn main() -> i32 {\n"
                "    let n = nothing();\n"
                "    let x = match n { Some(v) => v, None => panic(\"no value\"), };\n"
                "    ret x;\n"
                "}",
        "panicked: no value");
}

// A function declared to return never must actually diverge; the check lives in
// MIRBuilder (conservative: the body must contain at least one diverging call).
TEST_F(RuntimeTest, NeverFunctionMustDiverge)
{
    expectCompileFail("fn boom() -> never { print_str(\"side\"); }\nfn main() -> i32 { ret 0; }",
        "but never diverges");
    expectCompileFail("fn boom() -> never { }\nfn main() -> i32 { ret 0; }",
        "but never diverges");
}

TEST_F(RuntimeTest, BareGenericParameterTypeRejected)
{
    expectCompileFail("fn f(o: Option) -> i32 { ret 0; }\nfn main() -> i32 { ret f(Option::Some(1)); }",
        "without its argument(s)");
}

TEST_F(RuntimeTest, PanicArityIsChecked)
{
    expectCompileFail("fn main() -> i32 { panic(); ret 0; }",
        "builtin 'panic' expects 1 argument, got 0");
}

// ── H: Option::unwrap / expect (stdlib) ───────────────────────────────────────
//
// Methods on Option<T> (Source/Std/option.lis): both consume the option and
// return the payload, aborting through panic when it is None. They are
// implementable only because panic returns never — the None arm produces no
// value, so the method still type-checks as returning T.

TEST_F(RuntimeTest, OptionUnwrapReturnsThePayload)
{
    expectRun("fn main() -> i32 { let a = Option::Some(7); ret a.unwrap(); }", 7);
}

TEST_F(RuntimeTest, OptionUnwrapOnHelperResult)
{
    // first(range(1, 5)) is Option<i32> holding 1.
    expectRun("fn main() -> i32 { ret first(range(1, 5)).unwrap(); }", 1);
    // unwrap_or stays available (and non-panicking) alongside unwrap.
    expectRun("fn main() -> i32 { ret unwrap_or(Option::Some(4), 0) + Option::Some(5).unwrap(); }", 9);
}

TEST_F(RuntimeTest, GenericEnumTwoTypesImplMethod)
{
    expectRun("enum Pair<A, B> { Both(A, B), Neither }\n"
              "impl Pair {\n"
              "    fn first(self) -> A { match self { Both(a, b) => { ret a; }, Neither => { panic(\"empty\"); } } }\n"
              "}\n"
              "fn main() -> i32 { let p = Pair::Both(11, 'z'); ret p.first(); }",
        11);
}

TEST_F(RuntimeTest, UninitCopyBindingUnusedIsAllowed)
{
    expectRun("fn main() -> i32 { let x: i32; ret 0; }", 0);
}

TEST_F(RuntimeTest, UninitReadRejected)
{
    expectCompileFail("fn main() -> i32 { let x: i32; ret x; }", "use of uninitialized value");
}

TEST_F(RuntimeTest, UninitBothBranchesAssignAccepted)
{
    expectRun("fn main() -> i32 { let mut x: i32; let c = 1;\n"
              "    if c > 0 { x = 1; } else { x = 2; }\n"
              "    ret x; }",
        1);
}

TEST_F(RuntimeTest, UninitMatchAllArmsAssignAccepted)
{
    expectRun("fn main() -> i32 { let mut x: i32; let o = Option::Some(1);\n"
              "    match o { Some(v) => { x = v; }, None => { x = 2; } }\n"
              "    ret x; }",
        1);
}

// Code after a branch that always returns is unreachable, so it is not checked.
TEST_F(RuntimeTest, UninitChecksSkippedInUnreachableCode)
{
    expectRun("fn main() -> i32 { let x: i32; let c = 1;\n"
              "    if c > 0 { ret 1; } else { ret 2; }\n"
              "    ret x; }",
        1);
}

// ── K: Result and the postfix ? operator (error propagation) ─────────────────

TEST_F(RuntimeTest, ResultConstructAndHelpers)
{
    // kResultPrologue (not the default one): the bare name unwrap_or must resolve
    // to RESULT's helper here, and the default prologue imports option's.
    expectRunWithPrologue("fn one() -> Result<i32, i32> { ret Result::Ok(1); }\n"
                          "fn bad() -> Result<i32, i32> { ret Result::Err(9); }\n"
                          "fn main() -> i32 {\n"
                          "    let a = one();\n"
                          "    let b = bad();\n"
                          "    let ok = is_ok(a);\n"
                          "    let err = is_err(b);\n"
                          "    let v = unwrap_or(one(), 0) + unwrap_or(bad(), 5);\n"
                          "    if ok { if err { ret v; } }\n"
                          "    ret 0 - 1;\n"
                          "}",
        kResultPrologue,
        6);
}

// The operator may be followed by member access, and used as a statement.
TEST_F(RuntimeTest, TryOperatorSuffixAndStatementForms)
{
    expectRun("struct P { pub v: i32 }\n"
              "fn getp() -> Result<P, i32> { ret Result::Ok(P { v: 3 }); }\n"
              "fn one() -> Result<i32, i32> { ret Result::Ok(1); }\n"
              "fn f() -> Result<i32, i32> { let p = getp()?; ret Result::Ok(p.v + one()?); }\n"
              "fn main() -> i32 { let r = f();\n"
              "    match r { Ok(v) => { ret v; }, Err(e) => { ret 0; } } }",
        4);
    expectRun("fn one() -> Result<i32, i32> { ret Result::Ok(1); }\n"
              "fn f() -> Result<i32, i32> { one()?; ret Result::Ok(9); }\n"
              "fn main() -> i32 { let r = f();\n"
              "    match r { Ok(v) => { ret v; }, Err(e) => { ret 0; } } }",
        9);
}

TEST_F(RuntimeTest, TryOperatorRequiresMatchingErrorType)
{
    expectCompileFail("fn bad() -> Result<i32, i32> { ret Result::Err(1); }\n"
                      "fn f() -> Result<i32, bool> { let x = bad()?; ret Result::Ok(x); }\n"
                      "fn main() -> i32 { ret 0; }",
        "does not match the function error type");
}

// The end-to-end in-process path (compile a Lis snippet, JIT it, run it) is what
// expectRun/expectOutput do for ~500 cases; the trivial module above covers the
// JIT plumbing on its own.


