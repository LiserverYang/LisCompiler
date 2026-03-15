/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <string>

#include <cassert>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 前向声明
class TypeContext;
class Symbol;

// --- 基础类型类 ---
class Type : public std::enable_shared_from_this<Type>
{
public:
    enum class Kind
    {
        Primitive, // 原始类型 (int, float)
        Custom,    // 自定义类型 (struct, class)
        Reference, // 引用类型 (&T, &mut T)
        Function,  // 函数类型
    };

    explicit Type(Kind kind) : kind(kind) {}
    virtual ~Type() = default;

    Kind getKind() const
    {
        return kind;
    }

    // 类型相等判断 (核心接口)
    virtual bool equals(const std::shared_ptr<Type> &other) const = 0;

    // 调试用字符串
    virtual std::string toString() const = 0;

private:
    Kind kind;
};

// --- 1. 原始类型 ---
class PrimitiveType : public Type
{
public:
    enum class PrimKind
    {
        I8 = 0,
        I16,
        I32,
        I64,
        F32,
        F64,
        BOOL,
        CHAR,
        VOID
    };

    explicit PrimitiveType(PrimKind pk) : Type(Kind::Primitive), primKind(pk) {}

    PrimKind getPrimKind() const
    {
        return primKind;
    }

    bool equals(const std::shared_ptr<Type> &other) const override
    {
        if (auto pt = std::dynamic_pointer_cast<PrimitiveType>(other))
        {
            return pt->primKind == primKind;
        }
        return false;
    }

    std::string toString() const override
    {
        switch (primKind)
        {
        case PrimKind::I8: return "int8";
        case PrimKind::I16: return "int16";
        case PrimKind::I32: return "int32";
        case PrimKind::I64: return "int64";
        case PrimKind::F32: return "float32";
        case PrimKind::F64: return "float64";
        case PrimKind::BOOL: return "bool";
        case PrimKind::CHAR: return "char";
        case PrimKind::VOID: return "void";
        default: return "unknown";
        }
    }

    static PrimKind getKind(std::string str)
    {
        if (str == "i8")
        {
            return PrimKind::I8;
        }
        else if (str == "i16")
        {
            return PrimKind::I16;
        }
        else if (str == "i32")
        {
            return PrimKind::I32;
        }
        else if (str == "i64")
        {
            return PrimKind::I64;
        }
        else if (str == "f32")
        {
            return PrimKind::F32;
        }
        else if (str == "f64")
        {
            return PrimKind::F64;
        }
        else if (str == "bool")
        {
            return PrimKind::BOOL;
        }
        if (str == "char")
        {
            return PrimKind::CHAR;
        }

        return PrimKind::VOID;
    }

private:
    PrimKind primKind;
};

// --- 2. 引用类型 ---
// 专门处理 isReference 和 isMutReference
class ReferenceType : public Type
{
public:
    ReferenceType(std::shared_ptr<Type> base, bool isMutable)
        : Type(Kind::Reference), baseType(std::move(base)), isMutable(isMutable) {}

    std::shared_ptr<Type> getBaseType() const
    {
        return baseType;
    }

    bool isMutableRef() const
    {
        return isMutable;
    }

    bool equals(const std::shared_ptr<Type> &other) const override
    {
        if (auto rt = std::dynamic_pointer_cast<ReferenceType>(other))
        {
            return rt->isMutable == isMutable && rt->baseType->equals(baseType);
        }
        return false;
    }

    std::string toString() const override
    {
        return (isMutable ? "&mut " : "&") + baseType->toString();
    }

private:
    std::shared_ptr<Type> baseType;
    bool isMutable;
};

// --- 3. 自定义类型 (Nominal Type) ---
class CustomType : public Type
{
public:
    struct Field
    {
        std::string name;
        std::shared_ptr<Type> type;

        bool operator==(const Field &other) const
        {
            return name == other.name && type->equals(other.type);
        }

        bool operator==(const std::string &other) const
        {
            return name == other;
        }
    };

    struct Method
    {
        std::string name;                 // 方法名
        std::vector<Field> params;        // 参数（复用Field结构，因为参数也是"名+类型"）
        std::shared_ptr<Type> returnType; // 返回类型
        bool isStatic;                    // 是否为静态方法
    };

public:
    // name: 类型名
    CustomType(std::string name, std::vector<Field> fields)
        : Type(Kind::Custom), name(std::move(name)), fields(std::move(fields)) {}

    const std::string &getName() const
    {
        return name;
    }

    const std::vector<Field> &getFields() const
    {
        return fields;
    }

    void addMethods(std::vector<Method> methods)
    {
        for (auto &method : methods)
        {
            this->methods.push_back(method);
        }
    }

    bool equals(const std::shared_ptr<Type> &other) const override
    {
        if (auto ct = std::dynamic_pointer_cast<CustomType>(other))
        {
            if (name != ct->name || fields.size() != ct->fields.size())
                return false;
            for (size_t i = 0; i < fields.size(); ++i)
                if (fields[i].name != ct->fields[i].name || !fields[i].type->equals(ct->fields[i].type))
                    return false;
            return true;
        }
        return false;
    }

    std::string toString() const override
    {
        return name;
    }

private:
    std::string name;
    std::vector<Field> fields;
    std::vector<Method> methods;
};

class FunctionType : public Type
{
public:
    struct Param
    {
        std::string name;
        std::shared_ptr<Type> type;
    };

public:
    FunctionType(std::string name, std::vector<Param> params, std::shared_ptr<Type> returnType)
        : Type(Kind::Function), name(std::move(name)), params(std::move(params)), returnType(returnType) {}

    const std::string &getName() const
    {
        return name;
    }

    const std::vector<Param> &getParams() const
    {
        return params;
    }

    std::shared_ptr<Type> getReturnType() const
    {
        return returnType;
    }

    bool equals(const std::shared_ptr<Type> &other) const override
    {
        if (auto ft = std::dynamic_pointer_cast<FunctionType>(other))
        {
            if (name != ft->name || params.size() != ft->params.size() || !returnType->equals(ft->returnType))
                return false;

            for (size_t i = 0; i < params.size(); ++i)
                if (params[i].name != ft->params[i].name || !params[i].type->equals(ft->params[i].type))
                    return false;

            return true;
        }

        return false;
    }

    std::string toString() const override
    {
        return name;
    }

private:
    std::string name;
    std::vector<Param> params;
    std::shared_ptr<Type> returnType;
};