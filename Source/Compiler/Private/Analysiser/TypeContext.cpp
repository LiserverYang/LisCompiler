/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Analysiser/TypeContext.hpp"
#include <assert.h>
#include <functional>

template <typename T>
void hash_combine(size_t &seed, T const &v)
{
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std
{
size_t hash<std::vector<std::shared_ptr<Type>>>::operator()(const std::vector<std::shared_ptr<Type>> &vec) const
{
    size_t seed = vec.size();
    for (const auto &ptr : vec)
    {
        hash_combine(seed, ptr);
    }
    return seed;
}
} // namespace std

std::size_t RefHash::operator()(const std::pair<void *, bool> &p) const
{
    auto h1 = std::hash<void *>{}(p.first);
    auto h2 = std::hash<bool>{}(p.second);
    return h1 ^ (h2 << 1);
}

std::size_t FuncHash::operator()(const std::pair<std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>> &p) const
{
    auto h1 = std::hash<std::vector<std::shared_ptr<Type>>>{}(p.first);
    auto h2 = std::hash<std::shared_ptr<Type>>{}(p.second);
    return h1 ^ (h2 << 1);
}

TypeContext::TypeContext()
{
    primitives[PrimitiveType::PrimKind::I8] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I8);
    primitives[PrimitiveType::PrimKind::I16] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I16);
    primitives[PrimitiveType::PrimKind::I32] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I32);
    primitives[PrimitiveType::PrimKind::I64] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::I64);
    primitives[PrimitiveType::PrimKind::F32] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::F32);
    primitives[PrimitiveType::PrimKind::F64] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::F64);
    primitives[PrimitiveType::PrimKind::BOOL] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::BOOL);
    primitives[PrimitiveType::PrimKind::VOID] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::VOID);
}

std::shared_ptr<PrimitiveType> TypeContext::getPrimitive(PrimitiveType::PrimKind kind)
{
    return primitives[kind];
}

std::shared_ptr<ReferenceType> TypeContext::getReference(std::shared_ptr<Type> base, bool isMutable)
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

std::shared_ptr<CustomType> TypeContext::createCustom(std::string name, std::vector<CustomType::Field> fields)
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

