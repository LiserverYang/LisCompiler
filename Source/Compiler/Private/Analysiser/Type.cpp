/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Analysiser/Type.hpp"
#include "Core/ModuleUtils.hpp"

#include <cassert>
#include <unordered_map>

namespace detail
{
template <typename T, typename... Args>
bool is_one_of(const T &value, const Args &...args)
{
    return ((value == args) || ...);
}
} // namespace detail

// ========================= Type =========================
Type::Type(Kind kind) : kind(kind) {}

bool Type::implementsTrait(const std::string &name) const
{
    // Compare BARE trait names: implTrait entries carry the trait's module
    // prefix (e.g. "math$Numeric"), while callers query by the language-level
    // name ("Numeric"). displayName strips the module prefix; a generic-trait
    // instantiation ("Iterator$i32") would strip wrongly, but those never
    // collide with the fixed names queried here (Drop/Numeric/operator traits).
    for (const auto &t : implTrait)
        if (displayName(t->getName()) == name) return true;
    return false;
}

Type::Kind Type::getKind() const
{
    return kind;
}

// ========================= PrimitiveType =========================
PrimitiveType::PrimitiveType(PrimKind pk) : Type(Kind::Primitive), primKind(pk) {}

PrimitiveType::PrimKind PrimitiveType::getPrimKind() const
{
    return primKind;
}

bool PrimitiveType::equals(const std::shared_ptr<Type> &other) const
{
    if (other->getKind() != Kind::Primitive) return false;
    auto pt = std::static_pointer_cast<PrimitiveType>(other);
    return pt->primKind == primKind;
}

bool PrimitiveType::isFloat() const
{
    return detail::is_one_of(primKind, PrimKind::F32, PrimKind::F64);
}

bool PrimitiveType::isInteger() const
{
    return detail::is_one_of(primKind, PrimKind::I8, PrimKind::I16, PrimKind::I32, PrimKind::I64);
}

int PrimitiveType::integerBitWidth() const
{
    switch (primKind)
    {
    case PrimKind::I8: return 8;
    case PrimKind::I16: return 16;
    case PrimKind::I32: return 32;
    case PrimKind::I64: return 64;
    default: return 0; // not an integer kind
    }
}

std::string PrimitiveType::toString() const
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

PrimitiveType::PrimKind PrimitiveType::getKind(std::string str)
{
    if (str == "i8") return PrimKind::I8;
    if (str == "i16") return PrimKind::I16;
    if (str == "i32") return PrimKind::I32;
    if (str == "i64") return PrimKind::I64;
    if (str == "f32") return PrimKind::F32;
    if (str == "f64") return PrimKind::F64;
    if (str == "bool") return PrimKind::BOOL;
    if (str == "char") return PrimKind::CHAR;
    return PrimKind::VOID;
}

// ========================= ReferenceType =========================
ReferenceType::ReferenceType(std::shared_ptr<Type> base, bool isMutable)
    : Type(Kind::Reference), baseType(std::move(base)), isMutable(isMutable) {}

std::shared_ptr<Type> ReferenceType::getBaseType() const
{
    return baseType;
}

bool ReferenceType::isMutableRef() const
{
    return isMutable;
}

bool ReferenceType::equals(const std::shared_ptr<Type> &other) const
{
    if (other->getKind() != Kind::Reference) return false;
    auto rt = std::static_pointer_cast<ReferenceType>(other);
    return rt->isMutable == isMutable && rt->baseType->equals(baseType);
}

std::string ReferenceType::toString() const
{
    return (isMutable ? "&mut " : "&") + baseType->toString();
}

// ========================= ArrayType =========================
ArrayType::ArrayType(std::shared_ptr<Type> elementType, size_t size)
    : Type(Kind::Array), elementType(std::move(elementType)), size(size) {}

const std::shared_ptr<Type> &ArrayType::getElementType() const
{
    return elementType;
}

size_t ArrayType::getSize() const
{
    return size;
}

bool ArrayType::equals(const std::shared_ptr<Type> &other) const
{
    if (other->getKind() != Kind::Array) return false;
    auto at = std::static_pointer_cast<ArrayType>(other);
    return at->size == size && at->elementType->equals(elementType);
}

std::string ArrayType::toString() const
{
    return "[" + elementType->toString() + "; " + std::to_string(size) + "]";
}

