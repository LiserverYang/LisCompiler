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
    Print,     // print_str / println / print_int / print_float / print_bool / print_char
    Input,     // read_line / read_int / read_f64
    Heap,      // __alloc / __free / __memcpy / __strlen
    ToString,  // to_string_i32/i64/f64/bool/char
    NotBuiltin // any other name
};

/// Classify `name` into its builtin family (or NotBuiltin). Both sema and
/// codegen call this instead of maintaining their own name lists.
inline BuiltinCategory classifyBuiltin(const std::string &name)
{
    static const std::unordered_set<std::string> print = {
        "print_str",
        "println",
        "print_int",
        "print_float",
        "print_bool",
        "print_char",
    };
    static const std::unordered_set<std::string> input = {
        "read_line",
        "read_int",
        "read_f64",
    };
    static const std::unordered_set<std::string> heap = {
        "__alloc",
        "__free",
        "__memcpy",
        "__strlen",
    };
    static const std::unordered_set<std::string> toString = {
        "to_string_i32",
        "to_string_i64",
        "to_string_f64",
        "to_string_bool",
        "to_string_char",
    };
    if (print.count(name)) return BuiltinCategory::Print;
    if (input.count(name)) return BuiltinCategory::Input;
    if (heap.count(name)) return BuiltinCategory::Heap;
    if (toString.count(name)) return BuiltinCategory::ToString;
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
        "sprintf",
        "printf",
        "fgets",
        "strcspn",
        "atoi",
        "strtod",
        "abort",
    };
    return libc.count(name);
}
