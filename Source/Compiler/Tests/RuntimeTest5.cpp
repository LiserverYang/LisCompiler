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
    // A #[repr(C)] struct may now cross BY VALUE (the platform ABI is implemented
    // in FfiAbi, and RuntimeTest6 holds the differential matrix); the pointer form
    // stays available for everything else, and a struct WITHOUT the promise still
    // has to use it.
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
    // #[repr(packed)] alone is refused by its OWN rule now (packed is accepted in
    // the repr list, but needs C next to it -- see FfiPackedMisuse).
    expectCompileFailFfi("#[repr(packed)] struct S { pub v: i32 }\nfn main() -> i32 { ret 0; }\n",
        "alone is not supported");
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

// ── ownership transfer: the buffer leaves the language and comes back ────────

TEST_F(RuntimeTest, FfiStringOwnershipRoundTrip)
{
    // into_raw hands the buffer to C (which edits it in place and also returns a
    // NEW malloc buffer); from_raw adopts either one. The source String is left a
    // valid EMPTY one -- it is printed and dropped afterwards, which is the check
    // that the handed-out buffer was not freed twice.
    const std::string helper =
        "#include <cstdint>\n"
        "#include <cstdlib>\n"
        "#include <cstring>\n"
        "#include <cctype>\n"
        "extern \"C\" void lis_upper(char* s) { for (; *s; ++s) *s = (char)toupper((unsigned char)*s); }\n"
        "extern \"C\" char* lis_dup_reversed(const char* s) {\n"
        "    size_t n = strlen(s);\n"
        "    char* out = (char*)malloc(n + 1);\n"
        "    for (size_t i = 0; i < n; ++i) out[i] = s[n - 1 - i];\n"
        "    out[n] = 0;\n"
        "    return out;\n"
        "}\n";
    // Exit code only: the CONTENT is asserted in the snippet itself, and a
    // subprocess's stdout goes through the CRT's text mode (CRLF) on Windows.
    expectRunWithSources(
        "extern \"C\" fn lis_upper(s: *mut i8) -> void;\n"
        // *mut i8 -> &i8 is deliberately NOT allowed (the narrowing direction of
        // the FFI contract), so the C side is declared with the raw pointer it
        // actually takes.
        "extern \"C\" fn lis_dup_reversed(s: *mut i8) -> *mut i8;\n"
        "fn main() -> i32 {\n"
        "    let mut s = String::from_lit(\"hello\");\n"
        "    let raw = s.into_raw();\n"
        "    if s.len() != 0 { ret 1; }        // the source is a valid empty String\n"
        "    lis_upper(raw);                   // C edits the buffer in place\n"
        "    let back = String::from_raw(raw, 5, 6);\n"
        "    if back.to_cstr() != \"HELLO\" { ret 2; }\n"
        "    print(back.to_cstr()); println();\n"
        "    let mut t = String::from_lit(\"abc\");\n"
        "    let raw2 = t.into_raw();\n"
        "    let rev = lis_dup_reversed(raw2);    // C returns a NEW buffer\n"
        "    let reclaimed = String::from_raw(raw2, 3, 4);   // ... and we take ours back\n"
        "    if reclaimed.len() != 3 { ret 3; }\n"
        "    let rev_s = String::from_raw(rev, 3, 4);\n"
        "    if rev_s.to_cstr() != \"cba\" { ret 4; }\n"
        "    print(rev_s.to_cstr()); println();\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiVecOwnershipRoundTrip)
{
    const std::string helper =
        "#include <cstdint>\n"
        "#include <cstdlib>\n"
        "extern \"C\" int64_t lis_sum(const int32_t* xs, int64_t n) {\n"
        "    int64_t s = 0; for (int64_t i = 0; i < n; ++i) s += xs[i]; return s;\n"
        "}\n"
        "extern \"C\" int32_t* lis_iota(int64_t n) {\n"
        "    int32_t* out = (int32_t*)malloc((size_t)n * sizeof(int32_t));\n"
        "    for (int64_t i = 0; i < n; ++i) out[i] = (int32_t)i;\n"
        "    return out;\n"
        "}\n";
    expectRunWithSources(
        // The stdlib prologue promotes String but NOT Vec (its tests import it).
        "impt vec { Vec };\n"
        "extern \"C\" fn lis_sum(xs: *mut i32, n: i64) -> i64;\n"
        "extern \"C\" fn lis_iota(n: i64) -> *mut i32;\n"
        "fn main() -> i32 {\n"
        "    let mut v = Vec<i32>::new();\n"
        "    v.push(1); v.push(2); v.push(3);\n"
        "    let cap = v.cap();\n"
        "    let raw = v.into_raw();\n"
        "    if v.len() != 0 { ret 1; }        // still a valid empty Vec\n"
        "    if lis_sum(raw, 3 as i64) != 6 as i64 { ret 2; }\n"
        "    let back = Vec<i32>::from_raw(raw, 3, cap);\n"
        "    if back.len() != 3 { ret 3; }\n"
        "    if back[2] != 3 { ret 4; }\n"
        "    let p = lis_iota(5 as i64);       // a buffer C allocated\n"
        "    let nums = Vec<i32>::from_raw(p, 5, 5);\n"
        "    if nums.len() != 5 { ret 5; }\n"
        "    if nums[0] != 0 { ret 6; }\n"
        "    if nums[4] != 4 { ret 7; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

// ── opaque handles: an empty struct is the only thing that can be one ────────

TEST_F(RuntimeTest, FfiOpaqueHandleFileIo)
{
    // FILE* is the canonical opaque handle: the language never lays it out, it
    // only carries the pointer. No C helper is needed -- these are CRT calls, so
    // this runs through a real link (expectRunProcFfi).
    expectRunProcFfi(
        "struct File { }\n"
        "extern \"C\" fn fopen(path: &i8, mode: &i8) -> *mut File;\n"
        "extern \"C\" fn fputs(s: &i8, f: *mut File) -> i32;\n"
        "extern \"C\" fn fgets(buf: *mut i8, n: i32, f: *mut File) -> *mut i8;\n"
        "extern \"C\" fn fclose(f: *mut File) -> i32;\n"
        "extern \"C\" fn remove(path: &i8) -> i32;\n"
        "fn main() -> i32 {\n"
        "    let path = \"lis_ffi_opaque.tmp\";\n"
        "    let w = fopen(path, \"w\");\n"
        "    if fputs(\"hello\\n\", w) < 0 { ret 1; }\n"
        "    fclose(w);\n"
        "    #[i_know = \"byte buffer for C\"]\n"
        "    let mut buf: [i8; 16] = [(0 as i8); 16];\n"
        "    let r = fopen(path, \"r\");\n"
        // Raw pointers cannot be compared (there is no null literal either), so the
        // READ result is checked by CONTENT through the C-string builtin.
        "    fgets(&mut buf[0], 16, r);\n"
        "    fclose(r);\n"
        "    remove(path);\n"
        "    if str_cmp(&buf[0], \"hello\\n\") != 0 { ret 2; }\n"
        "    ret 0;\n"
        "}\n",
        0);
}

// ── export fn: the reverse boundary (C calls Lis) ───────────────────────────

TEST_F(RuntimeTest, FfiExportCalledFromC)
{
    // The Lis side has NO main: the C++ side provides it and calls in. Two
    // symbols are exported -- one under its own name, one renamed by
    // #[link_name] -- which also pins that the emitted symbol is the C name.
    const std::string helper =
        "#include <cstdint>\n"
        "extern \"C\" int32_t add(int32_t a, int32_t b);\n"
        "extern \"C\" int32_t lis_mul(int32_t a, int32_t b);\n"
        "int main() {\n"
        "    if (add(2, 3) != 5) return 1;\n"
        "    if (lis_mul(4, 5) != 20) return 2;\n"
        "    return 0;\n"
        "}\n";
    expectRunWithSources(
        "export fn add(a: i32, b: i32) -> i32 { ret a + b; }\n"
        "#[link_name = \"lis_mul\"] export fn multiply(a: i32, b: i32) -> i32 { ret a * b; }\n",
        {{"main.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiExportHandsOwnedBufferToC)
{
    // The ownership protocol from the C side: the export returns a buffer from
    // String::into_raw, C reads it, and C releases it with lis_free (the stdlib
    // ffi module), which is what keeps the allocator behind the boundary.
    const std::string helper =
        "#include <cstring>\n"
        "extern \"C\" char* make_greeting(void);\n"
        "extern \"C\" void lis_free(char* p);\n"
        "int main() {\n"
        "    char* g = make_greeting();\n"
        "    if (std::strcmp(g, \"hello from Lis\") != 0) return 1;\n"
        "    lis_free(g);\n"
        "    return 0;\n"
        "}\n";
    expectRunWithSources(
        // String arrives with the prologue; only ffi is ours to import.
        "impt ffi;\n"
        "export fn make_greeting() -> *mut i8 {\n"
        "    let s = String::from_lit(\"hello from Lis\");\n"
        "    let mut m = s;\n"
        "    ret m.into_raw();\n"
        "}\n",
        {{"main.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiExportMustBeWellFormed)
{
    // Generic: C cannot name an instantiation. Variadic: C would have to know
    // the promotions. Body-less: an export DEFINES the symbol.
    expectCompileFailFfi("export fn f<T>(x: T) -> i32 { ret 0; }\n",
        "cannot be generic");
    expectCompileFailFfi("export fn f(x: i32, ...) -> i32 { ret x; }\n",
        "cannot be variadic");
    expectCompileFailFfi("export fn f() -> i32;\n",
        "needs a body");
}

TEST_F(RuntimeTest, FfiExportSignatureMustBeFfiSafe)
{
    // The whitelist is the same in both directions: a String would hand C a
    // layout it cannot use.
    expectCompileFailFfi("impt string { String };\n"
                         "export fn f(s: String) -> i32 { ret 0; }\n",
        "cannot cross the C boundary");
}

TEST_F(RuntimeTest, FfiExportRequiresCapability)
{
    expectCompileFail("export fn f() -> i32 { ret 0; }\n",
        "need the FFI capability");
}

TEST_F(RuntimeTest, FfiExportDuplicateAndReservedSymbols)
{
    // Two definitions of one C symbol would collide in the object file.
    expectCompileFailFfi("#[link_name = \"same\"] export fn a() -> i32 { ret 0; }\n"
                         "#[link_name = \"same\"] export fn b() -> i32 { ret 1; }\n",
        "already exported");
    // ... and the symbol must not be one the compiler already emits.
    expectCompileFailFfi("#[link_name = \"assert_fail\"] export fn a() -> i32 { ret 0; }\n",
        "reserved by the compiler");
}

TEST_F(RuntimeTest, FfiLinkAttribute)
{
    // #[link(name = "m")] records a linker request (emitted as
    // llvm.linker.options). lld honours it; GNU ld ignores it, which is why the
    // docs also keep the "add -lm to the link line" recipe.
    expectRunFfi("#[link(name = \"m\")]\n"
                 "fn main() -> i32 { ret 0; }\n",
        0);
    expectCompileFailFfi("#[link(name = 3)]\nfn main() -> i32 { ret 0; }\n",
        "expected a library name");
    expectCompileFailFfi("#[link(foo = \"m\")]\nfn main() -> i32 { ret 0; }\n",
        "expected 'name'");
}

// ── callbacks: C calls back into Lis ────────────────────────────────────────

TEST_F(RuntimeTest, FfiCallbackQsort)
{
    // The canonical callback test: qsort drives a Lis comparator. The parameter
    // type is written `fn(&i32, &i32) -> i32` -- the first way to NAME a
    // function type, which is what an FFI declaration needs.
    expectRunFfi(
        // The declaration's parameter is NOT named cmp: a parameter may not shadow
        // an existing definition, and the comparator below is a global function.
        "extern \"C\" fn qsort(base: *mut i32, n: i64, size: i64, f: fn(&i32, &i32) -> i32) -> void;\n"
        "fn cmp(a: &i32, b: &i32) -> i32 { ret *a - *b; }\n"
        "fn main() -> i32 {\n"
        "    let mut xs: [i32; 6] = [5, 3, 1, 6, 2, 4];\n"
        "    qsort(&mut xs[0], 6 as i64, 4 as i64, cmp);\n"
        "    let mut i = 0;\n"
        "    while i < 6 { if xs[i] != i + 1 { ret 1; } i = i + 1; }\n"
        "    ret 0;\n"
        "}\n",
        0);
}

TEST_F(RuntimeTest, FfiCallbackTypeMustBeFfiSafe)
{
    // A callback signature crosses too, so it obeys the same whitelist: C cannot
    // receive a String, and a variadic signature would need the promotions of
    // what C passes back.
    expectCompileFailFfi("extern \"C\" fn f(cb: fn(String) -> i32) -> i32;\n"
                         "fn main() -> i32 { ret 0; }\n",
        "callback parameter");
}

TEST_F(RuntimeTest, FfiCallbackExternNamePassedThrough)
{
    // An extern "C" name is an ordinary function VALUE now (stage 0 refused it
    // because the address used to materialise with the wrong signature), so a C
    // function can be handed to C as a callback.
    const std::string helper =
        "#include <cstdint>\n"
        "typedef int64_t (*strlen_fn)(const char*);\n"
        "extern \"C\" int64_t call_it(strlen_fn f, const char* s) { return f(s); }\n";
    expectRunWithSources(
        "extern \"C\" fn strlen(s: &i8) -> i64;\n"
        "extern \"C\" fn call_it(f: fn(&i8) -> i64, s: &i8) -> i64;\n"
        "fn main() -> i32 {\n"
        "    if call_it(strlen, \"abcd\") != 4 as i64 { ret 1; }\n"
        "    let again = call_it(strlen, \"abcdef\");\n"
        "    if again != 6 as i64 { ret 2; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

// ── #[repr(C, packed)]: the layout WITHOUT padding ──────────────────────────

TEST_F(RuntimeTest, FfiPackedStructLayoutMatchesC)
{
    // C gets the same thing from #pragma pack(1). Ordinar-#[repr(C)] would add
    // padding (8 bytes, b at offset 4); packed is 7 bytes with b at 1.
    const std::string helper =
        "#include <cstdint>\n"
        "#include <cstddef>\n"
        "#pragma pack(push, 1)\n"
        "struct Packed { int8_t a; int32_t b; int16_t c; };\n"
        "#pragma pack(pop)\n"
        "extern \"C\" int64_t packed_size() { return (int64_t)sizeof(Packed); }\n"
        "extern \"C\" int64_t packed_off_b() { return (int64_t)offsetof(Packed, b); }\n"
        "extern \"C\" int64_t packed_off_c() { return (int64_t)offsetof(Packed, c); }\n"
        "extern \"C\" int64_t packed_sum(const Packed* p) { return (int64_t)p->a + p->b + p->c; }\n";
    expectRunWithSources(
        "#[repr(C, packed)] struct Packed { pub a: i8, pub b: i32, pub c: i16 }\n"
        "extern \"C\" fn packed_size() -> i64;\n"
        "extern \"C\" fn packed_off_b() -> i64;\n"
        "extern \"C\" fn packed_off_c() -> i64;\n"
        "extern \"C\" fn packed_sum(p: &Packed) -> i64;\n"
        "fn main() -> i32 {\n"
        "    if packed_size() != 7 as i64 { ret 1; }\n"
        "    if packed_off_b() != 1 as i64 { ret 2; }\n"
        "    if packed_off_c() != 5 as i64 { ret 3; }\n"
        "    #[i_know = \"fits in an i16\"]\n"
        "    let c = 3 as i16;\n"
        "    #[i_know = \"fits in an i8\"]\n"
        "    let a = 1 as i8;\n"
        "    let p = Packed { a: a, b: 2, c: c };\n"
        "    if packed_sum(&p) != 6 as i64 { ret 4; }\n"
        "    ret 0;\n"
        "}\n",
        {{"helper.cpp", helper}}, 0);
}

TEST_F(RuntimeTest, FfiPackedMisuse)
{
    // packed alone has no layout promise to make; and a packed struct cannot go
    // BY VALUE at all (its fields are unaligned).
    expectCompileFailFfi("#[repr(packed)] struct P { pub a: i8 }\nfn main() -> i32 { ret 0; }\n",
        "alone is not supported");
    expectCompileFailFfi("#[repr(C, packed)] struct P { pub a: i8, pub b: i32 }\n"
                         "extern \"C\" fn f(p: P) -> i32;\nfn main() -> i32 { ret 0; }\n",
        "#[repr(C, packed)] struct can only cross BEHIND A POINTER");
}

TEST_F(RuntimeTest, FfiExportAggregateByValueRefused)
{
    // The matrix proves the CALL direction; the reverse one would have to unpack the
    // ABI arguments inside the exported body, so it is refused instead of letting C
    // mis-call. The pointer form still works (see the export tests above).
    expectCompileFailFfi("#[repr(C)] struct S { pub a: i32, pub b: i32 }\n"
                         "export fn takes(s: S) -> i32 { ret s.a; }\n",
        "BY VALUE yet");
    expectCompileFailFfi("#[repr(C)] struct S { pub a: i32, pub b: i32 }\n"
                         "export fn gives() -> S { ret S { a: 1, b: 2 }; }\n",
        "RETURN the struct");
}
