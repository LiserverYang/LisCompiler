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
    Local,   // points into this function's stack frame → dangles on return
    Param,   // points into caller-owned memory (a reference-typed param / &self)
    Global,  // points into static / global storage
    Unknown  // cannot determine → treat as safe (conservative)
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
     * Field paths moved out of this variable by partial (field) moves, e.g.
     * `let x = p.a` → [["a"]], `let x = p.a.b` → [["a","b"]]. Mirrors the
     * MIR-side partiallyMovedFields_. A whole-value use while this is non-empty
     * is a double-free and is rejected by the semantic analyzer.
     */
    std::vector<std::vector<std::string>> movedFields;

    // ── Borrow-checker Stage 3: dangling / escape origins ──────────────────────
    // These are only populated for LOCAL bindings (params/globals are derived
    // on demand from SymbolKind). They die with the scope, like the binding.

    /// Where the reference VALUE held by this binding points. Set for
    /// reference-typed bindings; read by originOfBinding(). Unset → Unknown.
    std::optional<RefOrigin> refOrigin;

    /// For struct-typed bindings: origin of each reference-typed FIELD's value,
    /// keyed by field name. Set at declaration / assignment; an absent field
    /// (e.g. the struct came from a call) → Unknown.
    std::map<std::string, RefOrigin> refFieldOrigins;

    /// Where a reference-typed LOCAL binding points, as a place (root, path).
    /// Used to resolve field accesses THROUGH the reference (`let r = &h; ret r.v`
    /// needs h's field origins, not r's own slot origin).
    std::optional<std::pair<std::string, std::vector<std::string>>> refTarget;

    std::vector<std::string> implementedTraits;   // 结构体实现的Trait列表
    std::vector<std::string> structsImplementing; // 实现该Trait的结构体列表
};