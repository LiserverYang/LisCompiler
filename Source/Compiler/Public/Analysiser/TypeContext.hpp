/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Analysiser/Type.hpp"

template <typename T>
void hash_combine(size_t &seed, T const &v);

namespace std
{
template <>
struct hash<std::vector<std::shared_ptr<Type>>>
{
    size_t operator()(const std::vector<std::shared_ptr<Type>> &vec) const;
};
} // namespace std

struct RefHash
{
    std::size_t operator()(const std::pair<void *, bool> &p) const;
};

struct FuncHash
{
    std::size_t operator()(const std::tuple<std::vector<std::shared_ptr<Type>>, std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>> &p) const;
};

class TypeContext
{
public:
    TypeContext();

    std::shared_ptr<PrimitiveType> getPrimitive(PrimitiveType::PrimKind kind);

    std::shared_ptr<ReferenceType> getReference(std::shared_ptr<Type> base, bool isMutable);

    std::shared_ptr<CustomType> createCustom(std::string name, std::vector<CustomType::Field> fields);

    /** Create (or return the cached) CustomType with empty fields — a shell
     *  registered so forward/recursive references resolve before fields are set. */
    std::shared_ptr<CustomType> createCustomShell(std::string name);
    std::shared_ptr<CustomType> createGenericCustomShell(std::string name, std::vector<std::shared_ptr<Type>> genericParams);

    std::optional<std::shared_ptr<CustomType>> getCustom(std::string name);

    /**
     * Substitute generic params in `ty` via `subst` (param name → concrete type).
     * Single implementation shared by the semantic analyzer, monomorphization and
     * struct-field instantiation.
     *
     * @param strict  when true, a GenericParam missing from `subst` throws (mono's
     *                fail-fast invariant); when false it passes through unchanged
     *                (sema/type-context callers substitute partial maps).
     */
    std::shared_ptr<Type> substitute(std::shared_ptr<Type> ty,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &subst,
        bool strict = false);

    std::shared_ptr<FunctionType> getFunction(std::vector<std::shared_ptr<Type>> params, std::shared_ptr<Type> returnType);

    std::shared_ptr<TraitType> createTrait(std::string name, std::vector<TraitType::Method> methods);

    std::optional<std::shared_ptr<TraitType>> getTrait(std::string name);

    std::shared_ptr<SelfType> createSelf(std::string name, bool isMut, bool isRef);

    std::optional<std::shared_ptr<SelfType>> getSelf(std::string name);

    std::shared_ptr<FunctionType> getGenericFunction(
        std::vector<std::shared_ptr<GenericParamType>> genericParams,
        std::vector<std::shared_ptr<Type>> params,
        std::shared_ptr<Type> returnType);

    std::shared_ptr<CustomType> createGenericCustom(
        std::string name,
        std::vector<std::shared_ptr<Type>> genericParams,
        std::vector<CustomType::Field> fields);

    std::shared_ptr<TraitType> createGenericTrait(
        std::string name,
        std::vector<std::shared_ptr<Type>> genericParams,
        std::vector<TraitType::Method> methods);

    /** Create an instantiated TraitType (e.g. Iterator<i32>) with use-site args. */
    std::shared_ptr<TraitType> instantiateTrait(
        std::shared_ptr<TraitType> generic,
        std::vector<std::shared_ptr<Type>> args);

    std::shared_ptr<CustomType> instantiateCustom(
        std::shared_ptr<CustomType> generic,
        std::vector<std::shared_ptr<Type>> args);

    /** Re-derive an instantiation's implTrait from its origin (order-independent
     *  conformance; a cache hit may be older than the origin's implTrait). */
    void reSyncImplTrait(CustomType *inst, const std::shared_ptr<CustomType> &generic,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &subst);

    /** Re-derive an instantiation's FIELDS from its origin (order-independent
     *  layout; an instance created while its origin was still an empty pass-1b
     *  shell keeps empty fields unless re-synced on a cache hit). */
    void reSyncFields(CustomType *inst, const std::shared_ptr<CustomType> &generic,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &subst);

    const std::unordered_map<std::string, std::shared_ptr<CustomType>> &
    getInstantiatedCustoms() const
    {
        return instantiatedCustoms;
    }

    void printTypeTable() const;

private:
    std::unordered_map<PrimitiveType::PrimKind, std::shared_ptr<PrimitiveType>> primitives;
    std::unordered_map<std::string, std::shared_ptr<CustomType>> customs;
    // Key = (genericParams, params, returnType). getFunction uses an empty
    // genericParams vector, so a generic and a non-generic function with the
    // same signature never collide (and two generic functions with different
    // generic-param lists don't alias either).
    std::unordered_map<std::tuple<std::vector<std::shared_ptr<Type>>, std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>>, std::shared_ptr<FunctionType>, FuncHash> functions;
    std::unordered_map<std::pair<void *, bool>, std::shared_ptr<ReferenceType>, RefHash> refCache;
    std::unordered_map<std::string, std::shared_ptr<TraitType>> traits;
    std::unordered_map<std::string, std::shared_ptr<SelfType>> selfs;
    std::unordered_map<std::string, std::shared_ptr<GenericParamType>> gParams;
    std::unordered_map<std::string, std::shared_ptr<CustomType>> instantiatedCustoms;
};