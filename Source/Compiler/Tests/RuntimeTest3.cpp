// Split out of RuntimeTest.cpp: the fixture and the helpers live in
// RuntimeTestFixture.hpp, and each runtime test TU compiles in PARALLEL
// (see TestModule.py). Keep new tests in whichever file fits; the split is
// purely about compile time.

#include "RuntimeTestFixture.hpp"


// P1 regression: an integer literal too large for int64 used as a VALUE used to
// crash HIRBuilder's unguarded std::stoll (std::terminate). It must now be
// rejected with a clean diagnostic instead.
TEST_F(RuntimeTest, ValueIntegerLiteralOverflowRejected)
{
    expectCompileFail("fn main() -> i32 { let a = 99999999999999999999; ret a as i32; }",
        "overflows");
}

TEST_F(RuntimeTest, CastI32ToI64WideningAllowed)
{
    // i32 → i64 is a widening cast — must compile and run.
    expectRun("fn main() -> i64 { ret 5 as i64; }", 5);
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

TEST_F(RuntimeTest, EnumMatchPayloadBinding)
{
    expectRunWithPrologue("enum Option<T> { Some(T), None } fn main() -> i32 {"
                          " let o = Option::Some(7);"
                          " match o { Some(v) => { ret v; }, None => { ret 0; }, }"
                          " }",
        "",
        7);
}

TEST_F(RuntimeTest, EnumMatchNonCopyPayload)
{
    // A non-Copy payload is MOVED into the binding; reading it works.
    expectRunWithPrologue("enum Option<T> { Some(T), None } struct Inner { pub v: i32 }"
                          " fn main() -> i32 { let o = Option::Some(Inner{v: 5}); let mut got = 0;"
                          " match o { Some(x) => { got = x.v; }, None => { got = 99; }, }"
                          " ret got; }",
        "",
        5);
}

TEST_F(RuntimeTest, PrintFloatAndBool)
{
    expectOutput("fn main() -> i32 { print_float(3.5); println(); print_bool(true); println(); ret 0; }",
        "3.500000\n1\n",
        0);
}

TEST_F(RuntimeTest, MathMinMaxFloat)
{
    expectRun("fn main() -> i32 { if max(3.5, 4.5) == 4.5 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, UnconstrainedGenericOpRejected)
{
    // A generic function using `<` on T must declare `T: Numeric`.
    expectCompileFail("fn m<T>(a: T, b: T) -> T { if a < b { ret a; } ret b; }"
                      " fn main() -> i32 { ret m(1, 2); }",
        "Numeric");
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

TEST_F(RuntimeTest, ReadLine)
{
    // read_line strips the trailing newline.
    expectOutputWithInput(
        "fn main() -> i32 { let line = read_line(); print_str(line); println(); ret 0; }",
        "hello\n",
        "hello\n",
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

TEST_F(RuntimeTest, StringFromLitAndPrint)
{
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hi\");"
                 " print_str(s.to_cstr()); println(); ret 0; }",
        "hi\n",
        0);
}

TEST_F(RuntimeTest, StringFreedExactlyOnce)
{
    // A heap-owning struct with a Drop counter: moving it transfers ownership,
    // so the scope-end drop runs exactly once — never twice. The heap buffer is
    // a String now (the heap primitives themselves are stdlib-private).
    expectRun("let frees = 0;"
              " struct Buf { pub s: String }"
              " impl Drop for Buf { fn drop(self) { frees = frees + 1; } }"
              " fn main() -> i32 {"
              "   { let b = Buf { s: String::from_lit(\"abc\") }; let c = b; }" // b moved into c, c drops → frees 1
              "   ret frees; }",
        1);
}

TEST_F(RuntimeTest, PointerIsNotAnInteger)
{
    // Casts stay primitive-to-primitive, so a raw pointer can never be fabricated
    // from a literal or from an integer: there is no way to forge an address.
    expectCompileFail("fn main() -> i32 { let p: *mut i8 = 0; ret 0; }",
        "type mismatch in variable declaration");
}

TEST_F(RuntimeTest, ArrayOfRawPointersRejected)
{
    // Same rule as reference elements: an array of indirections would let an
    // owner escape into a copied element.
    expectCompileFail("fn f(a: [*mut i8; 2]) -> i32 { ret 0; } fn main() -> i32 { ret 0; }",
        "raw pointer");
}

TEST_F(RuntimeTest, PrivateFieldIsAccessibleFromItsTypesOwnMethods)
{
    // An instance method AND a static one — the latter is how a type constructs
    // itself (String::new builds the private triple). The declaring type is what
    // matters, not the kind of method.
    expectRun("struct S { v: i32 } impl S { fn make() -> S { ret S { v: 7 }; }"
              " fn get(self: &S) -> i32 { ret self.v; } }"
              " fn main() -> i32 { let s = S::make(); ret s.get(); }",
        7);
}

// B4: empty array literal has no element type to infer.
TEST_F(RuntimeTest, EmptyArrayLiteralRejected)
{
    expectCompileFail("fn main() -> i32 { let a = []; ret 0; }",
        "empty array literal");
}

// B3: arrays are not first-class — reject as function params / returns before
// the LLVM backend asserts on a non-first-class ArrayType.
TEST_F(RuntimeTest, ArrayAsFunctionParamRejected)
{
    expectCompileFail("fn f(a: [i32; 2]) -> i32 { ret a[0]; } fn main() -> i32 { ret 0; }",
        "array type cannot be a function parameter");
}

// E4: reference elements would escape origin tracking — reject the array.
TEST_F(RuntimeTest, ArrayOfReferencesRejected)
{
    expectCompileFail("fn main() -> i32 { let mut x = 1; let a = [&mut x]; ret 0; }",
        "cannot be a reference");
}

// E2 (runtime): write through a &mut reference to an array is allowed.
TEST_F(RuntimeTest, WriteThroughMutRefToArray)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; let r = &mut a;"
              " r[1] = 9; ret a[1]; }",
        9);
}

// D1 (runtime): in-bounds indexing does NOT abort.
TEST_F(RuntimeTest, ArrayInBoundsNoAbort)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3]; let mut s = 0; let mut i = 0;"
              " while i < 3 { s = s + a[i]; i = i + 1; } ret s; }",
        6);
}

