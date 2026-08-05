/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <string>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>

// 前向声明
class TypeContext;
class Symbol;
class TraitType;

// --- 基础类型类 ---
class Type
{
public:
    enum class Kind
    {
        Primitive,
        Custom,
        Reference,
        Function,
        Trait,
        Self,
        GenericParam,
    };

    explicit Type(Kind kind);
    virtual ~Type() = default;

    Kind getKind() const;
    virtual bool equals(const std::shared_ptr<Type> &other) const = 0;
    virtual std::string toString() const = 0;

    /// Copy semantics: primitives and references are Copy (non-owning); structs
    /// and trait objects are Move. Single source of truth for the three former
    /// isCopyType copies (sema / MIRBuilder / codegen).
    bool isCopyable() const
    {
        return kind == Kind::Primitive || kind == Kind::Reference;
    }

    /// True if this type's implTrait contains a trait named `name` (e.g. "Drop").
    bool implementsTrait(const std::string &name) const;

    std::vector<std::shared_ptr<TraitType>> implTrait;

private:
    Kind kind;
};

// --- 1. 原始类型 ---
class PrimitiveType : public Type
{
public:
    enum class PrimKind
    {
        I8 = 0, I16, I32, I64,
        F32, F64, BOOL, CHAR, VOID
    };

    explicit PrimitiveType(PrimKind pk);
    PrimKind getPrimKind() const;
    bool equals(const std::shared_ptr<Type> &other) const override;
    bool isFloat() const;
    bool isInteger() const;
    std::string toString() const override;
    static PrimKind getKind(std::string str);

private:
    PrimKind primKind;
};

// --- 2. 引用类型 ---
class ReferenceType : public Type
{
public:
    ReferenceType(std::shared_ptr<Type> base, bool isMutable);
    std::shared_ptr<Type> getBaseType() const;
    bool isMutableRef() const;
    bool equals(const std::shared_ptr<Type> &other) const override;
    std::string toString() const override;

private:
    std::shared_ptr<Type> baseType;
    bool isMutable;
};

// --- 3. 自定义类型 ---
class CustomType : public Type
{
public:
    struct Field
    {
        std::string name;
        std::shared_ptr<Type> type;
        bool operator==(const Field &other) const;
        bool operator==(const std::string &other) const;
    };

    struct Method
    {
        std::string name;
        std::string traitName;
        std::vector<Field> params;
        std::shared_ptr<Type> returnType;
        bool isStatic;
        bool isTraitImpl;
    };

public:
    CustomType(std::string name, std::vector<Field> fields);
    CustomType(std::string name,
               std::vector<std::shared_ptr<Type>> genericParams,
               std::vector<Field> fields);

    const std::string &getName() const;
    /** For an instantiation, the origin definition's name (e.g. "Range" for
     *  "Range$i32"); for a definition, its own name. Method symbols are
     *  registered under the origin's name. */
    const std::string &getOriginName() const;
    const std::vector<Field> &getFields() const;
    const std::vector<Method> &getMethods() const;
    void addMethods(std::vector<Method> methods);
    bool equals(const std::shared_ptr<Type> &other) const override;
    std::string toString() const override;

    // --- generics ---
    bool isGeneric() const { return !genericParams.empty() && genericArgs.empty(); }
    bool isInstantiated() const { return !genericArgs.empty(); }
    const std::vector<std::shared_ptr<Type>> &getGenericParams() const { return genericParams; }
    const std::vector<std::shared_ptr<Type>> &getGenericArgs() const { return genericArgs; }
    void setGenericArgs(std::vector<std::shared_ptr<Type>> args) { genericArgs = std::move(args); }
    void setFields(std::vector<Field> f) { fields = std::move(f); }

    // For instantiations: points to the generic definition.
    // Methods always live on the origin; instantiations look them up there.
    std::shared_ptr<CustomType> genericOrigin;

private:
    std::string name;
    std::vector<Field> fields;
    std::vector<Method> methods;
    std::vector<std::shared_ptr<Type>> genericParams;
    std::vector<std::shared_ptr<Type>> genericArgs;
};

// --- 4. 函数类型 ---
class FunctionType : public Type
{
public:
    FunctionType(std::vector<std::shared_ptr<Type>> params,
                 std::shared_ptr<Type> returnType);
    FunctionType(std::vector<std::shared_ptr<Type>> genericParams,
                 std::vector<std::shared_ptr<Type>> params,
                 std::shared_ptr<Type> returnType);

    bool isGeneric() const;
    const std::vector<std::shared_ptr<Type>> &getParams() const;
    const std::vector<std::shared_ptr<Type>> &getGenericParams() const;
    const std::shared_ptr<Type> &getReturnType() const;
    bool equals(const std::shared_ptr<Type> &other) const override;
    std::string toString() const override;

private:
    std::vector<std::shared_ptr<Type>> params;
    std::shared_ptr<Type> returnType;
    std::vector<std::shared_ptr<Type>> genericParams;
};

// --- 5. Trait 类型 ---
class TraitType : public Type
{
public:
    using Method = CustomType::Method;
    TraitType(std::string name, std::vector<Method> methods);
    TraitType(std::string name, std::vector<std::shared_ptr<Type>> genericParams, std::vector<Method> methods);
    const std::string &getName() const;
    const std::vector<std::shared_ptr<Type>> &getGenericParams() const;
    void setGenericArgs(std::vector<std::shared_ptr<Type>> args);
    const std::vector<std::shared_ptr<Type>> &getGenericArgs() const;
    const std::vector<Method> &getMethods() const;
    std::optional<Method> findMethod(const std::string &methodName) const;
    bool equals(const std::shared_ptr<Type> &other) const override;
    std::string toString() const override;

private:
    std::string name;
    std::vector<std::shared_ptr<Type>> genericParams; // declaration params (Iterator<T> -> [T])
    std::vector<std::shared_ptr<Type>> genericArgs;   // use-site args (Iterator<i32> -> [i32])
    std::vector<Method> methods;
};

// --- 6. Self 类型 ---
class SelfType : public Type
{
public:
    SelfType(const std::string &trait_name, bool isMut, bool isRef);
    const std::string &getTraitName() const;
    bool isMutable() const;
    bool isReference();
    bool equals(const std::shared_ptr<Type> &other) const override;
    std::string toString() const override;

private:
    std::string trait_name;
    bool isMut;
    bool isRef;
};

// --- 7. 泛型参数类型 ---
class GenericParamType : public Type
{
public:
    explicit GenericParamType(std::string paramName);
    const std::string &getParamName() const;
    const std::vector<std::shared_ptr<TraitType>> &getConstraints() const;
    void updateContraints(std::vector<std::shared_ptr<TraitType>> constraints);
    bool equals(const std::shared_ptr<Type> &other) const override;
    std::string toString() const override;

private:
    std::string paramName;
    std::vector<std::shared_ptr<TraitType>> constraints;
};