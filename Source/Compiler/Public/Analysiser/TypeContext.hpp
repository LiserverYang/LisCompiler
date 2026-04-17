/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
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
    std::size_t operator()(const std::pair<std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>> &p) const;
};

class TypeContext
{
public:
    TypeContext();

    std::shared_ptr<PrimitiveType> getPrimitive(PrimitiveType::PrimKind kind);

    std::shared_ptr<ReferenceType> getReference(std::shared_ptr<Type> base, bool isMutable);

    std::shared_ptr<CustomType> createCustom(std::string name, std::vector<CustomType::Field> fields);

    std::optional<std::shared_ptr<CustomType>> getCustom(std::string name);

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

    std::shared_ptr<CustomType> instantiateCustom(
        std::shared_ptr<CustomType> generic,
        std::vector<std::shared_ptr<Type>> args);

    const std::unordered_map<std::string, std::shared_ptr<CustomType>> &
    getInstantiatedCustoms() const
    {
        return instantiatedCustoms;
    }

    void printTypeTable() const;

private:
    std::unordered_map<PrimitiveType::PrimKind, std::shared_ptr<PrimitiveType>> primitives;
    std::unordered_map<std::string, std::shared_ptr<CustomType>> customs;
    std::unordered_map<std::pair<std::vector<std::shared_ptr<Type>>, std::shared_ptr<Type>>, std::shared_ptr<FunctionType>, FuncHash> functions;
    std::unordered_map<std::pair<void *, bool>, std::shared_ptr<ReferenceType>, RefHash> refCache;
    std::unordered_map<std::string, std::shared_ptr<TraitType>> traits;
    std::unordered_map<std::string, std::shared_ptr<SelfType>> selfs;
    std::unordered_map<std::string, std::shared_ptr<GenericParamType>> gParams;
    std::unordered_map<std::string, std::shared_ptr<CustomType>> instantiatedCustoms;
};