// ── 2026-08-12 spec decisions ────────────────────────────────────────────
// 1. Default parameter values (`a: i32 = 5`) were dead syntax (parsed then
//    ignored by every later pass) — removed; must now be a parse error.
TEST_F(RuntimeTest, DefaultParameterValuesRejected)
{
    expectCompileFail("fn f(a: i32 = 5) -> i32 { ret a; } fn main() -> i32 { ret f(1); }",
        "default parameter values are not supported");
}

TEST_F(RuntimeTest, WhileInsideIfBodyExecutes)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut r = 0;"
              " if true { while i < 2 { r = r + 1; i = i + 1; } } ret r; }",
        2);
}

TEST_F(RuntimeTest, NestedIfInElseBodyExecutes)
{
    expectRun("fn main() -> i32 { let x = 1; let mut r = 0;"
              " if x > 3 { r = 1; } else { if x > 0 { r = 2; } } ret r; }",
        2);
}

TEST_F(RuntimeTest, StatementAfterNestedIfExecutes)
{
    // Not only the nested if's own body — a statement AFTER it in the same block
    // must execute too (the pre-fix seal overwrote control flow past the if).
    expectRun("fn main() -> i32 { let mut r = 0;"
              " if true { if true { r = 1; } r = 5; } ret r; }",
        5);
}

TEST_F(RuntimeTest, BareIfThenElseBranchRuns)
{
    expectRun("fn main() -> i32 { let x = 1; let mut r = 0;"
              " if true if x > 0 { r = 2; } ret r; }",
        2);
}

TEST_F(RuntimeTest, BoolTrueCastToInt64IsOne)
{
    expectRun("fn main() -> i64 { let b = true; ret b as i64; }", 1);
}

// ── B: arithmetic semantics ─────────────────────────────────────────────────────

TEST_F(RuntimeTest, ArithmeticLargeProduct)
{
    expectRun("fn main() -> i32 { ret 1000000 * 1000; }", 1000000000);
}

TEST_F(RuntimeTest, ArithmeticModuloPositive)
{
    expectRun("fn main() -> i32 { ret 17 % 5; }", 2);
}

TEST_F(RuntimeTest, ArithmeticMixedOps)
{
    // 2 + 6 * 7 / 3 - 4 = 2 + 14 - 4 = 12.
    expectRun("fn main() -> i32 { ret 2 + 6 * 7 / 3 - 4; }", 12);
}

TEST_F(RuntimeTest, I8WidenToI32)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; ret a as i32; }", 65);
}

TEST_F(RuntimeTest, I8WidenToI64SignExtends)
{
    // -1 as i8 widens to i64 as -1 (sign extension, not zero extension).
    expectRun("fn main() -> i64 { let a = 'A' as i8; let b = 'B' as i8;"
              " let c = a - b; ret c as i64; }",
        -1);
}

TEST_F(RuntimeTest, IntegerWidthChainWidening)
{
    // i8 → i16 → i32 → i64, value preserved at each step.
    expectRun("fn main() -> i64 { let a = 'A' as i8; let b = a as i16;"
              " let c = b as i32; let d = c as i64; ret d; }",
        65);
}

TEST_F(RuntimeTest, FloatDivision)
{
    expectOutput("fn main() -> i32 { print_float(7.0 / 2.0); println(); ret 0; }",
        "3.500000\n",
        0);
}

TEST_F(RuntimeTest, I64ToFloatCast)
{
    expectOutput("fn main() -> i32 { let a = 3 as i64; let b = a as f64;"
                 " print_float(b); println(); ret 0; }",
        "3.000000\n",
        0);
}

// ── B: casts ───────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, CharToIntCast)
{
    expectRun("fn main() -> i32 { let c = 'A'; ret c as i32; }", 65);
}

TEST_F(RuntimeTest, I64ToI32NarrowingRejected)
{
    expectCompileFail("fn main() -> i32 { let a = 5 as i64; ret a as i32; }",
        "smaller integer type");
}

TEST_F(RuntimeTest, CastInExpression)
{
    // A cast can appear anywhere an expression can.
    expectRun("fn main() -> i32 { ret ('B' as i32) + 1; }", 67);
}

// ── B: control flow (while / for / break / continue) ───────────────────────────

TEST_F(RuntimeTest, WhileSum0To9)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 10 { s = s + i; i = i + 1; } ret s; }",
        45);
}

TEST_F(RuntimeTest, WhileContinueSkipsEven)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 10 { i = i + 1; if (i % 2) == 0 { continue; } s = s + i; } ret s; }",
        25);
}

TEST_F(RuntimeTest, ForRangeBackwardEmpty)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(10, 1) { s = s + x; } ret s; }", 0);
}

TEST_F(RuntimeTest, ForVarNotLeakedOutside)
{
    // The loop variable is scoped to the loop — reusing the name outside fails
    // (single-name rule), but a DIFFERENT name works.
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 3) { s = s + x; }"
              " let y = 100; ret s + y; }",
        103);
}

TEST_F(RuntimeTest, RecursionCountDown)
{
    // `down` (not `count` — that name collides with the preloaded stdlib's
    // `count<T: Iterator>`).
    expectRun("fn down(n: i32, acc: i32) -> i32 { if n == 0 { ret acc; }"
              " ret down(n - 1, acc + 1); } fn main() -> i32 { ret down(10, 0); }",
        10);
}

TEST_F(RuntimeTest, FunctionPointerReassignment)
{
    expectRun("fn dbl(x: i32) -> i32 { ret x * 2; } fn id(x: i32) -> i32 { ret x; }"
              " fn main() -> i32 { let mut f = dbl; f = id; ret f(5); }",
        5);
}

// ── B: bitwise & and | (infix) ─────────────────────────────────────────────────

TEST_F(RuntimeTest, BitAndBasic)
{
    expectRun("fn main() -> i32 { ret 6 & 3; }", 2);
}

TEST_F(RuntimeTest, BitAndAssociativity)
{
    // 15 & 12 = 12, 12 & 10 = 8.
    expectRun("fn main() -> i32 { ret 15 & 12 & 10; }", 8);
}

TEST_F(RuntimeTest, BitwiseOnI64)
{
    expectRun("fn main() -> i64 { let a = 12 as i64; let b = 10 as i64;"
              " let c = a & b; ret c; }",
        8);
}

TEST_F(RuntimeTest, EnumMultiPayload)
{
    expectRun("enum E { P(i32, i32), Q } fn main() -> i32 { let e = E::P(3, 4);"
              " match e { P(a, b) => { ret a + b; }, Q => { ret 0; } } }",
        7);
}

