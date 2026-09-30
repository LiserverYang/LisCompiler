// FFI stage 1/2 tests (2026-09-26) -- see Document/src/ffi.md.
//
// These live in their own TU because every one of them touches the C boundary:
// each declares extern "C" or export, and the ones that need a hand-written C
// helper can only reach it through a REAL link (the in-process JIT has no linker
// and resolves a fixed symbol table -- see RuntimeJit.cpp). The capability is
// per-snippet (expectRunFfi / ...WithSources), so the rest of the suite keeps
// pinning the default: denied.

#include "RuntimeTestFixture.hpp"

// ── #[repr(C)]: the layout witness, read by C THROUGH A POINTER ─────────────
//
// By-value struct passing is deliberately NOT supported (see the FFI whitelist):
// it needs the platform aggregate ABI materialised in the IR, which clang does
// and the LLVM backend does not. A pointer is verified in both directions, and
// #[repr(C)] is the promise that C may read the fields behind it.

TEST_F(RuntimeTest, FfiReprCStructReadThroughPointer)
{
    const std::string helper =
        "#include <cstdint>\n"
        "struct Point { int32_t x; int32_t y; };\n"
        "extern \"C\" int32_t point_sum(const Point* p) { return p->x + p->y; }\n"
        "extern \"C\" void point_scale(Point* p, int32_t k) { p->x *= k; p->y *= k; }\n";
    expectRunWithSources(
        "#[repr(C)] struct Point { pub x: i32, pub y: i32 }\n"
        "extern \"C\" fn point_sum(p: &Point) -> i32;\n"
        "extern \"C\" fn point_scale(p: &mut Point, k: i32) -> void;\n"
        "fn main() -> i32 {\n"
        "    let mut p = Point { x: 3, y: 4 };\n"
        "    if point_sum(&p) != 7 { ret 1; }\n"
        "    point_scale(&mut p, 10);          // C WRITES through the borrow\n"
        "    if p.x != 30 { ret 2; }\n"
        "    if p.y != 40 { ret 3; }\n"
        "    if point_sum(&p) != 70 { ret 4; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiReprCLayoutMatchesC)
{
    // Padding and alignment, not just field order: the C side reports its own
    // sizeof/offsetof AND sums the fields it reads at C offsets through the
    // pointer. A layout difference (reordering, packing, a different alignment
    // rule) makes the sum wrong, which no amount of matching constants hides.
    const std::string helper =
        "#include <cstdint>\n"
        "#include <cstddef>\n"
        "struct Mixed { int8_t a; int64_t b; double c; int32_t d; };\n"
        "extern \"C\" int64_t mixed_size() { return (int64_t)sizeof(Mixed); }\n"
        "extern \"C\" int64_t mixed_off_b() { return (int64_t)offsetof(Mixed, b); }\n"
        "extern \"C\" int64_t mixed_off_c() { return (int64_t)offsetof(Mixed, c); }\n"
        "extern \"C\" int64_t mixed_off_d() { return (int64_t)offsetof(Mixed, d); }\n"
        "extern \"C\" int64_t mixed_sum(const Mixed* m) { return (int64_t)m->a + m->b + (int64_t)m->c + m->d; }\n";
    expectRunWithSources(
        "#[repr(C)] struct Mixed { pub a: i8, pub b: i64, pub c: f64, pub d: i32 }\n"
        "extern \"C\" fn mixed_size() -> i64;\n"
        "extern \"C\" fn mixed_off_b() -> i64;\n"
        "extern \"C\" fn mixed_off_c() -> i64;\n"
        "extern \"C\" fn mixed_off_d() -> i64;\n"
        "extern \"C\" fn mixed_sum(m: &Mixed) -> i64;\n"
        "fn main() -> i32 {\n"
        "    if mixed_size() != 32 as i64 { ret 1; }\n"
        "    if mixed_off_b() != 8 as i64 { ret 2; }\n"
        "    if mixed_off_c() != 16 as i64 { ret 3; }\n"
        "    if mixed_off_d() != 24 as i64 { ret 4; }\n"
        "    #[i_know = \"1 fits in an i8\"]\n"
        "    let one = 1 as i8;\n"
        "    let m = Mixed { a: one, b: 2 as i64, c: 4.0, d: 8 };\n"
        "    if mixed_sum(&m) != 15 as i64 { ret 5; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiReprCNestedAndArrayFields)
{
    const std::string helper =
        "#include <cstdint>\n"
        "struct Inner { int32_t v; int32_t w; };\n"
        "struct Outer { Inner in; int32_t xs[3]; };\n"
        "extern \"C\" int32_t outer_sum(const Outer* o) { return o->in.v + o->in.w + o->xs[0] + o->xs[1] + o->xs[2]; }\n";
    expectRunWithSources(
        "#[repr(C)] struct Inner { pub v: i32, pub w: i32 }\n"
        "#[repr(C)] struct Outer { pub inner: Inner, pub xs: [i32; 3] }\n"
        "extern \"C\" fn outer_sum(o: &Outer) -> i32;\n"
        "fn main() -> i32 {\n"
        "    let o = Outer { inner: Inner { v: 1, w: 2 }, xs: [3, 4, 5] };\n"
        "    if outer_sum(&o) != 15 { ret 1; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiStructByValueRejected)
{
    // Structs cross BEHIND A POINTER. By value would need the platform aggregate
    // ABI in the IR: measured, even {i32,i32} reached C with its second field
    // zeroed. repr(C) or not makes no difference to THAT rule -- it is what
    // makes the pointer useful.
    expectCompileFailFfi("#[repr(C)] struct S { pub v: i32 }\n"
                         "extern \"C\" fn f(s: S) -> i32;\n"
                         "fn main() -> i32 { ret 0; }\n",
        "BEHIND A POINTER");
    expectCompileFailFfi("struct S { pub v: i32 }\n"
                         "extern \"C\" fn f(s: S) -> i32;\n"
                         "fn main() -> i32 { ret 0; }\n",
        "BEHIND A POINTER");
    // The pointer form is accepted for BOTH: an opaque handle needs no repr(C)
    // (only a field C READS does), and it compiles all the way to the object.
    expectRunWithSources(
        "struct Opaque { pub v: i32 }\n"
        "extern \"C\" fn opaque_sum(p: &Opaque) -> i32;\n"
        "fn main() -> i32 { let o = Opaque { v: 5 }; ret opaque_sum(&o); }\n",
        {{"helper.cpp",
          "#include <cstdint>\n"
          "struct Opaque { int32_t v; };\n"
          "extern \"C\" int32_t opaque_sum(const Opaque* p) { return p->v; }\n"}},
        5);
}

TEST_F(RuntimeTest, FfiReprCFieldRules)
{
    // A bool is one bit here and one byte in C; a char is 32-bit here and one byte
    // in C; a nested struct must carry the same promise itself.
    expectCompileFailFfi("#[repr(C)] struct S { pub b: bool, pub v: i32 }\n"
                         "fn main() -> i32 { ret 0; }\n",
        "has no C layout");
    expectCompileFailFfi("#[repr(C)] struct S { pub c: char, pub v: i32 }\n"
                         "fn main() -> i32 { ret 0; }\n",
        "has no C layout");
    expectCompileFailFfi("struct Plain { pub v: i32 }\n"
                         "#[repr(C)] struct S { pub p: Plain }\n"
                         "fn main() -> i32 { ret 0; }\n",
        "must be #[repr(C)] itself");
}

TEST_F(RuntimeTest, FfiReprCMisuse)
{
    expectCompileFailFfi("#[repr(C)] enum E { A(i32), B }\nfn main() -> i32 { ret 0; }\n",
        "can only be applied to a struct declaration");
    expectCompileFailFfi("#[repr(C)] fn f() -> i32 { ret 0; }\nfn main() -> i32 { ret 0; }\n",
        "can only be applied to a struct declaration");
    expectCompileFailFfi("#[repr(C)] struct G<T> { pub v: T }\nfn main() -> i32 { ret 0; }\n",
        "generic struct");
    expectCompileFailFfi("#[repr(C)] struct Empty { }\nfn main() -> i32 { ret 0; }\n",
        "empty struct");
    expectCompileFailFfi("#[repr(packed)] struct S { pub v: i32 }\nfn main() -> i32 { ret 0; }\n",
        "only #[repr(C)] is supported");
}

// ── bool across the boundary (C _Bool is one byte, a Lis bool is one bit) ─────

TEST_F(RuntimeTest, FfiBoolScalarUsesCAbi)
{
    const std::string helper =
        "#include <cstdint>\n"
        "extern \"C\" bool c_not(bool b) { return !b; }\n"
        "extern \"C\" int32_t c_sizeof_bool() { return (int32_t)sizeof(bool); }\n"
        "extern \"C\" int32_t c_bool_arg(bool b) { return b ? 7 : 9; }\n";
    expectRunWithSources(
        "extern \"C\" fn c_not(b: bool) -> bool;\n"
        "extern \"C\" fn c_sizeof_bool() -> i32;\n"
        "extern \"C\" fn c_bool_arg(b: bool) -> i32;\n"
        "fn main() -> i32 {\n"
        "    if c_sizeof_bool() != 1 { ret 1; }\n"
        "    if c_not(true) != false { ret 2; }\n"
        "    if c_not(false) != true { ret 3; }\n"
        "    if c_bool_arg(true) != 7 { ret 4; }\n"
        "    if c_bool_arg(false) != 9 { ret 5; }\n"
        "    let t = true;\n"
        "    if c_not(t) != false { ret 6; }   // a bool VALUE round trips too\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiVariadicBoolStillRejected)
{
    // C promotes a variadic bool to int, so it cannot cross as itself.
    expectCompileFailFfi("extern \"C\" fn printf(fmt: &i8, ...) -> i32;\n"
                         "fn main() -> i32 { ret printf(\"%d\", true); }\n",
        "cannot cross the C boundary");
}
