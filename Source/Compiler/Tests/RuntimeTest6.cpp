// FFI by-value aggregate ABI (2026-09-26) -- see FfiAbi and Document/src/ffi.md.
//
// Every shape here is a DIFFERENTIAL test: a g++-compiled C helper computes the
// same number the Lis side computes independently, in BOTH directions (a struct
// passed in, and one returned). This is the only honest way to test an ABI -- a
// wrong classification is a silent mis-call, which is exactly how "C received
// only the first field" survived until the coercion landed.
//
// SysV is not covered at runtime here (this machine cannot run Linux binaries):
// the classifier refuses the SysV shapes it does not implement rather than
// guessing, and Document/src/ffi-byvalue-plan.md records the clang-IR check to do.

#include "RuntimeTestFixture.hpp"

namespace
{
/// One C helper covering the shapes of a group.
std::string helper(const std::string &body)
{
    return std::string("#include <cstdint>\n") + body;
}
} // namespace

// ── register-sized aggregates: passed as an integer of the same size ────────

TEST_F(RuntimeTest, FfiAggregateIntCoercion)
{
    const std::string c = helper(
        "struct P2 { int32_t a; int32_t b; };\n"
        "struct I1 { int8_t a; int8_t b; int8_t c; int8_t d; };\n"
        "struct F2 { float a; float b; };\n"
        "extern \"C\" int64_t p2(P2 v) { return (int64_t)v.a * 10 + v.b; }\n"
        "extern \"C\" P2 p2_make(int32_t a, int32_t b) { P2 p{a, b}; return p; }\n"
        "extern \"C\" int64_t i1(I1 v) { return (int64_t)v.a * 1000 + v.b * 100 + v.c * 10 + v.d; }\n"
        "extern \"C\" double f2(F2 v) { return (double)v.a * 10.0 + (double)v.b; }\n");
    expectRunWithSources(
        "#[repr(C)] struct P2 { pub a: i32, pub b: i32 }\n"
        "#[repr(C)] struct I1 { pub a: i8, pub b: i8, pub c: i8, pub d: i8 }\n"
        "#[repr(C)] struct F2 { pub a: f32, pub b: f32 }\n"
        "extern \"C\" fn p2(v: P2) -> i64;\n"
        "extern \"C\" fn p2_make(a: i32, b: i32) -> P2;\n"
        "extern \"C\" fn i1(v: I1) -> i64;\n"
        "extern \"C\" fn f2(v: F2) -> f64;\n"
        "fn main() -> i32 {\n"
        "    if p2(P2 { a: 1, b: 2 }) != 12 as i64 { ret 1; }\n"
        "    let made = p2_make(3, 4);            // a struct RETURN\n"
        "    if made.a != 3 { ret 2; }\n"
        "    if made.b != 4 { ret 3; }\n"
        "    if p2(made) != 34 as i64 { ret 4; }\n"
        "    #[i_know = \"fits\"]\n"
        "    let a = 1 as i8;\n"
        "    #[i_know = \"fits\"]\n"
        "    let b = 2 as i8;\n"
        "    #[i_know = \"fits\"]\n"
        "    let c = 3 as i8;\n"
        "    #[i_know = \"fits\"]\n"
        "    let d = 4 as i8;\n"
        "    if i1(I1 { a: a, b: b, c: c, d: d }) != 1234 as i64 { ret 5; }\n"
        "    #[i_know = \"fits\"]\n"
        "    let fa = 1.0 as f32;\n"
        "    #[i_know = \"fits\"]\n"
        "    let fb = 2.0 as f32;\n"
        "    if f2(F2 { a: fa, b: fb }) != 12.0 { ret 6; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", c}}, 0);
}

// ── bigger aggregates: passed BY REFERENCE (a copy), returned via sret ──────

