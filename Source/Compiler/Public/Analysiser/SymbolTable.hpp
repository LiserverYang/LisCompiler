/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include "Analysiser/Scope.hpp"
#include "Analysiser/Symbol.hpp"
#include "Core/Debugging.hpp"

class SymbolTable
{
public:
    static SymbolTable &getInstance()
    {
        static SymbolTable instance;
        return instance;
    }

    // 初始化全局作用域
    void initGlobalScope()
    {
        currentScope_ = std::make_shared<Scope>();
    }

    // 进入/退出作用域
    void enterScope(std::shared_ptr<Scope> newScope)
    {
        newScope->parent_ = currentScope_;

        currentScope_ = newScope;
    }

    void exitScope()
    {
        if (currentScope_->parent_)
        {
            currentScope_ = currentScope_->parent_;
        }
    }

    // 插入/查询符号（代理到当前作用域）
    bool insertSymbol(const std::string &name, std::unique_ptr<Symbol> symbol)
    {
        return currentScope_->insert(name, std::move(symbol));
    }
    Symbol *lookupSymbol(const std::string &name)
    {
        // Selective-import alias symbols forward to their target (module
        // system): every lookup site transparently sees the target symbol.
        Symbol *sym = currentScope_->lookup(name);
        while (sym && sym->aliasTarget)
            sym = sym->aliasTarget;
        return sym;
    }

    // 获取当前作用域
    std::shared_ptr<Scope> getCurrentScope()
    {
        return currentScope_;
    }

private:
    SymbolTable() = default;
    std::shared_ptr<Scope> currentScope_; // 当前作用域
};