std::optional<std::shared_ptr<CustomType>> TypeContext::getCustom(std::string name)
{
    auto it = customs.find(name);
    if (it != customs.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::shared_ptr<FunctionType> TypeContext::getFunction(std::vector<std::shared_ptr<Type>> params, std::shared_ptr<Type> returnType)
{
    auto key = std::make_pair(params, returnType);
    auto it = functions.find(key);
    if (it != functions.end())
    {
        return it->second;
    }

    auto functionType = std::make_shared<FunctionType>(params, returnType);
    functions[key] = functionType;
    return functionType;
}

std::shared_ptr<TraitType> TypeContext::createTrait(std::string name, std::vector<TraitType::Method> methods)
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

std::optional<std::shared_ptr<TraitType>> TypeContext::getTrait(std::string name)
{
    auto it = traits.find(name);
    if (it != traits.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::shared_ptr<SelfType> TypeContext::createSelf(std::string name, bool isMut, bool isRef)
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

std::optional<std::shared_ptr<SelfType>> TypeContext::getSelf(std::string name)
{
    auto it = selfs.find(name);
    if (it != selfs.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::shared_ptr<FunctionType> TypeContext::getGenericFunction(
    std::vector<std::shared_ptr<GenericParamType>> genericParams,
    std::vector<std::shared_ptr<Type>> params,
    std::shared_ptr<Type> returnType)
{
    auto key = std::make_pair(params, returnType);
    auto it = functions.find(key);

    if (it != functions.end())
        return it->second;

    auto funcTy = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>(genericParams.begin(), genericParams.end()),
        params,
        returnType);
    functions[key] = funcTy;
    return funcTy;
}

void TypeContext::printTypeTable() const
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
            const auto &fields = type->getFields();
            for (size_t i = 0; i < fields.size(); ++i)
            {
                std::cout << fields[i].name << ": " << fields[i].type->toString();
                if (i != fields.size() - 1) std::cout << ", ";
            }
            std::cout << "] | Methods: [";
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

static std::string mangleCustomName(const std::string &name,
    const std::vector<std::shared_ptr<Type>> &args)
{
    std::string s = name + "$";
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i) s += "_";
        s += args[i]->toString();
    }
    return s;
}

static std::shared_ptr<Type> substTypeForCustom(
    TypeContext *ctx,
    std::shared_ptr<Type> ty,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &subst)
{
    if (!ty) return ty;
    switch (ty->getKind())
    {
    case Type::Kind::GenericParam:
    {
        auto gp = std::static_pointer_cast<GenericParamType>(ty);
        auto it = subst.find(gp->getParamName());
        return it != subst.end() ? it->second : ty;
    }
    case Type::Kind::Reference:
    {
        auto r = std::static_pointer_cast<ReferenceType>(ty);
        return ctx->getReference(substTypeForCustom(ctx, r->getBaseType(), subst), r->isMutableRef());
    }
    case Type::Kind::Custom:
    {
        auto c = std::static_pointer_cast<CustomType>(ty);
        if (c->getGenericArgs().empty()) return ty;
        std::vector<std::shared_ptr<Type>> newArgs;
        for (auto &a : c->getGenericArgs())
            newArgs.push_back(substTypeForCustom(ctx, a, subst));
        auto origin = c->genericOrigin ? c->genericOrigin : c;
        return ctx->instantiateCustom(origin, std::move(newArgs));
    }
    default:
        return ty;
    }
}

std::shared_ptr<CustomType> TypeContext::createGenericCustom(
    std::string name,
    std::vector<std::shared_ptr<Type>> genericParams,
    std::vector<CustomType::Field> fields)
{
    auto it = customs.find(name);
    if (it != customs.end()) return it->second;

    auto t = std::make_shared<CustomType>(std::move(name), std::move(genericParams), std::move(fields));
    customs[t->getName()] = t;
    return t;
}

std::shared_ptr<CustomType> TypeContext::instantiateCustom(
    std::shared_ptr<CustomType> generic,
    std::vector<std::shared_ptr<Type>> args)
{
    assert(generic);
    assert(generic->getGenericParams().size() == args.size());

    // Detect whether this instantiation is fully concrete (no generic params
    // anywhere inside the args). Only concrete ones are cached for codegen.
    std::function<bool(const std::shared_ptr<Type> &)> hasGenericParam =
        [&](const std::shared_ptr<Type> &t) -> bool
    {
        if (!t) return false;
        switch (t->getKind())
        {
        case Type::Kind::GenericParam: return true;
        case Type::Kind::Reference:
            return hasGenericParam(std::static_pointer_cast<ReferenceType>(t)->getBaseType());
        case Type::Kind::Custom:
        {
            auto c = std::static_pointer_cast<CustomType>(t);
            for (auto &a : c->getGenericArgs())
                if (hasGenericParam(a)) return true;
            return false;
        }
        default: return false;
        }
    };

    bool concrete = true;
    for (auto &a : args)
        if (hasGenericParam(a))
        {
            concrete = false;
            break;
        }

    std::string mangled = mangleCustomName(generic->getName(), args);

    if (concrete)
    {
        auto it = instantiatedCustoms.find(mangled);
        if (it != instantiatedCustoms.end()) return it->second;
    }

    std::unordered_map<std::string, std::shared_ptr<Type>> subst;
    for (size_t i = 0; i < args.size(); ++i)
    {
        auto gp = std::static_pointer_cast<GenericParamType>(generic->getGenericParams()[i]);
        subst[gp->getParamName()] = args[i];
    }

    std::vector<CustomType::Field> newFields;
    for (auto &f : generic->getFields())
        newFields.push_back(CustomType::Field{f.name, substTypeForCustom(this, f.type, subst)});

    auto inst = std::make_shared<CustomType>(mangled,
        std::vector<std::shared_ptr<Type>>{},
        std::move(newFields));

    inst->setGenericArgs(std::move(args));
    inst->genericOrigin = generic;

    if (concrete)
        instantiatedCustoms[mangled] = inst;

    customs[mangled] = inst;

    return inst;
}