TEST_F(RuntimeTest, FfiAggregateByReference)
{
    const std::string c = helper(
        "struct I3 { int32_t a; int32_t b; int32_t c; };\n"
        "struct A3 { int32_t xs[3]; };\n"
        "struct Mixed { int8_t a; int64_t b; double c; int32_t d; };\n"
        "struct Big { int64_t v[8]; };\n"
        "extern \"C\" int64_t i3(I3 v) { return (int64_t)v.a * 100 + v.b * 10 + v.c; }\n"
        "extern \"C\" I3 i3_make(int32_t a) { I3 v{a, a + 1, a + 2}; return v; }\n"
        "extern \"C\" int64_t a3(A3 v) { return (int64_t)v.xs[0] * 100 + v.xs[1] * 10 + v.xs[2]; }\n"
        "extern \"C\" int64_t mixed(Mixed v) { return (int64_t)v.a + v.b + (int64_t)v.c + v.d; }\n"
        "extern \"C\" int64_t big(Big v) { int64_t s = 0; for (int i = 0; i < 8; ++i) s += v.v[i]; return s; }\n"
        "extern \"C\" Big big_make(int64_t x) { Big b{}; for (int i = 0; i < 8; ++i) b.v[i] = x + i; return b; }\n");
    expectRunWithSources(
        "#[repr(C)] struct I3 { pub a: i32, pub b: i32, pub c: i32 }\n"
        "#[repr(C)] struct A3 { pub xs: [i32; 3] }\n"
        "#[repr(C)] struct Mixed { pub a: i8, pub b: i64, pub c: f64, pub d: i32 }\n"
        "#[repr(C)] struct Big { pub v: [i64; 8] }\n"
        "extern \"C\" fn i3(v: I3) -> i64;\n"
        "extern \"C\" fn i3_make(a: i32) -> I3;\n"
        "extern \"C\" fn a3(v: A3) -> i64;\n"
        "extern \"C\" fn mixed(v: Mixed) -> i64;\n"
        "extern \"C\" fn big(v: Big) -> i64;\n"
        "extern \"C\" fn big_make(x: i64) -> Big;\n"
        "fn main() -> i32 {\n"
        "    if i3(I3 { a: 1, b: 2, c: 3 }) != 123 as i64 { ret 1; }\n"
        "    let made = i3_make(5);\n"
        "    if made.a != 5 { ret 2; }\n"
        "    if made.c != 7 { ret 3; }\n"
        "    if a3(A3 { xs: [3, 4, 5] }) != 345 as i64 { ret 4; }\n"
        "    #[i_know = \"fits\"]\n"
        "    let one = 1 as i8;\n"
        "    if mixed(Mixed { a: one, b: 2 as i64, c: 4.0, d: 8 }) != 15 as i64 { ret 5; }\n"
        "    let zeros = Big { v: [0 as i64, 1 as i64, 2 as i64, 3 as i64, 4 as i64, 5 as i64, 6 as i64, 7 as i64] };\n"
        "    if big(zeros) != 28 as i64 { ret 6; }\n"
        "    let grown = big_make(10 as i64);   // 64-byte sret\n"
        "    if grown.v[0] != 10 as i64 { ret 7; }\n"
        "    if grown.v[7] != 17 as i64 { ret 8; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", c}}, 0);
}

// ── nested aggregate: a struct whose only field is another struct ───────────

TEST_F(RuntimeTest, FfiAggregateNested)
{
    const std::string c = helper(
        "struct Inner { int32_t v; int32_t w; };\n"
        "struct Nested { Inner in; };\n"
        "extern \"C\" int64_t nested(Nested o) { return (int64_t)o.in.v * 10 + o.in.w; }\n"
        "extern \"C\" Nested nested_make(int32_t v, int32_t w) { Nested n{}; n.in.v = v; n.in.w = w; return n; }\n");
    expectRunWithSources(
        "#[repr(C)] struct Inner { pub v: i32, pub w: i32 }\n"
        "#[repr(C)] struct Nested { pub inner: Inner }\n"
        "extern \"C\" fn nested(o: Nested) -> i64;\n"
        "extern \"C\" fn nested_make(v: i32, w: i32) -> Nested;\n"
        "fn main() -> i32 {\n"
        "    if nested(Nested { inner: Inner { v: 1, w: 2 } }) != 12 as i64 { ret 1; }\n"
        "    let made = nested_make(7, 8);\n"
        "    if made.inner.v != 7 { ret 2; }\n"
        "    if made.inner.w != 8 { ret 3; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", c}}, 0);
}
