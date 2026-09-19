// Split out of RuntimeTest.cpp: the fixture and the helpers live in
// RuntimeTestFixture.hpp, and each runtime test TU compiles in PARALLEL
// (see TestModule.py). Keep new tests in whichever file fits; the split is
// purely about compile time.

#include "RuntimeTestFixture.hpp"


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

// ── ownership / drop glue at runtime ──────────────────────────────────────────

TEST_F(RuntimeTest, DropGlueRunsAtBlockEnd)
{
    // A block-end drop must invoke the user Drop impl once (observable side effect).
    expectRun("let counter = 0; struct X { pub v: i32 } impl Drop for X { fn drop(self) { counter = counter + 1; } }"
              " fn main() -> i32 { { let x = X { v: 1 }; } ret counter; }",
        1);
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

TEST_F(RuntimeTest, EnumMatchWildcard)
{
    expectRunWithPrologue("enum Option<T> { Some(T), None } fn main() -> i32 {"
                          " let o = Option::Some(3);"
                          " match o { Some(v) => { ret v; }, _ => { ret 99; }, }"
                          " }",
        "",
        3);
}

TEST_F(RuntimeTest, EnumMatchNonCopyNoDoubleFree)
{
    // The moved payload is dropped exactly once (drop glue counter).
    expectRunWithPrologue("enum Option<T> { Some(T), None } struct Inner { pub v: i32 }"
                          " let ctr = 0; impl Drop for Inner { fn drop(self) { ctr = ctr + 1; } }"
                          " fn main() -> i32 { { let o = Option::Some(Inner{v: 5});"
                          " match o { Some(x) => { }, None => { }, } }"
                          " ret ctr; }",
        "impt drop { Drop };\n",
        1);
}

// ── math helpers ───────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, MathMinMax)
{
    expectRun("fn main() -> i32 { ret min(3, 7) + max(3, 7); }", 10);
}

TEST_F(RuntimeTest, StructLogicalOpRejected)
{
    expectCompileFail("struct box { pub a: i32 } fn main() -> i32 {"
                      " let x = box{a:1}; if x && x { ret 1; } ret 0; }",
        "bool");
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

// ── input builtins (read_line / read_int / read_f64) ─────────────────────────

TEST_F(RuntimeTest, ReadInt)
{
    expectOutputWithInput(
        "fn main() -> i32 { let n = read_int(); print_int(n); println(); ret 0; }",
        "42\n",
        "42\n",
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

TEST_F(RuntimeTest, ArrayMovedNotCopied)
{
    // Arrays are Move: `let b = a` invalidates a (single ownership).
    expectCompileFail("fn main() -> i32 { let a = [1, 2]; let b = a; ret a[0]; }",
        "moved");
}

TEST_F(RuntimeTest, StringPushAndGrow)
{
    // from_lit("hello") + push_char + push_str — exercises the buffer grow path.
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hello\"); let mut t = s;"
                 " t.push_char(' '); t.push_str(\"world\");"
                 " print_str(t.to_cstr()); println(); ret t.len(); }",
        "hello world\n",
        11);
}

TEST_F(RuntimeTest, ToStringBuiltins)
{
    expectOutput("fn main() -> i32 { let a = to_string_i32(42); print_str(a.to_cstr()); println();"
                 " let b = to_string_f64(3.5); print_str(b.to_cstr()); println();"
                 " let c = to_string_bool(true); print_str(c.to_cstr()); println(); ret 0; }",
        "42\n3.500000\n1\n",
        0);
}

// ── raw pointer types (*T / *mut T): the heap buffer's real type ───────────────
// A raw pointer is a first-class type (parameter, return type, struct field) and
// lowers to opaque `ptr`. It owns nothing: a struct holding one gets NO drop
// glue (that is why needsDrop() exists next to isCopyable()).

TEST_F(RuntimeTest, PointerTypeIsAFirstClassType)
{
    expectRun("struct Buf { pub data: *mut i8, pub count: i32 }"
              " fn count_of(b: &Buf) -> i32 { ret b.count; }"
              " fn take(p: *i8, q: *mut i8) -> i32 { ret 0; }"
              " fn main() -> i32 { ret 0; }",
        0);
}

TEST_F(RuntimeTest, PublicFieldIsAccessibleAnywhere)
{
    expectRun("struct S { pub v: i32 } fn main() -> i32 { let s = S { v: 5 }; ret s.v; }", 5);
}

TEST_F(RuntimeTest, PrivateFieldIsInaccessibleAcrossModules)
{
    // The same rule across module boundaries — another module cannot read the
    // field (the diagnostic itself is asserted by the tests above).
    EXPECT_FALSE(compileMulti(
        "impt types { S };\nfn main() -> i32 { let s = S::make(); ret s.v; }",
        {{"types", "struct S { v: i32 } impl S { fn make() -> S { ret S { v: 1 }; } }"}}));
}

// B2: array size above the element cap → clean error, not an LLVM assert.
TEST_F(RuntimeTest, ArraySizeOverLimitRejected)
{
    expectCompileFail("fn main() -> i32 { let a: [i32; 5000000000] = [1]; ret a[0]; }",
        "exceeds the limit");
}

TEST_F(RuntimeTest, ArrayAsFunctionReturnRejected)
{
    expectCompileFail("fn f() -> [i32; 2] { ret [1, 2]; } fn main() -> i32 { ret 0; }",
        "array type cannot be a function return");
}

TEST_F(RuntimeTest, GlobalCallInitializerRejected)
{
    expectCompileFail("let s = String::new(); fn main() -> i32 { ret 0; }",
        "global variable initializer must be a literal");
}

TEST_F(RuntimeTest, ReservedLibcNameRejected)
{
    expectCompileFail("fn strlen(s: &i8) -> i32 { ret 0; } fn main() -> i32 { ret 0; }",
        "reserved by the compiler");
}

// E2 (runtime): write THROUGH a &mut field of an immutable binding is allowed.
TEST_F(RuntimeTest, WriteThroughMutFieldIndex)
{
    // The old spelling wrote through String's `&mut i8` buffer field. That buffer
    // is a stdlib-private raw pointer now, so the same rule — writing through a
    // `&mut` FIELD does not require the root binding to be `mut` — is exercised
    // with a `&mut [i32; N]` field.
    expectOutput("struct Holder { pub buf: &mut [i32; 2] }"
                 " fn main() -> i32 { let mut a = [1, 2]; let h = Holder { buf: &mut a };"
                 " h.buf[0] = 5; print_int(a[0]); println(); ret 0; }",
        "5\n",
        0);
}

// Same, but the attribute in front of an ASSIGNMENT statement.
TEST_F(RuntimeTest, IKnowAttributeOnAssignment)
{
    expectRun("fn main() -> i32 { let big: i64 = 5 as i64; let mut t: i32 = 0;"
              " #[i_know] t = big as i32;"
              " ret t; }",
        5);
}

// ── P12 regression: control flow nested in an if body must execute ────────────
// buildIf sealed the branch ENTRY block (thenId/elseId) with Goto(join), which
// OVERWROTE the nested control flow's own terminator — the inner if/while/for
// body became unreachable. Seal the branch's actual END block instead.

TEST_F(RuntimeTest, NestedIfInsideIfExecutes)
{
    expectRun("fn main() -> i32 { let x = 5; let mut r = 0;"
              " if x > 3 { if x > 4 { r = 1; } } ret r; }",
        1);
}

TEST_F(RuntimeTest, IfInsideWhileBodyRegression)
{
    // Direction check: while body containing an if already worked; must stay.
    expectRun("fn main() -> i32 { let mut i = 0; let mut r = 0;"
              " while i < 2 { if true { r = 1; } i = i + 1; } ret r; }",
        1);
}

// ── P14 regression: bool→int cast must be a zero-extension ────────────────────
// bool is lowered to LLVM i1; the widening cast used SExt, so `true as i32`
// sign-extended to -1 instead of 1. bool is unsigned → ZExt.

TEST_F(RuntimeTest, BoolTrueCastToIntIsOne)
{
    expectRun("fn main() -> i32 { let b = true; ret b as i32; }", 1);
}

TEST_F(RuntimeTest, ArithmeticLeftAssociativeSubtraction)
{
    // Equal precedence is left-associative: (100 - 50) - 25 = 25, not
    // 100 - (50 - 25) = 75.
    expectRun("fn main() -> i32 { ret 100 - 50 - 25; }", 25);
}

TEST_F(RuntimeTest, ArithmeticModuloExactMultiple)
{
    expectRun("fn main() -> i32 { ret 20 % 5; }", 0);
}

// ── B: integer widths (i8/i16/i64) ─────────────────────────────────────────────
// i8/i16 values come from char casts (`'A' as i8`); i64 from widening casts.

TEST_F(RuntimeTest, I8AdditionWraps)
{
    // 65 + 65 = 130 overflows signed i8 → -126.
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = 'A' as i8;"
              " let c = a + b; ret c as i32; }",
        -126);
}

TEST_F(RuntimeTest, I64Division)
{
    expectRun("fn main() -> i64 { let a = 100 as i64; let b = 25 as i64;"
              " let c = a / b; ret c; }",
        4);
}

// ── B: float (f64) semantics ───────────────────────────────────────────────────

TEST_F(RuntimeTest, FloatAddition)
{
    expectOutput("fn main() -> i32 { print_float(1.5 + 2.5); println(); ret 0; }",
        "4.000000\n",
        0);
}

TEST_F(RuntimeTest, FloatEquality)
{
    expectRun("fn main() -> i32 { if 1.0 == 1.0 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, FloatScientificLiteral)
{
    // 1e2 is 100.0.
    expectOutput("fn main() -> i32 { print_float(1e2); println(); ret 0; }",
        "100.000000\n",
        0);
}

TEST_F(RuntimeTest, IntToCharCast)
{
    expectRun("fn main() -> i32 { let x = 66 as i32; let c = x as char; ret c as i32; }", 66);
}

TEST_F(RuntimeTest, FloatToIntCastRejected)
{
    expectCompileFail("fn main() -> i32 { let x = 3.7 as i32; ret x; }", "cannot be cast");
}

TEST_F(RuntimeTest, SameTypeCastUselessInfo)
{
    // i32 → i32 is a useless cast (INFO, not an error) — compile succeeds.
    expectRun("fn main() -> i32 { ret 5 as i32; }", 5);
}

TEST_F(RuntimeTest, I8ToI16Widening)
{
    expectRun("fn main() -> i32 { let a = 'A' as i8; let b = a as i16; ret b as i32; }", 65);
}

TEST_F(RuntimeTest, WhileTrueWithBreak)
{
    expectRun("fn main() -> i32 { let mut i = 0; while true { i = i + 1;"
              " if i > 5 { break; } } ret i; }",
        6);
}

TEST_F(RuntimeTest, WhileBodyUsesLoopVar)
{
    expectRun("fn main() -> i32 { let mut s = 0; let mut i = 1;"
              " while i <= 5 { s = s + i * 10; i = i + 1; } ret s; }",
        150);
}

TEST_F(RuntimeTest, ForRangeEmpty)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(5, 5) { s = s + x; } ret s; }", 0);
}

TEST_F(RuntimeTest, ForNestedProductSum)
{
    expectRun("fn main() -> i32 { let mut s = 0; for i in range(1, 4) {"
              " for j in range(1, 4) { s = s + i * j; } } ret s; }",
        36);
}

TEST_F(RuntimeTest, RecursionFibonacci)
{
    expectRun("fn fib(n: i32) -> i32 { if n < 2 { ret n; }"
              " ret fib(n - 1) + fib(n - 2); } fn main() -> i32 { ret fib(7); }",
        13);
}

TEST_F(RuntimeTest, RecursionSumUpTo)
{
    // `sumto` (not `sum` — collides with stdlib). Sum(20)=210 fits the 8-bit
    // exit code; sum(100)=5050 would truncate to 186.
    expectRun("fn sumto(n: i32) -> i32 { if n == 0 { ret 0; } ret n + sumto(n - 1); }"
              " fn main() -> i32 { ret sumto(20); }",
        210);
}

TEST_F(RuntimeTest, FunctionPointerChained)
{
    expectRun("fn inc(x: i32) -> i32 { ret x + 1; } fn main() -> i32 { let f = inc;"
              " let a = f(10); let b = f(a); ret b; }",
        12);
}

TEST_F(RuntimeTest, BitAndZero)
{
    expectRun("fn main() -> i32 { ret 7 & 0; }", 0);
}

TEST_F(RuntimeTest, BitOpsCombined)
{
    // (12 & 10) | 3 = 8 | 3 = 11.
    expectRun("fn main() -> i32 { ret (12 & 10) | 3; }", 11);
}

TEST_F(RuntimeTest, BitAndWithComparisons)
{
    expectRun("fn main() -> i32 { let a = 6 & 3; let b = 8 | 1;"
              " if a < b { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, EnumPayloadMatch)
{
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 { let o = O::Some(7);"
              " match o { Some(v) => { ret v; }, None => { ret 0; } } }",
        7);
}

TEST_F(RuntimeTest, EnumMatchNonExhaustiveRejected)
{
    expectCompileFail("enum E { A, B } fn main() -> i32 { let e = E::A;"
                      " match e { A => { ret 0; } } }",
        "exhaustive");
}

TEST_F(RuntimeTest, EnumNestedMatch)
{
    expectRun("enum A { X(i32), Y } enum B { M(i32), N } fn main() -> i32 {"
              " let a = A::X(3); match a { X(v) => { match B::M(v) {"
              " M(w) => { ret w; }, N => { ret 0; } } }, Y => { ret 0; } } }",
        3);
}

TEST_F(RuntimeTest, GlobalReadFromFunction)
{
    expectRun("let g = 5; fn get() -> i32 { ret g; } fn main() -> i32 { ret get(); }", 5);
}

TEST_F(RuntimeTest, GlobalFloatLiteral)
{
    expectOutput("let g = 2.5; fn main() -> i32 { print_float(g); println(); ret 0; }",
        "2.500000\n",
        0);
}

TEST_F(RuntimeTest, GenericStructPair)
{
    expectRun("struct Pair<T> { pub a: T, pub b: T } fn main() -> i32 {"
              " let p = Pair { a: 3, b: 4 }; ret p.a + p.b; }",
        7);
}

TEST_F(RuntimeTest, GenericUnboundedAddRejected)
{
    expectCompileFail("fn f<T>(x: T) -> T { ret x + x; } fn main() -> i32 { ret 0; }",
        "Numeric");
}

TEST_F(RuntimeTest, GenericOperatorOverload)
{
    expectRun("struct V { pub x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } fn main() -> i32 {"
              " let a = V { x: 3 }; let b = V { x: 4 }; ret (a + b).x; }",
        7);
}

// ── B: strings ─────────────────────────────────────────────────────────────────

TEST_F(RuntimeTest, StringNewIsEmpty)
{
    expectRun("fn main() -> i32 { let s = String::new(); ret s.len(); }", 0);
}

TEST_F(RuntimeTest, StringIndexInBounds)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"abc\");"
              " match s.index(1) { Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        98);
}

TEST_F(RuntimeTest, StringToCstrPrints)
{
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hello\");"
                 " let p = s.to_cstr(); print_str(p); println(); ret 0; }",
        "hello\n",
        0);
}

TEST_F(RuntimeTest, OpOverloadMultiplication)
{
    expectRun("struct V { pub x: i32 } impl Mul for V { fn mul(self, o: Self) -> V {"
              " ret V { x: self.x * o.x }; } } fn main() -> i32 {"
              " let a = V { x: 6 }; let b = V { x: 7 }; ret (a * b).x; }",
        42);
}

TEST_F(RuntimeTest, OpOverloadNotEqual)
{
    expectRun("struct V { pub x: i32 } impl PartialEq for V { fn eq(self: &Self, o: &Self) -> bool {"
              " ret self.x == o.x; } fn ne(self: &Self, o: &Self) -> bool { ret self.x != o.x; } }"
              " fn main() -> i32 { let a = V { x: 1 }; let b = V { x: 2 };"
              " if a != b { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, OpOverloadOnGeneric)
{
    expectRunWithPrologue("struct V { pub x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
                          " ret V { x: self.x + o.x }; } } fn sum<T: Add>(a: T, b: T) -> T { ret a + b; }"
                          " fn main() -> i32 { let r = sum(V { x: 2 }, V { x: 5 }); ret r.x; }",
        kMathPrologue,
        7);
}

TEST_F(RuntimeTest, OptionUnwrapOrNone)
{
    expectRun("fn main() -> i32 { ret unwrap_or(Option::None, 0); }", 0);
}

TEST_F(RuntimeTest, OptionAndNoneSome)
{
    // and(None, Some(5)) → None.
    expectRun("fn main() -> i32 { let a = and(Option::None, Option::Some(5));"
              " match a { Some(v) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, OptionOrNoneNone)
{
    expectRun("fn none_i() -> Option<i32> { ret Option::None; }"
              " fn main() -> i32 { let a = or(none_i(), none_i());"
              " match a { Some(v) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, MathMaxEqual)
{
    expectRun("fn main() -> i32 { ret max(9, 9); }", 9);
}

TEST_F(RuntimeTest, MathClampBelow)
{
    expectRun("fn main() -> i32 { ret clamp(0 - 1, 0, 10); }", 0);
}

TEST_F(RuntimeTest, MathAbsNegative)
{
    expectRun("fn main() -> i32 { ret abs(0 - 7); }", 7);
}

// ── E: math.lis — gcd/lcm/ipow ─────────────────────────────────────────────────

TEST_F(RuntimeTest, MathGcdBasic)
{
    expectRun("fn main() -> i32 { ret gcd(12, 18); }", 6);
}

TEST_F(RuntimeTest, MathLcmBasic)
{
    expectRun("fn main() -> i32 { ret lcm(4, 6); }", 12);
}

TEST_F(RuntimeTest, MathIpowBasic)
{
    expectRun("fn main() -> i32 { ret ipow(2, 10); }", 1024);
}

// ── E: math.lis — is_even/is_odd/sign ──────────────────────────────────────────

TEST_F(RuntimeTest, MathIsEvenTrue)
{
    expectRun("fn main() -> i32 { if is_even(10) { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, MathSignNegative)
{
    // sign(-5) = -1.
    expectRun("fn main() -> i32 { let s = sign(0 - 5); if s < 0 { ret 0 - s; } ret s; }", 1);
}

TEST_F(RuntimeTest, MathLerpMidpoint)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 0.5)); println(); ret 0; }",
        "5.000000\n",
        0);
}

TEST_F(RuntimeTest, MathLerpEnd)
{
    expectOutput("fn main() -> i32 { print_float(lerp(0.0, 10.0, 1.0)); println(); ret 0; }",
        "10.000000\n",
        0);
}

TEST_F(RuntimeTest, CharIsAlphaLower)
{
    expectRun("fn main() -> i32 { if is_alpha('a') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsAlphanumericLetter)
{
    expectRun("fn main() -> i32 { if is_alphanumeric('x') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharIsWhitespaceNewline)
{
    expectRun("fn main() -> i32 { if is_whitespace('\\n') { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, CharDigitToIntNonDigit)
{
    expectRun("fn main() -> i32 { ret digit_to_int('x'); }", 0);
}

TEST_F(RuntimeTest, IteratorRangeSumEmpty)
{
    expectRun("fn main() -> i32 { ret sum(range(5, 5)); }", 0);
}

TEST_F(RuntimeTest, IteratorFirstEmpty)
{
    expectRun("fn main() -> i32 { let f = first(range(5, 5));"
              " match f { Some(v) => { ret 1; }, None => { ret 0; } } }",
        0);
}

TEST_F(RuntimeTest, IteratorNthZero)
{
    expectRun("fn main() -> i32 { let n = nth(range(5, 10), 0);"
              " match n { Some(v) => { ret v; }, None => { ret 0; } } }",
        5);
}

// ── F: Examples regression ─────────────────────────────────────────────────────
// Every Examples/*.lis must keep compiling and producing its baseline exit code
// (the documented outputs: borrow 55 / iterator 23 / match 8 / method_ref 10 /
// operator 41 / ...). Prevents silent rot of the canonical examples.

TEST_F(RuntimeTest, ExampleBorrow)
{
    expectExample("borrow", 55);
}

TEST_F(RuntimeTest, DropRunsAtScopeEnd)
{
    // The drop body runs when the value goes out of scope; observable via a
    // global counter written in drop. (Globals use `let`, no `mut` keyword.)
    expectRun("let dropped = 0; struct D { pub v: i32 } impl Drop for D {"
              " fn drop(self) { dropped = dropped + 1; } }"
              " fn main() -> i32 { { let d = D { v: 1 }; } ret dropped; }",
        1);
}

// ── B2: more enums and match ───────────────────────────────────────────────────

TEST_F(RuntimeTest, EnumUnitConstructionAndDispatch)
{
    // A value-arm match must be `ret match ...` — a bare trailing match does not
    // implicitly return its value.
    expectRun("enum Color { Red, Green, Blue } fn main() -> i32 {"
              " let c = Color::Blue; ret match c { Red => 1, Green => 2, Blue => 3 }; }",
        3);
}

TEST_F(RuntimeTest, MatchValueThenSideEffect)
{
    // A value-arm match yields its value; side effects can run after on a global.
    // (`tally`, not `count` — the stdlib already has a `count` function.)
    expectRun("let tally = 0; enum E { A, B } fn main() -> i32 {"
              " let e = E::A; let y = match e { A => 10, B => 20 };"
              " tally = tally + y; ret tally; }",
        10);
}

TEST_F(RuntimeTest, StringToCstrLengthMatches)
{
    // `to_cstr` returns a borrow of the buffer; it must be exactly the content
    // (no `__strlen` — the heap primitives are stdlib-only now).
    expectOutput("fn main() -> i32 { let s = String::from_lit(\"hello\");"
                 " print_str(s.to_cstr()); ret s.len(); }",
        "hello",
        5);
}

TEST_F(RuntimeTest, OpOverloadLeGe)
{
    // The comparison traits borrow their operands, so one value can be compared
    // more than once.
    expectRun("struct V { pub x: i32 } impl PartialOrd for V { fn lt(self: &Self, o: &Self) -> bool {"
              " ret self.x < o.x; } fn gt(self: &Self, o: &Self) -> bool { ret self.x > o.x; }"
              " fn le(self: &Self, o: &Self) -> bool { ret self.x <= o.x; }"
              " fn ge(self: &Self, o: &Self) -> bool { ret self.x >= o.x; } }"
              " fn main() -> i32 { let a = V { x: 3 }; let b = V { x: 3 };"
              " let c = V { x: 3 }; let d = V { x: 3 };"
              " if a <= b && c >= d { ret 1; } ret 0; }",
        1);
}

TEST_F(RuntimeTest, CharCompareChain)
{
    expectRun("fn main() -> i32 { if 'a' < 'b' && 'b' < 'c' { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, FloatIntMixComparison)
{
    // f64 and int compare? They must be the same type for a binary op — reject.
    expectCompileFail("fn main() -> i32 { if 2.5 < 3 { ret 1; } ret 0; }",
        "same type");
}

TEST_F(RuntimeTest, GlobalShadowingRejected)
{
    expectCompileFail("let x = 1; let x = 2; fn main() -> i32 { ret x; }", "already");
}

TEST_F(RuntimeTest, GenericNestedThreeDeep)
{
    expectRun("struct B<T> { pub v: T } fn main() -> i32 {"
              " let b = B { v: B { v: B { v: 3 } } }; ret b.v.v.v; }",
        3);
}

// ── B2: more control flow ──────────────────────────────────────────────────────

TEST_F(RuntimeTest, ForBreakInsideNestedIf)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 10) {"
              " if x == 4 { break; } if x > 1 { s = s + x; } } ret s; }",
        5);
}

TEST_F(RuntimeTest, WhileWithLogicalCondition)
{
    expectRun("fn main() -> i32 { let mut i = 0; let mut s = 0;"
              " while i < 10 && s < 15 { i = i + 1; s = s + i; } ret i; }",
        5);
}

TEST_F(RuntimeTest, ArrayIndexCompare)
{
    expectRun("fn main() -> i32 { let a = [5, 6, 7]; if a[0] < a[2] { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, BoolExpressionLogic)
{
    expectRun("fn main() -> i32 { if (1 < 2) && (3 > 2) || false { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, ShadowedInDifferentFn)
{
    expectRun("fn f() -> i32 { let x = 1; ret x; } fn g() -> i32 { let x = 2; ret x; }"
              " fn main() -> i32 { ret f() + g(); }",
        3);
}

TEST_F(RuntimeTest, ArithmeticModOfSum)
{
    expectRun("fn main() -> i32 { ret (10 + 15) % 7; }", 4);
}

TEST_F(RuntimeTest, EmptyFunctionBody)
{
    expectRun("fn noop() { ret; } fn main() -> i32 { noop(); ret 0; }", 0);
}

TEST_F(RuntimeTest, FloatLargeExponent)
{
    expectOutput("fn main() -> i32 { print_float(1.5e5); println(); ret 0; }",
        "150000.000000\n",
        0);
}

TEST_F(RuntimeTest, I64Negative)
{
    expectRun("fn main() -> i64 { let a = 0 as i64; ret a; }", 0);
}

// ── B3: enums & match ──────────────────────────────────────────────────────────

TEST_F(RuntimeTest, EnumMatchWithMultipleSameType)
{
    expectRun("enum E { A, B, C } fn main() -> i32 { let mut r = 0;"
              " let e = E::A; match e { A => { r = 1; }, B => { r = 2; }, C => { r = 3; } }"
              " let f = E::C; match f { A => { ret r; }, B => { ret r + 1; }, C => { ret r + 2; } } }",
        3);
}

TEST_F(RuntimeTest, StringIndexAfterGrow)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " let mut i = 0; while i < 20 { s.push_char('x'); i = i + 1; }"
              " match s.index(19) { Some(c) => { ret c as i32; }, None => { ret 0; } } }",
        120);
}

TEST_F(RuntimeTest, StructMethodMutatingField)
{
    // A &mut self method that mutates a field through self, observable after.
    expectRun("struct C { pub v: i32 } impl C { fn set(self: &mut Self, x: i32) {"
              " self.v = x; } } fn main() -> i32 { let mut c = C { v: 1 };"
              " c.set(9); ret c.v; }",
        9);
}

TEST_F(RuntimeTest, GlobalBoolInCondition)
{
    expectRun("let flag = true; fn main() -> i32 { if flag { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, ArrayIndexFromVariableZero)
{
    expectRun("fn main() -> i32 { let a = [7, 8]; let i = 0; ret a[i]; }", 7);
}

// ── B3: operator overloading ───────────────────────────────────────────────────

TEST_F(RuntimeTest, OpOverloadDifferentTraits)
{
    expectRun("struct V { pub x: i32 } impl Add for V { fn add(self, o: Self) -> V {"
              " ret V { x: self.x + o.x }; } } impl Sub for V { fn sub(self, o: Self) -> V {"
              " ret V { x: self.x - o.x }; } } fn main() -> i32 {"
              " let a = V { x: 10 }; let b = V { x: 3 }; let c = V { x: 2 };"
              " let r = a - b + c; ret r.x; }",
        9);
}

TEST_F(RuntimeTest, IfElseIfChainedFour)
{
    expectRun("fn main() -> i32 { let x = 40; let mut r = 0;"
              " if x < 10 { r = 1; } else if x < 20 { r = 2; }"
              " else if x < 30 { r = 3; } else { r = 4; } ret r; }",
        4);
}

// ── B4: final runtime breadth ──────────────────────────────────────────────────

TEST_F(RuntimeTest, ArithmeticTripleNested)
{
    expectRun("fn main() -> i32 { ret ((1 + 2) * 3) + 4; }", 13);
}

TEST_F(RuntimeTest, CharLoopFromA)
{
    expectRun("fn main() -> i32 { let mut c = 'a' as i32; let mut n = 0;"
              " while c <= 'e' as i32 { n = n + 1; c = c + 1; } ret n; }",
        5);
}

TEST_F(RuntimeTest, StringConcatViaPush)
{
    expectRun("fn main() -> i32 { let mut s = String::new();"
              " s.push_str(\"foo\"); s.push_str(\"bar\"); ret s.len(); }",
        6);
}

TEST_F(RuntimeTest, IfReturnBothBranches)
{
    expectRun("fn main() -> i32 { let b = false; if b { ret 1; } else { ret 2; } }", 2);
}

TEST_F(RuntimeTest, I16ArithmeticChain)
{
    expectRun("fn main() -> i32 { let a = 'A' as i16; let b = 'A' as i16;"
              " let c = a + b; let d = c as i32; ret d; }",
        130);
}

TEST_F(RuntimeTest, StringReadViaIndexAll)
{
    expectRun("fn main() -> i32 { let s = String::from_lit(\"xyz\");"
              " let mut total = 0; let mut i = 0; while i < 3 {"
              " match s.index(i) { Some(c) => { total = total + (c as i32); }, None => {} }"
              " i = i + 1; } ret total; }",
        120 + 121 + 122);
}

TEST_F(RuntimeTest, NestedForBreakInner)
{
    // Inner break stops j at 2; i=1:3, i=2:6, i=3:9 → 18.
    expectRun("fn main() -> i32 { let mut s = 0; for i in range(1, 4) {"
              " for j in range(1, 4) { if j > 2 { break; } s = s + i * j; } } ret s; }",
        18);
}

TEST_F(RuntimeTest, MultipleGenericInstantiation)
{
    // `T: Copy` because `x + x` consumes a by-value operand twice (2026-09-19).
    expectRun("fn dbl<T: Numeric + Copy>(x: T) -> T { ret x + x; }"
              " fn main() -> i32 { let a = dbl(21); let b = dbl(2); ret a + b; }",
        46);
}

TEST_F(RuntimeTest, FloatCastIntRoundTrip)
{
    expectOutput("fn main() -> i32 { let x = 5 as f64; let y = x + 0.5;"
                 " print_float(y); println(); ret 0; }",
        "5.500000\n",
        0);
}

TEST_F(RuntimeTest, ForBreakPreservesLoopVar)
{
    expectRun("fn main() -> i32 { let mut lastv = 0; for x in range(1, 50) {"
              " if x > 4 { break; } lastv = x; } ret lastv; }",
        4);
}

TEST_F(RuntimeTest, ArrayOfArraysNotAllowedSema)
{
    // Arrays of non-Copy elements are rejected by sema.
    expectCompileFail("fn main() -> i32 { let a = [[1, 2], [3, 4]]; ret 0; }", "Copy");
}

TEST_F(RuntimeTest, NestedGenericEnums)
{
    expectRun("enum O<T> { Some(T), None } fn main() -> i32 {"
              " let inner = O::Some(3); let outer = O::Some(inner);"
              " match outer { Some(i) => { match i { Some(v) => { ret v; }, None => { ret 0; } } },"
              " None => { ret 0; } } }",
        3);
}

TEST_F(RuntimeTest, SimpleArithmeticDiv)
{
    expectRun("fn main() -> i32 { ret 81 / 9; }", 9);
}

TEST_F(RuntimeTest, SimpleComparisonGt)
{
    expectRun("fn main() -> i32 { if 4 > 3 { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleForRange)
{
    expectRun("fn main() -> i32 { let mut s = 0; for x in range(1, 3) { s = s + x; } ret s; }", 3);
}

TEST_F(RuntimeTest, SimpleEnumMatch)
{
    expectRun("enum E { A, B } fn main() -> i32 { let e = E::B;"
              " match e { A => { ret 1; }, B => { ret 2; } } }",
        2);
}

TEST_F(RuntimeTest, SimpleBoolAnd)
{
    expectRun("fn main() -> i32 { if true && true { ret 1; } ret 0; }", 1);
}

TEST_F(RuntimeTest, SimpleReturnEarly)
{
    expectRun("fn main() -> i32 { ret 1; ret 2; }", 1);
}

TEST_F(RuntimeTest, SimpleFloatLiteralPrint)
{
    expectOutput("fn main() -> i32 { print_float(1.0); println(); ret 0; }", "1.000000\n", 0);
}

TEST_F(RuntimeTest, ModuleNestedPathImport)
{
    ASSERT_TRUE(compileMulti(
        "impt lib.nums;\n"
        "fn main() -> i32 { ret nums::double_it(5) + nums::triple_it(10); }",
        {{"lib.nums", "fn double_it(x: i32) -> i32 { ret x * 2; }\n"
                      "fn triple_it(x: i32) -> i32 { ret x * 3; }"}}));
    EXPECT_EQ(linkAndRun(), 40);
}

TEST_F(RuntimeTest, ModuleQualifiedTypeAnnotation)
{
    ASSERT_TRUE(compileMulti(
        "impt geom;\n"
        "fn area(v: geom::Vec2) -> i32 { ret v.x * v.y; }\n"
        "fn main() -> i32 { ret area(geom::Vec2 { x: 3, y: 4 }); }",
        {{"geom", "struct Vec2 { pub x: i32, pub y: i32 }"}}));
    EXPECT_EQ(linkAndRun(), 12);
}

TEST_F(RuntimeTest, ModuleImportAfterLexErrorStillRejected)
{
    // A lex error (`@`) BEFORE an import must not be wiped by the module's nested
    // Lexer::run() — which calls ResetErrorCount() and would otherwise zero the
    // running total, letting a broken program compile (the parser gate would see
    // 0 errors). This imports a FRESH module (not in the stdlib prologue) so
    // loadModule actually runs the nested lexer. Regression for the
    // error-count-reset bug.
    std::string diag;
    bool ok = compileMulti(
        "@\nimpt fresh_mod;\nfn main() -> i32 { ret fresh_mod::f(); }",
        {{"fresh_mod", "fn f() -> i32 { ret 1; }"}},
        &diag);
    EXPECT_FALSE(ok) << "a lex error before an import must still fail the compile";
}

// A diverging BLOCK arm (statement match) next to a normal arm.
TEST_F(RuntimeTest, DivergingBlockArmInMatch)
{
    expectPanic("fn nothing() -> Option<i32> { ret Option::None; }\n"
                "fn get(o: Option<i32>) -> i32 { match o { Some(v) => { ret v; }, None => { panic(\"none arm\"); }, } }\n"
                "fn main() -> i32 { let n = nothing(); ret get(n); }",
        "panicked: none arm");
}

// never is a legal type for a binding (the language's bottom type): the binding
// can simply never hold a value.
TEST_F(RuntimeTest, NeverTypedBindingDeclarationAllowed)
{
    expectRun("fn main() -> i32 { let x: never; ret 0; }", 0);
}

TEST_F(RuntimeTest, NeverCoercesInAssignment)
{
    expectPanic("fn main() -> i32 { let mut x = 0; x = panic(\"assign\"); ret x; }",
        "panicked: assign");
}

// ...and it must NOT reject a body that diverges through another never function,
// nor one that diverges inside a while-true loop (whose exit edge exists
// statically — that is exactly why the check is not a full path analysis).
TEST_F(RuntimeTest, NeverFunctionDivergingIndirectly)
{
    expectPanic("fn inner() -> never { panic(\"inner\"); }\n"
                "fn outer() -> never { inner(); }\n"
                "fn main() -> i32 { outer(); ret 0; }",
        "panicked: inner");
}

// The inferred forms still work when the arguments ARE inferable.
TEST_F(RuntimeTest, GenericInferenceStillWorks)
{
    expectRun("fn get(o: Option<i32>) -> i32 { match o { Some(v) => { ret v; }, None => { ret 0; }, } }\n"
              "fn main() -> i32 { let o = Option::Some(9); ret get(o); }",
        9);
}

TEST_F(RuntimeTest, OptionExpectReturnsThePayload)
{
    expectRun("fn main() -> i32 { let a = Option::Some(41); ret a.expect(\"must be some\"); }", 41);
}

TEST_F(RuntimeTest, OptionExpectOnNoneUsesTheCallerMessage)
{
    expectPanic("fn nothing() -> Option<i32> { ret Option::None; }\n"
                "fn main() -> i32 { let n = nothing(); ret n.expect(\"the config value must be present\"); }",
        "panicked: the config value must be present");
}

// Chained directly on a construction, and on the Option a stdlib helper returns.
TEST_F(RuntimeTest, OptionUnwrapChained)
{
    expectRun("fn main() -> i32 { ret Option::Some(3).unwrap(); }", 3);
}

// ── I: enums with several non-Copy payload slots ──────────────────────────────
//
// A match arm that RETURNS EARLY consumes the scrutinee temp; the OTHER variant's
// payload slot was never written on that path, so dropping it would release
// uninitialized memory. Observable with a Drop counter: before the fix the bogus
// drop ran once per early exit (21 here instead of 11).

TEST_F(RuntimeTest, EnumTwoNonCopyPayloadsNoBogusDrop)
{
    expectRun("let g_drops = 0;\n"
              "struct Leaf { pub v: i32 }\n"
              "impl Drop for Leaf { fn drop(self) { g_drops = g_drops + 1; } }\n"
              "enum P { A(Leaf), B(Leaf) }\n"
              "fn f(p: P) -> i32 { match p { A(a) => { ret 1; }, B(b) => { ret 2; } } }\n"
              "fn main() -> i32 { let x = f(P::A(Leaf { v: 1 })); ret g_drops * 10 + x; }",
        11);
}

TEST_F(RuntimeTest, UninitOneBranchMissingRejected)
{
    expectCompileFail("fn main() -> i32 { let mut x: i32; let c = 1;\n"
                      "    if c > 0 { x = 1; }\n"
                      "    ret x; }",
        "use of uninitialized value");
}

TEST_F(RuntimeTest, UninitMatchArmMissingAssignRejected)
{
    expectCompileFail("fn main() -> i32 { let mut x: i32; let o = Option::Some(1);\n"
                      "    match o { Some(v) => { x = v; }, None => { } }\n"
                      "    ret x; }",
        "use of uninitialized value");
}

TEST_F(RuntimeTest, UninitMoveOfUninitRejected)
{
    expectCompileFail("fn main() -> i32 { let x: i32; let y = x; ret y; }",
        "use of uninitialized value");
}

// A Move binding cannot be declared without an initializer at all.
TEST_F(RuntimeTest, UninitMoveBindingWithoutInitializerRejected)
{
    expectCompileFail("fn main() -> i32 { let s: String; ret 0; }", "without an initializer");
    expectCompileFail("struct P { pub v: i32 }\nfn main() -> i32 { let p: P; ret 0; }",
        "without an initializer");
}

// On the Ok path the operator yields the payload.
TEST_F(RuntimeTest, TryOperatorPropagatesOkPayload)
{
    expectRun("fn ok42() -> Result<i32, i32> { ret Result::Ok(42); }\n"
              "fn f() -> Result<i32, i32> { let v = ok42()?; ret Result::Ok(v + 1); }\n"
              "fn main() -> i32 { let r = f();\n"
              "    match r { Ok(v) => { ret v; }, Err(e) => { ret 0 - e; } } }",
        43);
}

TEST_F(RuntimeTest, TryOperatorChainedInOneExpression)
{
    expectRun("fn one() -> Result<i32, i32> { ret Result::Ok(1); }\n"
              "fn two() -> Result<i32, i32> { ret Result::Ok(2); }\n"
              "fn f() -> Result<i32, i32> { ret Result::Ok(one()? + two()? + one()?); }\n"
              "fn main() -> i32 { let r = f();\n"
              "    match r { Ok(v) => { ret v; }, Err(e) => { ret 100; } } }",
        4);
}

// A whole-module import exposes module-qualified names — the way to reach
// result::unwrap_or when option's unwrap_or is imported as well.
TEST_F(RuntimeTest, ResultQualifiedAccessFromWholeModuleImport)
{
    expectRunWithPrologue(
        "impt result;\n"
        "fn bad() -> result::Result<i32, i32> { ret result::Result::Err(4); }\n"
        "fn main() -> i32 { ret result::unwrap_or(bad(), 11); }",
        "",
        11);
}

TEST_F(RuntimeTest, TryOperatorRequiresResultOperand)
{
    expectCompileFail("fn f() -> Result<i32, i32> { let x = 5?; ret Result::Ok(x); }\n"
                      "fn main() -> i32 { ret 0; }",
        "requires a 'Result' value");
}

// A void generic argument would become a void struct field (LLVM rejects that).
TEST_F(RuntimeTest, VoidGenericArgumentRejected)
{
    expectCompileFail("fn f() -> Result<void, i32> { ret Result::Ok(1); }\nfn main() -> i32 { ret 0; }",
        "cannot be a generic argument");
}