// ========================= CustomType =========================
CustomType::CustomType(std::string name, std::vector<Field> fields)
    : Type(Kind::Custom), name(std::move(name)), fields(std::move(fields)) {}

CustomType::CustomType(std::string name,
    std::vector<std::shared_ptr<Type>> genericParams,
    std::vector<Field> fields)
    : Type(Kind::Custom),
      name(std::move(name)),
      fields(std::move(fields)),
      genericParams(std::move(genericParams)) {}

const std::string &CustomType::getName() const
{
    return name;
}

const std::string &CustomType::getOriginName() const
{
    return genericOrigin ? genericOrigin->getName() : name;
}
const std::vector<CustomType::Field> &CustomType::getFields() const
{
    return fields;
}

const std::vector<CustomType::Method> &CustomType::getMethods() const
{
    // Instantiations share the origin's method list.
    if (!genericArgs.empty() && genericOrigin)
        return genericOrigin->getMethods();
    return methods;
}

void CustomType::addMethods(std::vector<Method> methods)
{
    // Methods always go on the origin.
    if (!genericArgs.empty() && genericOrigin)
    {
        genericOrigin->addMethods(std::move(methods));
        return;
    }
    for (auto &m : methods)
        this->methods.push_back(std::move(m));
}

bool CustomType::equals(const std::shared_ptr<Type> &other) const
{
    if (other->getKind() != Kind::Custom) return false;
    auto ct = std::static_pointer_cast<CustomType>(other);
    if (name != ct->name) return false;
    if (genericArgs.size() != ct->genericArgs.size()) return false;
    for (size_t i = 0; i < genericArgs.size(); ++i)
        if (!genericArgs[i]->equals(ct->genericArgs[i])) return false;
    // For non-instantiated forms, name equality is sufficient (nominal).
    return true;
}

std::string CustomType::toString() const
{
    // For instantiations, `name` is already mangled (e.g. "Box$i32").
    // For generic definitions, show Name<T, U>.
    if (genericArgs.empty() && !genericParams.empty())
    {
        std::string s = name + "<";
        for (size_t i = 0; i < genericParams.size(); ++i)
        {
            s += genericParams[i]->toString();
            if (i + 1 != genericParams.size()) s += ", ";
        }
        return s + ">";
    }
    return name;
}

bool CustomType::Field::operator==(const std::string &other) const
{
    return name == other;
}

bool CustomType::Field::operator==(const CustomType::Field &other) const
{
    return name == other.name;
}

// ========================= FunctionType =========================
FunctionType::FunctionType(std::vector<std::shared_ptr<Type>> params,
    std::shared_ptr<Type> returnType)
    : FunctionType({}, std::move(params), std::move(returnType)) {}

FunctionType::FunctionType(std::vector<std::shared_ptr<Type>> genericParams,
    std::vector<std::shared_ptr<Type>> params,
    std::shared_ptr<Type> returnType)
    : Type(Kind::Function),
      genericParams(std::move(genericParams)),
      params(std::move(params)),
      returnType(std::move(returnType)) {}

bool FunctionType::isGeneric() const
{
    return !genericParams.empty();
}

const std::vector<std::shared_ptr<Type>> &FunctionType::getParams() const
{
    return params;
}

const std::vector<std::shared_ptr<Type>> &FunctionType::getGenericParams() const
{
    return genericParams;
}

const std::shared_ptr<Type> &FunctionType::getReturnType() const
{
    return returnType;
}

bool FunctionType::equals(const std::shared_ptr<Type> &other) const
{
    if (this == other.get()) return true;
    if (other->getKind() != Kind::Function) return false;
    const auto &ft = static_cast<const FunctionType *>(other.get());
    return genericParams == ft->genericParams
           && params == ft->params
           && returnType->equals(ft->returnType);
}

std::string FunctionType::toString() const
{
    std::string str = "fn";
    if (isGeneric())
    {
        str += "<";
        for (size_t i = 0; i < genericParams.size(); ++i)
        {
            str += genericParams[i]->toString();
            if (i != genericParams.size() - 1) str += ", ";
        }
        str += ">";
    }
    str += "(";
    for (size_t i = 0; i < params.size(); ++i)
    {
        str += params[i]->toString();
        if (i != params.size() - 1) str += ", ";
    }
    str += ") -> " + returnType->toString();
    return str;
}

