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

struct Symbol
{
    SymbolKind kind;
    std::string name;
    SourcePosition position;    // 声明位置（用于报错）
    std::shared_ptr<Type> type; // 类型信息

    std::optional<std::vector<std::unique_ptr<Param>>> funcParams; // 函数参数
    std::optional<bool> isMutable;                                 // 变量是否可变
};