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

std::size_t FuncHash::operator()(const std::tuple<std::vector<std::shared_ptr<Type>>, std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>> &p) const
{
    auto h1 = std::hash<std::vector<std::shared_ptr<Type>>>{}(std::get<0>(p));
    auto h2 = std::hash<std::vector<std::shared_ptr<Type>>>{}(std::get<1>(p));
    auto h3 = std::hash<std::shared_ptr<Type>>{}(std::get<2>(p));
    return h1 ^ (h2 << 1) ^ (h3 << 2);
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
    primitives[PrimitiveType::PrimKind::CHAR] = std::make_shared<PrimitiveType>(PrimitiveType::PrimKind::CHAR);
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

std::shared_ptr<ArrayType> TypeContext::getArray(std::shared_ptr<Type> elementType, size_t size)
{
    auto key = std::make_pair((void *)elementType.get(), size);
    auto it = arrayCache.find(key);
    if (it != arrayCache.end())
        return it->second;

    auto arrTy = std::make_shared<ArrayType>(elementType, size);
    arrayCache[key] = arrTy;
    return arrTy;
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

std::shared_ptr<CustomType> TypeContext::createCustomShell(std::string name)
{
    auto it = customs.find(name);
    if (it != customs.end())
    {
        return it->second;
    }

    auto customType = std::make_shared<CustomType>(name, std::vector<CustomType::Field>{});
    customs[name] = customType;
    return customType;
}

std::shared_ptr<CustomType> TypeContext::createGenericCustomShell(std::string name, std::vector<std::shared_ptr<Type>> genericParams)
{
    auto it = customs.find(name);
    if (it != customs.end())
    {
        return it->second;
    }

    auto customType = std::make_shared<CustomType>(name, std::move(genericParams), std::vector<CustomType::Field>{});
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
    // Non-generic key: empty genericParams, so it never aliases a generic
    // function with the same signature.
    auto key = std::make_tuple(std::vector<std::shared_ptr<Type>>{}, params, returnType);
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

std::shared_ptr<TraitType> TypeContext::createGenericTrait(
    std::string name,
    std::vector<std::shared_ptr<Type>> genericParams,
    std::vector<TraitType::Method> methods)
{
    auto it = traits.find(name);
    if (it != traits.end())
    {
        return it->second;
    }

    auto traitType = std::make_shared<TraitType>(name, std::move(genericParams), std::move(methods));
    traits[name] = traitType;
    return traitType;
}

std::shared_ptr<TraitType> TypeContext::instantiateTrait(
    std::shared_ptr<TraitType> generic,
    std::vector<std::shared_ptr<Type>> args)
{
    // A malformed constraint like `T: Iterator<i32, i64>` on a single-param
    // trait would silently truncate in callers' zip loops. Fail loudly.
    assert(generic->getGenericParams().empty() || generic->getGenericParams().size() == args.size());

    auto inst = std::make_shared<TraitType>(
        generic->getName(), generic->getGenericParams(), generic->getMethods());
    inst->setGenericArgs(std::move(args));
    return inst;
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
    // P2: cache key MUST be the same triple SelfType::equals compares. The old
    // name-only key made `fn a(&self)` + `fn b(&mut self)` in one trait share a
    // single SelfType, so `b`'s receiver lost its mutability and the borrow
    // checker treated a `&mut self` method as `&self` (aliasing that should be
    // rejected slipped through).
    SelfKey key{std::move(name), isMut, isRef};
    auto it = selfs.find(key);
    if (it != selfs.end())
    {
        return it->second;
    }

    auto selfType = std::make_shared<SelfType>(key.name, isMut, isRef);
    selfs[key] = selfType;
    return selfType;
}

std::optional<std::shared_ptr<SelfType>> TypeContext::getSelf(const std::string &name, bool isMut, bool isRef)
{
    SelfKey key{name, isMut, isRef};
    auto it = selfs.find(key);
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
    auto key = std::make_tuple(
        std::vector<std::shared_ptr<Type>>(genericParams.begin(), genericParams.end()),
        params,
        returnType);
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
                      << " | Param Count: " << std::get<1>(key).size()
                      << " | Return Type: " << std::get<2>(key)->toString() << std::endl;
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

std::shared_ptr<Type> TypeContext::substitute(
    std::shared_ptr<Type> ty,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &subst,
    bool strict)
{
    if (!ty) return ty;
    switch (ty->getKind())
    {
    case Type::Kind::GenericParam:
    {
        auto gp = std::static_pointer_cast<GenericParamType>(ty);
        auto it = subst.find(gp->getParamName());
        if (it == subst.end())
        {
            if (strict)
                throw std::runtime_error("Generic parameter '" + gp->getParamName() + "' not found in substitution map");
            return ty;
        }
        return it->second;
    }
    case Type::Kind::Reference:
    {
        auto r = std::static_pointer_cast<ReferenceType>(ty);
        return getReference(substitute(r->getBaseType(), subst, strict), r->isMutableRef());
    }
    case Type::Kind::Array:
    {
        auto a = std::static_pointer_cast<ArrayType>(ty);
        return getArray(substitute(a->getElementType(), subst, strict), a->getSize());
    }
    case Type::Kind::Custom:
    {
        auto c = std::static_pointer_cast<CustomType>(ty);
        if (!c->getGenericArgs().empty())
        {
            // Already-instantiated type (e.g. Foo$i32): substitute nested args.
            std::vector<std::shared_ptr<Type>> newArgs;
            for (auto &a : c->getGenericArgs())
                newArgs.push_back(substitute(a, subst, strict));
            auto origin = c->genericOrigin ? c->genericOrigin : c;
            return instantiateCustom(origin, std::move(newArgs));
        }
        if (!c->getGenericParams().empty())
        {
            // Generic DEFINITION (e.g. Foo<T>: params, no args) — appears as a
            // field of a generic struct or `self: Foo<T>` in generic-impl method
            // bodies. Instantiate from the substitution map.
            std::vector<std::shared_ptr<Type>> newArgs;
            bool anySubstituted = false;
            for (auto &gp : c->getGenericParams())
            {
                auto gpTy = std::static_pointer_cast<GenericParamType>(gp);
                auto it = subst.find(gpTy->getParamName());
                if (it != subst.end())
                {
                    newArgs.push_back(it->second);
                    anySubstituted = true;
                }
                else
                {
                    newArgs.push_back(gp); // keep the param if not in map
                }
            }
            if (anySubstituted)
                return instantiateCustom(c, std::move(newArgs));
            return c;
        }
        return ty; // fully concrete
    }
    case Type::Kind::Function:
    {
        auto ft = std::static_pointer_cast<FunctionType>(ty);
        std::vector<std::shared_ptr<Type>> newParams;
        for (const auto &p : ft->getParams())
            newParams.push_back(substitute(p, subst, strict));
        auto newRet = substitute(ft->getReturnType(), subst, strict);
        return getFunction(std::move(newParams), newRet);
    }
    default:
        // Primitive / Trait / Self — pass through unchanged.
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
        case Type::Kind::Array:
            return hasGenericParam(std::static_pointer_cast<ArrayType>(t)->getElementType());
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

    // Build the substitution map BEFORE the cache check so a cache hit can
    // re-derive conformance (the origin's implTrait may have grown since the
    // instance was first created — see reSyncImplTrait).
    std::unordered_map<std::string, std::shared_ptr<Type>> subst;
    for (size_t i = 0; i < args.size(); ++i)
    {
        auto gp = std::static_pointer_cast<GenericParamType>(generic->getGenericParams()[i]);
        subst[gp->getParamName()] = args[i];
    }

    if (concrete)
    {
        auto it = instantiatedCustoms.find(mangled);
        if (it != instantiatedCustoms.end())
        {
            reSyncImplTrait(it->second.get(), generic, subst);
            // A same-file forward reference may have instantiated this from an
            // empty pass-1b shell — re-derive the fields from the now-filled
            // origin so the cached instance has the real layout.
            reSyncFields(it->second.get(), generic, subst);
            return it->second;
        }
    }

    std::vector<CustomType::Field> newFields;
    for (auto &f : generic->getFields())
        newFields.push_back(CustomType::Field{f.name, substitute(f.type, subst)});

    // Substitute enum variant payload types too (empty for plain structs).
    std::vector<CustomType::EnumVariantInfo> newVariants;
    for (auto &v : generic->getVariants())
    {
        CustomType::EnumVariantInfo nv;
        nv.name = v.name;
        for (auto &pt : v.payloadTypes)
            nv.payloadTypes.push_back(substitute(pt, subst));
        newVariants.push_back(std::move(nv));
    }

    auto inst = std::make_shared<CustomType>(mangled,
        std::vector<std::shared_ptr<Type>>{},
        std::move(newFields));

    inst->setGenericArgs(std::move(args));
    inst->genericOrigin = generic;
    inst->setVariants(std::move(newVariants));
    reSyncImplTrait(inst.get(), generic, subst);

    if (concrete)
        instantiatedCustoms[mangled] = inst;

    customs[mangled] = inst;

    return inst;
}

void TypeContext::reSyncFields(CustomType *inst, const std::shared_ptr<CustomType> &generic, const std::unordered_map<std::string, std::shared_ptr<Type>> &subst)
{
    if (generic->getFields().empty())
        return; // origin still an empty shell — nothing to sync yet
    std::vector<CustomType::Field> newFields;
    for (auto &f : generic->getFields())
        newFields.push_back(CustomType::Field{f.name, substitute(f.type, subst)});
    inst->setFields(std::move(newFields));

    // Re-sync enum variant payload types too (a cached instance created while
    // the origin was an empty shell needs its variants re-derived).
    std::vector<CustomType::EnumVariantInfo> newVariants;
    for (auto &v : generic->getVariants())
    {
        CustomType::EnumVariantInfo nv;
        nv.name = v.name;
        for (auto &pt : v.payloadTypes)
            nv.payloadTypes.push_back(substitute(pt, subst));
        newVariants.push_back(std::move(nv));
    }
    inst->setVariants(std::move(newVariants));
}

void TypeContext::reSyncImplTrait(CustomType *inst, const std::shared_ptr<CustomType> &generic, const std::unordered_map<std::string, std::shared_ptr<Type>> &subst)
{
    // Propagate trait conformance, substituting the struct's generic args into
    // the trait's use-site args — `impl<T> Iterator<T> for Range<T>` must yield
    // Iterator<i32> (not Iterator<T>) on Range$i32 for bound checks to pass.
    inst->implTrait.clear();
    for (auto &tr : generic->implTrait)
    {
        auto traitTy = std::static_pointer_cast<TraitType>(tr);
        std::vector<std::shared_ptr<Type>> newArgs;
        for (auto &a : traitTy->getGenericArgs())
            newArgs.push_back(substitute(a, subst));
        inst->implTrait.push_back(instantiateTrait(traitTy, std::move(newArgs)));
    }
}