// ========================= TraitType =========================
TraitType::TraitType(std::string name, std::vector<Method> methods)
    : Type(Kind::Trait), name(std::move(name)), methods(std::move(methods)) {}

TraitType::TraitType(std::string name, std::vector<std::shared_ptr<Type>> genericParams, std::vector<Method> methods)
    : Type(Kind::Trait), name(std::move(name)), genericParams(std::move(genericParams)), methods(std::move(methods)) {}

const std::string &TraitType::getName() const
{
    return name;
}

const std::vector<std::shared_ptr<Type>> &TraitType::getGenericParams() const
{
    return genericParams;
}

void TraitType::setGenericArgs(std::vector<std::shared_ptr<Type>> args)
{
    genericArgs = std::move(args);
}

const std::vector<std::shared_ptr<Type>> &TraitType::getGenericArgs() const
{
    return genericArgs;
}

const std::vector<TraitType::Method> &TraitType::getMethods() const
{
    return methods;
}

std::optional<TraitType::Method> TraitType::findMethod(const std::string &methodName) const
{
    for (const auto &method : methods)
        if (method.name == methodName)
            return method;
    return std::nullopt;
}

bool TraitType::equals(const std::shared_ptr<Type> &other) const
{
    if (other->getKind() != Kind::Trait) return false;
    auto tt = std::static_pointer_cast<TraitType>(other);
    if (tt->name != name) return false;
    if (tt->genericArgs.size() != genericArgs.size()) return false;
    for (size_t i = 0; i < genericArgs.size(); ++i)
        if (!genericArgs[i]->equals(tt->genericArgs[i]))
            return false;
    return true;
}

std::string TraitType::toString() const
{
    std::string str = "trait " + name + " { ";
    for (size_t i = 0; i < methods.size(); ++i)
    {
        const auto &method = methods[i];
        str += method.name + "(";
        for (size_t j = 0; j < method.params.size(); ++j)
        {
            str += method.params[j].type->toString();
            if (j != method.params.size() - 1) str += ", ";
        }
        str += ") -> " + method.returnType->toString();
        if (i != methods.size() - 1) str += ", ";
    }
    str += " }";
    return str;
}

// ========================= SelfType =========================
SelfType::SelfType(const std::string &trait_name, bool isMut, bool isRef)
    : Type(Kind::Self), trait_name(trait_name), isMut(isMut), isRef(isRef) {}

const std::string &SelfType::getTraitName() const
{
    return trait_name;
}

bool SelfType::isMutable() const
{
    return isMut;
}

bool SelfType::isReference()
{
    return isRef;
}

bool SelfType::equals(const std::shared_ptr<Type> &other) const
{
    if (!other) return false;
    auto other_self = std::dynamic_pointer_cast<SelfType>(other);
    if (!other_self) return false;
    return trait_name == other_self->trait_name
           && isMut == other_self->isMut
           && isRef == other_self->isRef;
}

std::string SelfType::toString() const
{
    return "self<" + trait_name + ">";
}

// ========================= GenericParamType =========================
GenericParamType::GenericParamType(std::string paramName)
    : Type(Kind::GenericParam), paramName(std::move(paramName)) {}

const std::string &GenericParamType::getParamName() const
{
    return paramName;
}

const std::vector<std::shared_ptr<TraitType>> &GenericParamType::getConstraints() const
{
    return constraints;
}

void GenericParamType::updateContraints(std::vector<std::shared_ptr<TraitType>> constraints)
{
    this->constraints = std::move(constraints);

    // P7: implTrait is the trait bounds this generic param carries (inherited
    // from Type). buildStructType/buildEnumType call this on the SAME generic
    // param twice (pass-1b pre-registration + pass-2 full analysis), so without
    // a clear the bounds were appended again → duplicate entries (non-idempotent
    // state update). `constraints` above is a plain overwrite (no duplicate).
    this->implTrait.clear();
    for (auto &it : this->constraints)
    {
        this->implTrait.push_back(it);
    }
}

bool GenericParamType::equals(const std::shared_ptr<Type> &other) const
{
    if (other->getKind() != Kind::GenericParam) return false;
    auto gp = std::static_pointer_cast<GenericParamType>(other);
    return gp->paramName == paramName;
}

std::string GenericParamType::toString() const
{
    return paramName;
}