TEST_F(RuntimeTest, EnumMatchNonEnumRejected)
{
    expectCompileFail("fn main() -> i32 { let x = 5; match x { 1 => { ret 0; },"
                      " _ => { ret 1; } } }",
        "");
}

TEST_F(RuntimeTest, EnumGenericTwoInstantiations)
{
    // The same generic enum instantiated with two different payload types.
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 {"
              " let a = O::Some(5); let b = O::Some('A');"
              " let mut r = 0; match a { Some(v) => { r = v; }, None => { r = 0; } }"
              " match b { Some(c) => { ret r + (c as i32); }, None => { ret r; } } }",
        70);
}

TEST_F(RuntimeTest, GlobalBoolLiteral)
{
    expectRun("let g = true; fn main() -> i32 { if g { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, GenericPickLarger)
{
    expectRun("fn pick<T: Numeric>(a: T, b: T) -> T { if a > b { ret a; } ret b; }"
              " fn main() -> i32 { ret pick(3, 9); }",
        9);
}

TEST_F(RuntimeTest, GenericFloatInstantiation)
{
    expectOutput("fn id<T>(x: T) -> T { ret x; } fn main() -> i32 {"
                 " let f = id(3.5); print_float(f); println(); ret 0; }",
        "3.500000\n",
        0);
}

TEST_F(RuntimeTest, ArrayMutableElement)
{
    expectRun("fn main() -> i32 { let mut a = [1, 2, 3]; a[1] = 9; ret a[1]; }", 9);
}

TEST_F(RuntimeTest, ArrayLoopSum)
{
    expectRun("fn main() -> i32 { let a = [10, 20, 30]; let mut s = 0; let mut i = 0;"
              " while i < 3 { s = s + a[i]; i = i + 1; } ret s; }",
        60);
}

TEST_F(RuntimeTest, StringPushStr)
{
    expectRun("fn main() -> i32 { let mut s = String::new(); s.push_str(\"hello\");"
              " ret s.len(); }",
        5);
}

TEST_F(RuntimeTest, StringEmptyIndex)
{
    expectRun("fn main() -> i32 { let s = String::new();"
              " match s.index(0) { Some(c) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, StringMutateByte)
{
    // `s.data[0] = 'x'` is no longer expressible from user code (the buffer is a
    // stdlib-private raw pointer), so the round-trip goes through the safe API.
    expectRun("fn main() -> i32 { let mut s = String::from_lit(\"hi\");"
              " s.push_char('x'); match s.index(2) { Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        120);
}

TEST_F(RuntimeTest, OpOverloadChainedPrecedence)
{
    // a + b * c: * binds tighter than + even under overloading → 1 + 6 = 7.
    expectRun("struct V { pub x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } impl Mul for V { fn mul(self, o: Self) -> V {"
              " ret V { x: self.x * o.x }; } } fn main() -> i32 {"
              " let a = V { x: 1 }; let b = V { x: 2 }; let c = V { x: 3 };"
              " let r = a + b * c; ret r.x; }",
        7);
}

// ── E: option.lis ──────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, OptionIsSomeOnSome)
{
    expectRun("fn main() -> i32 { let o = Option::Some(7); if is_some(o) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, OptionIsNoneOnNone)
{
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let o = none_i(); if is_none(o) { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, OptionOrSomeNone)
{
    // or(Some(1), Some(9)) → Some(1) — first Some wins.
    expectRun("fn main() -> i32 { let a = or(Option::Some(1), Option::Some(9));"
              " match a { Some(v) => { ret v; }, None => { ret 0; } } }",
        1);
}

// ── E: math.lis — min/max/clamp/abs ───────────────────────────────────────────

TEST_F(RuntimeTest, MathMinBasic)
{
    expectRun("fn main() -> i32 { ret min(3, 7); }", 3);
}

TEST_F(RuntimeTest, MathMinNegative)
{
    // min(-7, 3): -7 < 3 → -7. Take the magnitude to keep the exit code clean.
    expectRun("fn main() -> i32 { let m = min(0 - 7, 3); if m < 0 { ret 0 - m; } ret m; }", 7);
}

TEST_F(RuntimeTest, MathClampEqual)
{
    expectRun("fn main() -> i32 { ret clamp(10, 0, 10); }", 10);
}

TEST_F(RuntimeTest, MathFabsPositive)
{
    expectOutput("fn main() -> i32 { print_float(fabs(2.25)); println(); ret 0; }",
        "2.250000\n",
        0);
}

TEST_F(RuntimeTest, MathGcdSame)
{
    expectRun("fn main() -> i32 { ret gcd(6, 6); }", 6);
}

TEST_F(RuntimeTest, MathLcmCoprime)
{
    expectRun("fn main() -> i32 { ret lcm(3, 5); }", 15);
}

TEST_F(RuntimeTest, MathIpowZeroExponent)
{
    expectRun("fn main() -> i32 { ret ipow(5, 0); }", 1);
}

TEST_F(RuntimeTest, MathIsEvenFalse)
{
    expectRun("fn main() -> i32 { if is_even(7) { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, MathIsOddFalse)
{
    expectRun("fn main() -> i32 { if is_odd(10) { ret 1; } ret 0; }", 0);
}

// ── E: math.lis — float helpers ────────────────────────────────────────────────

TEST_F(RuntimeTest, MathDegToRadHalfPi)
{
    // deg_to_rad(90) = π/2 ≈ 1.5707963...
    expectOutput("fn main() -> i32 { let r = deg_to_rad(90.0);"
                 " print_float(r); println(); ret 0; }",
        "1.570796\n",
        0);
}

TEST_F(RuntimeTest, CharIsDigitFalse)
{
    expectRun("fn main() -> i32 { if is_digit('a') { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, CharIsAlphaNonLetter)
{
    expectRun("fn main() -> i32 { if is_alpha('1') { ret 1; } ret 0; }", 0);
}

TEST_F(RuntimeTest, CharIsWhitespaceSpace)
{
    expectRun("fn main() -> i32 { if is_whitespace(' ') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharDigitToInt)
{
    expectRun("fn main() -> i32 { ret digit_to_int('7'); }", 7);
}

TEST_F(RuntimeTest, CharDigitToIntNine)
{
    expectRun("fn main() -> i32 { ret digit_to_int('9'); }", 9);
}

TEST_F(RuntimeTest, IteratorRangeCount)
{
    expectRun("fn main() -> i32 { ret count(range(1, 5)); }", 4);
}

TEST_F(RuntimeTest, IteratorLastSome)
{
    expectRun("fn main() -> i32 { let l = last(range(1, 5));"
              " match l { Some(v) => { ret v; }, None => { ret 0; } } }",
        4);
}

TEST_F(RuntimeTest, IteratorNthOutOfBounds)
{
    expectRun("fn main() -> i32 { let n = nth(range(10, 20), 99);"
              " match n { Some(v) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, ExampleDrop)
{
    expectExample("drop", 0);
}

TEST_F(RuntimeTest, ExampleExample)
{
    expectExample("example", 0);
}

TEST_F(RuntimeTest, ExampleIo)
{
    // io.lis reads from stdin; the baseline run with empty input exits 0.
    expectExample("io", 0);
}

TEST_F(RuntimeTest, ExampleOwnership)
{
    expectExample("ownership", 0);
}

// ── B2: more drop semantics ────────────────────────────────────────────────────

TEST_F(RuntimeTest, DropImplCompilesAndRuns)
{
    expectRun("struct D { pub v: i32 } impl Drop for D { fn drop(self) { } }"
              " fn main() -> i32 { let d = D { v: 5 }; ret d.v; }",
        5);
}

TEST_F(RuntimeTest, DropStringFrees)
{
    // String has impl Drop (frees the buffer) — must not double-free or leak.
    expectRun("fn main() -> i32 { let s = String::from_lit(\"hi\"); ret s.len(); }", 2);
}

// ── Conditional drops (drop flags) ─────────────────────────────────────────────
//
// A value moved out on ONE path is still owned on the others, so its drop has
// no static home at the branch. The old lowering dropped it on the edge where
// it happened to be owned, right before the join — which RELEASED it while the
// rest of its scope was still live (observable through Drop, and a
// use-after-free for an alias the borrow checker does not track), and for a
// partial move it also left the scope-end decomposition to run again (double
// free). Slots are now dropped at the scope end under a run-time ownership bit.

TEST_F(RuntimeTest, ConditionalMoveDropRunsAtScopeEnd)
{
    // Before the fix the counter already read 100 here: the else edge had
    // dropped `a` before the join, i.e. before this statement ran.
    expectRun("let g = 0;\n"
              "struct D { pub v: i32 }\n"
              "impl Drop for D { fn drop(self) { g = g + 100; } }\n"
              "fn main() -> i32 { let a = D { v: 1 }; let c = 0;"
              " if c == 1 { let b = a; } let observed = g; ret observed; }",
        0);
}

TEST_F(RuntimeTest, ConditionalMoveOfParameterDropRunsAtExit)
{
    // By-value parameters are owned locals too: moving one on a single path
    // must neither lose its destructor nor run it twice (0 / 200 would be bugs).
    const std::string src =
        "let g = 0;\n"
        "struct D { pub v: i32 }\n"
        "impl Drop for D { fn drop(self) { g = g + 100; } }\n"
        "fn f(s: D, c: i32) -> i32 { if c == 1 { let b = s; } ret 0; }\n"
        "fn main() -> i32 { let r = f(D { v: 1 }, ";
    expectRun(src + "0); ret g; }", 100);
    expectRun(src + "1); ret g; }", 100);
}

TEST_F(RuntimeTest, ConditionalPartialMoveDropsEachFieldOnce)
{
    // A partial move on one path only. Each field must be released exactly
    // once: the moved-out A by its new owner, B by the scope-end drop. The
    // counter is positional (A = *10+1, B = *10+2), so a double free or a
    // lost drop shows up as a different number, not merely as a crash.
    const std::string src =
        "let g = 0;\n"
        "struct A { pub v: i32 }\n"
        "impl Drop for A { fn drop(self) { g = g * 10 + 1; } }\n"
        "struct B { pub v: i32 }\n"
        "impl Drop for B { fn drop(self) { g = g * 10 + 2; } }\n"
        "struct P { pub a: A, pub b: B }\n"
        "fn f(c: i32) -> i32 { let s = P { a: A { v: 1 }, b: B { v: 2 } };"
        " if c == 1 { let x = s.a; } ret 0; }\n"
        "fn main() -> i32 { let r = f(";
    // c == 0: nothing moved, so A then B are released at the scope end (12).
    expectRun(src + "0); ret g; }", 12);
    // c == 1: the moved-out A goes first (at the end of the then block), then
    // the surviving B at the scope end — still 12, never 22 (double free) and
    // never 2 (the moved A lost, or B alone).
    expectRun(src + "1); ret g; }", 12);
}

TEST_F(RuntimeTest, FailedMemberAccessReportsNoCascade)
{
    // An expression whose analysis failed has NO type (null is the analyzer's
    // error convention), so nothing downstream can pile a second diagnostic on
    // top of the real one: the old fallback type was a hard-coded i32, which
    // made `g(s.nope)` add a bogus "argument type mismatch", and the index
    // path's `void` fallback made `let y = x[0];` add "variable y cannot have
    // type void". Two uses of the unknown field, two diagnostics — no more.
    std::string diag;
    ASSERT_FALSE(compileCapture("struct S { pub a: i32 }\n"
                                "fn g(s: S) -> i32 { ret s.a; }\n"
                                "fn main() -> i32 { let s = S { a: 1 };\n"
                                "    let x = s.nope;\n"
                                "    ret g(s.nope); }",
        diag));
    size_t count = 0;
    for (size_t pos = diag.find("error["); pos != std::string::npos;
         pos = diag.find("error[", pos + 1))
        ++count;
    EXPECT_EQ(count, 2u) << "expected exactly one diagnostic per failed member access, got:\n"
                         << diag;
}


// ── E0509: a field cannot leave a type that implements Drop ────────────────────
//
// Language decision (2026-09-15): follow Rust. The type's own destructor
// releases its fields as a whole, so a partially-initialized value would either
// skip that destructor or hand it memory it must not touch (and the moved field
// would be released twice).

TEST_F(RuntimeTest, PartialMoveOutOfDropTypeRejected)
{
    expectCompileFail("struct A { pub v: i32 } impl Drop for A { fn drop(self) { } }"
                      " struct P { pub a: A, pub n: i32 } impl Drop for P { fn drop(self) { } }"
                      " fn main() -> i32 { let p = P { a: A { v: 1 }, n: 2 };"
                      " let x = p.a; ret p.n; }",
        "implements Drop");
}

TEST_F(RuntimeTest, MoveOutOfDropTypeBehindPlainStructRejected)
{
    // The type that must not be left partially initialized is the one the move
    // takes the field OUT of — here `Inner`, even though the root `Outer` has no
    // destructor of its own.
    expectCompileFail("struct Inner { pub s: String } impl Drop for Inner { fn drop(self) { } }"
                      " struct Outer { pub i: Inner }"
                      " fn main() -> i32 { let o = Outer { i: Inner { s: String::from_lit(\"x\") } };"
                      " let s = o.i.s; ret s.len(); }",
        "implements Drop");
}

TEST_F(RuntimeTest, DiscardedFieldIsAMove)
{
    // `p.s;` releases the field on the spot, so the value is gone: using it
    // again used to compile and hand out an already-freed buffer (double free).
    expectCompileFail("struct P { pub s: String }"
                      " fn main() -> i32 { let p = P { s: String::from_lit(\"hi\") };"
                      " p.s; let y = p.s; ret y.len(); }",
        "use of moved value");
}

TEST_F(RuntimeTest, DiscardedWholeValueIsAMove)
{
    expectCompileFail("struct D { pub v: i32 } impl Drop for D { fn drop(self) { } }"
                      " fn main() -> i32 { let d = D { v: 1 }; d; let e = d; ret 0; }",
        "use of moved value");
}

TEST_F(RuntimeTest, DiscardedFieldOutOfDropTypeRejected)
{
    // The E0509 rule covers the discarded spelling too — the field still leaves
    // a value whose destructor owns it.
    expectCompileFail("struct A { pub v: i32 } impl Drop for A { fn drop(self) { } }"
                      " struct P { pub a: A } impl Drop for P { fn drop(self) { } }"
                      " fn main() -> i32 { let p = P { a: A { v: 1 } }; p.a; ret 0; }",
        "implements Drop");
}
TEST_F(RuntimeTest, MoveOutOfReferenceRejected)
{
    // Rust's E0507 (language decision 2026-09-15): a field cannot be moved out
    // of a place the function only borrows. The borrow owns nothing, so the
    // value would go to the receiver while the referent keeps releasing it —
    // two owners of one buffer. Measured before the rule: heap corruption
    // (0xC0000374) on exit for exactly this snippet.
    expectCompileFail("struct P { pub s: String }"
                      " fn main() -> i32 { let mut p = P { s: String::from_lit(\"hi\") };"
                      " let r = &mut p; let x = r.s; ret x.len(); }",
        "behind the reference");
}

TEST_F(RuntimeTest, CopyFieldReadThroughReferenceAllowed)
{
    // A Copy field through a reference is a read, not a move: nothing is
    // handed over, so the rule does not apply.
    expectRun("struct P { pub n: i32 }"
              " fn main() -> i32 { let mut p = P { n: 7 }; let r = &mut p; let x = r.n; ret x; }",
        7);
}

TEST_F(RuntimeTest, MovingAReferenceItselfIsStillAllowed)
{
    // The rule is about moving a value OUT of a borrowed place, not about the
    // reference: `&mut T` is non-Copy, so `let q = r;` moves the reference
    // itself (locked semantics) and stays legal.
    expectRun("struct P { pub n: i32 }"
              " fn main() -> i32 { let mut p = P { n: 5 }; let r = &mut p; let q = r;"
              " q.n = 9; ret p.n; }",
        9);
}
TEST_F(RuntimeTest, CopyFieldMoveOutOfDropTypeAllowed)
{
    // A Copy field is a read, not a move: nothing is left half-initialized.
    expectRun("struct P { pub v: i32 } impl Drop for P { fn drop(self) { } }"
              " fn main() -> i32 { let p = P { v: 7 }; let x = p.v; ret x; }",
        7);
}

TEST_F(RuntimeTest, PartialMoveOutOfNonDropTypeAllowed)
{
    // No destructor of its own on `P`, so the move is legal — and the moved A
    // must still be released exactly once (by its new owner, at scope end).
    expectRun("let g = 0;"
              " struct A { pub v: i32 } impl Drop for A { fn drop(self) { g = g + 1; } }"
              " struct P { pub a: A, pub n: i32 }"
              " fn main() -> i32 { let p = P { a: A { v: 1 }, n: 2 }; let x = p.a; ret p.n + g; }",
        2);
}

TEST_F(RuntimeTest, WholeValueMoveOfDropTypeAllowed)
{
    // The rule is about a value being LEFT partially moved; moving the whole
    // value leaves nothing behind, so it stays legal.
    expectRun("let g = 0;"
              " struct P { pub v: i32 } impl Drop for P { fn drop(self) { g = g + 1; } }"
              " fn main() -> i32 { let p = P { v: 1 }; let q = p; ret g; }",
        0);
}
TEST_F(RuntimeTest, ConditionalMoveKeepsUntrackedAliasAlive)
{
    // The early drop was a use-after-free through an alias the borrow checker
    // cannot see: `p` is a raw pointer into `a`'s buffer (String::to_cstr()),
    // and the second allocation is what reuses the block the early drop
    // released. Before the fix this printed garbage bytes instead of 'hello'.
    expectOutput("fn main() -> i32 { let a = String::from_lit(\"hello\");"
                 " let p = a.to_cstr(); let c = 0; if c == 1 { let b = a; }"
                 " let z = String::from_lit(\"ZZZZZZZZZZZZZZZZ\");"
                 " print_str(p); println(); ret 0; }",
        "hello\n", 0);
}

TEST_F(RuntimeTest, EnumPayloadCopyBinding)
{
    // A Copy payload (i32) is bound by value; both binding and scrutinee usable.
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 { let o = O::Some(9);"
              " let y = match o { Some(v) => v, None => 0 }; ret y + 1; }",
        10);
}

TEST_F(RuntimeTest, EnumSameNameVariantsDifferentEnums)
{
    expectRun("enum A { X(i32) } enum B { X(i32) } fn main() -> i32 {"
              " let a = A::X(1); let b = B::X(2);"
              " let mut r = 0; match a { X(v) => { r = v; } }"
              " match b { X(v) => { ret r + v; } } }",
        3);
}

TEST_F(RuntimeTest, StringIndexEachPosition)
{
    // Indexing two positions and combining the results. (`acc`, not `sum` — the
    // stdlib already has a `sum` function.)
    expectRun("fn main() -> i32 { let s = String::from_lit(\"abc\");"
              " let mut acc = 0; match s.index(0) { Some(c) => { acc = c as i32; },"
              " None => { acc = 0; } } match s.index(2) { Some(c) => { ret acc + (c as i32); },"
              " None => { ret acc; } } }",
        97 + 99);
}

// ── B2: more operator overloading ──────────────────────────────────────────────

TEST_F(RuntimeTest, OpOverloadGreaterThan)
{
    expectRun("struct V { pub x: i32 } impl PartialOrd for V { fn lt(self, o: Self) -> bool {"
              " ret self.x < o.x; } fn gt(self, o: Self) -> bool { ret self.x > o.x; }"
              " fn le(self, o: Self) -> bool { ret self.x <= o.x; }"
              " fn ge(self, o: Self) -> bool { ret self.x >= o.x; } }"
              " fn main() -> i32 { let a = V { x: 5 }; let b = V { x: 2 };"
              " if a > b { ret 1; } ret 0; }",
        1);
}

// ── B2: more casts and widths ──────────────────────────────────────────────────

TEST_F(RuntimeTest, I16WidenToI32)
{
    expectRun("fn main() -> i32 { let a = 'A' as i16; ret a as i32; }", 65);
}

TEST_F(RuntimeTest, CharAsI32Arithmetic)
{
    expectRun("fn main() -> i32 { let c = 'A' as i32; let d = c + 32;"
              " let e = d as char; ret e as i32; }",
        97);
}

// ── B2: more globals ───────────────────────────────────────────────────────────

TEST_F(RuntimeTest, GlobalUsedInComputation)
{
    expectRun("let base = 10; fn main() -> i32 { ret base * 2 + 5; }", 25);
}

TEST_F(RuntimeTest, GenericEnumMethod)
{
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 {"
              " let o = O::Some(42); match o { Some(v) => { ret v; }, None => { ret 0; } } }",
        42);
}

TEST_F(RuntimeTest, GenericStructWithTwoTypes)
{
    expectRun("struct M<T, U> { pub a: T, pub b: U } fn main() -> i32 {"
              " let m = M { a: 3, b: 'x' }; ret m.a; }",
        3);
}

TEST_F(RuntimeTest, NestedIfDeep)
{
    expectRun("fn main() -> i32 { let x = 5; let mut r = 0;"
              " if x > 1 { if x > 2 { if x > 3 { if x > 4 { r = 1; } } } } ret r; }",
        1);
}

TEST_F(RuntimeTest, ArraySumWithMutableIndex)
{
    expectRun("fn main() -> i32 { let a = [1, 2, 3, 4]; let mut i = 0; let mut s = 0;"
              " while i < 4 { s = s + a[i]; i = i + 1; } ret s; }",
        10);
}

// ── B2: misc expressions ───────────────────────────────────────────────────────

TEST_F(RuntimeTest, ModuloChain)
{
    expectRun("fn main() -> i32 { ret 100 % 9 % 4; }", 1);
}

// ── B3: arithmetic & expressions ───────────────────────────────────────────────

TEST_F(RuntimeTest, ArithmeticModuloAssoc)
{
    expectRun("fn main() -> i32 { ret 17 % 5 % 3; }", 2);
}

TEST_F(RuntimeTest, ArithmeticDivMulPriority)
{
    expectRun("fn main() -> i32 { ret 8 / 2 * 3; }", 12);
}

TEST_F(RuntimeTest, NestedParens)
{
    expectRun("fn main() -> i32 { ret (((2))); }", 2);
}

// NOTE: `a = b = 5` (chained assignment) is not expressible — assignment is a
// statement, not a value. No test for it.

// ── B3: floats & casts ─────────────────────────────────────────────────────────

TEST_F(RuntimeTest, FloatPrecisionAddition)
{
    expectOutput("fn main() -> i32 { print_float(0.1 + 0.2); println(); ret 0; }",
        "0.300000\n",
        0);
}

TEST_F(RuntimeTest, EnumMatchWildcardValue)
{
    expectRun("enum E { A, B, C } fn main() -> i32 { let e = E::C;"
              " ret match e { A => 1, _ => 9 }; }",
        9);
}

// ── B3: strings ────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, StringPushManyChars)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " s.push_char('a'); s.push_char('b'); s.push_char('c');"
              " s.push_char('d'); s.push_char('e'); ret s.len(); }",
        5);
}

TEST_F(RuntimeTest, StringMultipleInFunction)
{
    expectRun("fn main() -> i32 { let a = String::from_lit(\"ab\");"
              " let b = String::from_lit(\"cd\"); ret a.len() + b.len(); }",
        4);
}

TEST_F(RuntimeTest, GenericEnumOfGeneric)
{
    expectRun("struct B<T> { pub v: T } enum O<T> { Some(T), None } fn main() -> i32 {"
              " let b = B { v: 6 }; let o = O::Some(b);"
              " match o { Some(bx) => { ret bx.v; }, None => { ret 0; } } }",
        6);
}

TEST_F(RuntimeTest, GlobalArrayNotLiteralRejected)
{
    expectCompileFail("let g = [1, 2]; fn main() -> i32 { ret 0; }",
        "global variable initializer must be a literal");
}

TEST_F(RuntimeTest, ArrayRefIndexLoop)
{
    expectRun("fn main() -> i32 { let a = [10, 20, 30]; let r = &a; let mut s = 0;"
              " let mut i = 0; while i < 3 { s = s + r[i]; i = i + 1; } ret s; }",
        60);
}

// ── B3: control flow ───────────────────────────────────────────────────────────

TEST_F(RuntimeTest, ForSumEvenOnly)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 11) {"
              " if (x % 2) == 0 { s = s + x; } } ret s; }",
        30);
}

TEST_F(RuntimeTest, BreakInsideNestedLoop)
{
    // Inner break exits only the inner loop.
    expectRun("fn main() -> i32 { let mut c = 0; let mut i = 0; while i < 3 {"
              " let mut j = 0; while j < 10 { c = c + 1; if j == 2 { break; } j = j + 1; }"
              " i = i + 1; } ret c; }",
        9);
}

TEST_F(RuntimeTest, RecursionMutualViaIndirect)
{
    expectRun("fn count2(n: i32) -> i32 { if n == 0 { ret 0; } ret 1 + count2(n - 1); }"
              " fn main() -> i32 { ret count2(5) + count2(3); }",
        8);
}

TEST_F(RuntimeTest, FloatSumOfInts)
{
    expectOutput("fn main() -> i32 { let a = 1 as f64; let b = 2 as f64;"
                 " print_float(a + b); println(); ret 0; }",
        "3.000000\n",
        0);
}

TEST_F(RuntimeTest, GenericIdOnFloat)
{
    expectOutput("fn id<T>(x: T) -> T { ret x; } fn main() -> i32 {"
                 " let f = id(2.5); print_float(f); println(); ret 0; }",
        "2.500000\n",
        0);
}

TEST_F(RuntimeTest, MatchOnGenericEnumBothTypes)
{
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 {"
              " let a = O::Some(3); let b = O::Some('x');"
              " let mut r = 0; match a { Some(v) => { r = v; }, None => {} }"
              " match b { Some(c) => { ret r + (c as i32); }, None => { ret r; } } }",
        123);
}

TEST_F(RuntimeTest, EnumWithTwoPayloadTypes)
{
    expectRun("enum M { A(i32), B(f64) } fn main() -> i32 { let m = M::A(7);"
              " match m { A(v) => { ret v; }, B(_f) => { ret 0; } } }",
        7);
}

// NOTE: `impl W<i32>` (method impl on a specific generic instantiation) resolves
// self.v as the generic T, not i32 — a known limitation; GenericStructMethod
// covers the non-specialized impl form.

TEST_F(RuntimeTest, CharDigitClassification)
{
    expectRun("fn main() -> i32 { if is_digit('9') && is_alpha('z') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, BoolArithmeticGate)
{
    expectRun("fn main() -> i32 { let b = true; let x = b as i32; ret 5 * x; }", 5);
}

TEST_F(RuntimeTest, IfElseNestedThree)
{
    expectRun("fn main() -> i32 { let x = 5; let mut r = 0;"
              " if x > 10 { r = 1; } else { if x > 3 { r = 2; } else { r = 3; } } ret r; }",
        2);
}

TEST_F(RuntimeTest, CharToLowerViaArith)
{
    expectRun("fn main() -> i32 { let upper = 'A' as i32; let lower = upper + 32;"
              " let c = lower as char; ret c as i32; }",
        97);
}

TEST_F(RuntimeTest, MultipleStringsInStruct)
{
    expectRun("struct Pair { pub a: String, pub b: String } fn main() -> i32 {"
              " let p = Pair { a: String::from_lit(\"ab\"), b: String::from_lit(\"cde\") };"
              " ret p.a.len() + p.b.len(); }",
        5);
}

TEST_F(RuntimeTest, FloatMultiplyByIntCast)
{
    expectOutput("fn main() -> i32 { let x = 1.5; let n = 2 as f64;"
                 " print_float(x * n); println(); ret 0; }",
        "3.000000\n",
        0);
}

// ── B5: final certainty batch ──────────────────────────────────────────────────

TEST_F(RuntimeTest, SimpleArithmeticSum)
{
    expectRun("fn main() -> i32 { ret 10 + 20; }", 30);
}

TEST_F(RuntimeTest, SimpleComparisonLt)
{
    expectRun("fn main() -> i32 { if 3 < 4 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleWhileOnce)
{
    expectRun("fn main() -> i32 { let mut i = 0; while i < 1 { i = i + 1; } ret i; }", 1);
}

TEST_F(RuntimeTest, SimpleStructField)
{
    expectRun("struct S { pub v: i32 } fn main() -> i32 { let s = S { v: 4 }; ret s.v; }", 4);
}

TEST_F(RuntimeTest, SimpleStringLen)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"ab\"); ret s.len(); }", 2);
}

TEST_F(RuntimeTest, SimpleBoolOr)
{
    expectRun("fn main() -> i32 { if true || false { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleUnreachableAfterReturn)
{
    expectRun("fn main() -> i32 { let mut x = 0; ret 5; x = x + 1; ret x; }", 5);
}

// ── module system (import) ────────────────────────────────────────────────────

TEST_F(RuntimeTest, MultiFileFullImport)
{
    ASSERT_TRUE(compileMulti(
        "impt math_lib;\n"
        "fn main() -> i32 { ret math_lib::double_it(21); }",
        {{"math_lib", "fn double_it(x: i32) -> i32 { ret x * 2; }"}}));
    EXPECT_EQ(linkAndRun(), 42);
}

TEST_F(RuntimeTest, ModuleQualifiedStructLiteral)
{
    ASSERT_TRUE(compileMulti(
        "impt geom;\n"
        "fn main() -> i32 { let v = geom::Vec2 { x: 5, y: 6 }; ret v.x + v.y; }",
        {{"geom", "struct Vec2 { pub x: i32, pub y: i32 }"}}));
    EXPECT_EQ(linkAndRun(), 11);
}

TEST_F(RuntimeTest, CircularImportRejected)
{
    // a.lis imports b, b.lis imports a — the cycle must be diagnosed.
    std::string diag;
    bool ok = compileMulti(
        "impt a;\nfn main() -> i32 { ret a::f(); }",
        {{"a", "impt b;\nfn f() -> i32 { ret b::g(); }"},
            {"b", "impt a;\nfn g() -> i32 { ret 1; }"}},
        &diag);
    EXPECT_FALSE(ok) << "circular imports must be rejected";
}

// The statement after a diverging call must not run: reaching 'ret 7' would
// exit 7 instead of aborting.
TEST_F(RuntimeTest, PanicSkipsFollowingStatements)
{
    expectPanic("fn main() -> i32 { panic(\"stop\"); ret 7; }", "panicked: stop");
}

// ret panic(...) in a VALUE-returning function: never coerces to any type.
TEST_F(RuntimeTest, ReturnOfPanicSatisfiesAnyReturnType)
{
    expectPanic("fn classify(n: i32) -> i32 { if n > 0 { ret 1; } ret panic(\"negative\"); }\n"
                "fn main() -> i32 { ret classify(0 - 1); }",
        "panicked: negative");
}

// Divergence nested inside a larger expression: the enclosing builder keeps
// emitting (the remaining argument, the call itself) and emit() drops those
// unreachable statements.
TEST_F(RuntimeTest, DivergenceInsideNestedCallArguments)
{
    expectPanic("fn add(a: i32, b: i32) -> i32 { ret a + b; }\n"
                "fn main() -> i32 { ret add(panic(\"in an argument\"), 5); }",
        "panicked: in an argument");
}

// ...but READING such a binding is an error: no value of an uninhabited type can
// exist.
TEST_F(RuntimeTest, NeverBindingCannotBeRead)
{
    expectCompileFail("fn main() -> i32 { let x: never; print_int(x); ret 0; }",
        "uninhabited type 'never'");
}

TEST_F(RuntimeTest, NeverCoercesAsArrayElement)
{
    expectPanic("fn main() -> i32 { let a = [panic(\"elem\"), 2, 3]; ret 0; }",
        "panicked: elem");
}

// A value whose generic arguments cannot be inferred keeps the type DEFINITION
// (whose fields still contain the bare parameter). That is rejected with an
// explanation instead of reaching codegen.
TEST_F(RuntimeTest, UninferredGenericArgumentInCallRejected)
{
    expectCompileFail("fn get(o: Option<i32>) -> i32 { ret 0; }\n"
                      "fn main() -> i32 { ret get(Option::None); }",
        "cannot infer the generic argument(s) of 'Option'");
}

TEST_F(RuntimeTest, PanicArgumentTypeIsChecked)
{
    expectCompileFail("fn main() -> i32 { panic(42); ret 0; }",
        "builtin 'panic' expects an argument of type '&int8'");
}

// void is a no-value type: a void-typed binding could never be read.
TEST_F(RuntimeTest, VoidTypedVariableRejected)
{
    expectCompileFail("fn side() { }\nfn main() -> i32 { let x = side(); ret 0; }",
        "cannot have type 'void'");
}

// unwrap takes self BY VALUE, so the option is moved (and could not be unwrapped
// twice).
TEST_F(RuntimeTest, OptionUnwrapMovesTheOption)
{
    expectCompileFail("fn main() -> i32 {\n"
                      "    let a = Option::Some(1);\n"
                      "    let x = a.unwrap();\n"
                      "    let y = a.unwrap();\n"
                      "    ret x + y;\n"
                      "}",
        "use of moved value: 'a'");
}

// A two-parameter generic ENUM is what Result<T, E> needs: construct, match, a
// generic function over both parameters, and a method on the generic enum.
TEST_F(RuntimeTest, GenericEnumWithTwoParameterTypes)
{
    expectRun("enum Pair<A, B> { Both(A, B), Neither }\n"
              "fn main() -> i32 { let p = Pair::Both(3, 'x');\n"
              "    match p { Both(a, b) => { ret a; }, Neither => { ret 0; } } }",
        3);
}

TEST_F(RuntimeTest, GenericEnumTwoTypesNonCopyPayloads)
{
    expectOutput("enum Pair<A, B> { Both(A, B), Neither }\n"
                 "fn main() -> i32 {\n"
                 "    let p = Pair::Both(String::from_lit(\"hello\"), String::from_lit(\"world\"));\n"
                 "    match p { Both(a, b) => { print_str(a.to_cstr()); }, Neither => { print_int(0); } }\n"
                 "    println();\n"
                 "    ret 0;\n"
                 "}",
        "hello\n", 0);
}

// A branch that cannot fall through (it returns) contributes no state: the other
// branch decides.
TEST_F(RuntimeTest, UninitReturningBranchDoesNotBlock)
{
    expectRun("fn f(c: bool) -> i32 { let mut x: i32;\n"
              "    if c { x = 1; } else { ret 9; }\n"
              "    ret x; }\n"
              "fn main() -> i32 { ret f(true); }",
        1);
}

TEST_F(RuntimeTest, UninitAssignedBeforeLoopAccepted)
{
    expectRun("fn main() -> i32 { let mut x: i32; x = 0; let mut i = 0;\n"
              "    while i < 3 { i = i + 1; let y = x + 1; }\n"
              "    ret x; }",
        0);
}

TEST_F(RuntimeTest, UninitBorrowOfUninitRejected)
{
    expectCompileFail("fn main() -> i32 { let x: i32; let r = &x; ret 0; }",
        "use of uninitialized value");
}

TEST_F(RuntimeTest, ResultUnwrapAndExpect)
{
    expectRun("fn one() -> Result<i32, i32> { ret Result::Ok(3); }\n"
              "fn main() -> i32 { let r = one(); ret r.unwrap() + one().expect(\"wanted a value\"); }",
        6);
    expectPanic("fn bad() -> Result<i32, i32> { ret Result::Err(9); }\n"
                "fn main() -> i32 { let r = bad(); ret r.unwrap(); }",
        "panicked: called unwrap on an Err value");
    expectPanic("fn bad() -> Result<i32, i32> { ret Result::Err(9); }\n"
                "fn main() -> i32 { let r = bad(); ret r.expect(\"a value was required\"); }",
        "panicked: a value was required");
}

TEST_F(RuntimeTest, TryOperatorInsideMatchArmAndLoop)
{
    expectRun("fn one() -> Result<i32, i32> { ret Result::Ok(1); }\n"
              "fn wrap(c: i32) -> Result<i32, i32> { ret Result::Ok(c); }\n"
              "fn pick(c: i32) -> Result<i32, i32> {\n"
              "    match wrap(c) {\n"
              "        Ok(v) => { let w = one()?; ret Result::Ok(v + w); },\n"
              "        Err(e) => { ret Result::Err(e); },\n"
              "    }\n"
              "}\n"
              "fn main() -> i32 { let r = pick(5);\n"
              "    match r { Ok(v) => { ret v; }, Err(e) => { ret 0; } } }",
        6);
    expectRun("fn one() -> Result<i32, i32> { ret Result::Ok(1); }\n"
              "fn f() -> Result<i32, i32> {\n"
              "    let mut s = 0;\n"
              "    let mut i = 0;\n"
              "    while i < 3 { s = s + one()?; i = i + 1; }\n"
              "    ret Result::Ok(s);\n"
              "}\n"
              "fn main() -> i32 { let r = f();\n"
              "    match r { Ok(v) => { ret v; }, Err(e) => { ret 0; } } }",
        3);
}

TEST_F(RuntimeTest, TryOperatorRequiresResultReturningFunction)
{
    expectCompileFail("fn one() -> Result<i32, i32> { ret Result::Ok(1); }\n"
                      "fn f() -> i32 { let x = one()?; ret x; }\n"
                      "fn main() -> i32 { ret 0; }",
        "requires the enclosing function to return");
}
