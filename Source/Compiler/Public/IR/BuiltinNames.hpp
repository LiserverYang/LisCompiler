/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * BuiltinNames.hpp — single source of truth for the compiler-reserved function
 * names.
 *
 * Two disjoint sets:
 *  - BUILTIN: names the compiler intercepts at call sites and lowers to libc
 *    itself (`print_*`/`read_*` → printf/fgets, `__alloc` → malloc, etc.).
 *    A user/`stdlib` `fn` with one of these names would be silently shadowed
 *    by the interception — reject it.
 *  - LIBC: names codegen declares as external libc symbols. A user function
 *    redefining `malloc`/`strlen`/… would collide with that declaration (LLVM
 *    errors out, or worse silently type-mismatches) — reject these too.
 *
 * Used by:
 *  - HIRSemanticAnalyzer::preRegister — reject a top-level `fn` with a
 *    reserved name.
 *  - (future) the consolidated builtin table for sema + codegen.
 */

#pragma once

#include <string>
#include <unordered_set>

/// The builtin family a compiler-reserved name belongs to. One source of truth
/// so sema (`handlePrintBuiltin` etc.) and codegen (`isPrintBuiltin` etc.) can
/// never drift apart when a new builtin is added.
enum class BuiltinCategory
{
    // NOTE (2026-09-25): the print_* / read_* families are GONE. Printing and
    // reading are ordinary Lis code in Source/Std/io.lis -- print / println /
    // flush and the read_* readers -- built on the IO primitives below plus the
    // Display lowering for the primitives. The compiler no longer knows those
    // names, which is what keeps the API from growing a function per type.
    Heap,      // __alloc / __free / __memcpy / __strlen
    Ptr,       // __deref / __deref_mut  (raw pointer → reference)
    ToString,  // to_string_i32/i64/f64/bool/char
    Panic,     // panic
    Assert,    // assert (and the synthesized assert_fail backend entry)
    Str,       // str_len / str_cmp (C-string helpers over &i8)
    IO,        // __read_byte / __write / __flush — the byte-stream primitives
    Internal,  // __show_* — the Display lowering for the primitives
    NotBuiltin // any other name
};

/// Classify `name` into its builtin family (or NotBuiltin). Both sema and
/// codegen call this instead of maintaining their own name lists.
inline BuiltinCategory classifyBuiltin(const std::string &name)
{
    static const std::unordered_set<std::string> heap = {
        "__alloc",
        "__free",
        "__memcpy",
        "__strlen",
        "__sizeof", // size of a type in bytes, as a compile-time constant
        "__drop",   // release the value at a place NOW (the container-side drop)
    };
    // Raw-pointer → reference conversion. The reverse of the implicit
    // `&T → *T` coercion, and deliberately NOT implicit: turning an unverified
    // address into a borrow would let raw pointers bypass the borrow checker, so
    // it is a stdlib-only builtin instead.
    static const std::unordered_set<std::string> ptr = {
        "__deref",
        "__deref_mut",
    };
    static const std::unordered_set<std::string> toString = {
        "to_string_i32",
        "to_string_i64",
        "to_string_f64",
        "to_string_bool",
        "to_string_char",
    };
    static const std::unordered_set<std::string> panic = {
        "panic",
    };
    // `assert` is mapped to a conditional divergence by the MIR builder, and
    // `assert_fail` is the block it branches to — the backend entry that writes
    // the location + message and aborts. Both are reserved so a user function
    // cannot collide with (or impersonate) them.
    static const std::unordered_set<std::string> assertNames = {
        "assert",
        "assert_fail",
    };
    // C-string helpers over `&i8`. `str_len` is `strlen` and `str_cmp` is
    // `strcmp`; `&i8` is how this language spells a C string (print_str, panic,
    // String::from_lit all take one), and `==` on two of them lower to str_cmp.
    static const std::unordered_set<std::string> str = {
        "str_len",
        "str_cmp",
    };
    // The IO PRIMITIVES: one buffered byte in, one byte block out, one flush.
    // These are what the standard library's `io` module is built on — tokenizing,
    // number parsing and line handling are POLICY and live in io.lis, not here.
    // Like the heap primitives they are stdlib-only (E3013): user code goes
    // through io's safe API.
    static const std::unordered_set<std::string> io = {
        "__read_byte", // () -> i32: the next byte, -1 at end of input
        "__write",     // (*i8, i32) -> void: write n bytes to stdout
        "__flush",     // () -> void: flush stdout
    };
    // INTERNAL entries: the `Display` lowering for the primitives, emitted by
    // monomorphization (a primitive cannot carry a method body). They are
    // reserved so no user function can collide with them, but they are NOT
    // callable from source — an explicit call falls through to normal name
    // resolution and reports "undefined identifier", which is the honest answer.
    static const std::unordered_set<std::string> internal = {
        "__show_i8",
        "__show_i16",
        "__show_i32",
        "__show_i64",
        "__show_f32",
        "__show_f64",
        "__show_bool",
        "__show_char",
        "__show_str",
    };
    if (heap.count(name)) return BuiltinCategory::Heap;
    if (ptr.count(name)) return BuiltinCategory::Ptr;
    if (toString.count(name)) return BuiltinCategory::ToString;
    if (panic.count(name)) return BuiltinCategory::Panic;
    if (assertNames.count(name)) return BuiltinCategory::Assert;
    if (str.count(name)) return BuiltinCategory::Str;
    if (io.count(name)) return BuiltinCategory::IO;
    if (internal.count(name)) return BuiltinCategory::Internal;
    return BuiltinCategory::NotBuiltin;
}

/// True if `name` is reserved by the compiler (a builtin or a libc symbol) and
/// must not be defined by user/`stdlib` code as a top-level function.
inline bool isReservedFunctionName(const std::string &name)
{
    if (classifyBuiltin(name) != BuiltinCategory::NotBuiltin)
        return true;
    static const std::unordered_set<std::string> libc = {
        "malloc",
        "free",
        "memcpy",
        "strlen",
        "strcmp",
        "sprintf",
        "printf",
        "fgets",
        "strcspn",
        "atoi",
        "strtod",
        "abort",
        "fprintf",
    };
    return libc.count(name);
}
