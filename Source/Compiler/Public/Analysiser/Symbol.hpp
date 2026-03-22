/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Analysiser/Type.hpp"
#include "Core/SourcePosition.hpp"
#include "Parser/AST.hpp"

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

    std::vector<std::string> implementedTraits;   // 结构体实现的Trait列表
    std::vector<std::string> structsImplementing; // 实现该Trait的结构体列表
};