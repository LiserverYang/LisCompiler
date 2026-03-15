/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Analysiser/Scope.hpp"

bool Scope::insert(const std::string &name, std::unique_ptr<Symbol> symbol)
{
    if (symbols_.find(name) != symbols_.end())
        return false;

    symbols_[name] = std::move(symbol);

    return true;
}

Symbol *Scope::lookup(const std::string &name)
{
    Scope *currentScope = this;

    while (currentScope != nullptr)
    {
        auto result = currentScope->symbols_.find(name);

        if (result != currentScope->symbols_.end())
        {
            return result->second.get();
        }

        currentScope = currentScope->parent_.get();
    }

    return nullptr;
}

std::shared_ptr<Scope> Scope::createChild()
{
    return std::make_shared<Scope>(shared_from_this());
}