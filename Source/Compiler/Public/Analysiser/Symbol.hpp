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

enum class VarState
{
    Valid,   // 可用
    Moved,   // 已移动
    Borrowed // 被借用
};

struct Symbol
{
    SymbolKind kind;
    std::string name;
    SourcePosition position;
    std::shared_ptr<Type> type;

    std::optional<bool> isMutable; // 变量是否可变
    VarState state;

    /**
     * Definite assignment: false only for a binding declared WITHOUT an
     * initializer (`let x;`), which the language allows for Copy types. Every
     * other binding — globals, params, match bindings, for-loop temporaries —
     * starts initialized, so the conservative default cannot produce a false
     * positive. Reads of a not-definitely-initialized binding are rejected; the
     * flag is flow-sensitive (merged with AND across if/match branches, and
     * reset across a loop body).
     */
    bool initialized = true;

    /**
     * Field paths moved out of this variable by partial (field) moves, e.g.
     * `let x = p.a` → [["a"]], `let x = p.a.b` → [["a","b"]]. Mirrors the
     * MIR-side partiallyMovedFields_. A whole-value use while this is non-empty
     * is a double-free and is rejected by the semantic analyzer.
     */
    std::vector<std::vector<std::string>> movedFields;

    std::vector<std::string> implementedTraits;   // 结构体实现的Trait列表
    std::vector<std::string> structsImplementing; // 实现该Trait的结构体列表

    /// A selective-import alias symbol (`impt math { max }` promotes `max` in
    /// the importing module). It forwards everything (type/kind/name) to the
    /// target symbol so lookups always see the target's latest state.
    Symbol *aliasTarget = nullptr;
};