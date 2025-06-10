/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Lexer/Token.hpp"
#include "Parser/AST.hpp"
#include "llvm/IR/Type.h"

class Type
{
public:
    enum class TypeKind
    {
        PRIMITIVE,
        STRUCT,
        FUNCTION,
        VOID
    };

    Type(TypeKind kind, const std::string &name)
        : kind(kind), typeName(name) {}

    virtual ~Type() = default;

    bool isPrimitive() const
    {
        return kind == TypeKind::PRIMITIVE;
    }
    bool isStruct() const
    {
        return kind == TypeKind::STRUCT;
    }
    bool isFunction() const
    {
        return kind == TypeKind::FUNCTION;
    }
    bool isVoid() const
    {
        return kind == TypeKind::VOID;
    }

    const std::string &getName() const
    {
        return typeName;
    }
    TypeKind getKind() const
    {
        return kind;
    }

    virtual llvm::Type *toLLVMType(llvm::LLVMContext &context) const = 0;

    static std::shared_ptr<Type> fromToken(const Token &token);
    static std::shared_ptr<Type> getVoidType();

private:
    TypeKind kind;
    std::string typeName;
};

class PrimitiveType : public Type
{
public:
    PrimitiveType(const std::string &name)
        : Type(TypeKind::PRIMITIVE, name) {}

    llvm::Type *toLLVMType(llvm::LLVMContext &context) const override;
};

class StructType : public Type
{
public:
    StructType(const std::string &name, const std::vector<MemberVarDef> &members)
        : Type(TypeKind::STRUCT, name), members(members) {}

    llvm::Type *toLLVMType(llvm::LLVMContext &context) const override;

    const std::vector<MemberVarDef> &getMembers() const
    {
        return members;
    }
    int getMemberIndex(const std::string &name) const;

private:
    std::vector<MemberVarDef> members;
};

class FunctionType : public Type
{
public:
    FunctionType(std::shared_ptr<Type> returnType,
        const std::vector<std::shared_ptr<Type>> &paramTypes)
        : Type(TypeKind::FUNCTION, "function"),
          returnType(returnType),
          paramTypes(paramTypes) {}

    llvm::Type *toLLVMType(llvm::LLVMContext &context) const override;

    std::shared_ptr<Type> getReturnType() const
    {
        return returnType;
    }
    const std::vector<std::shared_ptr<Type>> &getParamTypes() const
    {
        return paramTypes;
    }

private:
    std::shared_ptr<Type> returnType;
    std::vector<std::shared_ptr<Type>> paramTypes;
};

class VoidType : public Type
{
public:
    VoidType() : Type(TypeKind::VOID, "void") {}

    llvm::Type *toLLVMType(llvm::LLVMContext &context) const override
    {
        return llvm::Type::getVoidTy(context);
    }
};