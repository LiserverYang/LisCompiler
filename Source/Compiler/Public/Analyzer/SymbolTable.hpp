/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include <map>
#include <memory>
#include <stack>
#include <string>
#include <vector>

#include "Parser/AST.hpp"
#include "llvm/IR/Value.h"

class SymbolTable
{
    enum class SymbolKind
    {
        VARIABLE,
        FUNCTION,
        STRUCT,
        PARAMETER
    };

    struct Symbol
    {
        SymbolKind kind;
        std::string name;
        std::shared_ptr<Type> type;
        llvm::Value *llvmValue = nullptr;
        bool isMutable = false;
        bool isGlobal = false;

        // For functions
        std::vector<std::shared_ptr<Type>> paramTypes;
        std::shared_ptr<Type> returnType;

        // For structs
        std::vector<MemberVarDef> members;
    };

    void enterScope()
    {
        scopes.push({});
    }

    void exitScope()
    {
        scopes.pop();
    }

    bool addSymbol(const std::string &name, Symbol symbol)
    {
        if (scopes.top().count(name))
            return false;

        scopes.top()[name] = symbol;
        return true;
    }

    Symbol *lookup(const std::string &name);

    Symbol *lookupCurrentScope(const std::string &name);

    void addStruct(const std::string &name, const std::vector<MemberVarDef> &members);
    const StructDef *getStruct(const std::string &name) const;

    void setCurrentFunction(Symbol *func)
    {
        currentFunction = func;
    }
    Symbol *getCurrentFunction() const
    {
        return currentFunction;
    }

    void setCurrentStruct(Symbol *structSym)
    {
        currentStruct = structSym;
    }
    Symbol *getCurrentStruct() const
    {
        return currentStruct;
    }

private:
    std::stack<std::map<std::string, Symbol>> scopes;
    std::map<std::string, StructDef> structs;

    Symbol *currentFunction = nullptr;
    Symbol *currentStruct = nullptr;
};