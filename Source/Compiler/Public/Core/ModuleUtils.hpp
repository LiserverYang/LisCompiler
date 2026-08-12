/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * ModuleUtils.hpp — module-system name helpers and data structures.
 *
 * The module system gives every top-level declaration a module-qualified
 * INTERNAL name: `modulePath + "$" + bareName`. The `$` is not a legal
 * identifier character (the lexer's IDENTIFIER is `[a-zA-Z_][a-zA-Z0-9_]*`),
 * so user source can never write a name containing `$` — the rule
 * "a name containing `$` is already internal; never prefix it again" holds
 * globally. The root module (the main file) has path "" and gets NO prefix,
 * so single-file behavior is identical to before (per-phase testability).
 *
 * The cascade is consistent, all reusing existing mangle mechanisms with a
 * prefixed base name:
 *   - types:            `math$Option`, instantiated `math$Option$i32`
 *   - static methods:   `math$Option::new`
 *   - mono functions:   `math$min_Mono_i32`
 *   - drop glue:        `__drop_math$Option`
 *
 * Name production points are concentrated in the Parser (reference side) and
 * HIRBuilder (declaration side); downstream mono/LLVM are untouched.
 */

#pragma once

#include <string>
#include <vector>

/// True if `n` already carries a module prefix (contains `$`). Such a name is
/// final — never prefix it again, and never strip it except for diagnostics.
inline bool isInternalName(const std::string &n)
{
    return n.find('$') != std::string::npos;
}

/// Build the internal name of bare name `bare` inside module `mod`
/// ("" = root module / main file, no prefix).
inline std::string internalName(const std::string &mod, const std::string &bare)
{
    return mod.empty() ? bare : mod + "$" + bare;
}

/// Strip the module prefix for user-facing diagnostics only.
/// `math$max` → `max`; `math$Option$i32` → `Option$i32` (keeps mono suffix).
inline std::string displayName(const std::string &internal)
{
    size_t pos = internal.find('$');
    if (pos == std::string::npos)
        return internal;
    return internal.substr(pos + 1);
}

/// One `impt` statement's binding, as recorded against the importing module.
struct ImportBinding
{
    std::string canonicalModule;   // "math" / "foo.bar" (module path, not internal)
    std::string boundName;         // alias, or the last path segment ("m" / "bar")
    bool selective = false;        // `impt foo.bar { a, b };` form
    std::vector<std::string> symbols; // selective imports (bare names)
};

/// Module attribution of one top-level statement: which module (and file) it
/// belongs to. Parallel to Program::globalStatements, filled by the Parser.
struct StmtAttribution
{
    std::string modulePath; // "" = root module
    std::string filePath;   // diagnostics
};

/// One active module frame during parsing (the module stack for cycle
/// detection).
struct ModuleFrame
{
    std::string modulePath;
    std::string filePath;
    std::vector<ImportBinding> imports;
};
