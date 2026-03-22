/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <memory>
#include <unordered_map>

#include "Analysiser/Symbol.hpp"

class Scope : public std::enable_shared_from_this<Scope>
{
    friend class SymbolTable;

public:
    explicit Scope(std::shared_ptr<Scope> parent = nullptr) : parent_(std::move(parent)) {}

    // 插入符号（检查当前作用域是否重定义）
    bool insert(const std::string &name, std::unique_ptr<Symbol> symbol);

    // 查询符号（递归向上查找父作用域）
    Symbol *lookup(const std::string &name);

    // 创建子作用域（如函数体、循环体）
    std::shared_ptr<Scope> createChild();

    const std::unordered_map<std::string, std::unique_ptr<Symbol>> &getSymbols() const
    {
        return symbols_;
    }

    std::shared_ptr<Scope> getParent()
    {
        return parent_;
    }

private:
    std::shared_ptr<Scope> parent_;                                    // 父作用域
    std::unordered_map<std::string, std::unique_ptr<Symbol>> symbols_; // 当前作用域符号
};