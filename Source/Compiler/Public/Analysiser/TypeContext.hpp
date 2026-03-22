/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <iomanip>
#include <iostream>
#include <optional>
#include <unordered_map>

#include "Analysiser/Type.hpp"

template <typename T>
void hash_combine(size_t &seed, T const &v)
{
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std
{
template <>
struct hash<std::vector<std::shared_ptr<Type>>>
{
    size_t operator()(const std::vector<std::shared_ptr<Type>> &vec) const
    {
        size_t seed = vec.size();
        for (const auto &ptr : vec)
        {
            hash_combine(seed, ptr);
        }
        return seed;
    }
};
} // namespace std

struct RefHash
{
    std::size_t operator()(const std::pair<void *, bool> &p) const
    {
        auto h1 = std::hash<void *>{}(p.first);
        auto h2 = std::hash<bool>{}(p.second);
        return h1 ^ (h2 << 1); // 避免对称性
    }
};

struct FuncHash
{
    std::size_t operator()(const std::pair<std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>> &p) const
    {
        auto h1 = std::hash<std::vector<std::shared_ptr<Type>>>{}(p.first);
        auto h2 = std::hash<std::shared_ptr<Type>>{}(p.second);
        return h1 ^ (h2 << 1); // 避免对称性
    }
};

class TypeContext
{
public:
    TypeContext()
    {
        // 初始化原始类型单例
        primitives[PrimitiveType::PrimKind::I8] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I8);
        primitives[PrimitiveType::PrimKind::I16] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I16);
        primitives[PrimitiveType::PrimKind::I32] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I32);
        primitives[PrimitiveType::PrimKind::I64] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I64);
        primitives[PrimitiveType::PrimKind::F32] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::F32);
        primitives[PrimitiveType::PrimKind::F64] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::F64);
        primitives[PrimitiveType::PrimKind::BOOL] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::BOOL);
        primitives[PrimitiveType::PrimKind::VOID] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::VOID);
    }

    // 获取原始类型
    std::shared_ptr<PrimitiveType> getPrimitive(PrimitiveType::PrimKind kind)
    {
        return primitives[kind];
    }

    // 获取或创建引用类型 (Flyweight 模式)
    std::shared_ptr<ReferenceType> getReference(std::shared_ptr<Type> base, bool isMutable)
    {
        void *basePtr = base.get();
        auto key = std::make_pair(basePtr, isMutable);

        auto it = refCache.find(key);
        if (it != refCache.end())
        {
            return it->second;
        }

        auto refType = std::make_shared<ReferenceType>(base, isMutable);
        refCache[key] = refType;
        return refType;
    }

    std::shared_ptr<CustomType> createCustom(std::string name, std::vector<CustomType::Field> fields)
    {
        auto it = customs.find(name);

        if (it != customs.end())
        {
            return it->second;
        }

        auto customType = std::make_shared<CustomType>(name, fields);
        customs[name] = customType;
        return customType;
    }

    std::optional<std::shared_ptr<CustomType>> getCustom(std::string name)
    {
        auto it = customs.find(name);

        if (it != customs.end())
        {
            return it->second;
        }

        return std::nullopt;
    }

    // 获取函数类型
    std::shared_ptr<FunctionType> getFunction(std::vector<std::shared_ptr<Type>> params, std::shared_ptr<Type> returnType)
    {
        auto it = functions.find(std::make_pair(params, returnType));

        if (it != functions.end())
        {
            return it->second;
        }

        auto functionType = std::make_shared<FunctionType>(params, returnType);
        functions[std::make_pair(params, returnType)] = functionType;
        return functionType;
    }

    std::shared_ptr<TraitType> createTrait(std::string name, std::vector<TraitType::Method> methods)
    {
        auto it = traits.find(name);
        if (it != traits.end())
        {
            return it->second;
        }

        auto traitType = std::make_shared<TraitType>(name, std::move(methods));
        traits[name] = traitType;
        return traitType;
    }

    std::optional<std::shared_ptr<TraitType>> getTrait(std::string name)
    {
        auto it = traits.find(name);
        if (it != traits.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::shared_ptr<SelfType> createSelf(std::string name, bool isMut, bool isRef)
    {
        auto it = selfs.find(name);
        if (it != selfs.end())
        {
            return it->second;
        }

        auto selfType = std::make_shared<SelfType>(name, isMut, isRef);
        selfs[name] = selfType;
        return selfType;
    }

    std::optional<std::shared_ptr<SelfType>> getSelf(std::string name)
    {
        auto it = selfs.find(name);
        if (it != selfs.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    void printTypeTable() const
    {
        std::cout << "==================================== TYPE TABLE ====================================" << std::endl;

        std::cout << "\n[1] Primitive Types (" << primitives.size() << "):" << std::endl;
        std::cout << "-----------------------------------------------------------------------------------" << std::endl;
        for (const auto &[kind, type] : primitives)
        {
            std::cout << "  " << std::setw(10) << std::left << type->toString()
                      << " | Kind: " << static_cast<int>(kind) << std::endl;
        }

        std::cout << "\n[2] Custom Types (" << customs.size() << "):" << std::endl;
        std::cout << "-----------------------------------------------------------------------------------" << std::endl;
        if (customs.empty())
        {
            std::cout << "  (No custom types defined)" << std::endl;
        }
        else
        {
            for (const auto &[name, type] : customs)
            {
                std::cout << "  " << std::setw(20) << std::left << name << " | Fields: [";
                // 打印字段
                const auto &fields = type->getFields();
                for (size_t i = 0; i < fields.size(); ++i)
                {
                    std::cout << fields[i].name << ": " << fields[i].type->toString();
                    if (i != fields.size() - 1) std::cout << ", ";
                }
                std::cout << "] | Methods: [";
                // 打印方法
                const auto &methods = type->getMethods();
                for (size_t i = 0; i < methods.size(); ++i)
                {
                    std::cout << methods[i].name << "(";
                    for (size_t j = 0; j < methods[i].params.size(); ++j)
                    {
                        std::cout << methods[i].params[j].type->toString();
                        if (j != methods[i].params.size() - 1) std::cout << ", ";
                    }
                    std::cout << ") -> " << methods[i].returnType->toString()
                              << (methods[i].isStatic ? " (static)" : "");
                    if (i != methods.size() - 1) std::cout << ", ";
                }
                std::cout << "]" << std::endl;
            }
        }

        std::cout << "\n[3] Reference Type Cache (" << refCache.size() << "):" << std::endl;
        std::cout << "-----------------------------------------------------------------------------------" << std::endl;
        if (refCache.empty())
        {
            std::cout << "  (No reference types cached)" << std::endl;
        }
        else
        {
            for (const auto &[key, type] : refCache)
            {
                std::cout << "  " << std::setw(20) << std::left << type->toString()
                          << " | Base Type Ptr: " << key.first
                          << " | Mutable: " << (key.second ? "Yes" : "No") << std::endl;
            }
        }

        std::cout << "\n[4] Function Types (" << functions.size() << "):" << std::endl;
        std::cout << "-----------------------------------------------------------------------------------" << std::endl;
        if (functions.empty())
        {
            std::cout << "  (No function types defined)" << std::endl;
        }
        else
        {
            for (const auto &[key, type] : functions)
            {
                std::cout << "  " << std::setw(40) << std::left << type->toString()
                          << " | Param Count: " << key.first.size()
                          << " | Return Type: " << key.second->toString() << std::endl;
            }
        }

        std::cout << "\n[5] Trait Types (" << traits.size() << "):" << std::endl;
        std::cout << "-----------------------------------------------------------------------------------" << std::endl;
        if (traits.empty())
        {
            std::cout << "  (No traits defined)" << std::endl;
        }
        else
        {
            for (const auto &[name, type] : traits)
            {
                std::cout << "  " << std::setw(20) << std::left << name << " | Methods: [";
                const auto &methods = type->getMethods();
                for (size_t i = 0; i < methods.size(); ++i)
                {
                    std::cout << methods[i].name << "(";
                    for (size_t j = 0; j < methods[i].params.size(); ++j)
                    {
                        std::cout << methods[i].params[j].type->toString();
                        if (j != methods[i].params.size() - 1) std::cout << ", ";
                    }
                    std::cout << ") -> " << methods[i].returnType->toString();
                    if (i != methods.size() - 1) std::cout << ", ";
                }
                std::cout << "]" << std::endl;
            }
        }

        std::cout << "\n===================================================================================" << std::endl;
    }

private:
    std::unordered_map<PrimitiveType::PrimKind, std::shared_ptr<PrimitiveType>> primitives;
    std::unordered_map<std::string, std::shared_ptr<CustomType>> customs;
    std::unordered_map<std::pair<std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>>, std::shared_ptr<FunctionType>, FuncHash> functions;
    std::unordered_map<std::pair<void *, bool>, std::shared_ptr<ReferenceType>, RefHash> refCache;
    std::unordered_map<std::string, std::shared_ptr<TraitType>> traits;
    std::unordered_map<std::string, std::shared_ptr<SelfType>> selfs;
};