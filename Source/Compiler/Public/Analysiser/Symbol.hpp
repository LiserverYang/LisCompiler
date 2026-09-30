/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Analysiser/Type.hpp"
#include "Core/SourcePosition.hpp"
#include "Parser/AST.hpp"

/**
 * Borrow-checker Stage 3: where a reference-typed value ultimately points.
 *
 * A reference is dangling after this function returns iff it (transitively)
 * points into this function's own stack frame. Params (reference-typed) and
 * globals outlive the function; by-value param slots, local slots and local
 * struct fields do not. Unknown → conservative allow (never falsely reject).
 */
enum class RefOrigin
{
    Local,  // points into this function's stack frame → dangles on return
    Param,  // points into caller-owned memory (a reference-typed param / &self)
    Global, // points into static / global storage
    Unknown // cannot determine → treat as safe (conservative)
};

enum class SymbolKind
{
    GlobalVar,
    LocalVar,
    Function,
    Struct,
    Trait,
    StructMethod,
    TraitMethod,
    Param
};

struct Symbol
{
    SymbolKind kind;
    std::string name;
    SourcePosition position;
    std::shared_ptr<Type> type;

    std::optional<bool> isMutable; // 变量是否可变

    std::vector<std::string> implementedTraits;   // 结构体实现的Trait列表
    std::vector<std::string> structsImplementing; // 实现该Trait的结构体列表

    /// A selective-import alias symbol (`impt math { max }` promotes `max` in
    /// the importing module). It forwards everything (type/kind/name) to the
    /// target symbol so lookups always see the target's latest state.
    Symbol *aliasTarget = nullptr;

    /// An `extern "C"` DECLARATION (2026-09-26): codegen declares the C symbol
    /// (cName) instead of looking for a Lis definition, and the call must not go
    /// through monomorphization.
    bool isExtern = false;
    /// The C symbol to call. Differs from `name` when the declaration used
    /// #[link_name] (which is how a module wraps a libc name without shadowing
    /// its own function of that name).
    std::string cName;
};