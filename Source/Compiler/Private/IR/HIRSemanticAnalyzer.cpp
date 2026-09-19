/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/HIRSemanticAnalyzer.hpp"
#include "IR/BuiltinNames.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <unordered_set>

// ============================================================
//  Helpers
// ============================================================

namespace
{
/// True if two types are compatible as a by-value self receiver: same origin
/// struct (tolerating a generic-definition `Foo<T>` vs its instantiation
/// `Foo$i32` mismatch), structurally equal, or a `&mut T` → `&T` mutability
/// coercion (a mutable reference may be READ as a shared one — both are `ptr`).
/// Directional: `expected` is the declared param/return type, `actual` is the
/// argument/expression, so a shared `&T` never sneaks in where `&mut T` is
/// expected.
bool typesCompatible(const std::shared_ptr<Type> &expected, const std::shared_ptr<Type> &actual)
{
    if (!expected || !actual) return true;
    // `never` (the type of a diverging `panic(...)` call) has no values, so an
    // expression of this type never actually produces a mismatching value —
    // it coerces to whatever the context expects. This is what lets
    // `ret panic("x");` satisfy any declared return type, and a diverging
    // match arm / call argument slot in next to a normally-typed one.
    // One-directional (actual → expected only): panic() can stand in for i32,
    // but an i32 obviously cannot stand in for a diverging call.
    if (actual->getKind() == Type::Kind::Primitive
        && std::static_pointer_cast<PrimitiveType>(actual)->getPrimKind() == PrimitiveType::PrimKind::NEVER)
        return true;
    auto ce = std::dynamic_pointer_cast<CustomType>(expected);
    auto ca = std::dynamic_pointer_cast<CustomType>(actual);
    if (ce && ca && ce->getOriginName() == ca->getOriginName())
        return true;
    auto re = std::dynamic_pointer_cast<ReferenceType>(expected);
    auto ra = std::dynamic_pointer_cast<ReferenceType>(actual);
    if (re && ra && !re->isMutableRef() && ra->isMutableRef()
        && re->getBaseType()->equals(ra->getBaseType()))
        return true;

    // ── implicit indirection coercions (all in the SOUND direction) ──────────
    //   &T     → *T        a shared borrow decays to a read-only raw pointer
    //   &mut T → *mut T    a mutable borrow decays to a writable raw pointer
    //   &mut T → *T        (mutability simply dropped)
    //   *mut T → *T        (mutability simply dropped)
    // The REVERSE (*T → &T) is deliberately NOT a coercion: turning an
    // unverified address into a borrow would let a raw pointer bypass the borrow
    // checker entirely. The stdlib does that explicitly with `__deref`.
    auto pe = std::dynamic_pointer_cast<PointerType>(expected);
    auto pa = std::dynamic_pointer_cast<PointerType>(actual);
    if (pe)
    {
        if (pa && !pe->isMutablePtr() && pa->isMutablePtr()
            && pe->getBaseType()->equals(pa->getBaseType()))
            return true; // *mut T → *T
        if (ra && pe->getBaseType()->equals(ra->getBaseType())
            && (ra->isMutableRef() || !pe->isMutablePtr()))
            return true; // &mut T → *mut T / *T, and &T → *T
    }

    return expected->equals(actual);
}

/// True when a RAW field type mentions `name` BY VALUE — i.e. somewhere that is
/// not behind a reference or a raw pointer (those hold an address, so they stop
/// the expansion). `struct S<T> { pub v: S<S<T>> }` mentions S inside its own
/// generic argument and therefore expands forever: resolving the field would
/// instantiate S with S as its own argument, recursively, until the stack died
/// (measured 0xC0000005 inside instantiateCustom <-> substitute). Checking the
/// SOURCE spelling catches it before any instantiation happens; Rust rejects the
/// same definition with E0391. Mutual cycles that need no generic argument
/// (`struct A { pub b: B } struct B { pub a: A }`) are caught afterwards by
/// checkForRecursiveTypes, which walks the resolved types.
bool RawTypeMentionsSelf(const HIRRawType &type, const std::string &name)
{
    // A reference or a raw pointer holds an ADDRESS: the type stays finite even
    // when it points at itself, so the walk stops there.
    if (type.isRef || type.isPtr) return false;
    if (!name.empty() && type.name == name) return true;
    for (const auto &arg : type.genericArgs)
        if (RawTypeMentionsSelf(arg, name)) return true;
    if (type.element && RawTypeMentionsSelf(*type.element, name)) return true;
    return false;
}
/// True if ty is the uninhabited (never) type.
///
/// never is the language bottom type: an expression of it never produces a
/// value, so it is compatible with ANY expected type (see typesCompatible).
/// This is the narrow form for the sites that otherwise compare with
/// Type::equals — initialisers, assignments and array-literal elements — so that
/// a never-valued initialiser/assignment/element behaves like the argument,
/// return and match-arm positions. It does NOT relax anything else (a &mut T is
/// still not accepted where &T is declared).
bool isNever(const std::shared_ptr<Type> &ty)
{
    return ty && ty->getKind() == Type::Kind::Primitive
           && std::static_pointer_cast<PrimitiveType>(ty)->getPrimKind() == PrimitiveType::PrimKind::NEVER;
}

/// True when actual is the UN-INSTANTIATED definition of a generic type whose
/// origin matches expected — i.e. a value whose generic arguments could not be
/// inferred (a context-free Option::None where Option<i32> is required). The
/// definition FIELDS still contain the bare T, so lowering it builds an LLVM
/// struct with a generic-param field and crashes codegen; it must be rejected
/// wherever a concrete type is expected.
bool isUninferredGenericDefinition(const std::shared_ptr<Type> &expected, const std::shared_ptr<Type> &actual)
{
    auto ce = std::dynamic_pointer_cast<CustomType>(expected);
    auto ca = std::dynamic_pointer_cast<CustomType>(actual);
    if (!ce || !ca) return false;
    if (ce->getName() != ca->getName() && ce->getOriginName() != ca->getOriginName()) return false;
    return ce->getGenericArgs().size() != ca->getGenericArgs().size();
}

/// Path segment for an index that is NOT a compile-time constant. It denotes
/// "some element", so it overlaps every other index segment (conservative, and
/// the only sound answer without value tracking).
constexpr const char *kIndexWildcard = "[*]";

/// Path segment marking a DEREF of the place built so far (`*p`, `(*p).a`).
/// `*p` for a reference whose referent is unknown (a parameter, or any binding
/// without an alias entry) stays keyed by this segment: all `*p` of the same
/// expression shape denote the same memory, which is enough to catch two
/// borrows derived from the same reference.
constexpr const char *kDeref = "*";

/// `[7]` for a constant index, `[*]` for anything else.
std::string indexSegment(HIRExpr *index)
{
    if (auto *lit = dynamic_cast<HIRLiteral *>(index))
        if (lit->kind == HIRLiteral::Kind::Int && !lit->overflowed)
            return "[" + std::to_string(std::get<int64_t>(lit->value)) + "]";
    return kIndexWildcard;
}

/// True when two path SEGMENTS may denote the same memory.
///  - two field names overlap only when equal;
///  - two index segments overlap when equal OR when either is the wildcard
///    (`[*]` means "an element we cannot pin down");
///  - a field never overlaps an index (`a.f` vs `a[0]`);
///  - the deref marker of an unresolved reference overlaps only itself.
bool pathSegmentOverlaps(const std::string &a, const std::string &b)
{
    if (a == b) return true;
    if (a == kDeref || b == kDeref) return false;
    bool aIndex = a.size() >= 2 && a.front() == '[';
    bool bIndex = b.size() >= 2 && b.front() == '[';
    if (aIndex != bIndex) return false;
    if (!aIndex) return false; // two different field names
    return a == kIndexWildcard || b == kIndexWildcard;
}

/// Decompose a member-access / index / deref chain into the root variable name
/// and its place path. The path segments are: a field name ("a"), a constant
/// index ("[0]"), the unknown-index wildcard ("[*]") or the deref marker ("*").
///
/// SYNTAX ONLY — `*r` comes back as (root "r", ["*"]). Conflict detection uses
/// resolvePlace(), which rewrites that through the alias table so `*r` denotes
/// the place `r` was borrowed from (this is what makes `&mut *r` and `&mut x`
/// conflict). Move/ownership bookkeeping deliberately keeps the syntactic form:
/// `movedFields` is keyed by the BINDING, not by what a reference points at.
bool extractRootAndPath(HIRExpr *expr, std::string &root, std::vector<std::string> &path)
{
    if (auto *ma = dynamic_cast<HIRMemberAccess *>(expr))
    {
        if (!extractRootAndPath(ma->object.get(), root, path)) return false;
        path.push_back(ma->memberName);
        return true;
    }
    if (auto *ia = dynamic_cast<HIRIndexAccess *>(expr))
    {
        if (!extractRootAndPath(ia->object.get(), root, path)) return false;
        path.push_back(indexSegment(ia->index.get()));
        return true;
    }
    if (auto *d = dynamic_cast<HIRDeref *>(expr))
    {
        if (!extractRootAndPath(d->operand.get(), root, path)) return false;
        path.push_back(kDeref);
        return true;
    }
    if (auto *nr = dynamic_cast<HIRNameRef *>(expr))
    {
        root = nr->name;
        path.clear();
        return true;
    }
    return false;
}

std::string joinPath(const std::vector<std::string> &path)
{
    // A field is dot-separated (`p.a`), an index segment is appended (`a[0]`),
    // so diagnostics read like the source.
    std::string s;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (i && path[i].front() != '[') s += ".";
        s += path[i];
    }
    return s;
}
/// Render a place for a diagnostic: `a[0].f`, `*r`, `*r.a`, `a[*]`.
std::string placeName(const std::string &root, const std::vector<std::string> &path)
{
    std::string s = root;
    for (const auto &seg : path)
    {
        if (seg == kDeref)
            s = "*" + s;
        else if (!seg.empty() && seg.front() == '[')
            s += seg;
        else
            s += "." + seg;
    }
    return s;
}
} // namespace

std::shared_ptr<Type>
HIRSemanticAnalyzer::resolveType(const HIRRawType &raw, HIRNode &errorNode)
{
    if (!raw.isPresent)
        return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    std::shared_ptr<Type> base;

    // Pointer type `*T` / `*mut T`. Handled before everything else: the base is
    // the pointee, and the generic-instantiation path below (which is about
    // `Foo<T>` naming a CustomType) must never see a pointer. Returning early is
    // safe because a pointer never carries genericArgs of its own.
    if (raw.isPtr)
    {
        auto pointeeTy = raw.element ? resolveType(*raw.element, errorNode)
                                     : context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        std::shared_ptr<Type> ptrTy = context->typeContext->getPointer(pointeeTy, raw.isMutPtr);
        // `&*T` (a reference to a raw pointer) still wraps.
        if (raw.isRef)
            return context->typeContext->getReference(ptrTy, raw.isMutRef);
        return ptrTy;
    }

    // Array type `[T; N]`. Elements must be Copy and must NOT be references
    // (v1 — no element drop glue / origin tracking, so a reference element
    // could escape its owner without being caught). The array itself is always
    // Move regardless of its element.
    if (raw.isArray)
    {
        if (raw.arraySize <= 0)
        {
            if (!suppressTypeErrors_)
                log(errorNode, "array size must be a positive integer (got " + std::to_string(raw.arraySize) + ").");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        if ((size_t)raw.arraySize > MAX_ARRAY_ELEMENTS)
        {
            if (!suppressTypeErrors_)
                log(errorNode, "array size " + std::to_string(raw.arraySize) + " exceeds the limit of " + std::to_string(MAX_ARRAY_ELEMENTS) + " elements.");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        auto elemTy = raw.element ? resolveType(*raw.element, errorNode)
                                  : context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        if (elemTy && (isReferenceType(elemTy) || elemTy->getKind() == Type::Kind::Pointer))
        {
            if (!suppressTypeErrors_)
                log(errorNode, "array element type '" + elemTy->toString() + "' cannot be a reference or a raw pointer (indirection elements are not supported yet).");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        if (elemTy && !elemTy->isCopyable())
        {
            if (!suppressTypeErrors_)
                log(errorNode, "array element type '" + elemTy->toString() + "' must be Copy (arrays of non-Copy types are not supported yet).");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        base = context->typeContext->getArray(elemTy, (size_t)raw.arraySize);
        // Fall through to reference wrapping below (for `&[T; N]` etc.)
    }
    else if (raw.isPrimitive)
    {
        base = context->typeContext->getPrimitive(PrimitiveType::getKind(raw.name));
    }
    else
    {
        // `Self` in a trait method signature resolves to the trait's SelfType
        // (substituted by conformance checks); inside a struct/impl body it
        // resolves to the struct being implemented (by-value). This lets
        // operator traits be written `fn add(self, other: Self) -> Self`.
        if (raw.name == "Self")
        {
            if (isInTraitMethod)
                base = context->typeContext->createSelf(traitName, raw.isMutRef, raw.isRef);
            else if (currentStructType)
                base = currentStructType;
            else
            {
                if (!suppressTypeErrors_)
                    log(errorNode, "the type 'Self' can only be used inside a trait or impl.");
                return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            }
            // The REFERENCE form is applied exactly once. `createSelf` already
            // encodes it for a trait method (it is passed isRef/isMut), so that
            // branch returns the SelfType as-is; an impl resolves Self to the
            // concrete struct and needs the wrap here. Falling through used to
            // add a SECOND reference at the end of this function, so `&Self`
            // resolved to `&&Self` — a call site could never match it (and the
            // trait-impl conformance check rejected the (correct) `&S` the
            // implementation wrote).
            if (isInTraitMethod)
                return base;
            if (raw.isRef)
                return context->typeContext->getReference(base, raw.isMutRef);
            return base;
        }
        else
        {
            // Look up generic param scopes in priority order: function, trait method, struct, then customs.
            std::shared_ptr<GenericParamType> gp;
            if (functionInfo.isInFunction)
            {
                auto it = functionInfo.gParams.find(raw.name);
                if (it != functionInfo.gParams.end()) gp = it->second;
            }
            if (!gp && isInTraitMethod)
            {
                auto it = traitGParams.find(raw.name);
                if (it != traitGParams.end()) gp = it->second;
            }
            if (!gp)
            {
                auto it = structGParams.find(raw.name);
                if (it != structGParams.end()) gp = it->second;
            }

            if (gp)
            {
                base = gp;
            }
            else
            {
                // Module-aware custom-type lookup: a `$`-prefixed name is already
                // internal; a bare name resolves in the current module first,
                // then the root module; a selective-import alias symbol (pass 1d)
                // forwards to the module's internal type.
                std::string typeName = raw.name;
                auto custom = context->typeContext->getCustom(typeName);
                if (!custom.has_value() && !isInternalName(typeName) && !currentModule_.empty())
                {
                    typeName = internalName(currentModule_, raw.name);
                    custom = context->typeContext->getCustom(typeName);
                }
                if (!custom.has_value())
                {
                    if (auto *sym = lookupModuleAware(raw.name))
                    {
                        if (sym->type && sym->type->getKind() == Type::Kind::Custom)
                            custom = std::static_pointer_cast<CustomType>(sym->type);
                    }
                }
                if (!custom.has_value())
                {
                    if (!suppressTypeErrors_)
                        log(errorNode, "the type '" + raw.name + "' cannot be found.");
                    return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                }
                base = custom.value();
            }
        }
    }

    // Generic instantiation: Foo<T1, T2, ...>
    if (!raw.genericArgs.empty())
    {
        auto custom = std::dynamic_pointer_cast<CustomType>(base);
        if (!custom)
        {
            if (!suppressTypeErrors_)
                log(errorNode, "type '" + raw.name + "' is not a struct and cannot take generic arguments.");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        if (!custom->isGeneric())
        {
            if (!suppressTypeErrors_)
                log(errorNode, "type '" + raw.name + "' is not generic.");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        if (custom->getGenericParams().size() != raw.genericArgs.size())
        {
            if (!suppressTypeErrors_)
                log(errorNode, "generic argument count mismatch for '" + raw.name + "': expected " + std::to_string(custom->getGenericParams().size()) + ", got " + std::to_string(raw.genericArgs.size()) + ".");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }

        std::vector<std::shared_ptr<Type>> typeArgs;
        for (auto &g : raw.genericArgs)
        {
            auto argTy = resolveType(g, errorNode);
            // `void` has no value, so it cannot be STORED — and a generic argument
            // becomes a field of the instantiated struct/enum (`Result<void, E>`
            // would be a struct with a void field, which LLVM rejects outright).
            // Use an empty struct (e.g. the stdlib `Unit`) for a result that
            // carries no value.
            if (argTy && argTy->getKind() == Type::Kind::Primitive
                && std::static_pointer_cast<PrimitiveType>(argTy)->getPrimKind() == PrimitiveType::PrimKind::VOID)
            {
                log(errorNode, "the type 'void' cannot be a generic argument (void has no value).");
                argTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::NEVER);
            }
            typeArgs.push_back(argTy);
        }

        // Constraint check
        for (size_t i = 0; i < typeArgs.size(); ++i)
        {
            auto gp = std::static_pointer_cast<GenericParamType>(custom->getGenericParams()[i]);
            for (auto &constraint : gp->getConstraints())
            {
                bool ok = false;
                for (auto &impled : typeArgs[i]->implTrait)
                    if (impled->equals(constraint))
                    {
                        ok = true;
                        break;
                    }
                if (!ok)
                    log(errorNode, "type '" + typeArgs[i]->toString() + "' does not implement trait '" + constraint->toString() + "' required by '" + raw.name + "'.");
            }
        }

        base = context->typeContext->instantiateCustom(custom, std::move(typeArgs));
    }

    if (raw.isRef)
        return context->typeContext->getReference(base, raw.isMutRef);

    return base;
}

void HIRSemanticAnalyzer::analyzeExpr(HIRExpr *expr)
{
    if (expr) expr->accept(this);
}

void HIRSemanticAnalyzer::analyzeStmt(HIRStmt *stmt)
{
    if (!stmt) return;
    // Statement-scope borrow marker: temporary (non-promoted) borrows created
    // inside this statement end when it completes (`foo(&x); bar(&mut x);` must
    // be legal). Promoted variable borrows (`let r = &x`) survive.
    ++stmtOrdinal_; // NLL: statement ordinal for borrow liveness
    size_t saved = stmtBorrowStart_;
    stmtBorrowStart_ = activeBorrows_.size();
    stmt->accept(this);
    endTemporaryBorrowsSince(stmtBorrowStart_);
    stmtBorrowStart_ = saved;
}

std::vector<std::shared_ptr<Type>> HIRSemanticAnalyzer::inferGenericArguments(
    const std::vector<std::shared_ptr<Type>> &genericParams,
    const std::vector<std::shared_ptr<Type>> &paramTypes,
    const std::vector<std::unique_ptr<HIRExpr>> &args)
{
    std::unordered_map<std::string, std::shared_ptr<Type>> genericMap;
    std::vector<std::shared_ptr<Type>> result;

    for (size_t i = 0; i < args.size() && i < paramTypes.size(); ++i)
    {
        analyzeExpr(args[i].get());
        auto argTy = args[i]->type;
        auto paramTy = paramTypes[i];
        matchGenericType(paramTy, argTy, genericMap);
    }

    for (const auto &gp : genericParams)
    {
        assert(gp->getKind() == Type::Kind::GenericParam);
        auto newTy = std::static_pointer_cast<GenericParamType>(gp);
        auto it = genericMap.find(newTy->getParamName());
        if (it != genericMap.end()) result.push_back(it->second);
    }
    return result;
}

// 递归匹配泛型类型（辅助函数）
void HIRSemanticAnalyzer::matchGenericType(
    std::shared_ptr<Type> paramTy,
    std::shared_ptr<Type> argTy,
    std::unordered_map<std::string, std::shared_ptr<Type>> &genericMap)
{
    if (!paramTy || !argTy) return;

    if (auto gp = std::dynamic_pointer_cast<GenericParamType>(paramTy))
    {
        genericMap[gp->getParamName()] = argTy;
        return;
    }
    if (auto refParam = std::dynamic_pointer_cast<ReferenceType>(paramTy))
    {
        if (auto refArg = std::dynamic_pointer_cast<ReferenceType>(argTy))
            matchGenericType(refParam->getBaseType(), refArg->getBaseType(), genericMap);
        return;
    }
    // A `*mut T` / `*T` FIELD carries the parameter just like a reference does:
    // a container buffer (`struct Vec<T: Copy> { data: *mut T, ... }`) could not
    // be constructed from its own methods without this — `Vec { data: <*mut T>,
    // ... }` reported "cannot infer generic parameter T". (`Foo<i32> { ... }` is
    // NOT a way out: the parser rejects explicit generic arguments on a struct
    // literal, because `Name<T> {` is ambiguous with `a < b {`.)
    if (auto ptrParam = std::dynamic_pointer_cast<PointerType>(paramTy))
    {
        if (auto ptrArg = std::dynamic_pointer_cast<PointerType>(argTy))
            matchGenericType(ptrParam->getBaseType(), ptrArg->getBaseType(), genericMap);
        return;
    }
    if (auto arrParam = std::dynamic_pointer_cast<ArrayType>(paramTy))
    {
        if (auto arrArg = std::dynamic_pointer_cast<ArrayType>(argTy))
            matchGenericType(arrParam->getElementType(), arrArg->getElementType(), genericMap);
        return;
    }
    if (auto ctParam = std::dynamic_pointer_cast<CustomType>(paramTy))
    {
        auto ctArg = std::dynamic_pointer_cast<CustomType>(argTy);
        if (!ctArg) return;
        // Compare *origin* names: mangled names (Option$T vs Option$int32)
        // differ by generic args, which we recurse into below.
        auto pName = ctParam->getOriginName();
        auto aName = ctArg->getOriginName();
        if (pName != aName) return;
        // A side written WITHOUT generic arguments in the source (a field declared
        // `src: &Vec<T>`, i.e. the generic DEFINITION) keeps its parameters in
        // genericParams and has EMPTY genericArgs, so matching only the args
        // silently inferred nothing: a generic struct could not be built out of
        // another generic type's parameter (`VecIter { src: self, pos: 0 }` in
        // vec.lis reported "cannot infer generic parameter 'T'"). Fall back to the
        // parameter list on either side; the caller re-checks the result against
        // the declared type, so a wrong guess surfaces as a type error.
        const auto &pa = ctParam->getGenericArgs().empty() ? ctParam->getGenericParams()
                                                           : ctParam->getGenericArgs();
        const auto &aa = ctArg->getGenericArgs().empty() ? ctArg->getGenericParams()
                                                         : ctArg->getGenericArgs();
        for (size_t i = 0; i < pa.size() && i < aa.size(); ++i)
            matchGenericType(pa[i], aa[i], genericMap);
    }
}

std::shared_ptr<FunctionType> HIRSemanticAnalyzer::instantiateGenericFunction(
    std::shared_ptr<FunctionType> genericFunc,
    const std::vector<std::shared_ptr<Type>> &genericArgs)
{
    std::unordered_map<std::string, std::shared_ptr<Type>> subst;
    const auto &gParams = genericFunc->getGenericParams();
    for (size_t i = 0; i < gParams.size() && i < genericArgs.size(); ++i)
        subst[std::static_pointer_cast<GenericParamType>(gParams[i])->getParamName()] = genericArgs[i];

    // 实例化参数类型
    std::vector<std::shared_ptr<Type>> instParams;
    for (const auto &p : genericFunc->getParams())
        instParams.push_back(substituteType(p, subst));

    // 实例化返回值类型
    auto instRet = substituteType(genericFunc->getReturnType(), subst);
    return context->typeContext->getFunction(instParams, instRet);
}

// 类型替换（辅助函数）— delegates to the single TypeContext::substitute.
std::shared_ptr<Type> HIRSemanticAnalyzer::substituteType(
    std::shared_ptr<Type> ty,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &subst)
{
    return context->typeContext->substitute(std::move(ty), subst, /*strict=*/false);
}

// ============================================================
//  Pre-registration pass (forward declarations for the global scope)
// ============================================================

bool HIRSemanticAnalyzer::bareNameConflicts(const Symbol *existing) const
{
    if (!existing) return false;
    // Locals, params, functions, types: the bare name really is taken.
    if (existing->kind != SymbolKind::GlobalVar) return true;
    // A module-level `let` is visible under its bare name only when it belongs
    // to the ROOT module (internalName leaves it unprefixed there). A stdlib
    // module's own global is keyed `math$base`, so a param named `base` in
    // math.lis does not collide with a user's `let base = 10;` in test.lis —
    // which is the case the stdlib relies on.
    return currentModule_.empty();
}

void HIRSemanticAnalyzer::preRegister(HIRNode *item)
{
    // Phase A: register top-level *names* only. Type creation happens in
    // buildStructType / preRegisterFunctionType, which run afterwards so that
    // forward references between structs and function signatures resolve.
    if (auto *s = dynamic_cast<HIRStruct *>(item))
    {
        if (SymbolTable::getInstance().lookupSymbol(s->name)) return;
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Struct;
        sym->name = s->name;
        sym->type = nullptr;
        SymbolTable::getInstance().insertSymbol(s->name, std::move(sym));
    }
    else if (auto *t = dynamic_cast<HIRTrait *>(item))
    {
        if (SymbolTable::getInstance().lookupSymbol(t->name)) return;
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Trait;
        sym->name = t->name;
        sym->type = nullptr;
        SymbolTable::getInstance().insertSymbol(t->name, std::move(sym));
    }
    else if (auto *e = dynamic_cast<HIREnum *>(item))
    {
        // An enum is structurally a CustomType (fat tagged union) — reuse the
        // Struct symbol kind.
        if (SymbolTable::getInstance().lookupSymbol(e->name)) return;
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Struct;
        sym->name = e->name;
        sym->type = nullptr;
        SymbolTable::getInstance().insertSymbol(e->name, std::move(sym));
    }
    else if (auto *f = dynamic_cast<HIRFunction *>(item))
    {
        // A user/`stdlib` `fn` named like a builtin or a libc symbol would be
        // silently shadowed by the compiler's call-site interception (builtins)
        // or collide with codegen's external declaration (libc). Reject it.
        // `f->name` carries the module prefix — strip it: `foo$strlen` is still
        // a redefinition of the reserved libc name.
        if (isReservedFunctionName(displayName(f->name)))
        {
            log(*f, "function name '" + displayName(f->name) + "' is reserved by the compiler.");
            return;
        }
        if (SymbolTable::getInstance().lookupSymbol(f->name)) return;
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Function;
        sym->name = f->name;
        sym->type = nullptr;
        SymbolTable::getInstance().insertSymbol(f->name, std::move(sym));
    }
    else if (auto *v = dynamic_cast<HIRVarDecl *>(item))
    {
        // Module-level `let` (a constant, in practice). Registered HERE, in the
        // name pass, because pass 1d promotes selective imports by looking the
        // target up in the symbol table: `impt m { K };` used to fail with
        // "module 'm' has no member 'K'" simply because the global's symbol was
        // only created later, in pass 2. visit(HIRVarDecl) then UPDATES this slot
        // in place (it must not re-insert: Scope::insert silently keeps the old
        // symbol, so a second insert would leave the provisional type forever).
        if (!v->isGlobal) return;
        if (SymbolTable::getInstance().lookupSymbol(v->name)) return;
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::GlobalVar;
        sym->name = v->name;
        sym->type = bestEffortGlobalType(v);
        sym->isMutable = v->isMutable;
        SymbolTable::getInstance().insertSymbol(v->name, std::move(sym));
        preRegisteredGlobals_.insert(v);
    }
}

// Best-effort type of a module-level `let`, for the name pass above. Two
// sources, in order: the explicit annotation, or the LITERAL initializer's
// kind (globals must be literal-initialized — visit(HIRVarDecl) enforces it).
// Anything else stays untyped until pass 2; type errors here are suppressed,
// exactly like the function-signature pre-registration.
std::shared_ptr<Type> HIRSemanticAnalyzer::bestEffortGlobalType(HIRVarDecl *decl)
{
    bool saved = suppressTypeErrors_;
    suppressTypeErrors_ = true;

    std::shared_ptr<Type> ty;
    if (decl->hasExplicitType)
    {
        ty = resolveType(decl->rawType, *decl);
    }
    else if (decl->init.has_value())
    {
        if (auto *lit = dynamic_cast<HIRLiteral *>(decl->init.value().get()))
        {
            switch (lit->kind)
            {
            case HIRLiteral::Kind::Int: ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32); break;
            case HIRLiteral::Kind::Float: ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::F64); break;
            case HIRLiteral::Kind::Bool: ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL); break;
            case HIRLiteral::Kind::Char: ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::CHAR); break;
            case HIRLiteral::Kind::String:
                ty = context->typeContext->getReference(
                    context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);
                break;
            }
        }
    }

    suppressTypeErrors_ = saved;
    return ty;
}

// ---------------------------------------------------------------------------
// Best-effort function signature resolution for the pre-registration pass.
// Struct CustomTypes have already been built by buildStructType by the time
// this runs, so struct params/returns resolve correctly. Type errors found
// here are suppressed — the full analysis pass reports the authoritative ones.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Impl-method pre-registration (pass 1c-3)
// ---------------------------------------------------------------------------
// Every method's SIGNATURE is resolved and attached to its type BEFORE any
// method body is analyzed. visit(HIRImpl) used to attach the method set only
// after visiting all of the bodies, so a body calling a sibling
// (`self.helper()`) failed with "struct 'X' has no method 'helper'": the
// receiver's type genuinely had no methods yet (visit(HIRCall)'s Method branch
// looks the name up in `customTy->getMethods()`). Best-effort signatures are
// enough for that lookup; pass 2 refreshes them through the same upsert.
void HIRSemanticAnalyzer::preRegisterMethodType(HIRImpl *impl, HIRFunction *m,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns,
    std::vector<CustomType::Method> &out)
{
    m->isTraitMethod = impl->traitName.has_value();
    if (impl->traitName.has_value())
        m->associatedTrait = impl->traitName.value();

    // The method SYMBOL is keyed `Struct::name` (visit(HIRImpl) uses the same
    // rule). The HIR function's own `name` is the bare method name, so the
    // top-level pre-registration helper cannot be reused for it directly.
    const std::string funcName = impl->structName + "::" + m->name;
    if (!SymbolTable::getInstance().lookupSymbol(funcName))
    {
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Function;
        sym->name = funcName;
        sym->type = nullptr;
        SymbolTable::getInstance().insertSymbol(funcName, std::move(sym));
    }

    auto inferred = inferredReturns.find(m->name);
    std::vector<std::shared_ptr<Type>> paramTypes;
    std::shared_ptr<Type> funcType = resolveFunctionSignature(m,
        (inferred != inferredReturns.end()) ? inferred->second : nullptr,
        &paramTypes);
    if (auto *sym = SymbolTable::getInstance().lookupSymbol(funcName))
        sym->type = funcType;

    // The parameter list the CALL path reads (argument checking, MIR). It is
    // derived from the resolved types, receiver first, so the two can never
    // drift apart.
    std::vector<CustomType::Field> paramFields;
    size_t typeIndex = 0;
    if (m->isMethod && m->hasSelf && typeIndex < paramTypes.size())
        paramFields.emplace_back("self", paramTypes[typeIndex++]);
    for (auto &[pname, rawTy] : m->rawParams)
    {
        if (typeIndex >= paramTypes.size()) break;
        paramFields.emplace_back(pname, paramTypes[typeIndex++]);
    }

    const std::string traitName = impl->traitName.has_value() ? impl->traitName.value() : "";
    out.push_back(CustomType::Method{
        m->name, traitName, paramFields, m->returnType, m->isStatic, impl->traitName.has_value()});
}

void HIRSemanticAnalyzer::preRegisterImplMethods(HIRImpl *impl)
{
    auto *structSym = lookupModuleAware(impl->structName);
    if (!structSym || structSym->kind != SymbolKind::Struct) return;
    auto baseStruct = std::dynamic_pointer_cast<CustomType>(structSym->type);
    if (!baseStruct) return;

    // Install the impl's type scope the way visit(HIRImpl) does, and restore it
    // afterwards: this pass must not leak state into the passes that follow.
    auto savedStruct = currentStructType;
    auto savedGParams = structGParams;
    currentStructType = baseStruct;
    structGParams.clear();
    if (baseStruct->isGeneric())
    {
        for (auto &gp : baseStruct->getGenericParams())
        {
            auto gpTy = std::static_pointer_cast<GenericParamType>(gp);
            structGParams[gpTy->getParamName()] = gpTy;
        }
    }
    for (auto &gp : impl->gParams)
        structGParams[gp->getParamName()] = gp;

    suppressTypeErrors_ = true;

    // Best-effort return types for methods that infer theirs from the body —
    // the same scan pass 1c-1 uses for top-level functions. Keyed by the BARE
    // method name: the map is local to this impl, so two impls' `get` cannot
    // collide the way a program-wide `f->name` key (`m$get`) would.
    std::unordered_map<std::string, std::shared_ptr<Type>> inferredReturns;
    for (auto &m : impl->methods)
    {
        if (m->hasReturnType || !m->body) continue;
        std::unordered_map<std::string, std::shared_ptr<Type>> paramTypes;
        for (auto &[pname, rawTy] : m->rawParams)
            paramTypes[pname] = resolveType(rawTy, *m);
        if (auto ty = scanInferredReturn(m->body.get(), paramTypes, inferredReturns))
            inferredReturns[m->name] = ty;
    }

    std::vector<CustomType::Method> methods;
    for (auto &m : impl->methods)
        preRegisterMethodType(impl, m.get(), inferredReturns, methods);
    baseStruct->upsertMethods(std::move(methods));

    suppressTypeErrors_ = false;
    currentStructType = savedStruct;
    structGParams = std::move(savedGParams);
}

// Signature resolution shared by the top-level function pre-pass (pass 1c-2)
// and the impl-method pre-pass (pass 1c-3). Fills `f->type` / `f->returnType`
// and hands the resolved parameter types back through `paramTypesOut`.
//
// It deliberately does NOT touch `f->params`: visit(HIRFunction) APPENDS to
// that vector (it also appends the implicit `self`), so anything written here
// would survive as a duplicate entry. A caller that needs the name/type pairs
// builds them from `f->rawParams` plus `paramTypesOut`.
std::shared_ptr<Type> HIRSemanticAnalyzer::resolveFunctionSignature(HIRFunction *f,
    const std::shared_ptr<Type> &inferredRet,
    std::vector<std::shared_ptr<Type>> *paramTypesOut)
{
    // Bring the function's own generic params into scope so `it: T` resolves
    // to the generic param T rather than a silent VOID. resolveType only
    // consults functionInfo.gParams when isInFunction is set — mimic that here.
    auto savedGParams = std::move(functionInfo.gParams);
    bool savedInFunction = functionInfo.isInFunction;
    functionInfo.isInFunction = true;
    for (auto &gp : f->gParams)
        functionInfo.gParams[gp->getParamName()] = gp;

    std::vector<std::shared_ptr<Type>> paramTypes;
    // A METHOD's signature starts with the implicit receiver, exactly as
    // visit(HIRFunction) builds it: the method-call path reads params[0] as
    // `self` (`instantiatedFuncType->getParams()[0]`) and would index past the
    // end of a receiver-less signature. It also must be the reference form of
    // the struct for a `&self` / `&mut self` receiver.
    if (f->isMethod && f->hasSelf && currentStructType)
    {
        std::shared_ptr<Type> selfTy = currentStructType;
        if (f->selfIsRef)
            selfTy = context->typeContext->getReference(currentStructType, f->selfIsMut);
        f->selfType = selfTy;
        paramTypes.push_back(selfTy);
    }
    for (auto &[pname, rawTy] : f->rawParams)
        paramTypes.push_back(resolveType(rawTy, *f));

    std::shared_ptr<Type> retTy;
    if (f->hasReturnType)
        retTy = resolveType(f->rawReturnType, *f);
    else if (inferredRet)
        retTy = inferredRet; // best-effort scan; visit(HIRFunction) corrects it
    else
        retTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    functionInfo.gParams = std::move(savedGParams);
    functionInfo.isInFunction = savedInFunction;

    std::shared_ptr<Type> funcType = nullptr;
    if (f->isGeneric)
        funcType = context->typeContext->getGenericFunction(f->gParams, paramTypes, retTy);
    else
        funcType = context->typeContext->getFunction(paramTypes, retTy);

    f->returnType = retTy;
    f->type = funcType;
    if (paramTypesOut)
        *paramTypesOut = std::move(paramTypes);
    return funcType;
}

void HIRSemanticAnalyzer::preRegisterFunctionType(HIRFunction *f,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns)
{
    auto *sym = SymbolTable::getInstance().lookupSymbol(f->name);
    if (!sym) return;

    suppressTypeErrors_ = true;

    auto it = inferredReturns.find(f->name);
    const std::shared_ptr<Type> inferred = (it != inferredReturns.end()) ? it->second : nullptr;
    sym->type = resolveFunctionSignature(f, inferred);

    suppressTypeErrors_ = false;
}

// ---------------------------------------------------------------------------
// Best-effort type of a `ret <expr>` in the pre-pass, before the body has been
// analyzed. Handles literals, params/generic-params, plain calls, refs, casts.
// Returns a concrete type or VOID (unknown) — never a foreign GenericParamType.
// ---------------------------------------------------------------------------

std::shared_ptr<Type> HIRSemanticAnalyzer::bestEffortRetType(HIRExpr *expr,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &paramTypes,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    if (!expr) return voidTy;

    if (auto *lit = dynamic_cast<HIRLiteral *>(expr))
    {
        switch (lit->kind)
        {
        case HIRLiteral::Kind::Int: return context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
        case HIRLiteral::Kind::Float: return context->typeContext->getPrimitive(PrimitiveType::PrimKind::F64);
        case HIRLiteral::Kind::Bool: return context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        case HIRLiteral::Kind::Char: return context->typeContext->getPrimitive(PrimitiveType::PrimKind::CHAR);
        case HIRLiteral::Kind::String:
            return context->typeContext->getReference(
                context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);
        }
    }
    else if (auto *ref = dynamic_cast<HIRNameRef *>(expr))
    {
        auto it = paramTypes.find(ref->name);
        if (it != paramTypes.end())
            return it->second->getKind() == Type::Kind::GenericParam ? voidTy : it->second;
        // Not a param — a local (unknown in the pre-pass). VOID.
        return voidTy;
    }
    else if (auto *call = dynamic_cast<HIRCall *>(expr))
    {
        if (call->callKind == HIRCall::CallKind::Regular)
        {
            if (auto *nameRef = dynamic_cast<HIRNameRef *>(call->callee.get()))
            {
                if (auto *sym = SymbolTable::getInstance().lookupSymbol(nameRef->name))
                {
                    if (auto ft = std::dynamic_pointer_cast<FunctionType>(sym->type))
                        return ft->getReturnType();
                }
                auto it = inferredReturns.find(nameRef->name);
                if (it != inferredReturns.end())
                    return it->second ? it->second : voidTy;
            }
        }
        return voidTy;
    }
    else if (auto *r = dynamic_cast<HIRRef *>(expr))
    {
        return bestEffortRetType(r->expr.get(), paramTypes, inferredReturns);
    }
    else if (auto *cast = dynamic_cast<HIRCast *>(expr))
    {
        return resolveType(cast->rawTargetType, *cast);
    }
    else if (auto *bin = dynamic_cast<HIRBinaryOp *>(expr))
    {
        using H = HIRBinaryOp::OpKind;
        // Comparisons and logical ops always yield bool.
        switch (bin->opKind)
        {
        case H::Eq:
        case H::Ne:
        case H::Lt:
        case H::Gt:
        case H::Le:
        case H::Ge:
        case H::And:
        case H::Or:
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        default: break;
        }
        // Arithmetic / bitwise / shift: result type == operand type. A literal
        // operand is typed by HIRBuilder, so `ret y + 1` infers i32 here.
        auto isVoid = [&](const std::shared_ptr<Type> &t)
        {
            return !t
                   || (t->getKind() == Type::Kind::Primitive
                       && std::static_pointer_cast<PrimitiveType>(t)->getPrimKind() == PrimitiveType::PrimKind::VOID);
        };
        auto rightTy = bestEffortRetType(bin->right.get(), paramTypes, inferredReturns);
        if (!isVoid(rightTy)) return rightTy;
        auto leftTy = bestEffortRetType(bin->left.get(), paramTypes, inferredReturns);
        if (!isVoid(leftTy)) return leftTy;
        return voidTy;
    }
    else if (auto *ma = dynamic_cast<HIRMemberAccess *>(expr))
    {
        // Field access on a struct literal etc. — unknown in the pre-pass.
        (void)ma;
        return voidTy;
    }
    return voidTy;
}

// ---------------------------------------------------------------------------
// Recursively scan a function body for the first concrete `ret` value type.
// ---------------------------------------------------------------------------

std::shared_ptr<Type> HIRSemanticAnalyzer::scanInferredReturn(HIRBlock *body,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &paramTypes,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    if (!body) return voidTy;

    for (auto &stmt : body->stmts)
    {
        if (auto *ret = dynamic_cast<HIRReturn *>(stmt.get()))
        {
            if (ret->value.has_value())
            {
                auto ty = bestEffortRetType(ret->value.value().get(), paramTypes, inferredReturns);
                if (ty && ty->getKind() != Type::Kind::GenericParam && !ty->equals(voidTy))
                    return ty;
            }
        }
        else if (auto *blk = dynamic_cast<HIRBlock *>(stmt.get()))
        {
            auto ty = scanInferredReturn(blk, paramTypes, inferredReturns);
            if (ty && ty->getKind() != Type::Kind::GenericParam && !ty->equals(voidTy))
                return ty;
        }
        else if (auto *ifs = dynamic_cast<HIRIf *>(stmt.get()))
        {
            auto ty = scanInferredReturn(ifs->thenBlock.get(), paramTypes, inferredReturns);
            if (ty && ty->getKind() != Type::Kind::GenericParam && !ty->equals(voidTy))
                return ty;
            if (ifs->elseBlock.has_value())
            {
                ty = scanInferredReturn(ifs->elseBlock.value().get(), paramTypes, inferredReturns);
                if (ty && ty->getKind() != Type::Kind::GenericParam && !ty->equals(voidTy))
                    return ty;
            }
        }
        else if (auto *loop = dynamic_cast<HIRLoop *>(stmt.get()))
        {
            auto ty = scanInferredReturn(loop->body.get(), paramTypes, inferredReturns);
            if (ty && ty->getKind() != Type::Kind::GenericParam && !ty->equals(voidTy))
                return ty;
        }
    }
    return voidTy;
}

// ---------------------------------------------------------------------------
// Resolve an impl's trait generic args (`Iterator<i32>` -> [i32]) using the
// struct's + impl's generic scope. Shared by the pre-registration conformance
// pass and visit(HIRImpl)'s signature substitution.
// ---------------------------------------------------------------------------

std::vector<std::shared_ptr<Type>> HIRSemanticAnalyzer::resolveTraitArgs(
    HIRImpl *node,
    const std::shared_ptr<TraitType> &traitType,
    std::unordered_map<std::string, std::shared_ptr<Type>> *outSubst)
{
    std::vector<std::shared_ptr<Type>> traitArgs;
    if (!node || !traitType || node->traitGenericArgs.empty())
        return traitArgs;

    const auto &traitParams = traitType->getGenericParams();
    if (traitParams.size() != node->traitGenericArgs.size())
    {
        log(*node, "trait generic argument count mismatch for '" + node->traitName.value() + "'.");
        return traitArgs; // empty -> callers skip the push
    }

    for (size_t i = 0; i < traitParams.size(); ++i)
    {
        auto argTy = resolveType(node->traitGenericArgs[i], *node);
        traitArgs.push_back(argTy);
        if (outSubst)
        {
            auto gp = std::static_pointer_cast<GenericParamType>(traitParams[i]);
            (*outSubst)[gp->getParamName()] = argTy;
        }
    }
    return traitArgs;
}

// ---------------------------------------------------------------------------
// Resolve a single trait bound (`T: Iterator<i32>`) to an instantiated
// TraitType. Shared by buildStructType and visit(HIRFunction) so the two
// generic-param constraint paths can't diverge.
// ---------------------------------------------------------------------------

std::shared_ptr<TraitType> HIRSemanticAnalyzer::resolveTraitConstraint(
    const HIRGenericConstraint &con,
    HIRNode &errNode,
    bool silent)
{
    auto *traitSym = lookupModuleAware(con.traitName);
    if (!traitSym)
    {
        if (!silent)
            log(errNode, "can not find the trait '" + con.traitName + "' for trait constraint.");
        return nullptr;
    }
    if (traitSym->kind != SymbolKind::Trait || !traitSym->type)
    {
        if (!silent)
            log(errNode, "trait constraint '" + con.traitName + "' is not a trait name.");
        return nullptr;
    }

    auto traitTy = std::static_pointer_cast<TraitType>(traitSym->type);
    std::vector<std::shared_ptr<Type>> argTypes;
    for (auto &raw : con.args)
        argTypes.push_back(resolveType(raw, errNode));

    // A bare-name constraint on a generic trait (`T: Iterator` with no <i32>)
    // is a user error — fail with a diagnostic instead of tripping
    // instantiateTrait's arity assert on the wrong count.
    if (!traitTy->getGenericParams().empty()
        && argTypes.size() != traitTy->getGenericParams().size())
    {
        if (!silent)
            log(errNode, "trait constraint '" + con.traitName + "' expects " + std::to_string(traitTy->getGenericParams().size()) + " generic argument(s), got " + std::to_string(argTypes.size()) + ".");
        return nullptr;
    }

    return context->typeContext->instantiateTrait(traitTy, std::move(argTypes));
}

// ---------------------------------------------------------------------------
// Pre-register a struct's trait conformance (implTrait) from an impl, BEFORE
// pass 2, so bound checks like `T: Iterator<i32>` accept concrete args even
// when the impl appears later in file order. Errors here are suppressed; pass 2
// (visit(HIRImpl)) reports the authoritative ones.
// ---------------------------------------------------------------------------

void HIRSemanticAnalyzer::preRegisterImplTrait(HIRImpl *node)
{
    if (!node || !node->traitName.has_value())
        return;

    auto *structSym = SymbolTable::getInstance().lookupSymbol(node->structName);
    auto *traitSym = SymbolTable::getInstance().lookupSymbol(node->traitName.value());
    if (!structSym || structSym->kind != SymbolKind::Struct)
        return;
    if (!traitSym || traitSym->kind != SymbolKind::Trait || !traitSym->type)
        return;

    auto baseStruct = std::dynamic_pointer_cast<CustomType>(structSym->type);
    auto traitType = std::static_pointer_cast<TraitType>(traitSym->type);
    if (!baseStruct || !traitType)
        return;

    // Generic scope = the struct's params + the impl's own params.
    structGParams.clear();
    for (auto &gp : baseStruct->getGenericParams())
        structGParams[std::static_pointer_cast<GenericParamType>(gp)->getParamName()] = std::static_pointer_cast<GenericParamType>(gp);
    for (auto &gp : node->gParams)
        structGParams[gp->getParamName()] = gp;

    auto traitArgs = resolveTraitArgs(node, traitType, nullptr);
    // Arity must match: a bare `impl Iterator` on a generic trait would trip
    // instantiateTrait's assert. Pass 2 reports the authoritative error.
    bool arityOk = traitType->getGenericParams().empty()
                   || traitArgs.size() == traitType->getGenericParams().size();
    if (arityOk && (!traitArgs.empty() || node->traitGenericArgs.empty()))
        baseStruct->implTrait.push_back(context->typeContext->instantiateTrait(traitType, traitArgs));

    structGParams.clear();
}

// ============================================================
//  Program
// ============================================================

void HIRSemanticAnalyzer::setModuleForItem(size_t index)
{
    currentModule_ = (index < context->stmtAttributions.size())
                         ? context->stmtAttributions[index].modulePath
                         : std::string();
    currentFilePath_ = (index < context->stmtAttributions.size())
                           ? context->stmtAttributions[index].filePath
                           : std::string();
}

// ---------------------------------------------------------------------------
// checkForRecursiveTypes — a type that contains itself by value is infinite
// ---------------------------------------------------------------------------
//
// `struct A { pub a: A }` has no finite layout, so the LLVM lowering produces an
// opaque `%A` and `__drop_A` GEPs into it; the module verifier then rejects the
// IR ("GEP into unsized type!") and the compiler used to die with that message
// plus an uncaught exception. Rust reports E0072 here. Note that a cycle through
// a POINTER or REFERENCE is fine (the field is an address, not the value), which
// is why only by-value containment is followed.

namespace
{
/// Depth-first walk of the by-value containment graph starting at `type`.
/// `stack` is the chain of names currently being expanded — re-entering one of
/// them is a cycle. `finite` memoizes the types already proven finite so a
/// diamond-shaped graph is walked once.
bool ReachesItselfByName(const std::shared_ptr<CustomType> &type,
    std::vector<std::string> &stack,
    std::unordered_set<std::string> &finite)
{
    if (!type) return false;
    const std::string &name = type->getName();
    if (finite.count(name)) return false;
    if (std::find(stack.begin(), stack.end(), name) != stack.end()) return true;

    stack.push_back(name);
    for (const auto &field : type->getFields())
    {
        if (!field.type) continue;
        // By-value containment only: a pointer/reference/function field holds an
        // address, so it stops the walk.
        if (auto inner = std::dynamic_pointer_cast<CustomType>(field.type))
        {
            if (ReachesItselfByName(inner, stack, finite))
            {
                stack.pop_back();
                return true;
            }
        }
        else if (auto array = std::dynamic_pointer_cast<ArrayType>(field.type))
        {
            if (auto element = std::dynamic_pointer_cast<CustomType>(array->getElementType()))
            {
                if (ReachesItselfByName(element, stack, finite))
                {
                    stack.pop_back();
                    return true;
                }
            }
        }
    }
    stack.pop_back();
    finite.insert(name);
    return false;
}
} // namespace

void HIRSemanticAnalyzer::checkForRecursiveTypes(HIRProgram *program)
{
    if (!program) return;

    std::unordered_set<std::string> finite;
    for (size_t i = 0; i < program->items.size(); ++i)
    {
        HIRNode *item = program->items[i].get();
        std::string typeName;
        if (auto *s = dynamic_cast<HIRStruct *>(item))
            typeName = s->name;
        else if (auto *e = dynamic_cast<HIREnum *>(item))
            typeName = e->name;
        else
            continue;

        // The HIR name is the source name; the registered CustomType is keyed by
        // the module-internal name, so ask the symbol table (as preRegister does).
        auto *symbol = SymbolTable::getInstance().lookupSymbol(typeName);
        if (!symbol || !symbol->type) continue;
        auto custom = std::dynamic_pointer_cast<CustomType>(symbol->type);
        if (!custom) continue;

        setModuleForItem(i);
        std::vector<std::string> stack;
        if (ReachesItselfByName(custom, stack, finite))
        {
            std::string chain = stack.empty() ? custom->getName() : stack.front();
            for (size_t k = 1; k < stack.size(); ++k)
                chain += " -> " + stack[k];
            log(*item,
                "recursive type '" + displayName(custom->getName())
                    + "' has infinite size: it contains itself by value (" + chain
                    + "). Store it behind a pointer or reference instead.",
                E_RecursiveType);
            // Reported once per cycle: the remaining types of the same cycle would
            // only repeat it.
            finite.insert(custom->getName());
        }
    }
}

void HIRSemanticAnalyzer::visit(HIRProgram *node)
{
    if (!node) return;

    // Pass 1: register all top-level names so forward references work.
    preRegisteredGlobals_.clear();
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        preRegister(node->items[i].get());
    }

    // Pre-registration type-building is best-effort; the full analysis pass is
    // authoritative, so suppress its diagnostics.
    suppressTypeErrors_ = true;

    // Pass 1b: register every struct as an EMPTY CustomType shell so forward /
    // recursive / cross-file references (e.g. `struct Node { next: Option<Node> }`,
    // or an impl in a later alphabetically-loaded stdlib file) resolve before any
    // fields are resolved. visit(HIRStruct) fills the fields in pass 2.
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        auto &item = node->items[i];
        if (auto *s = dynamic_cast<HIRStruct *>(item.get()))
        {
            if (auto *sym = SymbolTable::getInstance().lookupSymbol(s->name))
            {
                if (s->isGeneric)
                {
                    std::vector<std::shared_ptr<Type>> gParamTypes;
                    for (auto &gp : s->gParams)
                        gParamTypes.push_back(gp);
                    sym->type = context->typeContext->createGenericCustomShell(s->name, std::move(gParamTypes));
                }
                else
                {
                    sym->type = context->typeContext->createCustomShell(s->name);
                }
            }
        }
        else if (auto *e = dynamic_cast<HIREnum *>(item.get()))
        {
            // Enums are CustomTypes too — register an empty shell so forward
            // variant-construction / type references resolve before fill.
            if (auto *sym = SymbolTable::getInstance().lookupSymbol(e->name))
            {
                if (e->isGeneric)
                {
                    std::vector<std::shared_ptr<Type>> gParamTypes;
                    for (auto &gp : e->gParams)
                        gParamTypes.push_back(gp);
                    sym->type = context->typeContext->createGenericCustomShell(e->name, std::move(gParamTypes));
                }
                else
                {
                    sym->type = context->typeContext->createCustomShell(e->name);
                }
            }
        }
    }

    // Pass 1d: promote selective imports (`impt math { max }`). Each promoted
    // bare name gets a FORWARDING alias symbol in the importing module's
    // namespace (aliasTarget → the internal-name symbol), so bare-name lookups
    // and resolveType resolve `max` → `math$max`. Runs before trait/struct
    // type building so signatures like `fn next() -> Option<T>` resolve the
    // promoted type; forwarding keeps the alias's type current.
    for (auto &[mod, bindings] : context->importsByModule)
    {
        for (auto &b : bindings)
        {
            if (!b.selective) continue;
            for (auto &symName : b.symbols)
            {
                std::string target = internalName(b.canonicalModule, symName);
                Symbol *targetSym = SymbolTable::getInstance().lookupSymbol(target);
                if (!targetSym)
                {
                    Logger::Log(Logger::LogLevel::ERROR,
                        Logger::LogInfo{&context->fileValue, context->filePath, "module '" + b.canonicalModule + "' has no member '" + symName + "'.", 0, 0, 0, 0, E_UndefinedIdentifier, false, false, 1});
                    continue;
                }
                std::string aliasKey = internalName(mod, symName);
                if (SymbolTable::getInstance().lookupSymbol(aliasKey))
                {
                    // The importing module defines its own `symName` — a real
                    // name clash. Strict: report it (an explicit selective
                    // import must not silently lose to an unrelated definition).
                    Logger::Log(Logger::LogLevel::ERROR,
                        Logger::LogInfo{&context->fileValue, context->filePath, "selective import of '" + symName + "' conflicts with an existing name in this module.", 0, 0, 0, 0, E_UndefinedIdentifier, false, false, 1});
                    continue;
                }
                auto aliasSym = std::make_unique<Symbol>();
                aliasSym->kind = targetSym->kind;
                aliasSym->name = aliasKey; // slot name; lookups forward to target
                aliasSym->type = targetSym->type;
                aliasSym->aliasTarget = targetSym;
                SymbolTable::getInstance().insertSymbol(aliasKey, std::move(aliasSym));
            }
        }
    }

    // Pass 1b': build trait types so struct/function trait bounds resolve
    // regardless of declaration order. visit(HIRTrait) is idempotent (traits are
    // cached by name), so re-running it in pass 2 is harmless.
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        auto &item = node->items[i];
        if (auto *t = dynamic_cast<HIRTrait *>(item.get()))
            visit(t);
    }

    // Pass 1b'': fill struct fields. Every shell now exists, so forward field
    // references resolve to the shell type (correct identity); setFields fills
    // the origin. This must complete before any struct-init / impl body is
    // analyzed in pass 2 (those read struct fields).
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        auto &item = node->items[i];
        if (auto *s = dynamic_cast<HIRStruct *>(item.get()))
            buildStructType(s);
        else if (auto *e = dynamic_cast<HIREnum *>(item.get()))
            buildEnumType(e);
    }

    // Pass 1b''': register every impl's trait conformance on its struct BEFORE
    // any body/call analysis, so bound checks are order-independent (an impl
    // may appear after its first use in file order).
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        auto &item = node->items[i];
        if (auto *impl = dynamic_cast<HIRImpl *>(item.get()))
            preRegisterImplTrait(impl);
    }

    // Pass 1c-1: scan best-effort return types for functions with an inferred
    // return, iterating to a fixpoint so forward call chains resolve. This lets
    // a forward call to `fn later() { ret 1; }` see a usable (i32) return type
    // instead of a VOID placeholder.
    std::unordered_map<std::string, std::shared_ptr<Type>> inferredReturns;
    auto voidTyForScan = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t i = 0; i < node->items.size(); ++i)
        {
            setModuleForItem(i);
            auto &item = node->items[i];
            auto *f = dynamic_cast<HIRFunction *>(item.get());
            if (!f || f->hasReturnType) continue;

            auto savedGParams = std::move(functionInfo.gParams);
            bool savedInFunction = functionInfo.isInFunction;
            functionInfo.isInFunction = true;
            for (auto &gp : f->gParams)
                functionInfo.gParams[gp->getParamName()] = gp;

            std::unordered_map<std::string, std::shared_ptr<Type>> paramTypes;
            for (auto &[pname, rawTy] : f->rawParams)
                paramTypes[pname] = resolveType(rawTy, *f);

            auto ty = scanInferredReturn(f->body.get(), paramTypes, inferredReturns);

            functionInfo.gParams = std::move(savedGParams);
            functionInfo.isInFunction = savedInFunction;

            if (ty && !ty->equals(voidTyForScan))
            {
                auto it = inferredReturns.find(f->name);
                if (it == inferredReturns.end() || !it->second || !it->second->equals(ty))
                {
                    inferredReturns[f->name] = ty;
                    changed = true;
                }
            }
        }
    }

    // Pass 1c-2: resolve function signatures (best-effort, errors suppressed).
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        auto &item = node->items[i];
        if (auto *f = dynamic_cast<HIRFunction *>(item.get()))
            preRegisterFunctionType(f, inferredReturns);
    }

    // Pass 1c-3: attach every impl's method signatures to its type, so a method
    // body can call a sibling (`self.helper()`), including one declared in a
    // LATER impl block (see preRegisterImplMethods).
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        if (auto *impl = dynamic_cast<HIRImpl *>(node->items[i].get()))
            preRegisterImplMethods(impl);
    }

    suppressTypeErrors_ = false;

    // Pass 2: full analysis
    for (size_t i = 0; i < node->items.size(); ++i)
    {
        setModuleForItem(i);
        context->typeContext->clearInstantiationOverflow();
        node->items[i]->accept(this);

        // TypeContext stops an instantiation that expands without end (a type
        // used inside its own type argument: `struct S<T> { pub v: S<S<T>> }`)
        // but has no source position to report it at, so the check lands here,
        // on the item that triggered it.
        if (context->typeContext->hasInstantiationOverflow())
        {
            log(*node->items[i],
                "type instantiation is too deep: a type is used inside its own type "
                "argument and expands without end, so it has no finite layout. "
                "Store the recursive part behind a pointer or reference instead.",
                E_RecursiveType);
            context->typeContext->clearInstantiationOverflow();
        }
    }

    // Pass 3: a type that contains itself by value has no finite layout. This
    // has to run after every body is analyzed (a cycle may cross two items, and
    // pass 1b only creates empty shells).
    checkForRecursiveTypes(node);
}

// ============================================================
//  Top-level declarations
// ============================================================

// ---------------------------------------------------------------------------
// Build the CustomType for a struct and return it. Shared by the
// pre-registration pass (so function signatures can reference the struct) and
// the full analysis pass. createCustom / createGenericCustom are idempotent by
// name, so running this twice yields the same type object.
// ---------------------------------------------------------------------------

std::shared_ptr<Type> HIRSemanticAnalyzer::buildStructType(HIRStruct *node)
{
    // Bring struct's generic params into scope and resolve their constraints.
    isInStruct = true;
    structGParams.clear();

    if (node->isGeneric)
    {
        for (auto &gp : node->gParams)
        {
            std::vector<std::shared_ptr<TraitType>> traits;
            auto cIt = node->unsolveConstraints.find(gp->getParamName());
            if (cIt != node->unsolveConstraints.end())
            {
                for (auto &con : cIt->second)
                {
                    if (auto trait = resolveTraitConstraint(con, *node, /*silent=*/suppressTypeErrors_))
                        traits.push_back(trait);
                }
            }
            gp->updateContraints(std::move(traits));
            structGParams[gp->getParamName()] = gp;
        }
    }

    std::unordered_set<std::string> seen;
    std::vector<CustomType::Field> fields;

    for (auto &member : node->members)
    {
        if (seen.count(member.name))
        {
            log(*node, "the member variable '" + member.name + "' is already defined.");
            continue;
        }
        seen.insert(member.name);

        // A field that names THIS type by value cannot have a finite layout:
        // `struct S<T> { pub v: S<S<T>> }` instantiates S with itself as the
        // argument, forever. Reject it before resolving (the resolution IS the
        // recursion), and give the field a primitive so the rest of the analysis
        // still has a type to work with — the error gate stops the pipeline.
        if (RawTypeMentionsSelf(member.rawType, node->name))
        {
            log(*node,
                "recursive type '" + node->name
                    + "' has infinite size: a field names the type itself by value "
                    "(directly, or inside a generic argument). Store the recursive "
                    "part behind a pointer or reference instead.",
                E_RecursiveType);
            member.type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
            CustomType::Field bad;
            bad.name = member.name;
            bad.type = member.type;
            bad.isPublic = member.isPublic;
            fields.push_back(std::move(bad));
            continue;
        }

        member.type = resolveType(member.rawType, *node);
        CustomType::Field f;
        f.name = member.name;
        f.type = member.type;
        f.isPublic = member.isPublic;
        fields.push_back(std::move(f));
    }

    std::shared_ptr<Type> ty;
    if (node->isGeneric)
    {
        std::vector<std::shared_ptr<Type>> gParamTypes(node->gParams.begin(), node->gParams.end());
        ty = context->typeContext->createGenericCustom(node->name, std::move(gParamTypes), fields);
    }
    else
    {
        ty = context->typeContext->createCustom(node->name, fields);
    }

    // The type may be a shell created in pass 1b (with empty fields) that was
    // returned by the name-keyed create* above — set the resolved fields now.
    if (auto ct = std::dynamic_pointer_cast<CustomType>(ty))
        ct->setFields(std::move(fields));

    structGParams.clear();
    isInStruct = false;
    return ty;
}

std::shared_ptr<Type> HIRSemanticAnalyzer::buildEnumType(HIREnum *node)
{
    // Bring the enum's generic params into scope and resolve their constraints
    // (mirrors buildStructType).
    isInStruct = true;
    structGParams.clear();

    if (node->isGeneric)
    {
        for (auto &gp : node->gParams)
        {
            std::vector<std::shared_ptr<TraitType>> traits;
            auto cIt = node->unsolveConstraints.find(gp->getParamName());
            if (cIt != node->unsolveConstraints.end())
            {
                for (auto &con : cIt->second)
                {
                    if (auto trait = resolveTraitConstraint(con, *node, /*silent=*/suppressTypeErrors_))
                        traits.push_back(trait);
                }
            }
            gp->updateContraints(std::move(traits));
            structGParams[gp->getParamName()] = gp;
        }
    }

    // Fat tagged-union layout: a `__tag` discriminant (i32) followed by one slot
    // per variant payload (`<variant>_<idx>`), all at their own offsets. Reading
    // an inactive slot is never done (only the active variant's slots are read),
    // and drop glue is tag-aware (only drops the active variant's payloads).
    std::vector<CustomType::Field> fields;
    fields.emplace_back("__tag", context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32));

    std::vector<CustomType::EnumVariantInfo> variants;
    for (auto &variant : node->variants)
    {
        CustomType::EnumVariantInfo vi;
        vi.name = variant.name;
        for (size_t j = 0; j < variant.payloadRawTypes.size(); ++j)
        {
            // Same rule as a struct field: a payload naming the enum itself by
            // value (`enum E<T> { A(E<E<T>>) }`) expands forever. `enum E { A(E) }`
            // takes this path too, so the check covers both spellings.
            if (RawTypeMentionsSelf(variant.payloadRawTypes[j], node->name))
            {
                log(*node,
                    "recursive type '" + node->name
                        + "' has infinite size: a variant payload names the type itself "
                        "by value. Store it behind a pointer or reference instead.",
                    E_RecursiveType);
                variant.payloadTypes.push_back(
                    context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32));
                continue;
            }

            auto pt = resolveType(variant.payloadRawTypes[j], *node);
            variant.payloadTypes.push_back(pt);
            vi.payloadTypes.push_back(pt);
            fields.emplace_back(variant.name + "_" + std::to_string(j), pt);
        }
        variants.push_back(std::move(vi));
    }

    std::shared_ptr<Type> ty;
    if (node->isGeneric)
    {
        std::vector<std::shared_ptr<Type>> gParamTypes(node->gParams.begin(), node->gParams.end());
        ty = context->typeContext->createGenericCustom(node->name, std::move(gParamTypes), fields);
    }
    else
    {
        ty = context->typeContext->createCustom(node->name, fields);
    }

    // The type may be a pass-1b shell returned by the name-keyed create* — set
    // the resolved fields + variant metadata now.
    if (auto ct = std::dynamic_pointer_cast<CustomType>(ty))
    {
        ct->setFields(std::move(fields));
        ct->setVariants(std::move(variants));
    }

    structGParams.clear();
    isInStruct = false;
    return ty;
}

// ---------------------------------------------------------------------------

void HIRSemanticAnalyzer::visit(HIRStruct *node)
{
    auto sym = SymbolTable::getInstance().lookupSymbol(node->name);
    if (!sym)
    {
        log(*node, "internal: struct symbol '" + node->name + "' was not pre-registered.");
        return;
    }

    sym->type = buildStructType(node);
    node->structSymbol = sym;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIREnum *node)
{
    auto sym = SymbolTable::getInstance().lookupSymbol(node->name);
    if (!sym)
    {
        log(*node, "internal: enum symbol '" + node->name + "' was not pre-registered.");
        return;
    }

    sym->type = buildEnumType(node);
    node->enumSymbol = sym;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRTrait *node)
{
    auto sym = SymbolTable::getInstance().lookupSymbol(node->name);
    if (!sym)
    {
        log(*node, "internal: trait symbol '" + node->name + "' was not pre-registered.");
        return;
    }

    isInTraitMethod = true;
    traitName = node->name;

    // Bring the trait's generic params into scope so method signatures can
    // reference them (e.g. `fn next(&mut self) -> Option<T>`).
    traitGParams.clear();
    for (auto &gp : node->gParams)
        traitGParams[gp->getParamName()] = gp;

    std::vector<TraitType::Method> traitMethods;

    for (auto &method : node->methods)
    {
        // Analyse the method signature only (no body for trait declarations)
        // We do NOT call visit(method.get()) directly because that would try
        // to enter a scope and analyse a body that might be absent.
        // Instead, just resolve types manually.

        std::vector<CustomType::Field> methodParams;

        if (method->hasSelf)
        {
            std::shared_ptr<Type> selfTy =
                context->typeContext->createSelf(node->name, method->selfIsMut, method->selfIsRef);
            method->selfType = selfTy;
            methodParams.emplace_back("self", selfTy);
        }

        functionInfo.gParams.clear();

        for (auto &it : method->gParams)
        {
            traitGParams[it->getParamName()] = it;
        }

        for (auto &[pname, rawTy] : method->rawParams)
        {
            auto t = resolveType(rawTy, *node);
            method->params.emplace_back(pname, t);
            methodParams.emplace_back(pname, t);
        }

        std::shared_ptr<Type> retTy =
            method->hasReturnType
                ? resolveType(method->rawReturnType, *node)
                : context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

        method->returnType = retTy;

        TraitType::Method tm{
            method->name,
            node->name,
            std::move(methodParams),
            retTy,
            !method->hasSelf, // isStatic
            true};
        traitMethods.push_back(tm);
    }

    if (node->isGeneric)
        sym->type = context->typeContext->createGenericTrait(
            node->name,
            std::vector<std::shared_ptr<Type>>(node->gParams.begin(), node->gParams.end()),
            std::move(traitMethods));
    else
        sym->type = context->typeContext->createTrait(node->name, std::move(traitMethods));
    node->traitSymbol = sym;

    // Builtin operator traits: PRIMITIVES auto-implement them, so a generic
    // `fn max<T: Numeric>(...)` accepts i32/f64 and rejects structs/enums at the
    // call site (no LLVM crash on invalid ICmp). Declared in the stdlib like any
    // trait; the only difference is this seeding. bool is intentionally excluded
    // from Numeric (`true + false` would codegen an i1 add), but does implement
    // the equality/ordering traits (PartialEq/PartialOrd).
    //
    // Seeding rules per trait family:
    //   Numeric / Integer : ints + floats (+ char for Numeric)  [marker traits]
    //   Add Sub Mul Div Rem : ints + floats (returns Self)
    //   PartialEq PartialOrd : ints + floats + char + bool (returns bool)
    //   BitAnd ... Shr : ints only
    //   Copy : every primitive (a generic container needs `T: Copy` to be able
    //          to read an element through a reference at all — see
    //          Type::isCopyable).
    if (displayName(node->name) == "Numeric" || displayName(node->name) == "Integer"
        || displayName(node->name) == "Copy" || isOperatorTrait(displayName(node->name)))
    {
        auto traitTy = std::static_pointer_cast<TraitType>(sym->type);
        auto seed = [&](PrimitiveType::PrimKind k)
        {
            auto p = context->typeContext->getPrimitive(k);
            if (!p->implementsTrait(displayName(node->name)))
                p->implTrait.push_back(traitTy);
        };
        const std::string n = displayName(node->name);
        bool isNumeric = (n == "Numeric");
        bool isInteger = (n == "Integer");
        bool isCmp = (n == "PartialEq" || n == "PartialOrd");
        bool isArith = (n == "Add" || n == "Sub" || n == "Mul" || n == "Div" || n == "Rem");
        bool isBitwise = (n == "BitAnd" || n == "BitOr" || n == "BitXor" || n == "Shl" || n == "Shr");
        bool isCopyMarker = (n == "Copy");

        // ints get every family (Numeric/Integer markers + all operator traits).
        if (isNumeric || isInteger || isCmp || isArith || isBitwise || isCopyMarker)
        {
            seed(PrimitiveType::PrimKind::I8);
            seed(PrimitiveType::PrimKind::I16);
            seed(PrimitiveType::PrimKind::I32);
            seed(PrimitiveType::PrimKind::I64);
        }
        // floats get Numeric + arithmetic + comparison, NOT Integer / bitwise.
        if (isNumeric || isCmp || isArith || isCopyMarker)
        {
            seed(PrimitiveType::PrimKind::F32);
            seed(PrimitiveType::PrimKind::F64);
        }
        if (isNumeric || isCmp || isCopyMarker)
            seed(PrimitiveType::PrimKind::CHAR); // char compares via i32
        if (isCmp || isCopyMarker)
            seed(PrimitiveType::PrimKind::BOOL); // == / < work on bool
    }

    isInTraitMethod = false;
    traitName = "";
    traitGParams.clear();
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRImpl *node)
{
    auto *structSym = lookupModuleAware(node->structName);
    if (!structSym)
    {
        log(*node, "cannot find struct '" + node->structName + "'.");
        return;
    }
    if (structSym->kind != SymbolKind::Struct)
    {
        log(*node, "'" + node->structName + "' is not a struct.");
        return;
    }

    auto baseStruct = std::dynamic_pointer_cast<CustomType>(structSym->type);
    if (!baseStruct)
    {
        log(*node, "internal: struct '" + node->structName + "' has no resolved type.");
        return;
    }

    // For generic structs, adopt the struct's gParams positionally so methods can
    // reference T, U, ... and `self` resolves to Foo<T, U, ...>.
    structGParams.clear();
    std::shared_ptr<CustomType> selfTypeForMethods = baseStruct;

    if (baseStruct->isGeneric())
    {
        if (!node->gParams.empty() && node->gParams.size() != baseStruct->getGenericParams().size())
            log(*node, "impl generic param count does not match struct '" + node->structName + "'.");

        for (auto &gp : baseStruct->getGenericParams())
        {
            auto gpTy = std::static_pointer_cast<GenericParamType>(gp);
            structGParams[gpTy->getParamName()] = gpTy;
        }
        // The impl's own generic params (e.g. X in `impl<X> Iterator<X> for Range<X>`)
        // are also in scope when resolving the trait/struct generic args below.
        //
        // Their BOUNDS are resolved here, exactly like a function's or a struct's
        // (see visit(HIRFunction)/buildStructType): an impl may RE-declare the
        // struct's parameter to constrain it (`impl<T: Copy> Index<T> for Vec<T>`),
        // and the T its methods see must carry that bound — otherwise a body
        // reading an element is classified as a MOVE (E3017) and the call site has
        // nothing to check, so the bound was silently ignored.
        for (auto &gp : node->gParams)
        {
            std::vector<std::shared_ptr<TraitType>> traits;
            auto cIt = node->unsolveConstraints.find(gp->getParamName());
            if (cIt != node->unsolveConstraints.end())
                for (auto &con : cIt->second)
                    if (auto trait = resolveTraitConstraint(con, *node, /*silent=*/suppressTypeErrors_))
                        traits.push_back(trait);
            gp->updateContraints(std::move(traits));
            structGParams[gp->getParamName()] = gp;
        }
        // NOTE: selfTypeForMethods stays as baseStruct (the generic definition).
        // `self` inside methods has type Box (with T referring to structGParams).
        // At call sites, the Method branch in visit(HIRCall) substitutes T -> concrete.
    }

    currentStructType = selfTypeForMethods;

    std::shared_ptr<TraitType> traitType = nullptr;
    if (node->traitName.has_value())
    {
        const std::string &tn = node->traitName.value();
        // Numeric/Integer are PRIMITIVES-ONLY marker traits (the compiler seeds
        // them on i32/f64/...). A struct impl would make `max<box>` pass the
        // bound, but the generic body's `>` still monomorphizes to an ICmp on
        // the struct → LLVM crash. Operator overloading must go through the
        // per-operator traits (Add/Sub/PartialOrd/...), which ARE implementable.
        if (displayName(tn) == "Numeric" || displayName(tn) == "Integer")
        {
            log(*node, "builtin marker trait '" + displayName(tn) + "' is primitives-only and cannot be implemented by a struct; implement the operator traits (Add, Sub, ...) instead (operator overloading).");
            currentStructType = nullptr;
            structGParams.clear();
            return;
        }
        auto *traitSym = lookupModuleAware(tn);
        if (!traitSym || traitSym->kind != SymbolKind::Trait)
        {
            log(*node, "cannot find trait '" + tn + "'.");
            currentStructType = nullptr;
            structGParams.clear();
            return;
        }
        traitType = std::static_pointer_cast<TraitType>(traitSym->type);

        auto &impl = structSym->implementedTraits;
        if (std::find(impl.begin(), impl.end(), tn) != impl.end())
        {
            log(*node, "struct '" + node->structName + "' already implements trait '" + tn + "'.");
            currentStructType = nullptr;
            structGParams.clear();
            return;
        }
    }

    // For `impl Iterator<i32> for Range`, map the trait's generic params (T)
    // to the concrete args so the conformance check compares substituted
    // signatures (e.g. Option<i32>) against the impl methods.
    std::unordered_map<std::string, std::shared_ptr<Type>> traitSubst;
    std::vector<std::shared_ptr<Type>> traitArgs =
        traitType ? resolveTraitArgs(node, traitType, &traitSubst) : std::vector<std::shared_ptr<Type>>{};

    std::vector<CustomType::Method> methods;
    std::unordered_map<std::string, CustomType::Method> methodMap;

    for (auto &method : node->methods)
    {
        method->isTraitMethod = (traitType != nullptr);
        if (traitType)
            method->associatedTrait = node->traitName.value();

        visit(method.get());

        std::vector<CustomType::Field> paramFields;
        for (auto &[pname, ptype] : method->params)
            paramFields.emplace_back(pname, ptype);

        CustomType::Method cm{
            method->name,
            traitType ? node->traitName.value() : "",
            paramFields,
            method->returnType,
            method->isStatic,
            traitType != nullptr};
        methods.push_back(cm);
        methodMap[method->name] = cm;

        std::string funcName = node->structName + "::" + method->name;
        // Register the symbol with the type visit(HIRFunction) built — that is
        // generic-aware for methods of generic structs (their genericParams
        // include the struct's gParams). Rebuilding via getFunction here would
        // strip that genericity and break monomorphization.
        //
        // The symbol normally EXISTS already (pass 1c-3 pre-registered it), and
        // Scope::insert refuses duplicates, so the existing symbol must be
        // refreshed IN PLACE — re-inserting would silently keep the best-effort
        // signature the pre-pass wrote.
        auto funcType = method->type;
        if (auto *existing = SymbolTable::getInstance().lookupSymbol(funcName))
        {
            existing->kind = SymbolKind::Function;
            existing->type = funcType;
            method->funcSymbol = existing;
        }
        else
        {
            auto funcSym = std::make_unique<Symbol>();
            funcSym->kind = SymbolKind::Function;
            funcSym->name = funcName;
            funcSym->type = funcType;
            SymbolTable::getInstance().insertSymbol(funcName, std::move(funcSym));
            method->funcSymbol = SymbolTable::getInstance().lookupSymbol(funcName);
        }
    }

    // Methods always go on the origin (baseStruct), not the self instantiation.
    // UPSERT, not append: pass 1c-3 already attached best-effort signatures so
    // that method bodies could resolve their siblings, and these authoritative
    // ones replace them.
    baseStruct->upsertMethods(methods);

    // Trait conformance checks (unchanged from before, but using baseStruct for self comparison)
    if (traitType)
    {
        const std::string &tn = node->traitName.value();
        for (const auto &tm : traitType->getMethods())
        {
            auto it = methodMap.find(tm.name);
            if (it == methodMap.end())
            {
                log(*node, "struct '" + node->structName + "' does not implement trait method '" + tm.name + "' from '" + tn + "'.");
                continue;
            }
            const auto &sm = it->second;

            // Substitute the trait's generic params (e.g. T -> i32 in
            // Iterator<i32>) into the expected signature before comparing.
            // A `Self` return (operator traits write `-> Self`) maps to the
            // struct being implemented.
            std::shared_ptr<Type> expectedRet = substituteType(tm.returnType, traitSubst);
            if (expectedRet->getKind() == Type::Kind::Self)
            {
                auto selfTy = std::static_pointer_cast<SelfType>(expectedRet);
                std::shared_ptr<Type> base = selfTypeForMethods;
                if (selfTy->isReference())
                    base = context->typeContext->getReference(base, selfTy->isMutable());
                expectedRet = base;
            }
            if (!sm.returnType->equals(expectedRet))
                log(*node, "method '" + tm.name + "' return type mismatch: expected '" + expectedRet->toString() + "', got '" + sm.returnType->toString() + "'.");

            if (sm.params.size() != tm.params.size())
            {
                log(*node, "method '" + tm.name + "' param count mismatch.");
                continue;
            }

            for (size_t i = 0; i < tm.params.size(); ++i)
            {
                std::shared_ptr<Type> expected = substituteType(tm.params[i].type, traitSubst);
                if (expected->getKind() == Type::Kind::Self)
                {
                    auto selfTy = std::static_pointer_cast<SelfType>(expected);
                    std::shared_ptr<Type> base = selfTypeForMethods;
                    if (selfTy->isReference())
                        base = context->typeContext->getReference(base, selfTy->isMutable());
                    expected = base;
                }
                if (!sm.params[i].type->equals(expected))
                    log(*node, "method '" + tm.name + "' param " + std::to_string(i + 1) + " type mismatch.");
            }

            if (tm.isStatic != sm.isStatic)
                log(*node, "method '" + tm.name + "' static modifier mismatch.");
        }

        structSym->implementedTraits.push_back(tn);
        SymbolTable::getInstance().lookupSymbol(tn)->structsImplementing.push_back(node->structName);

        // NOTE: the struct's implTrait was already populated by the
        // preRegisterImplTrait pre-pass (order-independent). Nothing to push
        // here.
    }

    currentStructType = nullptr;
    structGParams.clear();
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::canAccessPrivateFieldsOf(const std::shared_ptr<CustomType> &type) const
{
    // Any method of the declaring type qualifies — including the STATIC ones,
    // which are how a type constructs itself (`String::new` builds the private
    // data/len/cap triple). A free function does not.
    if (!type || !currentStructType)
        return false;
    auto current = std::dynamic_pointer_cast<CustomType>(currentStructType);
    if (!current)
        return false;
    // Origin names match for a generic definition and all of its instantiations.
    return current->getOriginName() == type->getOriginName();
}

void HIRSemanticAnalyzer::checkFieldAccess(const CustomType::Field &field, const std::shared_ptr<CustomType> &type, HIRNode &errNode)
{
    if (field.isPublic || canAccessPrivateFieldsOf(type))
        return;
    log(errNode,
        "field '" + field.name + "' of '" + displayName(type->getOriginName()) + "' is private; add 'pub', or access it inside a method of that type.",
        E_PrivateFieldAccess);
}

void HIRSemanticAnalyzer::visit(HIRFunction *node)
{

    // Duplicate check for top-level functions (methods are checked by HIRImpl)
    if (!node->isMethod)
    {
        // Symbol was pre-registered; just verify it's not a re-definition coming
        // from user code (the pre-registration already caught it if it existed before).
    }

    auto funcScope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(funcScope);

    // Per-function NLL borrow state resets (activeBorrows_ is managed by the
    // block markers; the promoted/pending state must not leak across functions).
    stmtOrdinal_ = 0;
    promotedBorrows_.clear();
    holderLastUseStmt_.clear();
    pendingBorrowConflicts_.clear();
    sequenceTerminated_ = false; // per-function: reachability is not inherited
    loopBodyBreaks_.clear();

    functionInfo.isInFunction = true;
    functionInfo.gParams.clear();

    for (auto &it : node->gParams)
    {
        std::vector<std::shared_ptr<TraitType>> types;

        for (auto &con : node->unsolveConstraints[it->getParamName()])
        {
            if (auto trait = resolveTraitConstraint(con, *node, /*silent=*/false))
                types.push_back(trait);
        }

        it->updateContraints(std::move(types));

        functionInfo.gParams[it->getParamName()] = it;
    }

    // --- self param (methods only) ---
    if (node->isMethod && node->hasSelf && currentStructType)
    {
        std::shared_ptr<Type> selfTy;
        if (isInTraitMethod)
        {
            selfTy = context->typeContext->createSelf(traitName, node->selfIsMut, node->selfIsRef);
        }
        else
        {
            selfTy = currentStructType;
            if (node->selfIsRef)
                selfTy = context->typeContext->getReference(currentStructType, node->selfIsMut);
        }
        node->selfType = selfTy;
        node->params.emplace_back("self", selfTy);

        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Param;
        sym->name = "self";
        sym->type = selfTy;
        sym->isMutable = node->selfIsMut || !node->selfIsRef;
        SymbolTable::getInstance().insertSymbol("self", std::move(sym));
    }

    // --- regular params ---
    std::vector<std::shared_ptr<Type>> paramTypes; // for building FunctionType

    for (auto &[pname, rawTy] : node->rawParams)
    {
        if (bareNameConflicts(SymbolTable::getInstance().lookupSymbol(pname)))
            log(*node, "param '" + pname + "' shadows a previous definition.");

        auto resolvedTy = resolveType(rawTy, *node);
        if (!rawTy.isPresent)
            log(*node, "cannot deduce type for param '" + pname + "'.");
        if (resolvedTy && resolvedTy->getKind() == Type::Kind::Array)
        {
            log(*node, "array type cannot be a function parameter yet (pass a reference instead).");
            resolvedTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        // A bare generic type (`o: Option`) is the type DEFINITION: its fields
        // still contain the bare parameter, so there is no usable layout. Outside
        // a generic context (where mono substitutes it) report it instead of
        // letting codegen crash on the generic field.
        if (auto ct = std::dynamic_pointer_cast<CustomType>(resolvedTy); ct && ct->isGeneric() && !inGenericContext())
        {
            log(*node, "parameter '" + pname + "' has the generic type '" + displayName(ct->getName()) + "' without its argument(s); write them explicitly (e.g. 'Option<i32>').");
            resolvedTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }

        node->params.emplace_back(pname, resolvedTy);
        paramTypes.push_back(resolvedTy);

        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Param;
        sym->name = pname;
        sym->type = resolvedTy;
        SymbolTable::getInstance().insertSymbol(pname, std::move(sym));
    }

    // --- return type ---
    functionInfo.hasReturnValue = node->hasReturnType;
    functionInfo.declaredReturnType = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    if (node->hasReturnType)
    {
        node->returnType = resolveType(node->rawReturnType, *node);
        if (node->returnType && node->returnType->getKind() == Type::Kind::Array)
        {
            log(*node, "array type cannot be a function return type yet.");
            node->returnType = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        functionInfo.declaredReturnType = node->returnType;
    }
    else
    {
        node->returnType = functionInfo.declaredReturnType;
    }

    // --- body ---
    if (node->body)
        visit(node->body.get());

    // NLL: resolve deferred promoted-borrow conflicts now that every holder's
    // last use is known.
    resolvePromotedBorrows();

    functionInfo.isInFunction = false;

    // If return type was inferred from return statements, update node
    if (!node->hasReturnType)
        node->returnType = functionInfo.declaredReturnType;

    // Build FunctionType (self type is already in node->params for methods)
    // For the function symbol type we include all params (including self)
    std::vector<std::shared_ptr<Type>> allParamTypes;
    for (auto &[n, t] : node->params)
        allParamTypes.push_back(t);

    // Build the FunctionType. node->returnType is valid regardless of whether
    // it was explicitly declared (resolved above) or inferred from `ret` stmts.
    // Methods of a GENERIC struct are generic in the struct's gParams too —
    // MIRBuilder prepends them to the mono signature (MIRBuilder.cpp:548-569),
    // so mirror that order here (struct params first, deduped by name) to keep
    // the symbol's isGeneric() true and mono's positional substitution aligned.
    std::vector<std::shared_ptr<GenericParamType>> genericParams = node->gParams;
    auto structTy = std::dynamic_pointer_cast<CustomType>(currentStructType);
    bool methodOfGenericStruct = node->isMethod && structTy && structTy->isGeneric();
    if (methodOfGenericStruct)
    {
        std::vector<std::shared_ptr<GenericParamType>> structParams;
        for (auto &gp : structTy->getGenericParams())
        {
            auto gpTy = std::static_pointer_cast<GenericParamType>(gp);
            // A parameter the impl RE-declared with bounds is the one in scope
            // (visit(HIRImpl) put it into structGParams), and its bound is what a
            // call site must satisfy — the method's signature must carry THAT
            // param, not the struct's unconstrained one. This is what makes
            // `impl<T: Copy> Index<T> for Vec<T>` checkable at all.
            if (auto scopeIt = structGParams.find(gpTy->getParamName()); scopeIt != structGParams.end())
                if (!scopeIt->second->getConstraints().empty())
                    gpTy = scopeIt->second;
            bool present = false;
            for (const auto &existing : genericParams)
                if (existing->getParamName() == gpTy->getParamName())
                {
                    present = true;
                    break;
                }
            if (!present) structParams.push_back(gpTy);
        }
        genericParams.insert(genericParams.begin(), structParams.begin(), structParams.end());
    }

    if (node->isGeneric || methodOfGenericStruct)
    {
        node->type = context->typeContext->getGenericFunction(genericParams, allParamTypes, node->returnType);
    }
    else
    {
        node->type = context->typeContext->getFunction(allParamTypes, node->returnType);
    }

    // Update pre-registered symbol for top-level functions
    if (!node->isMethod)
    {
        if (auto *sym = SymbolTable::getInstance().lookupSymbol(node->name))
        {
            sym->type = node->type;
            node->funcSymbol = sym;
        }
    }

    SymbolTable::getInstance().exitScope();
}

// ============================================================
//  Statements
// ============================================================

void HIRSemanticAnalyzer::visit(HIRBlock *node)
{
    auto scope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(scope);
    node->scope = scope;

    // Block-scope borrow marker: all borrows created in this block (variable and
    // temporary) end when the block exits (`{ let r = &x; } x.v = 5;` is legal).
    blockBorrowMarkers_.push_back(activeBorrows_.size());

    // Reachability is tracked PER STATEMENT SEQUENCE: a `ret`/`break`/`continue`
    // (or a diverging call) inside this block does not make the ENCLOSING
    // sequence unreachable, so the flag is saved on entry and OR-ed back out.
    // It only suppresses definite-assignment diagnostics in dead code.
    bool savedTerminated = sequenceTerminated_;
    sequenceTerminated_ = false;
    for (auto &stmt : node->stmts)
        analyzeStmt(stmt.get());
    bool blockTerminated = sequenceTerminated_;
    sequenceTerminated_ = savedTerminated || blockTerminated;

    activeBorrows_.resize(blockBorrowMarkers_.back());
    blockBorrowMarkers_.pop_back();

    SymbolTable::getInstance().exitScope();
}

// ---------------------------------------------------------------------------
// Borrow-checker helpers (Stage 1: lexical lifetimes)
// ---------------------------------------------------------------------------

bool HIRSemanticAnalyzer::pathsOverlap(const std::vector<std::string> &a,
    const std::vector<std::string> &b)
{
    // One place is a prefix of the other (`a` overlaps `a[0]`, `a[0]` overlaps
    // `a[0].f`). Each shared segment is compared by pathSegmentOverlaps, so the
    // index wildcard `[*]` still matches a concrete `[0]`, while two DIFFERENT
    // constant indices are correctly disjoint.
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (!pathSegmentOverlaps(a[i], b[i])) return false;
    return true;
}

void HIRSemanticAnalyzer::borrowConflictInfo(std::string &msg, size_t &errorId, const std::string &name, BorrowUseKind kind)
{
    switch (kind)
    {
    case BorrowUseKind::BorrowMut:
        msg = "cannot borrow '" + name + "' as mutable because it is already borrowed";
        errorId = E_CannotBorrowMutWhileBorrowed;
        break;
    case BorrowUseKind::BorrowShared:
        msg = "cannot borrow '" + name + "' because it is already borrowed as mutable";
        errorId = E_CannotBorrowWhileMutBorrowed;
        break;
    case BorrowUseKind::Write:
        msg = "cannot assign to '" + name + "' because it is borrowed";
        errorId = E_CannotMutateWhileBorrowed;
        break;
    case BorrowUseKind::Move:
        msg = "cannot move out of '" + name + "' because it is borrowed";
        errorId = E_CannotMoveWhileBorrowed;
        break;
    case BorrowUseKind::Read:
        msg = "cannot read '" + name + "' because it is borrowed as mutable";
        errorId = E_CannotBorrowWhileMutBorrowed;
        break;
    }
}

bool HIRSemanticAnalyzer::checkBorrowUse(const std::string &root,
    const std::vector<std::string> &path,
    BorrowUseKind kind,
    HIRNode &errNode,
    const std::vector<std::string> *skipHolders)
{
    // In `mir` mode the borrow checks are MIRBorrowCheck's job (see the flag).
    if (mirBorrowCheck_) return true;
    if (activeBorrows_.empty()) return true;
    bool ok = true;
    std::string name = placeName(root, path);

    for (const auto &b : activeBorrows_)
    {
        if (b.root != root || !pathsOverlap(b.path, path)) continue;
        // A reference is never blocked by the borrows of its own derivation
        // chain: the place it points at is exactly what those borrows granted it
        // (see exemptionChain).
        if (skipHolders && !skipHolders->empty()
            && std::find(skipHolders->begin(), skipHolders->end(), b.holderName) != skipHolders->end())
            continue;

        bool conflict = false;
        switch (kind)
        {
        case BorrowUseKind::BorrowMut: conflict = true; break;       // any borrow blocks &mut
        case BorrowUseKind::BorrowShared: conflict = b.isMut; break; // &mut blocks &; & allows &
        case BorrowUseKind::Write: conflict = true; break;           // any borrow blocks write
        case BorrowUseKind::Move: conflict = true; break;            // any borrow blocks move
        case BorrowUseKind::Read: conflict = b.isMut; break;         // &mut blocks read; & allows
        }

        // TWO-PHASE BORROW: a &mut borrow taken FOR A CALL (a method receiver or
        // a reference argument) is only RESERVED while the rest of the call is
        // still being evaluated. The callee has not touched the place yet, so a
        // read or a shared borrow of it in a sibling argument is fine:
        //     s.set(s.n + 5)                   code.patch(jz, code.count)
        //     vm_push(self, self.vars[arg])    f(&mut x, x.n)
        // Writes and moves are NOT relaxed (the reservation is exclusive once the
        // callee starts): x.m(x) is still rejected, and so is a second &mut in
        // another argument. The reservation dies with the statement (it is a
        // temporary borrow), so nothing outside the statement changes.
        if (conflict && b.isTwoPhase && b.isMut
            && (kind == BorrowUseKind::Read || kind == BorrowUseKind::BorrowShared))
            conflict = false;

        if (conflict)
        {
            // NLL: a conflict against a PROMOTED (variable) borrow is deferred to
            // the end of the function — the borrow may have already ended at its
            // holder's last use before this use. Moves stay inline (conservative:
            // moving a borrowed place would leave the reference dangling).
            if (b.isPromoted && kind != BorrowUseKind::Move)
            {
                pendingBorrowConflicts_.push_back(
                    PendingConflict{root, path, kind, stmtOrdinal_, errNode.position, errNode.length,
                        skipHolders ? *skipHolders : std::vector<std::string>{}});
            }
            else
            {
                std::string msg;
                size_t errId = E_SemanticError;
                borrowConflictInfo(msg, errId, name, kind);
                log(errNode, msg, errId);
            }
            ok = false;
        }
    }
    return ok;
}

void HIRSemanticAnalyzer::logAtPosition(const SourcePosition &pos, size_t length, const std::string &msg, size_t errorId)
{
    Logger::LogInfo info{};
    info.codePath = currentFilePath_.empty() ? context->filePath : currentFilePath_;
    info.code = &context->fileValue;
    info.col = pos.col;
    info.line = pos.line;
    info.length = length;
    info.beginPosition = pos.lineStart;
    info.msg = msg;
    info.errorId = errorId;
    info.exit = false; // non-fatal — the sema reports all errors, then gates.
    Logger::Log(Logger::LogLevel::ERROR, info);
}

void HIRSemanticAnalyzer::resolvePromotedBorrows()
{
    if (mirBorrowCheck_)
    {
        pendingBorrowConflicts_.clear();
        return;
    }
    for (const auto &pc : pendingBorrowConflicts_)
    {
        for (const auto &b : promotedBorrows_)
        {
            if (b.root != pc.root || !pathsOverlap(b.path, pc.path)) continue;
            // Same exemption the inline check applied (see checkBorrowUse).
            if (!pc.skipHolders.empty()
                && std::find(pc.skipHolders.begin(), pc.skipHolders.end(), b.holderName) != pc.skipHolders.end())
                continue;

            // The borrow is live at the conflicting use iff it was created at or
            // before that statement AND its holder is still possibly used there.
            size_t lastUse = b.createStmt;
            auto it = holderLastUseStmt_.find(b.holderName);
            if (it != holderLastUseStmt_.end()) lastUse = it->second;
            // A borrow is live at the conflicting use iff it was created BEFORE
            // that statement (a borrow created AT the same ordinal is the very
            // access being checked, not a pre-existing conflicting borrow) and
            // its holder is still possibly used there.
            bool live = b.createStmt < pc.ordinal && pc.ordinal <= lastUse;

            if (live)
            {
                std::string name = placeName(pc.root, pc.path);
                std::string msg;
                size_t errId = E_SemanticError;
                borrowConflictInfo(msg, errId, name, pc.kind);
                logAtPosition(pc.pos, pc.length, msg, errId);
                break;
            }
        }
    }
    pendingBorrowConflicts_.clear();
}

bool HIRSemanticAnalyzer::registerBorrow(const std::string &root,
    const std::vector<std::string> &path,
    bool isMut,
    bool isPromoted,
    HIRNode &errNode,
    bool isTwoPhase,
    const std::vector<std::string> *skipHolders)
{
    if (mirBorrowCheck_) return true; // MIRBorrowCheck owns this in `mir` mode
    if (!checkBorrowUse(root, path, isMut ? BorrowUseKind::BorrowMut : BorrowUseKind::BorrowShared, errNode, skipHolders))
        return false;
    activeBorrows_.push_back(Borrow{root, "", path, isMut, isPromoted, 0, errNode.position, isTwoPhase});
    return true;
}

void HIRSemanticAnalyzer::recordAlias(const std::string &holder, HIRExpr *init)
{
    if (!init)
    {
        aliasOf_.erase(holder);
        return;
    }

    // `let r = &mut <place>;` — remember the referent, resolved through any
    // alias in between, so a chain (`let a = &mut x; let b = &mut *a;`) still
    // points at `x`.
    if (auto *ref = dynamic_cast<HIRRef *>(init))
    {
        std::string root;
        std::vector<std::string> path;
        if (resolvePlace(ref->expr.get(), root, path))
        {
            aliasOf_[holder] = {std::move(root), std::move(path)};

            // `&mut *r` also REBORROWS through r: remember that, because an
            // access through the new reference has to be exempt from r's own
            // borrow of the place as well (see exemptionChain).
            std::string hroot;
            std::vector<std::string> hpath;
            if (derefHolderOf(ref->expr.get(), hroot, hpath) && hpath.empty())
                aliasParent_[holder] = std::move(hroot);
            else
                aliasParent_.erase(holder);
            return;
        }
    }

    // `let q = r;` / `q = r;` — a reference moved (or shared-copied) from another
    // binding keeps the same referent.
    if (auto *nr = dynamic_cast<HIRNameRef *>(init))
    {
        auto it = aliasOf_.find(nr->name);
        if (it != aliasOf_.end())
        {
            aliasOf_[holder] = it->second;
            // The copy takes over the source's role; the walk in exemptionChain
            // follows the source's own chain from here.
            aliasParent_[holder] = nr->name;
            return;
        }
    }

    // Anything else (`r = other_ref_param;`, a conditional, ...) leaves the
    // referent unknown: drop the entries so a stale target is never used.
    aliasOf_.erase(holder);
    aliasParent_.erase(holder);
}

std::vector<std::string> HIRSemanticAnalyzer::exemptionChain(const std::string &holder)
{
    std::vector<std::string> chain;
    if (holder.empty()) return chain;
    chain.push_back(holder);

    std::string cur = holder;
    for (size_t guard = 0; guard < 32; ++guard)
    {
        auto it = aliasParent_.find(cur);
        if (it == aliasParent_.end() || it->second.empty()) break;
        cur = it->second;
        if (std::find(chain.begin(), chain.end(), cur) != chain.end()) break; // cycle guard
        chain.push_back(cur);
    }
    return chain;
}

bool HIRSemanticAnalyzer::derefHolderOf(HIRExpr *expr,
    std::string &root,
    std::vector<std::string> &path)
{
    std::string sroot;
    std::vector<std::string> spath;
    if (!extractRootAndPath(expr, sroot, spath)) return false;

    for (size_t i = 0; i < spath.size(); ++i)
        if (spath[i] == kDeref)
        {
            root = std::move(sroot);
            path.assign(spath.begin(), spath.begin() + i);
            return true;
        }
    return false;
}

bool HIRSemanticAnalyzer::resolvePlace(HIRExpr *expr,
    std::string &root,
    std::vector<std::string> &path)
{
    if (!extractRootAndPath(expr, root, path)) return false;

    // Rewrite a leading deref into the place the holder was borrowed from:
    // `*r` (with `r = &mut x`) IS `x`, and `(*r).a` is `x.a`. A reference with no
    // known referent (a parameter, or a binding whose value was overwritten)
    // keeps the deref segment, which still gives every `*p` of the same shape one
    // shared identity.
    for (size_t guard = 0; guard < 32; ++guard)
    {
        if (path.empty() || path.front() != kDeref) break;
        auto it = aliasOf_.find(root);
        if (it == aliasOf_.end()) break;

        std::vector<std::string> resolved = it->second.second;
        resolved.insert(resolved.end(), path.begin() + 1, path.end());
        root = it->second.first;
        path = std::move(resolved);
    }
    return true;
}

void HIRSemanticAnalyzer::endTemporaryBorrowsSince(size_t marker)
{
    // Remove temporary (non-promoted) borrows created after `marker`.
    for (size_t i = activeBorrows_.size(); i > marker; --i)
        if (!activeBorrows_[i - 1].isPromoted)
            activeBorrows_.erase(activeBorrows_.begin() + (i - 1));
}

// ---------------------------------------------------------------------------
// Stage 3: dangling / escape analysis (RefOrigin).
//
// A reference is dangling after this function returns iff it (transitively)
// points into this function's stack frame. Everything on the Symbol that was
// populated during analysis is LOCAL-only; params and globals are derived on
// demand from SymbolKind.
// ---------------------------------------------------------------------------

bool HIRSemanticAnalyzer::isReferenceType(const std::shared_ptr<Type> &ty)
{
    if (!ty) return false;
    if (ty->getKind() == Type::Kind::Reference) return true;
    // Defensive: trait-method self params are represented as SelfType.
    if (auto st = std::dynamic_pointer_cast<SelfType>(ty))
        return st->isReference();
    return false;
}

bool HIRSemanticAnalyzer::structHasRefFields(const std::shared_ptr<Type> &ty)
{
    auto ct = std::dynamic_pointer_cast<CustomType>(ty);
    if (!ct) return false;
    for (const auto &f : ct->getFields())
        if (isReferenceType(f.type)) return true;
    return false;
}
// ---------------------------------------------------------------------------
// Move semantics of consuming `source` (whole variable or field path).
// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::tryReborrowArg(HIRExpr *arg, const std::shared_ptr<Type> &paramTy, HIRNode &errNode)
{
    auto paramRef = std::dynamic_pointer_cast<ReferenceType>(paramTy);
    if (!paramRef || !arg || !arg->type)
        return false;
    // Only references reborrow. A raw pointer is Copy and is not tracked by the
    // borrow checker at all, so there is nothing to register for it.
    if (!isReferenceType(arg->type))
        return false;
    std::string root;
    std::vector<std::string> path;
    if (!extractRootAndPath(arg, root, path))
        return false; // a temporary value: nothing to reuse, a move is fine
    if (!SymbolTable::getInstance().lookupSymbol(root))
        return false;
    // Register the temporary borrow against the argument's own root, exactly as
    // the method-receiver path does: the callee's access ends when the call
    // returns, so the borrow dies with the call and the binding stays usable.
    // (Like the rest of the borrow checker this is deliberately permissive: a
    // conflict routed through a *different* alias of the same referent is missed
    // rather than falsely rejected.)
    // A TWO-PHASE reservation: the callee has not started using the referent
    // while the remaining arguments are still being evaluated, so a sibling
    // argument may still READ it (`f(&mut x, x.n)`).
    registerBorrow(root, path, paramRef->isMutableRef(), /*isPromoted=*/false, errNode, /*isTwoPhase=*/true);
    return true;
}

// ───────────────────────────────────────────────────────────────────────────
// checkCallArgs — the shared argument type-check for every call form
// ───────────────────────────────────────────────────────────────────────────
//
// The four call paths (free function, instance method, static method, trait
// method) each used to carry their own copy of this loop, and the copies had
// drifted apart: one guarded the argument type before comparing, one skipped
// the reborrow for non-place arguments, one indexed parameters with a +1
// offset without saying so. One helper, one rule set.

void HIRSemanticAnalyzer::checkCallArgs(
    const std::vector<std::unique_ptr<HIRExpr>> &args,
    const std::vector<std::shared_ptr<Type>> &params,
    size_t paramOffset,
    HIRCall &call,
    bool explainUninferredGeneric)
{
    for (size_t i = 0; i < args.size() && i + paramOffset < params.size(); ++i)
    {
        analyzeExpr(args[i].get());
        const std::shared_ptr<Type> &paramTy = params[i + paramOffset];

        // A context-free generic value (`get(Option::None)` where the parameter
        // is Option<i32>) keeps the type DEFINITION; explain the real problem
        // instead of a bare mismatch (and instead of letting a generic-param
        // field reach codegen). Only where monomorphization will NOT repair it:
        // a monomorphized call carries typedGenericParams, and
        // MIRMonomorphization rewrites both the argument operand AND its carrier
        // local from the definition to the instantiation (that is why
        // `unwrap_or(Option::None, 0)` works).
        if (explainUninferredGeneric && call.typedGenericParams.empty() && paramTy
            && args[i]->type && isUninferredGenericDefinition(paramTy, args[i]->type))
            log(*args[i], "cannot infer the generic argument(s) of '" + displayName(std::static_pointer_cast<CustomType>(args[i]->type)->getOriginName()) + "'; write them explicitly (e.g. 'Option<i32>::None').");
        else if (!typesCompatible(paramTy, args[i]->type))
            log(*args[i], "argument type mismatch.");

        // A by-value non-Copy argument consumes its source (`foo(p)` moves p); a
        // reference argument to a reference parameter is reborrowed instead.
        if (!tryReborrowArg(args[i].get(), paramTy, call))
            handleMoveSource(args[i].get(), call);
    }
}
std::vector<std::shared_ptr<Type>> HIRSemanticAnalyzer::moveOutContainers(HIRExpr *source)
{
    // Walk the member-access chain root-first: for `s.a.b` the chain is
    // [s.a, s.a.b], so chain[i-1]->type is the type of the place chain[i]
    // projects out of.
    std::vector<HIRMemberAccess *> chain;
    for (HIRExpr *cur = source; cur != nullptr;)
    {
        auto *ma = dynamic_cast<HIRMemberAccess *>(cur);
        if (!ma)
            break;
        chain.push_back(ma);
        cur = ma->object.get();
    }
    if (chain.empty())
        return {};
    std::reverse(chain.begin(), chain.end());

    std::vector<std::shared_ptr<Type>> containers;
    containers.push_back(chain[0]->object->type);
    // `(*p).f`: the chain root is an explicit DEREF, so the place is reached
    // through the reference `p`. The deref node itself carries the POINTEE type;
    // push the reference type too, so referenceMovedOutOf() sees the borrow and
    // a non-Copy field move is E3017 — the same rule `p.f` already gets from the
    // root binding's reference type.
    if (auto *d = dynamic_cast<HIRDeref *>(chain[0]->object.get()))
        if (d->operand->type) containers.push_back(d->operand->type);
    for (size_t i = 0; i + 1 < chain.size(); ++i)
        containers.push_back(chain[i]->type);
    return containers;
}

std::shared_ptr<ReferenceType> HIRSemanticAnalyzer::referenceMovedOutOf(HIRExpr *source)
{
    for (const auto &ty : moveOutContainers(source))
        if (auto ref = std::dynamic_pointer_cast<ReferenceType>(ty))
            return ref;
    return nullptr;
}

std::shared_ptr<CustomType> HIRSemanticAnalyzer::dropTypePartiallyMovedBy(HIRExpr *source)
{
    for (const auto &ty : moveOutContainers(source))
    {
        // Look THROUGH references: `r.a` where `r: &mut P` is rejected as a move
        // out of a borrow (referenceMovedOutOf) before this rule is consulted,
        // but `r.a.b` must still see the type behind `r.a` when that field is
        // itself a reference.
        std::shared_ptr<Type> base = ty;
        while (auto ref = std::dynamic_pointer_cast<ReferenceType>(base))
            base = ref->getBaseType();
        auto ct = std::dynamic_pointer_cast<CustomType>(base);
        if (ct && ct->implementsTrait("Drop"))
            return ct;
    }
    return nullptr;
}

void HIRSemanticAnalyzer::handleMoveSource(HIRExpr *source, HIRNode &errNode)
{
    // The move/ownership rules are re-implemented on the MIR CFG (see the flag).
    if (mirBorrowCheck_) return;

    // A source whose analysis FAILED has no type, so it is not a move of
    // anything: the real error was already reported, and the bookkeeping below
    // would only manufacture follow-on diagnostics. `let x = s.nope;` used to
    // mark the path `s.nope` as moved, so the next `s.nope` reported "use of
    // moved value" on top of "struct 'S' has no field 'nope'".
    if (!source || !source->type)
        return;

    // Borrow-check: moving a place that is currently borrowed would leave the
    // borrow dangling — reject it. Only genuinely non-Copy sources are moved;
    // a Copy source is a read (handled by the NameRef/MemberAccess read check
    // with NLL liveness).
    std::string root;
    std::vector<std::string> path;
    if (resolvePlace(source, root, path)
        && source->type && !source->type->isCopyable())
        checkBorrowUse(root, path, BorrowUseKind::Move, errNode);

    // Whole-variable use.
    if (auto *nameRef = dynamic_cast<HIRNameRef *>(source))
    {
        auto *sym = SymbolTable::getInstance().lookupSymbol(nameRef->name);
        if (!sym) return;
        // A whole-value use after ANY field moved out is a double-free
        // (the receiver's whole-struct drop would also drop the moved field).
        if (!sym->movedFields.empty())
        {
            log(errNode, "use of moved value: '" + nameRef->name + "' (partially moved)", E_UseOfMovedValue);
            return;
        }
        if (sym->state == VarState::Moved)
            log(errNode, "use of moved value: '" + nameRef->name + "'", E_UseOfMovedValue);
        if (sym->type && !sym->type->isCopyable())
            sym->state = VarState::Moved;
        return;
    }

    // Whole-value read THROUGH a deref: `let x = *p;`. A Copy pointee is an
    // ordinary read (the branch above does its bookkeeping); a non-Copy one
    // would hand the referent's value to the receiver while its owner still
    // releases it — the same E3017 as moving a field out of a borrow.
    if (auto *d = dynamic_cast<HIRDeref *>(source))
    {
        if (d->type && !d->type->isCopyable())
            log(errNode,
                "cannot move out of a reference: '*p' only borrows the value, so the referent still owns it.",
                E_MoveOutOfReference);
        return;
    }

    // Partial (field) move: `let x = p.a` where a is non-Copy.
    if (auto *ma = dynamic_cast<HIRMemberAccess *>(source))
    {
        std::string root;
        std::vector<std::string> path;
        if (!extractRootAndPath(ma, root, path)) return;
        auto *sym = SymbolTable::getInstance().lookupSymbol(root);
        if (!sym) return;

        if (sym->state == VarState::Moved)
        {
            log(errNode, "use of moved value: '" + root + "." + joinPath(path) + "'", E_UseOfMovedValue);
            return;
        }
        // Copy fields never move (mirrors MIR's isCopyType). A copy is a read:
        // nothing is left half-initialized, so the Drop rule below cannot
        // apply to it either (Rust allows `let n = p.count;` on a Drop type).
        if (ma->type && ma->type->isCopyable()) return;

        // E0507: a field cannot be moved out of a place we only BORROW. The
        // borrow owns nothing, so the value would be handed to the receiver
        // while the referent keeps releasing it — two owners of one buffer.
        // Measured before this rule: `let x = r.s;` with `r: &mut P` and
        // `s: String` corrupted the heap (0xC0000374) on exit.
        if (auto refTy = referenceMovedOutOf(source))
        {
            log(*ma,
                "cannot move out of '" + root + "." + joinPath(path)
                    + "': it is behind the reference '" + refTy->toString()
                    + "', so the value is only borrowed here and the referent still owns it.",
                E_MoveOutOfReference);
            return;
        }

        // E0509: a non-Copy field may not leave a value whose type implements
        // Drop. The destructor releases that type's fields as a whole, so the
        // partially-initialized value it would be handed is not a value it can
        // legally touch — and the field cannot be released twice. Rust rejects
        // the same move; whole-value moves are unaffected (nothing is left
        // behind).
        if (auto dropOwner = dropTypePartiallyMovedBy(source))
        {
            log(*ma,
                "cannot move out of '" + displayName(dropOwner->getName())
                    + "': the type implements Drop, so its fields are released together by its own destructor.",
                E_MoveOutOfDropType);
            return;
        }

        // Reject re-reading a moved field or an ancestor of it.
        for (const auto &existing : sym->movedFields)
        {
            bool existingIsPrefix = existing.size() <= path.size()
                                    && std::equal(existing.begin(), existing.end(), path.begin());
            bool pathIsPrefix = path.size() <= existing.size()
                                && std::equal(path.begin(), path.end(), existing.begin());
            if (existingIsPrefix || pathIsPrefix)
            {
                log(errNode, "use of moved value: '" + root + "." + joinPath(path) + "'", E_UseOfMovedValue);
                return;
            }
        }
        sym->movedFields.push_back(path);
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRVarDecl *node)
{
    // Variable shadowing is deliberately disallowed (design principle: a name
    // refers to exactly one binding anywhere in scope). This also rejects a
    // loop variable shadowing an outer binding, which is intended.
    //
    // A module-level `let` was pre-registered by the name pass (see
    // preRegister): finding THAT slot is not a redefinition, it is this very
    // declaration, so the check skips it and the symbol is updated in place
    // below (re-inserting would silently keep the provisional type, because
    // Scope::insert refuses duplicates).
    Symbol *preRegisteredGlobal = nullptr;
    if (auto *existing = SymbolTable::getInstance().lookupSymbol(node->name))
    {
        if (preRegisteredGlobals_.count(node))
            preRegisteredGlobal = existing; // this very declaration's slot
        else if (bareNameConflicts(existing))
            log(*node, "variable '" + node->name + "' already exists in this scope.");
    }

    std::shared_ptr<Type> initType;

    // True when the initializer's OWN analysis failed (its type is null —
    // the file's error convention, see visit(HIRMemberAccess)). The
    // diagnostics below are then restatements of that one real error, so
    // they are suppressed: `let x = s.nope;` used to add "cannot infer type
    // for 'x'" and then a bogus "cannot have type 'void'" on top of the
    // actual "struct 'S' has no field 'nope'".
    bool initFailed = false;

    if (node->init.has_value())
    {
        analyzeExpr(node->init.value().get());
        initType = node->init.value()->type;
        initFailed = !initType;

        // ── `let x = <Option/Result> else <expr>;` (2026-09-19) ──────────────
        // The binding takes the PAYLOAD type; the else expression supplies the
        // value for the None/Err path, or DIVERGES (`else panic("...")` — never
        // is compatible with anything). Only Option and Result are recognised,
        // exactly like the `?` operator.
        if (node->elseExpr.has_value())
        {
            if (node->isGlobal)
            {
                log(*node, "'let ... else' is not allowed at global scope (a global initializer must be a literal).");
            }
            else if (initType)
            {
                auto ct = std::dynamic_pointer_cast<CustomType>(initType);
                std::string origin = ct ? ct->getOriginName() : std::string();
                const char *payloadVariant = origin == "option$Option"  ? "Some"
                                             : origin == "result$Result" ? "Ok"
                                                                         : nullptr;
                if (!payloadVariant)
                {
                    log(*node, "'let ... else' requires an 'Option' or 'Result' initializer, got '" + initType->toString() + "'.");
                }
                else
                {
                    std::shared_ptr<Type> payloadTy;
                    for (const auto &v : ct->getVariants())
                        if (v.name == payloadVariant && !v.payloadTypes.empty())
                            payloadTy = v.payloadTypes[0];

                    if (!payloadTy)
                    {
                        log(*node, "'let ... else' requires '" + std::string(payloadVariant) + "(T)' in the initializer's type.");
                    }
                    else
                    {
                        analyzeExpr(node->elseExpr.value().get());
                        auto elseTy = node->elseExpr.value()->type;
                        if (elseTy && !isNever(elseTy) && !payloadTy->equals(elseTy))
                            log(*node->elseExpr.value(), "the fallback of 'let ... else' must have type '" + payloadTy->toString() + "', got '" + elseTy->toString() + "'.");

                        // The binding is the PAYLOAD, not the enum.
                        initType = payloadTy;
                    }
                }
            }
        }

        // Globals live in static storage: only a literal initializer can be
        // embedded in the object file. Anything else (a call, an array built at
        // runtime, a reference) would silently lower to zeroed memory — reject
        // it rather than miscompile.
        if (node->isGlobal && !dynamic_cast<HIRLiteral *>(node->init.value().get()))
        {
            log(*node, "global variable initializer must be a literal (not '" + (initType ? initType->toString() : std::string("?")) + "').");
        }

        // Move semantics check (whole-variable or field-path move source).
        handleMoveSource(node->init.value().get(), *node);

        // Borrow promotion: `let r = &p` binds the borrow to the variable r, so
        // it survives the statement. Under NLL its liveness runs to r's last use;
        // record the holder + creation ordinal and keep a function-scoped copy.
        if (auto *ref = dynamic_cast<HIRRef *>(node->init.value().get()))
        {
            // Everything the initialiser registered, not just the last entry: a
            // borrow through a deref also freezes the pointer it went through
            // (HIRRef::borrowMark).
            for (size_t i = ref->borrowMark; i < activeBorrows_.size(); ++i)
            {
                activeBorrows_[i].isPromoted = true;
                activeBorrows_[i].holderName = node->name;
                activeBorrows_[i].createStmt = stmtOrdinal_;
                promotedBorrows_.push_back(activeBorrows_[i]);
            }
        }
    }

    // Keep the alias table in step with this binding: `let r = &mut x;` records
    // r -> x (so `*r` denotes `x`), `let q = r;` copies the entry, and anything
    // else leaves the referent unknown (see recordAlias).
    recordAlias(node->name, node->init.has_value() ? node->init.value().get() : nullptr);

    if (node->hasExplicitType)
    {
        node->type = resolveType(node->rawType, *node);
        // A never-valued initialiser is compatible with any declared type
        // (bottom): the value cannot exist, so nothing about the declared type
        // is violated. Everything else still needs an exact match.
        if (initType && !initType->equals(node->type) && !isNever(initType))
        {
            if (isUninferredGenericDefinition(node->type, initType))
                log(*node, "cannot infer the generic argument(s) of '" + displayName(std::static_pointer_cast<CustomType>(initType)->getOriginName()) + "' for '" + node->name + "'; write them explicitly (e.g. 'Option<i32>::None').", E_TypeMismatch);
            else
                log(*node, "type mismatch in variable declaration.", E_TypeMismatch);
        }
    }
    else
    {
        if (!initType)
        {
            // Only a declaration with no initializer at all is a genuine
            // cannot-infer; a failed initializer was already reported.
            if (!initFailed)
                log(*node, "cannot infer type for '" + node->name + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        else
        {
            // A context-free generic value (`let n = Option::None;`) infers the
            // type DEFINITION (its fields still contain the bare T), which has no
            // usable layout — reject it here rather than crash in codegen. Inside
            // a generic context monomorphization substitutes the parameters, so
            // the same spelling is fine there.
            auto ct = std::dynamic_pointer_cast<CustomType>(initType);
            if (ct && ct->isGeneric() && !inGenericContext())
                log(*node, "cannot infer the generic argument(s) of '" + displayName(ct->getName()) + "' for '" + node->name + "'; write them explicitly (e.g. 'Option<i32>::None') or annotate the variable.", E_TypeMismatch);
            node->type = initType;
        }
    }

    // `void` is a NO-VALUE type (the manual scopes it to function returns), so a
    // void-typed binding could never be read: `let x = side_effect();` where the
    // callee returns void used to reach codegen as a load of a valueless place.
    // Report it as the semantic error it is. (`never` is deliberately NOT
    // rejected here — the uninhabited type is allowed on a binding; reading it
    // is what visit(HIRNameRef) rejects.)
    if (!initFailed && node->type
        && node->type->equals(context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID)))
    {
        log(*node, "variable '" + node->name + "' cannot have type 'void': void has no value.", E_TypeMismatch);
    }

    // Definite assignment: a declaration WITHOUT an initializer is the only
    // source of a maybe-uninitialized binding. It is allowed for Copy types
    // (every use is checked), but NOT for Move types: such a binding owns a
    // value that the scope-exit drop glue would release although it was never
    // constructed. Reporting here (rather than tracking drop points) is what
    // keeps the analysis small.
    bool initialized = node->init.has_value();
    if (!initialized && node->type && !node->type->isCopyable())
    {
        log(*node,
            "cannot declare '" + node->name + "' without an initializer: '" + node->type->toString() + "' is not a Copy type, so the binding could be released while uninitialized.",
            E_UninitializedNonCopyBinding);
        initialized = true; // reported once; avoid cascading uninitialized-use errors
    }

    if (preRegisteredGlobal)
    {
        // Refresh the slot the name pass created: same object, authoritative type.
        preRegisteredGlobal->kind = SymbolKind::GlobalVar;
        preRegisteredGlobal->type = node->type;
        preRegisteredGlobal->isMutable = node->isMutable;
        preRegisteredGlobal->initialized = initialized;
        node->varSymbol = preRegisteredGlobal;
    }
    else
    {
        auto sym = std::make_unique<Symbol>();
        sym->kind = node->isGlobal ? SymbolKind::GlobalVar : SymbolKind::LocalVar;
        sym->name = node->name;
        sym->type = node->type;
        sym->isMutable = node->isMutable;
        sym->initialized = initialized;
        SymbolTable::getInstance().insertSymbol(node->name, std::move(sym));

        node->varSymbol = SymbolTable::getInstance().lookupSymbol(node->name);
    }

}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRAssign *node)
{
    // The write TARGET is not a read: `let x: i32; x = 1;` must not be reported as
    // a use of an uninitialized value (the assignment is what initializes it).
    bool savedInAssignTarget = inAssignTarget_;
    bool savedInCompoundTarget = inCompoundAssignTarget_;
    inAssignTarget_ = true;
    inCompoundAssignTarget_ = node->isCompound;
    analyzeExpr(node->target.get());
    inAssignTarget_ = savedInAssignTarget;
    inCompoundAssignTarget_ = savedInCompoundTarget;
    analyzeExpr(node->value.get());

    // `x op= y` (2026-09-19) follows exactly the operand rules of `x op y`,
    // minus the operator-trait method call: the MIR lowers it as a read of the
    // target place, the primitive binary op, and a write back — so only operands
    // that lower to a PRIMITIVE binary op are accepted here (integers, floats,
    // and a generic parameter carrying the Numeric/Integer marker or the
    // operator trait bound, which monomorphization resolves to one of those).
    if (node->isCompound)
    {
        const std::string op = std::string("'") + node->compoundOpToString() + "'";
        const auto &lhsTy = node->target->type;
        const auto &rhsTy = node->value->type;

        if (lhsTy && rhsTy && !lhsTy->equals(rhsTy))
            log(*node, "operands of " + op + " must have the same type.");

        if (lhsTy)
        {
            const char *opTrait = operatorTraitName(node->compoundOp);
            if (auto p = std::dynamic_pointer_cast<PrimitiveType>(lhsTy))
            {
                if (!p->isInteger() && !p->isFloat())
                    log(*node, "operator " + op + " cannot be applied to type '" + lhsTy->toString() + "'.");
            }
            else if (auto gp = std::dynamic_pointer_cast<GenericParamType>(lhsTy))
            {
                if (!gp->implementsTrait("Numeric") && !(opTrait && gp->implementsTrait(opTrait)))
                    log(*node, "operator " + op + " requires the generic parameter to have a 'Numeric' constraint.");
            }
            else if (auto ct = std::dynamic_pointer_cast<CustomType>(lhsTy))
            {
                if (opTrait && ct->implementsTrait(opTrait))
                    log(*node, "compound assignment is not supported on a type that overloads the operator yet: write \"x = x " + std::string(1, node->compoundOpToString()[0]) + " y\" instead.");
                else
                    log(*node, "operator " + op + " cannot be applied to type '" + lhsTy->toString() + "'.");
            }
            else
                log(*node, "operator " + op + " cannot be applied to type '" + lhsTy->toString() + "'.");
        }
    }

    // Definite assignment on the TARGET side: assigning the whole binding
    // initializes it, but writing a FIELD/ELEMENT requires it to be initialized
    // already — the language tracks whole bindings, not individual fields, so a
    // partially constructed struct would otherwise be released half-built at
    // scope exit.
    if (dynamic_cast<HIRMemberAccess *>(node->target.get()) || dynamic_cast<HIRIndexAccess *>(node->target.get()))
        checkInitializedUse(node->target.get(), *node);

    // Borrow-check: writing to a borrowed place is forbidden. A write THROUGH a
    // reference (`r.v = 5` where r = &mut x) roots on `r`, which is not itself
    // a borrowed binding, so it passes here and is governed by mutability below.
    // Writing through a dereference uses the POINTER as well: `*r = v` needs r
    // itself to be free, and the referent check exempts r's own borrow — writing
    // through your own exclusive borrow is exactly what having one is for.
    std::string targetRoot;
    std::vector<std::string> targetPath;
    std::string holderRoot;
    std::vector<std::string> holderPath;
    bool viaHolder = derefHolderOf(node->target.get(), holderRoot, holderPath);
    if (viaHolder)
        checkBorrowUse(holderRoot, holderPath, BorrowUseKind::Write, *node);
    std::vector<std::string> exempt = viaHolder ? exemptionChain(holderRoot)
                                                : std::vector<std::string>{};
    if (resolvePlace(node->target.get(), targetRoot, targetPath))
        checkBorrowUse(targetRoot, targetPath, BorrowUseKind::Write, *node,
            exempt.empty() ? nullptr : &exempt);

    // A never-valued RHS is compatible with any target type (bottom), like a
    // never-valued initialiser or argument.
    if (node->target->type && node->value->type
        && !node->target->type->equals(node->value->type) && !isNever(node->value->type))
        log(*node, "assignment type mismatch.", E_TypeMismatch);

    // Mutability check: the assignment target must be writable. A plain
    // variable must be `let mut`; a field access inherits mutability from its
    // root binding OR from a `&mut T` reference held in that binding
    // (e.g. `let r = &mut x; r.f = 1;` is legal even though `r` is not `mut`).
    // NOTE: Symbol::isMutable is std::optional<bool>; `!sym->isMutable` would
    // only test whether the optional is engaged. Use value_or to test the flag.
    auto isMutableBinding = [](const Symbol *sym)
    {
        if (sym->isMutable.has_value() && *sym->isMutable)
            return true;
        if (sym->type)
        {
            if (auto ref = std::dynamic_pointer_cast<ReferenceType>(sym->type))
                return ref->isMutableRef();
        }
        return false;
    };

    // Walk a member/index chain down to its root binding (shared by all three
    // assignment shapes so they can't drift apart).
    auto walkToRootBinding = [](HIRExpr *&obj) -> HIRNameRef *
    {
        while (true)
        {
            if (auto *inner = dynamic_cast<HIRMemberAccess *>(obj))
                obj = inner->object.get();
            else if (auto *inner = dynamic_cast<HIRIndexAccess *>(obj))
                obj = inner->object.get();
            else
                break;
        }
        return dynamic_cast<HIRNameRef *>(obj);
    };

    if (auto *nameRef = dynamic_cast<HIRNameRef *>(node->target.get()))
    {
        auto *sym = SymbolTable::getInstance().lookupSymbol(nameRef->name);
        if (sym && !isMutableBinding(sym))
        {
            log(*node, "cannot assign to immutable variable '" + nameRef->name + "'", E_AssignToImmutable);
            return;
        }
    }
    else if (auto *member = dynamic_cast<HIRMemberAccess *>(node->target.get()))
    {
        // Re-seating a field (`s.data = p`) MODIFIES the struct, so it always
        // requires a mutable root — even when the field is itself a `&mut`
        // reference (that reference lives IN the struct).
        HIRExpr *obj = member->object.get();
        if (auto *rootRef = walkToRootBinding(obj))
        {
            auto *sym = SymbolTable::getInstance().lookupSymbol(rootRef->name);
            if (sym && !isMutableBinding(sym))
            {
                log(*node, "cannot assign to field of immutable variable '" + rootRef->name + "'", E_AssignToImmutable);
                return;
            }
        }
    }
    else if (auto *deref = dynamic_cast<HIRDeref *>(node->target.get()))
    {
        // `*out = v` writes THROUGH the reference, so the ROOT binding's own
        // mutability is irrelevant (exactly like indexing through a pointer),
        // but the reference must be exclusive: writing through a shared `&T`
        // would mutate a value the other holders were promised is read-only.
        // (The operand of a failed analysis has no type; its own error was
        // already reported, so nothing is added here.)
        if (auto refTy = std::dynamic_pointer_cast<ReferenceType>(deref->operand->type))
        {
            if (!refTy->isMutableRef())
            {
                log(*node, "cannot assign through a shared reference.", E_AssignToImmutable);
                return;
            }
        }
    }
    else if (auto *idx = dynamic_cast<HIRIndexAccess *>(node->target.get()))
    {
        // `a[i] = ...` / `h.buf[i] = ...` / `p[i] = ...`.
        //
        // Write-through: when the indexed object is itself an indirection
        // (`&mut T`, or a `*mut T` heap buffer), writing through it is allowed
        // even if the root binding is not `mut` — the write targets the
        // referent, not the holder. A shared reference or a `*T` is rejected.
        // Otherwise the root binding's mutability rules apply.
        if (auto ptrTy = std::dynamic_pointer_cast<PointerType>(idx->object->type))
        {
            // `p[i] = x` through a raw pointer: `*mut T` targets the referent (as
            // with `&mut T`, the root binding's mutability is irrelevant), while a
            // `*T` must not be writable through.
            if (!ptrTy->isMutablePtr())
            {
                log(*node, "cannot assign through a shared raw pointer.", E_AssignToImmutable);
                return;
            }
        }
        else if (auto refTy = std::dynamic_pointer_cast<ReferenceType>(idx->object->type))
        {
            if (!refTy->isMutableRef())
            {
                log(*node, "cannot assign through a shared reference.", E_AssignToImmutable);
                return;
            }
        }
        else
        {
            HIRExpr *obj = idx->object.get();
            if (auto *rootRef = walkToRootBinding(obj))
            {
                auto *sym = SymbolTable::getInstance().lookupSymbol(rootRef->name);
                if (sym && !isMutableBinding(sym))
                {
                    log(*node, "cannot assign to element of immutable variable '" + rootRef->name + "'", E_AssignToImmutable);
                    return;
                }
            }
        }
    }

    // Move semantics (whole-variable or field-path move source).
    handleMoveSource(node->value.get(), *node);

    // The assignment target is reinitialized — it owns a fresh value again, so
    // a prior move-out must not poison later reads (mirrors the MIR-side
    // movedLocals_.erase in MIRBuilder::buildAssign).
    if (auto *targetRef = dynamic_cast<HIRNameRef *>(node->target.get()))
    {
        if (auto *tsym = SymbolTable::getInstance().lookupSymbol(targetRef->name))
        {
            tsym->state = VarState::Valid;
            tsym->movedFields.clear();
            // A whole-binding write also satisfies definite assignment.
            tsym->initialized = true;
        }

        // A reference binding may now point somewhere else (`r = &mut y;`), or
        // nowhere we can name — keep the alias table honest either way.
        recordAlias(targetRef->name, node->value.get());
    }
    else if (auto *targetMa = dynamic_cast<HIRMemberAccess *>(node->target.get()))
    {
        // Re-writing `p.a` re-owns field a (and everything under it): drop any
        // moved path that starts with this field path.
        std::string root;
        std::vector<std::string> path;
        if (extractRootAndPath(targetMa, root, path))
        {
            if (auto *tsym = SymbolTable::getInstance().lookupSymbol(root))
            {
                tsym->movedFields.erase(
                    std::remove_if(tsym->movedFields.begin(), tsym->movedFields.end(), [&](const std::vector<std::string> &existing)
                        {
                            if (existing.size() < path.size()) return false;
                            return std::equal(path.begin(), path.end(), existing.begin()); }),
                    tsym->movedFields.end());
            }
        }
    }

}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRIf *node)
{
    // The condition is fully evaluated BEFORE either branch runs, so the
    // temporary borrows it created are over by then. A method receiver is only
    // RESERVED while its call is set up (two-phase, see the borrow notes), and a
    // reservation is not relaxed for a write — without releasing it here, the
    // body could not touch the very receiver the condition had asked:
    // `if i < v.len() { v[i] = x; }` reported E4003 ("cannot assign to 'v[*]'
    // because it is borrowed") against the condition's own `len()` call, because
    // a statement's temporaries otherwise live until the end of the whole `if`.
    const size_t condBorrowStart = activeBorrows_.size();
    analyzeExpr(node->cond.get());
    endTemporaryBorrowsSince(condBorrowStart);

    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
    if (node->cond->type && !node->cond->type->equals(boolTy))
        log(*node, "if condition must be bool.");

    // Definite assignment across the branch: snapshot at the join point, run each
    // branch from that snapshot, then combine. A branch that cannot FALL THROUGH
    // (it always returns / breaks / diverges) contributes no state at all — the
    // other branch decides. Otherwise the join is the element-wise AND, and a
    // missing `else` counts as an empty branch that assigns nothing.
    const bool reachable = !sequenceTerminated_;
    InitState entry = reachable ? snapshotInitState() : InitState{};

    sequenceTerminated_ = !reachable;
    if (node->thenBlock) visit(node->thenBlock.get());
    const bool thenTerminates = sequenceTerminated_;
    InitState thenState = reachable ? captureInitState(entry) : InitState{};

    // The else side starts from the ENTRY state again — otherwise a variable the
    // then-branch assigned would leak its "initialized" into a branch that never
    // touched it.
    if (reachable) restoreInitState(entry);
    sequenceTerminated_ = !reachable;
    if (node->elseBlock.has_value()) visit(node->elseBlock.value().get());
    const bool elseTerminates = sequenceTerminated_ && node->elseBlock.has_value();
    InitState elseState = reachable ? captureInitState(entry) : InitState{};

    if (reachable)
    {
        if (thenTerminates && !elseTerminates)
        {
            // Only the else path reaches the join (the implicit empty else when
            // there is none: it assigns nothing, i.e. the entry state).
            restoreInitState(elseState);
        }
        else if (elseTerminates && !thenTerminates)
        {
            restoreInitState(thenState);
        }
        else if (!thenTerminates && !elseTerminates)
        {
            mergeInitState(thenState, elseState);
            restoreInitState(thenState);
        }
    }

    // The statement after the if is reachable unless BOTH branches terminate.
    sequenceTerminated_ = !reachable || (thenTerminates && elseTerminates);
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRMatch *node)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    // The scrutinee is evaluated before any arm runs, so its temporary borrows
    // are over by then — `match f(&mut x) { ... x ... }` may use `x` in the arms.
    // (Before this, a `&mut` created in the scrutinee stayed live for the whole
    // match and every arm that touched the argument again was E4002 — the reason
    // lisvm had to bind the call result first and match the binding.)
    const size_t scrutineeBorrowStart = activeBorrows_.size();
    analyzeExpr(node->scrutinee.get());
    endTemporaryBorrowsSince(scrutineeBorrowStart);
    if (!node->scrutinee->type)
        return;

    // The scrutinee must be an owned ENUM (reference matching is a later stage).
    auto enumTy = std::dynamic_pointer_cast<CustomType>(node->scrutinee->type);
    if (!enumTy || !enumTy->isEnum())
    {
        log(*node, "match scrutinee must be an enum (got '" + node->scrutinee->type->toString() + "')", E_MatchOnNonEnum);
        return;
    }

    // Matching an owned enum consumes it (a whole-value move) — Rust semantics.
    handleMoveSource(node->scrutinee.get(), *node);

    const auto &variants = enumTy->getVariants();

    // The match's bindings die with the match; put them in a dedicated scope.
    auto matchScope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(matchScope);

    std::unordered_set<std::string> covered;
    bool hasWildcard = false;
    bool hasBlockArm = false;
    bool hasValueArm = false;
    std::shared_ptr<Type> matchResultType = nullptr;

    // Definite assignment across the arms: a binding is initialized after the
    // match only if EVERY arm that can fall through initialized it. Arms that
    // cannot fall through (ret / break / a diverging tail) contribute nothing.
    const bool reachable = !sequenceTerminated_;
    InitState entry = reachable ? snapshotInitState() : InitState{};
    InitState mergedArms;
    bool haveMergedArm = false;

    for (auto &arm : node->arms)
    {
        auto armScope = SymbolTable::getInstance().getCurrentScope()->createChild();
        SymbolTable::getInstance().enterScope(armScope);

        // Every arm starts from the match ENTRY state: one arm assignments must
        // never leak into a later arm.
        if (reachable) restoreInitState(entry);

        if (arm.isWildcard)
        {
            hasWildcard = true;
            if (!arm.bindings.empty())
                log(*node, "a wildcard '_' pattern cannot bind values.");
            // A wildcard matches everything — arms after it are unreachable.
            if (&arm != &node->arms.back())
                log(*node, "a '_' wildcard must be the last match arm.");
        }
        else
        {
            const CustomType::EnumVariantInfo *variant = nullptr;
            for (size_t i = 0; i < variants.size(); ++i)
            {
                if (variants[i].name == arm.variantName)
                {
                    variant = &variants[i];
                    break;
                }
            }
            if (!variant)
            {
                log(*node, "enum '" + enumTy->getName() + "' has no variant '" + arm.variantName + "'", E_UnknownVariant);
            }
            else
            {
                covered.insert(arm.variantName);
                if (arm.bindings.size() != variant->payloadTypes.size())
                {
                    log(*node, "variant '" + arm.variantName + "' pattern expects " + std::to_string(variant->payloadTypes.size()) + " binding(s), got " + std::to_string(arm.bindings.size()), E_VariantPatternMismatch);
                }
                else
                {
                    // Bind each pattern variable to its (instantiated) payload type.
                    // Non-Copy payloads are MOVED out of the scrutinee temp by
                    // buildMatch; the per-arm owned frame drops them exactly once.
                    for (size_t i = 0; i < arm.bindings.size(); ++i)
                    {
                        auto payloadTy = variant->payloadTypes[i];
                        arm.bindings[i].second = payloadTy; // fill the resolved type
                        auto sym = std::make_unique<Symbol>();
                        sym->kind = SymbolKind::LocalVar;
                        sym->name = arm.bindings[i].first;
                        sym->type = payloadTy;
                        SymbolTable::getInstance().insertSymbol(arm.bindings[i].first, std::move(sym));
                    }
                }
            }
        }

        // Analyze the arm body (statement match) or its tail value (value match).
        if (arm.body)
        {
            hasBlockArm = true;
            visit(arm.body.get());
        }
        else if (arm.tailValue)
        {
            hasValueArm = true;
            analyzeExpr(arm.tailValue.get());
            handleMoveSource(arm.tailValue.get(), *node);
            auto tailTy = arm.tailValue->type;
            // A DIVERGING arm (`None => panic("...")`) produces no value, so it
            // must not fix the match's type — `match o { Some(v) => v, None =>
            // panic("x") }` is a `T` match, not a type conflict. Such an arm is
            // simply skipped; if every arm diverges, matchResultType stays null
            // and the match becomes NEVER (see the type assignment below).
            if (isNeverType(tailTy))
            {
                // contributes no type
            }
            else if (!matchResultType)
            {
                matchResultType = tailTy;
            }
            else if (!matchResultType->equals(tailTy))
            {
                log(*node, "match arms have inconsistent types: '" + matchResultType->toString() + "' vs '" + tailTy->toString() + "'.");
            }
        }
        else
        {
            log(*node, "match arm has neither a block body nor a value.");
        }
        SymbolTable::getInstance().exitScope(); // armScope

        if (reachable)
        {
            const bool armTerminates = sequenceTerminated_
                                      || (arm.tailValue && isNeverType(arm.tailValue->type));
            if (!armTerminates)
            {
                InitState armState = captureInitState(entry);
                if (!haveMergedArm)
                {
                    mergedArms = armState;
                    haveMergedArm = true;
                }
                else
                {
                    mergeInitState(mergedArms, armState);
                }
            }
        }
    }

    // Mixed block arms (void) and value arms are inconsistent.
    if (hasValueArm && hasBlockArm)
        log(*node, "match cannot mix block arms with value arms.");

    // Exhaustiveness: every variant covered, or a `_` wildcard present.
    if (!hasWildcard)
    {
        for (const auto &v : variants)
        {
            if (!covered.count(v.name))
            {
                log(*node, "match is not exhaustive: variant '" + v.name + "' is not covered (add an arm or a '_' wildcard)", E_NonExhaustiveMatch);
                break;
            }
        }
    }

    // A statement match (all block arms) is VOID. A value match takes its arms'
    // unified type — but when EVERY value arm diverges, no arm contributed a
    // type and the match itself never produces a value: it is NEVER, so an
    // enclosing `let x: i32 = match ...` still type-checks via the never
    // coercion instead of failing against VOID.
    if (matchResultType)
        node->type = matchResultType;
    else if (hasValueArm)
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::NEVER);
    else
        node->type = voidTy;

    if (reachable)
    {
        if (haveMergedArm)
            restoreInitState(mergedArms);
        // Every arm terminates: nothing after the match can run.
        sequenceTerminated_ = !haveMergedArm;
    }

    SymbolTable::getInstance().exitScope(); // matchScope
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRLoop *node)
{
    if (node->cond.has_value())
    {
        // Same as visit(HIRIf): the condition's temporary reservations end with
        // the condition, so the body may write what the condition read —
        // `while i < v.len() { v[i] = ... }`.
        const size_t condBorrowStart = activeBorrows_.size();
        analyzeExpr(node->cond.value().get());
        endTemporaryBorrowsSince(condBorrowStart);

        auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        if (node->cond.value()->type && !node->cond.value()->type->equals(boolTy))
            log(*node, "loop condition must be bool.");
    }

    // Loop-carried move detection (flow-sensitive over the back-edge): the body
    // re-executes each iteration, so a whole/partial move of an ENCLOSING-scope
    // non-Copy local inside the body without re-assignment would be re-moved on
    // the next iteration — a runtime double-free. Snapshot the enclosing locals'
    // move state before the body and reject any that became moved in the body.
    std::unordered_map<std::string, std::pair<bool, bool>> preBodyMoves; // name → (moved, partial)
    for (auto s = SymbolTable::getInstance().getCurrentScope(); s; s = s->getParent())
        for (const auto &[name, sym] : s->getSymbols())
            if (sym->type && !sym->type->isCopyable())
                preBodyMoves[name] = {sym->state == VarState::Moved, !sym->movedFields.empty()};

    // Definite assignment across a loop: the body may run ZERO times, so the
    // state after the loop is the state before it (assignments inside do not
    // count). This is the standard conservative rule.
    const bool reachable = !sequenceTerminated_;
    InitState entry = reachable ? snapshotInitState() : InitState{};

    loopDepth_++;
    loopBodyBreaks_.push_back(false);
    if (node->body) visit(node->body.get());
    const bool bodyBreaks = loopBodyBreaks_.back();
    loopBodyBreaks_.pop_back();
    loopDepth_--;

    if (reachable) restoreInitState(entry);

    // `while true { ... }` with no break in the body never falls through, so the
    // statements after it are unreachable (which also suppresses their
    // definite-assignment diagnostics).
    bool neverFallsThrough = false;
    if (node->cond.has_value())
    {
        if (auto *lit = dynamic_cast<HIRLiteral *>(node->cond.value().get());
            lit && lit->kind == HIRLiteral::Kind::Bool)
        {
            neverFallsThrough = std::get<bool>(lit->value) && !bodyBreaks;
        }
    }
    sequenceTerminated_ = !reachable || neverFallsThrough;

    for (auto s = SymbolTable::getInstance().getCurrentScope(); s; s = s->getParent())
        for (const auto &[name, sym] : s->getSymbols())
        {
            if (!sym->type || sym->type->isCopyable()) continue;
            auto it = preBodyMoves.find(name);
            if (it == preBodyMoves.end()) continue; // declared in the body, not enclosing
            bool nowMoved = sym->state == VarState::Moved || !sym->movedFields.empty();
            bool wasMoved = it->second.first || it->second.second;
            if (nowMoved && !wasMoved && !mirBorrowCheck_)
                log(*node, "value '" + name + "' is moved inside this loop without being "
                                              "re-assigned; the next iteration would move it again.",
                    E_UseOfMovedValue);
        }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRBreak *node)
{
    if (loopDepth_ == 0)
        log(*node, "break can only be used inside a loop.");
    // Statements after a break are unreachable; the enclosing loop can exit (so it
    // must not be treated as an infinite loop by visit(HIRLoop)).
    sequenceTerminated_ = true;
    if (!loopBodyBreaks_.empty())
        loopBodyBreaks_.back() = true;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRContinue *node)
{
    if (loopDepth_ == 0)
        log(*node, "continue can only be used inside a loop.");
    sequenceTerminated_ = true; // statements after a continue are unreachable
}

// (break/continue share the single rule "must be inside a loop" — the two
// visitors above differ only in the keyword in the diagnostic.)

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRReturn *node)
{
    if (!functionInfo.isInFunction)
    {
        log(*node, "return statement can only be used inside a function.");
        return;
    }

    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    std::shared_ptr<Type> retTy = voidTy;

    if (node->value.has_value())
    {
        analyzeExpr(node->value.value().get());
        retTy = node->value.value()->type;

        if (!functionInfo.hasReturnValue)
        {
            functionInfo.hasReturnValue = true;
            functionInfo.declaredReturnType = retTy;
        }
        // Expected-type inference: a value whose generic enum/struct type could
        // not be resolved locally (`ret option::none;` has no payload args to
        // infer T from) is re-instantiated against the declared return type.
        if (functionInfo.hasReturnValue && functionInfo.declaredReturnType)
        {
            auto declaredCt = std::dynamic_pointer_cast<CustomType>(functionInfo.declaredReturnType);
            auto valueCt = std::dynamic_pointer_cast<CustomType>(retTy);
            if (declaredCt && valueCt && valueCt->isGeneric() && declaredCt->isInstantiated()
                && valueCt->getOriginName() == declaredCt->getOriginName())
            {
                retTy = context->typeContext->instantiateCustom(valueCt, declaredCt->getGenericArgs());
                node->value.value()->type = retTy;
            }
        }
        // A by-value non-Copy return consumes the source (`ret p` moves p).
        handleMoveSource(node->value.value().get(), *node);
    }

    if (functionInfo.declaredReturnType && !typesCompatible(functionInfo.declaredReturnType, retTy))
    {
        log(*node, "return type '" + retTy->toString() + "' does not match declared '" + functionInfo.declaredReturnType->toString() + "'.");
    }
    // (Returning a reference into this frame — the old Stage 3 'dangling return'
    // analysis — is now MIRBorrowCheck's E4007, computed from the returned PLACE.)

    // Nothing after a return in this sequence runs; the enclosing sequence OR-s
    // this into its own state.
    sequenceTerminated_ = true;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRExprStmt *node)
{
    analyzeExpr(node->expr.get());

    // A discarded expression statement CONSUMES its value: `p.a;` releases the
    // field right away and `d;` drops the whole value (MIR's buildExprStmt
    // lowers exactly that). Without the move bookkeeping here the value looked
    // untouched to every later check, so
    //     p.s; let y = p.s;        // s: String
    // compiled and moved a buffer that had already been freed (double free),
    // and a partial move out of a Drop type written in this form skipped the
    // E0509 check entirely.
    handleMoveSource(node->expr.get(), *node);

    // `panic("...");` (or any call returning the uninhabited type) never returns,
    // so the rest of this statement sequence is unreachable.
    if (node->expr && isNeverType(node->expr->type))
        sequenceTerminated_ = true;
}

// ============================================================
//  Expressions
// ============================================================

void HIRSemanticAnalyzer::visit(HIRLiteral *node)
{
    // P1: the HIR builder flags literals whose numeric value overflowed the
    // representation (e.g. an int64-overflowing integer). Report it here, where
    // the diagnostic survives to the error gate — logging in the builder would
    // be wiped by run()'s ResetErrorCount, silently turning the literal into 0.
    if (node->overflowed)
    {
        log(*node, "numeric literal overflows its type.", E_InvalidLiteralType);
    }

    switch (node->kind)
    {
    case HIRLiteral::Kind::Int:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
        break;
    case HIRLiteral::Kind::Float:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::F64);
        break;
    case HIRLiteral::Kind::Bool:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        break;
    case HIRLiteral::Kind::String:
        // No proper string type yet. Model a string literal as a raw pointer
        // to i8 — codegen already emits a GlobalStringPtr (i8*) for it. This
        // avoids a null type crashing the MIR/LLVM stages. Warn once, not per
        // literal (string-heavy code would otherwise flood the output).
        node->type = context->typeContext->getReference(
            context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8),
            /*isMutable=*/false);
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            log(*node, "string literals are modelled as &i8 (raw string pointer); a proper string type is not implemented yet.",
                /*errorId=*/1,
                Logger::LogLevel::WARNING);
        }
        break;
    case HIRLiteral::Kind::Char:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::CHAR);
        break;
    }
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::inGenericContext() const
{
    if (!functionInfo.gParams.empty())
        return true; // a generic function: mono rewrites this body
    auto ct = std::dynamic_pointer_cast<CustomType>(currentStructType);
    return ct && ct->isGeneric(); // a method of a generic struct: mono prepends its params
}
// ── definite assignment (`let x;` has no initializer) ────────────────────────

HIRSemanticAnalyzer::InitState HIRSemanticAnalyzer::snapshotInitState()
{
    InitState state;
    // Locals and params only: stop at the global scope (the only scope without a
    // parent) — top-level symbols are always initialized.
    for (auto scope = SymbolTable::getInstance().getCurrentScope(); scope && scope->getParent();
         scope = scope->getParent())
        for (const auto &[name, sym] : scope->getSymbols())
        {
            (void)name;
            state[sym.get()] = sym->initialized;
        }
    return state;
}

HIRSemanticAnalyzer::InitState HIRSemanticAnalyzer::captureInitState(const InitState &keys) const
{
    InitState state;
    state.reserve(keys.size());
    for (const auto &[sym, wasInitialized] : keys)
    {
        (void)wasInitialized;
        if (sym) state[sym] = sym->initialized;
    }
    return state;
}

void HIRSemanticAnalyzer::restoreInitState(const InitState &state)
{
    for (const auto &[sym, initialized] : state)
        if (sym) sym->initialized = initialized;
}

void HIRSemanticAnalyzer::mergeInitState(InitState &dst, const InitState &other)
{
    for (auto &[sym, initialized] : dst)
    {
        auto it = other.find(sym);
        if (it == other.end()) continue; // outside the key set: nothing to merge
        initialized = initialized && it->second;
    }
}

void HIRSemanticAnalyzer::checkInitializedUse(HIRExpr *placeExpr, HIRNode &errNode)
{
    // Definite assignment is CFG dataflow now: MIRBorrowCheck owns it.
    if (mirBorrowCheck_) return;

    // Unreachable code cannot read anything: after a `ret`/`break`/`continue`, a
    // diverging call, or a branch that never falls through, the definite-
    // assignment state is meaningless. MIRBuilder drops those statements too.
    if (!placeExpr || sequenceTerminated_) return;

    std::string root;
    std::vector<std::string> path;
    if (!extractRootAndPath(placeExpr, root, path)) return;

    Symbol *sym = SymbolTable::getInstance().lookupSymbol(root);
    if (!sym || sym->initialized) return;

    log(errNode, "use of uninitialized value: '" + root + "'", E_UseOfUninitializedValue);
}

Symbol *HIRSemanticAnalyzer::lookupModuleAware(const std::string &name)
{
    auto &table = SymbolTable::getInstance();

    Symbol *sym = nullptr;

    // A `$`-prefixed name is already internal (baked by the Parser for `m::x`).
    if (isInternalName(name))
    {
        sym = table.lookupSymbol(name);
    }
    else
    {
        // 1. Bare name through the LOCAL scope chain only (params/locals are
        //    bare; stop at the global scope, the only scope with no parent).
        for (auto scope = table.getCurrentScope(); scope && scope->getParent(); scope = scope->getParent())
        {
            const auto &syms = scope->getSymbols();
            auto it = syms.find(name);
            if (it != syms.end())
            {
                sym = it->second.get();
                break;
            }
        }

        // 2. The current module's top-level name (internal).
        if (!sym && !currentModule_.empty())
            sym = table.lookupSymbol(internalName(currentModule_, name));

        // 3. Root-module bare name (global-scope fallback; step 1 already
        //    excluded locals, so a hit here is a top-level symbol — including
        //    selective-import alias symbols promoted by pass 1d).
        if (!sym)
            sym = table.lookupSymbol(name);
    }

    // Selective-import aliases forward to their target symbol.
    while (sym && sym->aliasTarget)
        sym = sym->aliasTarget;
    return sym;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRNameRef *node)
{
    auto *sym = lookupModuleAware(node->name);
    if (!sym)
    {
        log(*node, "undefined identifier '" + displayName(node->name) + "'.", E_UndefinedIdentifier);
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }
    node->symbol = sym;
    node->type = sym->type;
    node->scope = SymbolTable::getInstance().getCurrentScope();
    // Write back the resolved name: top-level symbols carry their internal
    // (module-prefixed) name so MIRBuilder/codegen emit the right symbol;
    // locals/params keep their bare name.
    node->name = sym->name;

    // NLL: record the last use of any borrow-holder variable (r in `let r = &x`),
    // so its borrow's liveness can end here.
    if (!promotedBorrows_.empty())
    {
        for (const auto &b : promotedBorrows_)
            if (b.holderName == node->name)
            {
                auto it = holderLastUseStmt_.find(node->name);
                size_t cur = it != holderLastUseStmt_.end() ? it->second : 0;
                if (stmtOrdinal_ > cur)
                    holderLastUseStmt_[node->name] = stmtOrdinal_;
                break;
            }
    }

    // An UNINHABITED binding (`let x: never;`) has no value to read: the type is
    // allowed (it is the bottom type), but a value of it can never exist, so any
    // use of the binding is a bug — and materialising it would put a valueless
    // operand into MIR. Function symbols are exempt: the builtin `panic` callee
    // carries the `never` RESULT type on its name-ref, and a user
    // `fn f() -> never` name-ref is a FunctionType, not `never`.
    if (isNeverType(sym->type) && sym->kind != SymbolKind::Function)
    {
        log(*node, "cannot use '" + node->name + "': it has the uninhabited type 'never'.", E_UndefinedIdentifier);
        return;
    }

    // Definite assignment: reading a `let x;` binding before every path has
    // assigned it. Assignment targets are exempt (visit(HIRAssign) sets
    // inAssignTarget_ and checks the field/element rule itself), because the
    // write is what initializes the binding.
    if (!inAssignTarget_)
        checkInitializedUse(node, *node);

    // Borrow-check: a Copy read of a whole variable conflicts with active &mut
    // borrows. Non-Copy uses are moves and are checked at the consuming sites
    // (handleMoveSource). Also close the read-after-move gap in non-consuming
    // positions (e.g. `if x == 0` after `let y = x;`).
    if (!mirBorrowCheck_)
    {
        if (sym->type && sym->type->isCopyable())
            checkBorrowUse(node->name, {}, BorrowUseKind::Read, *node);
        if (sym->state == VarState::Moved)
            log(*node, "use of moved value: '" + node->name + "'", E_UseOfMovedValue);
    }
}

// ---------------------------------------------------------------------------
// ── operator overloading: OpKind → operator-trait helpers ─────────────────────

const char *HIRSemanticAnalyzer::operatorTraitName(HIRBinaryOp::OpKind op)
{
    using K = HIRBinaryOp::OpKind;
    switch (op)
    {
    case K::Add: return "Add";
    case K::Sub: return "Sub";
    case K::Mul: return "Mul";
    case K::Div: return "Div";
    case K::Mod: return "Rem";
    case K::Eq: return "PartialEq";
    case K::Ne: return "PartialEq";
    case K::Lt: return "PartialOrd";
    case K::Gt: return "PartialOrd";
    case K::Le: return "PartialOrd";
    case K::Ge: return "PartialOrd";
    case K::BitAnd: return "BitAnd";
    case K::BitOr: return "BitOr";
    case K::BitXor: return "BitXor";
    case K::ShiftLeft: return "Shl";
    case K::ShiftRight: return "Shr";
    case K::And:
    case K::Or: return nullptr; // logical ops never overload
    }
    return nullptr;
}

const char *HIRSemanticAnalyzer::operatorMethodName(HIRBinaryOp::OpKind op)
{
    using K = HIRBinaryOp::OpKind;
    switch (op)
    {
    case K::Add: return "add";
    case K::Sub: return "sub";
    case K::Mul: return "mul";
    case K::Div: return "div";
    case K::Mod: return "rem";
    case K::Eq: return "eq";
    case K::Ne: return "ne";
    case K::Lt: return "lt";
    case K::Gt: return "gt";
    case K::Le: return "le";
    case K::Ge: return "ge";
    case K::BitAnd: return "bitand";
    case K::BitOr: return "bitor";
    case K::BitXor: return "bitxor";
    case K::ShiftLeft: return "shl";
    case K::ShiftRight: return "shr";
    case K::And:
    case K::Or: return nullptr;
    }
    return nullptr;
}

bool HIRSemanticAnalyzer::isOperatorTrait(const std::string &name)
{
    static const std::unordered_set<std::string> ops = {
        "Add", "Sub", "Mul", "Div", "Rem", "PartialEq", "PartialOrd", "BitAnd", "BitOr", "BitXor", "Shl", "Shr"};
    // Callers may pass the trait's module-prefixed internal name.
    return ops.count(displayName(name)) != 0;
}

bool HIRSemanticAnalyzer::resolveOperatorMethod(HIRBinaryOp *node,
    const std::shared_ptr<CustomType> &ct,
    const char *opMethod,
    const char *opTrait)
{
    std::string baseName = ct->getOriginName();
    std::string symName = baseName + "::" + opMethod;
    Symbol *symbol = SymbolTable::getInstance().lookupSymbol(symName);
    if (!symbol)
    {
        log(*node, "operator '" + std::string(node->opToString()) + "' resolves to method '" + opMethod + "' but no such method is registered on '" + baseName + "'.");
        return false;
    }

    auto newTy = std::static_pointer_cast<FunctionType>(symbol->type);

    // A generic struct instantiation (MyStruct$i32) needs its gParams →
    // concrete args substituted into the method signature before the arg-type
    // and result checks below (mirrors the Method/Static branches of visit(HIRCall)).
    std::unordered_map<std::string, std::shared_ptr<Type>> structSubst;
    if (!ct->getGenericArgs().empty() && ct->genericOrigin)
    {
        const auto &gps = ct->genericOrigin->getGenericParams();
        const auto &gas = ct->getGenericArgs();
        for (size_t i = 0; i < gps.size() && i < gas.size(); ++i)
        {
            auto gp = std::static_pointer_cast<GenericParamType>(gps[i]);
            structSubst[gp->getParamName()] = gas[i];
        }
        newTy = std::static_pointer_cast<FunctionType>(substituteType(newTy, structSubst));
    }

    // Binary operator methods take exactly (self, other).
    if (newTy->getParams().size() != 2)
    {
        log(*node, std::string("operator method '") + opMethod + "' must take 2 parameters (self, other), got " + std::to_string(newTy->getParams().size()) + ".");
        return false;
    }
    if (!bindOperatorOperand(node->left, newTy->getParams()[0])
        || !bindOperatorOperand(node->right, newTy->getParams()[1]))
    {
        log(*node, "operator '" + std::string(node->opToString()) + "' operand types do not match the '" + opTrait + "' method signature.");
        return false;
    }

    node->operatorMethod = symbol;
    node->operatorMethodType = newTy;
    node->operatorMethodName = symName;
    node->operatorStructArgs = ct->getGenericArgs();
    node->type = newTy->getReturnType();
    return true;
}

bool HIRSemanticAnalyzer::resolveGenericOperatorMethod(HIRBinaryOp *node,
    const std::shared_ptr<GenericParamType> &gp,
    const char *opMethod,
    const char *opTrait)
{
    // Find the operator trait among the generic param's bounds. Constraint
    // trait names carry the module prefix ("math$Add") — compare bare names.
    std::shared_ptr<TraitType> matchedTrait = nullptr;
    for (const auto &traitTy : gp->getConstraints())
        if (displayName(traitTy->getName()) == opTrait)
        {
            matchedTrait = traitTy;
            break;
        }
    if (!matchedTrait)
    {
        log(*node, "generic parameter '" + gp->getParamName() + "' has no '" + opTrait + "' bound; cannot resolve operator '" + node->opToString() + "'.");
        return false;
    }

    auto method = matchedTrait->findMethod(opMethod);
    if (!method.has_value())
    {
        log(*node, "trait '" + std::string(opTrait) + "' has no method '" + opMethod + "'.");
        return false;
    }

    // Substitute the trait's generic args (operator traits are non-generic, so
    // this is a no-op today) and map Self → the generic param (substituteType
    // does not handle SelfType).
    std::unordered_map<std::string, std::shared_ptr<Type>> subst;
    const auto &traitParams = matchedTrait->getGenericParams();
    const auto &traitArgs = matchedTrait->getGenericArgs();
    for (size_t i = 0; i < traitParams.size() && i < traitArgs.size(); ++i)
    {
        auto p = std::static_pointer_cast<GenericParamType>(traitParams[i]);
        subst[p->getParamName()] = traitArgs[i];
    }

    std::vector<std::shared_ptr<Type>> paramTypes;
    for (size_t i = 0; i < method->params.size(); ++i)
    {
        auto t = substituteType(method->params[i].type, subst);
        if (auto selfTy = std::dynamic_pointer_cast<SelfType>(t))
            t = selfTy->isReference()
                    ? std::shared_ptr<Type>(context->typeContext->getReference(gp, selfTy->isMutable()))
                    : std::shared_ptr<Type>(gp);
        paramTypes.push_back(t);
    }
    auto retTy = substituteType(method->returnType, subst);
    if (auto selfTy = std::dynamic_pointer_cast<SelfType>(retTy))
        retTy = selfTy->isReference()
                    ? std::shared_ptr<Type>(context->typeContext->getReference(gp, selfTy->isMutable()))
                    : std::shared_ptr<Type>(gp);

    if (paramTypes.size() != 2)
    {
        log(*node, "operator method '" + std::string(opMethod) + "' must take 2 parameters (self, other), got " + std::to_string(paramTypes.size()) + ".");
        return false;
    }
    if (!bindOperatorOperand(node->left, paramTypes[0]) || !bindOperatorOperand(node->right, paramTypes[1]))
    {
        log(*node, "operator '" + std::string(node->opToString()) + "' operand types do not match the '" + opTrait + "' method signature.");
        return false;
    }

    node->operatorMethodType = context->typeContext->getFunction(paramTypes, retTy);
    node->operatorMethodName = "<" + gp->getParamName() + ">::" + opMethod;
    node->operatorStructArgs = {}; // monomorphization fills from the concrete struct
    node->type = retTy;
    return true;
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::bindOperatorOperand(std::unique_ptr<HIRExpr> &operand,
    const std::shared_ptr<Type> &paramTy)
{
    // A failed operand analysis has no type; its own error was already logged,
    // and reporting a signature mismatch on top of it would only mislead.
    if (!operand->type) return true;
    if (operand->type->equals(paramTy)) return true;

    // A reference parameter borrows the operand (the comparison traits do this
    // so that comparing never consumes). Operators only READ, so the borrow is
    // always shared; a `&mut Self` parameter is not something a binary operator
    // can be given.
    auto refParam = std::dynamic_pointer_cast<ReferenceType>(paramTy);
    if (refParam && !refParam->isMutableRef() && refParam->getBaseType()->equals(operand->type))
    {
        auto refExpr = std::make_unique<HIRRef>();
        refExpr->position = operand->position;
        refExpr->length = operand->length;
        refExpr->isMutable = false;
        refExpr->type = paramTy;
        refExpr->expr = std::move(operand);
        operand = std::move(refExpr);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRBinaryOp *node)
{
    analyzeExpr(node->left.get());
    analyzeExpr(node->right.get());

    // ── `&i8 == &i8` / `!=`: CONTENT comparison ──────────────────────────────
    // `&i8` is how this language spells a C string, so comparing two of them
    // compares the text. It has to be recognised before the general operator
    // rules: a reference is not a valid operand for them, and `&i8 == &i8` used
    // to be rejected with "operator '==' cannot be applied to type '&i8'".
    // Lowered as `str_cmp(a, b) == 0` (see MIRBuilder::buildBinaryOp).
    if (node->opKind == HIRBinaryOp::OpKind::Eq || node->opKind == HIRBinaryOp::OpKind::Ne)
    {
        auto isCStr = [](const std::shared_ptr<Type> &ty)
        {
            auto ref = std::dynamic_pointer_cast<ReferenceType>(ty);
            if (!ref) return false;
            auto base = std::dynamic_pointer_cast<PrimitiveType>(ref->getBaseType());
            return base && base->getPrimKind() == PrimitiveType::PrimKind::I8;
        };
        if (isCStr(node->left->type) && isCStr(node->right->type))
        {
            auto i8PtrTy = context->typeContext->getReference(
                context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);
            node->isStrCompare = true;
            node->operatorMethodName = "str_cmp";
            node->operatorMethodType = context->typeContext->getFunction(
                {i8PtrTy, i8PtrTy},
                context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32));
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
            return;
        }
    }

    if (node->left->type && node->right->type && !node->left->type->equals(node->right->type))
        log(*node, "operands of binary operator must have the same type.");

    // Operator type check: every binary op requires its operand types to support
    // it (the codegen would otherwise crash — e.g. ICmp on a struct). Concrete
    // primitives are classified by op family; a generic param must carry a
    // `Numeric`/`Integer` marker bound (or the specific operator trait, e.g.
    // `Add`); a struct/enum must implement the corresponding operator trait
    // (operator overloading), otherwise it is rejected here.
    using K = HIRBinaryOp::OpKind;
    bool isCmp = node->opKind == K::Eq || node->opKind == K::Ne || node->opKind == K::Lt
                 || node->opKind == K::Gt || node->opKind == K::Le || node->opKind == K::Ge;
    bool isLogical = node->opKind == K::And || node->opKind == K::Or;
    bool isBitwise = node->opKind == K::BitAnd || node->opKind == K::BitOr || node->opKind == K::BitXor
                     || node->opKind == K::ShiftLeft || node->opKind == K::ShiftRight;

    const char *opTrait = operatorTraitName(node->opKind);
    const char *opMethod = operatorMethodName(node->opKind);

    auto checkOperand = [&](const std::shared_ptr<Type> &ty) -> bool
    {
        if (!ty) return true;
        if (auto p = std::dynamic_pointer_cast<PrimitiveType>(ty))
        {
            if (isLogical) return p->getPrimKind() == PrimitiveType::PrimKind::BOOL;
            if (isBitwise) return p->isInteger();
            // arithmetic + comparison: ints/floats; char (i32); bool only for ==/!=
            if (p->isInteger() || p->isFloat()) return true;
            if (isCmp)
            {
                auto pk = p->getPrimKind();
                if (pk == PrimitiveType::PrimKind::CHAR) return true;
                if (pk == PrimitiveType::PrimKind::BOOL
                    && (node->opKind == K::Eq || node->opKind == K::Ne)) return true;
            }
            return false;
        }
        if (auto gp = std::dynamic_pointer_cast<GenericParamType>(ty))
        {
            if (isLogical) return false; // no Bool trait — require a concrete bool
            // Either the broad Numeric/Integer marker or the specific operator
            // trait bound (`T: Add`) qualifies the generic param.
            if (gp->implementsTrait(isBitwise ? "Integer" : "Numeric")) return true;
            if (opTrait && gp->implementsTrait(opTrait)) return true;
            return false;
        }
        if (auto ct = std::dynamic_pointer_cast<CustomType>(ty))
        {
            // Operator overloading: a struct/enum implementing the operator
            // trait (`impl Add for Vec2`) may use `+`. Logical ops never apply.
            if (isLogical || !opTrait) return false;
            return ct->implementsTrait(opTrait);
        }
        return false; // reference / function / other
    };

    std::string op = std::string("'") + node->opToString() + "'";
    for (auto *operand : {node->left.get(), node->right.get()})
    {
        if (!checkOperand(operand->type))
        {
            if (auto p = std::dynamic_pointer_cast<PrimitiveType>(operand->type))
            {
                if (isLogical)
                    log(*node, "operator " + op + " requires bool operands.");
                else if (isBitwise)
                    log(*node, "operator " + op + " requires integer operands.");
                else
                    log(*node, "operator " + op + " cannot be applied to type '" + operand->type->toString() + "'.");
                (void)p;
            }
            else if (std::dynamic_pointer_cast<GenericParamType>(operand->type))
            {
                if (isLogical)
                    log(*node, "operator " + op + " requires a concrete bool operand.");
                else
                {
                    std::string need = isBitwise ? "Integer" : "Numeric";
                    if (opTrait) need += std::string("' or '") + opTrait;
                    log(*node, "operator " + op + " requires the generic parameter to have a '" + need + "' constraint.");
                }
            }
            else
            {
                if (isLogical)
                    log(*node, "operator " + op + " requires bool operands.");
                else
                {
                    std::string msg = "operator " + op + " cannot be applied to type '" + operand->type->toString() + "'.";
                    if (opTrait && std::dynamic_pointer_cast<CustomType>(operand->type))
                        msg += " it does not implement the '" + std::string(opTrait) + "' trait.";
                    log(*node, msg);
                }
            }
        }
    }

    // ── Operator overloading ─────────────────────────────────────────────────
    // `a + b` becomes `a.add(b)`. Both operands must be the same type and
    // implement the operator trait; the resolved method's return type wins.
    //   * CustomType operands  → direct `<Struct>::method` call.
    //   * GenericParam operands (`fn f<T: Add> { a + b }`) → placeholder
    //     `<T>::method` that monomorphization retargets to the concrete struct,
    //     or falls back to a direct binary op for primitive instantiations.
    bool operatorResolved = false;
    if (opTrait && opMethod && !isLogical
        && node->left->type && node->right->type
        && node->left->type->equals(node->right->type))
    {
        if (auto ct = std::dynamic_pointer_cast<CustomType>(node->left->type))
        {
            if (ct->implementsTrait(opTrait))
                operatorResolved = resolveOperatorMethod(node, ct, opMethod, opTrait);
        }
        else if (auto gp = std::dynamic_pointer_cast<GenericParamType>(node->left->type))
        {
            if (gp->implementsTrait(opTrait))
                operatorResolved = resolveGenericOperatorMethod(node, gp, opMethod, opTrait);
        }
    }
    if (operatorResolved)
    {
        // By-value `self`/`other` consume both operands (mirrors call args). An
        // operand the resolver wrapped in a HIRRef is only BORROWED by the call
        // (that is what makes `a == b` on a non-Copy type possible at all).
        if (!dynamic_cast<HIRRef *>(node->left.get()))
            handleMoveSource(node->left.get(), *node);
        if (!dynamic_cast<HIRRef *>(node->right.get()))
            handleMoveSource(node->right.get(), *node);
        return;
    }

    switch (node->opKind)
    {
    case HIRBinaryOp::OpKind::Eq:
    case HIRBinaryOp::OpKind::Ne:
    case HIRBinaryOp::OpKind::Lt:
    case HIRBinaryOp::OpKind::Gt:
    case HIRBinaryOp::OpKind::Le:
    case HIRBinaryOp::OpKind::Ge:
    case HIRBinaryOp::OpKind::And:
    case HIRBinaryOp::OpKind::Or:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        break;
    default:
        node->type = node->left->type;
        break;
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRCast *node)
{
    analyzeExpr(node->expr.get());
    node->targetType = resolveType(node->rawTargetType, *node);
    node->type = node->targetType;

    auto fromType = node->expr->type;
    if (!fromType) return;

    if (fromType->equals(node->targetType))
    {
        log(*node, "useless cast from '" + fromType->toString() + "' to '" + node->targetType->toString() + "'.", 1, Logger::LogLevel::INFO);
        return;
    }

    // Same cast validity rules as SemanticAnalyzer::visit(CastExpr)
    if (fromType->getKind() == Type::Kind::Primitive)
    {
        auto rType = std::dynamic_pointer_cast<PrimitiveType>(fromType);
        switch (rType->getPrimKind())
        {
        case PrimitiveType::PrimKind::I8:
        case PrimitiveType::PrimKind::I16:
        case PrimitiveType::PrimKind::I32:
        case PrimitiveType::PrimKind::I64:
        {
            if (node->targetType->getKind() != Type::Kind::Primitive)
            {
                log(*node, "integer can only be cast to primitive type.");
                break;
            }
            auto tType = std::dynamic_pointer_cast<PrimitiveType>(node->targetType);
            // char is an i32 at runtime, so i8/i16/i32 → char are widening or
            // identity (`s.data[i] as char` reads a heap byte back as a char).
            // P5: i64 → char would silently truncate to i32 — reject it as
            // narrowing, consistent with the integer-narrowing check below.
            if (tType->getPrimKind() == PrimitiveType::PrimKind::CHAR)
            {
                if (rType->integerBitWidth() > 32)
                {
                    // #[i_know] (2026-08-12): the statement attribute turns the
                    // narrowing ERROR into a warning — the data may truncate.
                    if (node->iKnow)
                        log(*node, "high-to-low cast suppressed by #[i_know]; data may overflow.", E_SemanticError, Logger::LogLevel::WARNING);
                    else
                        log(*node, "cannot cast integer to a smaller integer type.");
                }
                break;
            }
            if (!tType->isFloat() && !tType->isInteger())
            {
                log(*node, "integer can only be cast to float or integer.");
                break;
            }
            // P4: explicit width comparison. The old `(size_t)rType->getPrimKind()
            // > (size_t)tType->getPrimKind()` silently depended on PrimKind being
            // declared in bit-width order (I8=0..I64=3); inserting/reordering an
            // enum entry would silently change which casts are considered
            // narrowing. integerBitWidth() states the widths explicitly.
            if (tType->isInteger() && rType->integerBitWidth() > tType->integerBitWidth())
            {
                // #[i_know] (2026-08-12): turns the narrowing ERROR into a
                // warning (see the CHAR case above).
                if (node->iKnow)
                    log(*node, "high-to-low cast suppressed by #[i_know]; data may overflow.", E_SemanticError, Logger::LogLevel::WARNING);
                else
                    log(*node, "cannot cast integer to a smaller integer type.");
            }
            break;
        }
        case PrimitiveType::PrimKind::F32:
            if (node->targetType->getKind() != Type::Kind::Primitive || std::dynamic_pointer_cast<PrimitiveType>(node->targetType)->getPrimKind() != PrimitiveType::PrimKind::F64)
                log(*node, "f32 can only be cast to f64.");
            break;
        case PrimitiveType::PrimKind::F64:
            log(*node, "f64 cannot be cast.");
            break;
        case PrimitiveType::PrimKind::BOOL:
        case PrimitiveType::PrimKind::CHAR:
            if (node->targetType->getKind() != Type::Kind::Primitive || !std::dynamic_pointer_cast<PrimitiveType>(node->targetType)->isInteger())
                log(*node, "bool/char can only be cast to integer.");
            break;
        case PrimitiveType::PrimKind::VOID:
            log(*node, "void cannot be cast.");
            break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Dispatch a method call on a generic-param receiver (`it.next()` where
// `it: T: Iterator<i32>`) by finding the method on one of T's trait bounds.
// The callee is emitted as a placeholder `<T>::method`; MIRMonomorphization
// rewrites it to the concrete `<Struct>::method` once T is substituted.
// ---------------------------------------------------------------------------

void HIRSemanticAnalyzer::dispatchGenericParamMethod(
    HIRCall *node,
    std::shared_ptr<GenericParamType> gp)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    // Detect ambiguity: if MORE THAN ONE trait bound defines the method, the
    // call is ambiguous — report it rather than silently dispatching to the
    // first bound. If none define it, report the missing-method error.
    std::shared_ptr<TraitType> matchedTrait = nullptr;
    for (const auto &traitTy : gp->getConstraints())
    {
        if (!traitTy->findMethod(node->methodName).has_value())
            continue;
        if (matchedTrait)
        {
            log(*node, "ambiguous call to '" + node->methodName + "': generic parameter '" + gp->getParamName() + "' is bound by multiple traits that define it ('" + matchedTrait->getName() + "' and '" + traitTy->getName() + "').");
            node->type = voidTy;
            return;
        }
        matchedTrait = traitTy;
    }
    if (!matchedTrait)
    {
        log(*node, "generic parameter '" + gp->getParamName() + "' has no method '" + node->methodName + "' (no matching trait bound).");
        node->type = voidTy;
        return;
    }

    {
        const auto &traitTy = matchedTrait;
        auto found = traitTy->findMethod(node->methodName);
        auto method = found.value();

        if (method.isStatic)
        {
            log(*node, "'" + node->methodName + "' is a static method; use '::' to call it.");
            node->type = voidTy;
            return;
        }

        // Substitute the trait's generic params (e.g. T -> i32 in Iterator<i32>).
        std::unordered_map<std::string, std::shared_ptr<Type>> subst;
        const auto &traitParams = traitTy->getGenericParams();
        const auto &traitArgs = traitTy->getGenericArgs();
        for (size_t i = 0; i < traitParams.size() && i < traitArgs.size(); ++i)
        {
            auto p = std::static_pointer_cast<GenericParamType>(traitParams[i]);
            subst[p->getParamName()] = traitArgs[i];
        }

        // Build the method signature, mapping Self to the generic param
        // (substituteType does not handle SelfType).
        std::vector<std::shared_ptr<Type>> paramTypes;
        for (size_t i = 0; i < method.params.size(); ++i)
        {
            auto t = substituteType(method.params[i].type, subst);
            if (auto selfTy = std::dynamic_pointer_cast<SelfType>(t))
            {
                if (selfTy->isReference())
                    t = context->typeContext->getReference(gp, selfTy->isMutable());
                else
                    t = gp;
            }
            paramTypes.push_back(t);
        }

        auto retTy = substituteType(method.returnType, subst);
        if (auto selfTy = std::dynamic_pointer_cast<SelfType>(retTy))
        {
            if (selfTy->isReference())
                retTy = context->typeContext->getReference(gp, selfTy->isMutable());
            else
                retTy = gp;
        }

        auto instantiatedFuncType = context->typeContext->getFunction(paramTypes, retTy);

        // Arg count (params[0] is self).
        size_t expectedArgs = paramTypes.size() > 0 ? paramTypes.size() - 1 : 0;
        if (node->args.size() != expectedArgs)
            log(*node, "method '" + node->methodName + "' expects " + std::to_string(expectedArgs) + " arguments, got " + std::to_string(node->args.size()) + ".");

        // Borrow-check: the receiver is borrowed for the call (a temporary
        // borrow); register it BEFORE the args so an arg conflicting with it is
        // caught. The self param is a reference (asserted below).
        if (!paramTypes.empty())
        {
            if (auto refTy = std::dynamic_pointer_cast<ReferenceType>(paramTypes[0]))
            {
                std::string rroot;
                std::vector<std::string> rpath;
                if (extractRootAndPath(node->object.get(), rroot, rpath))
                {
                    if (auto *rsym = SymbolTable::getInstance().lookupSymbol(rroot))
                        registerBorrow(rroot, rpath, refTy->isMutableRef(), /*isPromoted=*/false, *node, /*isTwoPhase=*/true);
                }
            }
        }

        // Type-check args against params[1..] (params[0] is the receiver).
        checkCallArgs(node->args, paramTypes,
            /*paramOffset=*/1, *node, /*explainUninferredGeneric=*/false);

        // Insert self as arg[0] (a &mut/& reference to the receiver).
        if (paramTypes.empty() || !std::dynamic_pointer_cast<ReferenceType>(paramTypes[0]))
        {
            log(*node, "internal: method '" + node->methodName + "' has a non-reference self.");
            node->type = voidTy;
            return;
        }
        // Insert self as arg[0]. A reference-typed receiver (`it: &mut T: Trait`)
        // is passed by VALUE (the pointer to the referent), exactly like the
        // concrete-struct branch — wrapping it as `&it` would pass the address of
        // the reference slot instead.
        auto selfRefTy = std::static_pointer_cast<ReferenceType>(paramTypes[0]);
        auto objRef = std::dynamic_pointer_cast<ReferenceType>(node->object->type);
        if (objRef && (objRef->isMutableRef() || !selfRefTy->isMutableRef()))
        {
            node->args.insert(node->args.begin(), std::move(node->object));
        }
        else
        {
            auto refExpr = std::make_unique<HIRRef>();
            refExpr->expr = std::move(node->object);
            refExpr->isMutable = selfRefTy->isMutableRef();
            refExpr->type = paramTypes[0];
            node->args.insert(node->args.begin(), std::move(refExpr));
        }

        node->type = retTy;

        // Placeholder callee — retargeted to the concrete struct by monomorphization.
        std::string funcName = "<" + gp->getParamName() + ">::" + node->methodName;
        auto callee = std::make_unique<HIRNameRef>();
        callee->name = funcName;
        callee->type = instantiatedFuncType;
        node->callee = std::move(callee);
        return;
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::handlePrintBuiltin(HIRCall *node, const std::string &name)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    auto f64Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::F64);
    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
    auto charTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::CHAR);
    auto i8PtrTy = context->typeContext->getReference(
        context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);

    std::shared_ptr<Type> argTy = nullptr;
    size_t argCount = 1;
    if (name == "print_str")
        argTy = i8PtrTy;
    else if (name == "print_int")
        argTy = i32Ty;
    else if (name == "print_float")
        argTy = f64Ty;
    else if (name == "print_bool")
        argTy = boolTy;
    else if (name == "print_char")
        argTy = charTy;
    else if (name == "println")
    {
        argTy = nullptr;
        argCount = 0;
    }
    else
        return false; // not a builtin print

    if (node->args.size() != argCount)
    {
        log(*node, "builtin '" + name + "' expects " + std::to_string(argCount) + " argument(s), got " + std::to_string(node->args.size()) + ".");
    }

    for (auto &arg : node->args)
    {
        analyzeExpr(arg.get());
        if (argTy && arg->type && !typesCompatible(argTy, arg->type))
            log(*arg, "builtin '" + name + "' expects an argument of type '" + argTy->toString() + "', got '" + arg->type->toString() + "'.");
    }

    node->type = voidTy;
    // A non-null (Copy) type so MIR's buildNameRef/makeTempPlace is safe.
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = voidTy;
    return true;
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::handleInputBuiltin(HIRCall *node, const std::string &name)
{
    std::shared_ptr<Type> retTy;
    if (name == "read_line")
        retTy = context->typeContext->getReference(
            context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);
    else if (name == "read_int")
        retTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    else if (name == "read_f64")
        retTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::F64);
    else
        return false; // not an input builtin

    if (!node->args.empty())
        log(*node, "builtin '" + name + "' takes no arguments.");
    node->type = retTy;
    // A non-null type so MIR's buildNameRef/makeTempPlace is safe.
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = retTy;
    return true;
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::handleHeapBuiltin(HIRCall *node, const std::string &name)
{
    auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    auto i8Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8);
    auto mutI8Ptr = context->typeContext->getPointer(i8Ty, /*isMutable=*/true);
    auto shI8Ptr = context->typeContext->getPointer(i8Ty, /*isMutable=*/false);
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    // The unsafe core is stdlib-only. These take RAW pointers and lower straight
    // to libc malloc/free/memcpy/strlen with no bounds, lifetime or aliasing
    // checking anywhere; keeping every call site inside <lstdlib> is what makes
    // the language's heap use auditable.
    if (!inStdLib())
    {
        log(*node, "the heap primitive '" + name + "' can only be called from the standard library (it is the compiler's unsafe core; user code goes through stdlib types such as String).", E_UnsafeBuiltinOutsideStdlib);
        node->type = voidTy;
        if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
            nr->type = voidTy;
        return true;
    }

    // `__sizeof(x)` is a TYPE query, not a call: it yields the size of the
    // argument type in bytes as a compile-time constant (no runtime access).
    // A generic container needs it because a byte count is what malloc/memcpy
    // take, while `T` is only known to the standard library as a parameter.
    if (name == "__sizeof")
    {
        if (node->args.size() != 1)
            log(*node, "builtin '__sizeof' expects 1 argument, got " + std::to_string(node->args.size()) + ".");
        if (!node->args.empty())
            analyzeExpr(node->args[0].get()); // any type; NOT moved (see MIRBuilder)

        node->type = i32Ty;
        if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
            nr->type = i32Ty;
        return true;
    }

    // `__drop(x)` releases the value at a PLACE right now — the container-side
    // counterpart of the scope-end drop glue, and the only way a standard
    // library collection can run its ELEMENTS' destructors (a Vec owns an
    // arbitrary number of them behind one raw pointer). It CONSUMES the place:
    // MIR lowers it to the ordinary MIRStmtDrop, whose bookkeeping (a local's
    // run-time drop flags, the partial-move decomposition, the dynamic case) is
    // exactly the scope-end path, so a later scope-end drop skips it.
    if (name == "__drop")
    {
        if (node->args.size() != 1)
            log(*node, "builtin '__drop' expects 1 argument, got " + std::to_string(node->args.size()) + ".");
        if (!node->args.empty())
        {
            analyzeExpr(node->args[0].get());
            // Only a place can be dropped: `__drop(1 + 2)` has nothing to
            // release. (A place that is itself behind a failed sub-expression
            // already reported its own error, so stay quiet about it here.)
            std::string droot;
            std::vector<std::string> dpath;
            if (node->args[0]->type && !resolvePlace(node->args[0].get(), droot, dpath))
                log(*node->args[0], "builtin '__drop' expects a place (a variable, a field, an index or a dereference), which is what it releases.");
        }

        node->type = voidTy;
        if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
            nr->type = voidTy;
        return true;
    }

    // The pointer parameters below are deliberately TYPE-AGNOSTIC: at the LLVM
    // level they are all opaque `ptr`, and libc neither knows nor cares about
    // the pointee. A container therefore frees/copies a `*mut T` buffer with the
    // same primitives String uses for its `*mut i8` one. (Only the standard
    // library can call them at all, see the gate above.)
    auto isAnyPtr = [](const std::shared_ptr<Type> &ty)
    {
        return ty && (ty->getKind() == Type::Kind::Pointer || ty->getKind() == Type::Kind::Reference);
    };

    std::shared_ptr<Type> retTy = nullptr;
    std::vector<std::shared_ptr<Type>> argTys;
    bool ptrArgsAreAnyPtr = false;
    if (name == "__alloc")
    {
        // `__alloc<T>(n)` hands back a `*mut T` buffer of n BYTES (same
        // meaning as always — the generic argument only chooses the POINTER
        // TYPE, so a container can store the buffer typed instead of casting
        // it; the caller scales n by __sizeof when it allocates n elements).
        // Without an explicit argument the result is the historical
        // `*mut i8` — String relies on that spelling.
        retTy = mutI8Ptr;
        if (!node->genericParams.empty())
        {
            auto pointee = resolveType(node->genericParams[0], *node);
            if (!pointee) pointee = i8Ty;
            retTy = context->typeContext->getPointer(pointee, /*isMutable=*/true);
        }
        argTys = {i32Ty}; // size, in BYTES (scale by __sizeof for elements)
    }
    else if (name == "__free")
    {
        retTy = voidTy;
        argTys = {shI8Ptr}; // free only needs the address
        ptrArgsAreAnyPtr = true;
    }
    else if (name == "__memcpy")
    {
        retTy = mutI8Ptr;                    // returns dst (unused in .lis)
        argTys = {mutI8Ptr, shI8Ptr, i32Ty}; // dst, src, n (bytes)
        ptrArgsAreAnyPtr = true;
    }
    else if (name == "__strlen")
    {
        retTy = i32Ty;      // length (truncated from size_t)
        argTys = {shI8Ptr}; // *i8
        ptrArgsAreAnyPtr = true;
    }
    else
        return false; // not a heap builtin

    if (node->args.size() != argTys.size())
    {
        log(*node, "builtin '" + name + "' expects " + std::to_string(argTys.size()) + " argument(s), got " + std::to_string(node->args.size()) + ".");
    }
    for (size_t i = 0; i < node->args.size() && i < argTys.size(); ++i)
    {
        analyzeExpr(node->args[i].get());
        const auto &argTy = node->args[i]->type;
        // A pointer parameter accepts ANY pointee (see ptrArgsAreAnyPtr); every
        // other parameter keeps the strict check.
        const bool ok = argTy && (ptrArgsAreAnyPtr && argTys[i]->getKind() == Type::Kind::Pointer
                                      ? isAnyPtr(argTy)
                                      : typesCompatible(argTys[i], argTy));
        if (argTy && !ok)
            log(*node->args[i], "builtin '" + name + "' expects argument of type '" + argTys[i]->toString() + "', got '" + argTy->toString() + "'.");
    }

    node->type = retTy;
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = retTy;
    return true;
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::inStdLib() const
{
    // The FILE of the item being analyzed decides (module items keep their own
    // path). Both sides are canonicalized so mixed separators or relative
    // segments cannot defeat the check, and an unresolvable path is treated as
    // NOT stdlib — the gate fails closed, never open.
    if (currentFilePath_.empty())
        return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path file = fs::weakly_canonical(fs::path(currentFilePath_), ec);
    if (ec)
        return false;
    for (const auto &dir : context->stdLibDirs)
    {
        std::error_code ec2;
        fs::path base = fs::weakly_canonical(fs::path(dir), ec2);
        if (ec2)
            continue;
        auto rel = file.lexically_relative(base);
        if (rel.empty())
            continue; // not comparable (different drive / no relation)
        // `rel` starting with ".." means the file is OUTSIDE `base`. Compare the
        // first component as a path (never as a narrow string: this toolchain
        // builds with -DUNICODE, so native() is a wstring).
        if (*rel.begin() == "..")
            continue;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::handlePtrBuiltin(HIRCall *node, const std::string &name)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    // Same unsafe-core boundary as the heap primitives: converting an address
    // into a borrow is exactly the step the borrow checker cannot verify.
    if (!inStdLib())
    {
        log(*node, "the raw-pointer conversion '" + name + "' can only be used inside the standard library (user code cannot turn an address into a reference).", E_PointerOpOutsideStdlib);
        node->type = voidTy;
        if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
            nr->type = voidTy;
        return true;
    }

    if (node->args.size() != 1)
    {
        log(*node, "builtin '" + name + "' expects 1 argument, got " + std::to_string(node->args.size()) + ".");
        node->type = voidTy;
        if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
            nr->type = voidTy;
        return true;
    }

    analyzeExpr(node->args[0].get());
    auto argTy = node->args[0]->type;
    auto pt = std::dynamic_pointer_cast<PointerType>(argTy);
    bool bad = false;
    if (!pt)
    {
        log(*node->args[0], "builtin '" + name + "' expects a raw pointer argument, got '" + (argTy ? argTy->toString() : std::string("?")) + "'.");
        bad = true;
    }
    else if (name == "__deref_mut" && !pt->isMutablePtr())
    {
        log(*node->args[0], "builtin '__deref_mut' expects a '*mut T' argument (a '*T' cannot produce a mutable reference).");
        bad = true;
    }
    if (bad)
    {
        node->type = voidTy;
        if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
            nr->type = voidTy;
        return true;
    }

    // The pointee drives the result type — no generics machinery needed.
    auto retTy = context->typeContext->getReference(pt->getBaseType(), /*isMutable=*/name == "__deref_mut");
    node->type = retTy;
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = retTy;
    return true;
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::handleToStringBuiltin(HIRCall *node, const std::string &name)
{
    auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    auto i64Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I64);
    auto f64Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::F64);
    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
    auto charTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::CHAR);

    std::shared_ptr<Type> argTy = nullptr;
    if (name == "to_string_i32")
        argTy = i32Ty;
    else if (name == "to_string_i64")
        argTy = i64Ty;
    else if (name == "to_string_f64")
        argTy = f64Ty;
    else if (name == "to_string_bool")
        argTy = boolTy;
    else if (name == "to_string_char")
        argTy = charTy;
    else
        return false; // not a to_string builtin

    // The return type is the stdlib String struct (its module-prefixed internal
    // name — the `string` module must be imported for the type to exist).
    auto strOpt = context->typeContext->getCustom("string$String");
    if (!strOpt.has_value())
    {
        log(*node, "builtin '" + name + "' requires the stdlib 'String' type (add `impt string;`).");
        return false;
    }

    if (node->args.size() != 1)
        log(*node, "builtin '" + name + "' expects 1 argument, got " + std::to_string(node->args.size()) + ".");
    if (!node->args.empty())
    {
        analyzeExpr(node->args[0].get());
        if (node->args[0]->type && !node->args[0]->type->equals(argTy))
            log(*node->args[0], "builtin '" + name + "' expects an argument of type '" + argTy->toString() + "', got '" + node->args[0]->type->toString() + "'.");
    }

    node->type = strOpt.value();
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = strOpt.value();
    return true;
}

bool HIRSemanticAnalyzer::isNeverType(const std::shared_ptr<Type> &ty)
{
    if (!ty || ty->getKind() != Type::Kind::Primitive) return false;
    return std::static_pointer_cast<PrimitiveType>(ty)->getPrimKind() == PrimitiveType::PrimKind::NEVER;
}

// ---------------------------------------------------------------------------
bool HIRSemanticAnalyzer::handlePanicBuiltin(HIRCall *node, const std::string &name)
{
    if (name != "panic")
        return false; // not the panic builtin

    auto i8PtrTy = context->typeContext->getReference(
        context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);
    auto neverTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::NEVER);

    if (node->args.size() != 1)
        log(*node, "builtin 'panic' expects 1 argument, got " + std::to_string(node->args.size()) + ".");

    for (auto &arg : node->args)
    {
        analyzeExpr(arg.get());
        if (arg->type && !typesCompatible(i8PtrTy, arg->type))
            log(*arg, "builtin 'panic' expects an argument of type '" + i8PtrTy->toString() + "', got '" + arg->type->toString() + "'.");
    }

    node->type = neverTy;
    // A non-null type so MIR's buildNameRef/makeTempPlace is safe.
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = neverTy;
    return true;
}

// ---------------------------------------------------------------------------
// Builtin `assert(cond)` / `assert(cond, msg)`.
//
// It writes `file:line: assertion failed: <msg>` to stderr and aborts — the
// same fatal path as `panic`, which is what makes it usable as the assertion of
// a test suite (there was no way to assert anything without it). The return type
// is VOID, not never: control flow continues when the condition holds, so the
// statements after an assert stay reachable.
bool HIRSemanticAnalyzer::handleAssertBuiltin(HIRCall *node, const std::string &name)
{
    if (name != "assert")
        return false; // not the assert builtin

    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
    auto i8PtrTy = context->typeContext->getReference(
        context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    if (node->args.empty() || node->args.size() > 2)
        log(*node, "builtin 'assert' expects 1 or 2 arguments (a condition, and an optional message), got "
                       + std::to_string(node->args.size()) + ".");

    for (size_t i = 0; i < node->args.size(); ++i)
    {
        analyzeExpr(node->args[i].get());
        if (i == 0 && node->args[i]->type && !typesCompatible(boolTy, node->args[i]->type))
            log(*node->args[i], "builtin 'assert' expects a 'bool' condition, got '"
                                    + node->args[i]->type->toString() + "'.");
        if (i == 1 && node->args[i]->type && !typesCompatible(i8PtrTy, node->args[i]->type))
            log(*node->args[i], "builtin 'assert' expects the message to be a '&i8' (a string literal), got '"
                                    + node->args[i]->type->toString() + "'.");
    }

    node->type = voidTy;
    // A non-null type so MIR's buildNameRef/makeTempPlace is safe.
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = voidTy;
    return true;
}

// ---------------------------------------------------------------------------
// Builtin C-string helpers over `&i8` (the language's C-string spelling):
// `str_len` = libc strlen, `str_cmp` = libc strcmp (negative / 0 / positive).
bool HIRSemanticAnalyzer::handleStrBuiltin(HIRCall *node, const std::string &name)
{
    if (name != "str_len" && name != "str_cmp")
        return false; // not a string builtin

    auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    auto i8PtrTy = context->typeContext->getReference(
        context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);

    const size_t expected = (name == "str_len") ? 1 : 2;
    if (node->args.size() != expected)
        log(*node, "builtin '" + name + "' expects " + std::to_string(expected)
                       + " argument(s), got " + std::to_string(node->args.size()) + ".");

    for (auto &arg : node->args)
    {
        analyzeExpr(arg.get());
        if (arg->type && !typesCompatible(i8PtrTy, arg->type))
            log(*arg, "builtin '" + name + "' expects a '&i8' argument (a C string), got '"
                          + arg->type->toString() + "'.");
    }

    node->type = i32Ty;
    // A non-null type so MIR's buildNameRef/makeTempPlace is safe.
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = i32Ty;
    return true;
}

void HIRSemanticAnalyzer::visit(HIRCall *node)
{
    // Idempotency: once a call is resolved, re-analysis is a no-op. This
    // matters when a method-call result is an ARGUMENT of a generic function
    // (`take(s.get())`) — it is analyzed by both inferGenericArguments and the
    // arg loop, and the second pass would deref the method receiver that was
    // already MOVED into args[0] (a null `node->object`). The explicit flag
    // (not `node->type`) distinguishes "not yet analyzed" from "analyzed as
    // VOID" — some builtin paths set the callee's type but leave the call's
    // type null until a later branch.
    if (node->analyzed)
        return;
    node->analyzed = true;

    switch (node->callKind)
    {
    // ---- Regular function call -------------------------------------------
    case HIRCall::CallKind::Regular:
    {
        // Builtin print/input/heap functions — recognized by name before the
        // normal callee resolution (a NameRef to `print_int` has no symbol, so
        // it would log "undefined identifier").
        if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        {
            switch (classifyBuiltin(nr->name))
            {
            case BuiltinCategory::Print:
                handlePrintBuiltin(node, nr->name);
                return;
            case BuiltinCategory::Input:
                handleInputBuiltin(node, nr->name);
                return;
            case BuiltinCategory::Heap:
                handleHeapBuiltin(node, nr->name);
                return;
            case BuiltinCategory::Ptr:
                handlePtrBuiltin(node, nr->name);
                return;
            case BuiltinCategory::ToString:
                handleToStringBuiltin(node, nr->name);
                return;
            case BuiltinCategory::Panic:
                handlePanicBuiltin(node, nr->name);
                return;
            case BuiltinCategory::Assert:
                handleAssertBuiltin(node, nr->name);
                return;
            case BuiltinCategory::Str:
                handleStrBuiltin(node, nr->name);
                return;
            case BuiltinCategory::NotBuiltin:
                break;
            }
        }
        analyzeExpr(node->callee.get());
        auto funcType = std::dynamic_pointer_cast<FunctionType>(node->callee->type);
        if (!funcType)
        {
            log(*node, "attempt to call a non-function.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }
        if (node->args.size() != funcType->getParams().size())
            log(*node, "argument count mismatch.");

        std::shared_ptr<FunctionType> instantiatedFuncType;
        if (funcType->isGeneric())
        {
            // 1. 获取函数定义的泛型参数列表
            const auto &genericParams = funcType->getGenericParams();
            // 2. 处理手动指定的泛型参数 / 自动推断
            std::vector<std::shared_ptr<Type>> genericArgs;

            if (!node->genericParams.empty())
            {
                // 手动指定泛型参数：解析原始类型为具体类型
                for (const auto &rawTy : node->genericParams)
                {
                    auto ty = resolveType(rawTy, *node);
                    if (!ty)
                    {
                        log(*node, "invalid generic parameter type: " + rawTy.name);
                        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                        return;
                    }
                    genericArgs.push_back(ty);
                }

                // 校验泛型参数数量
                if (genericArgs.size() != genericParams.size())
                {
                    log(*node, "generic parameter count mismatch: expected " + std::to_string(genericParams.size()) + ", got " + std::to_string(genericArgs.size()));
                    node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                    return;
                }
            }
            else
            {
                // 无手动指定：从调用参数自动推断泛型
                genericArgs = inferGenericArguments(genericParams, funcType->getParams(), node->args);

                // 推断失败处理
                if (genericArgs.size() != genericParams.size())
                {
                    log(*node, "failed to infer generic parameters, please specify them explicitly");
                    node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                    return;
                }
            }

            for (size_t i = 0; i < genericArgs.size(); i++)
            {
                const auto &constraints = std::static_pointer_cast<GenericParamType>(funcType->getGenericParams()[i])->getConstraints();

                for (auto &constraint : constraints)
                {
                    bool finded = false;

                    for (auto &impledTrait : genericArgs[i]->implTrait)
                    {
                        if (impledTrait->equals(constraint))
                        {
                            finded = true;
                            break;
                        }
                    }

                    if (!finded)
                    {
                        log(*node, "type '" + genericArgs[i]->toString() + "' didn't implement the trait constraint '" + constraint->toString() + "'.");
                    }
                }
            }

            // 3. 填充推断完成的泛型实参
            node->typedGenericParams = genericArgs;
            // 4. 实例化泛型函数类型（替换泛型参数为具体类型）
            instantiatedFuncType = instantiateGenericFunction(funcType, genericArgs);
        }
        else
        {
            // 非泛型函数，直接使用原类型
            instantiatedFuncType = funcType;
        }
        // ==============================================================

        // 参数数量检查
        if (node->args.size() != instantiatedFuncType->getParams().size())
            log(*node, "argument count mismatch.");

        // 参数类型检查（使用实例化后的具体类型）。Free functions get the
        // context-free-generic explanation (see checkCallArgs).
        checkCallArgs(node->args, instantiatedFuncType->getParams(),
            /*paramOffset=*/0, *node, /*explainUninferredGeneric=*/true);
        // 设置返回值类型为实例化后的类型
        node->type = instantiatedFuncType->getReturnType();
        break;
    }

    // ---- Instance method call  obj.method(args) -------------------------
    case HIRCall::CallKind::Method:
    {
        analyzeExpr(node->object.get());
        std::shared_ptr<Type> objTy = node->object->type;
        if (!objTy)
        {
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        std::shared_ptr<Type> baseTy = objTy;
        if (auto ref = std::dynamic_pointer_cast<ReferenceType>(objTy))
            baseTy = ref->getBaseType();

        // A generic param receiver (e.g. `it.next()` where `it: T: Iterator<i32>`)
        // dispatches through the param's trait bounds.
        if (auto gp = std::dynamic_pointer_cast<GenericParamType>(baseTy))
        {
            dispatchGenericParamMethod(node, gp);
            return;
        }

        auto customTy = std::dynamic_pointer_cast<CustomType>(baseTy);
        if (!customTy)
        {
            log(*node, "cannot call method on non-struct type '" + objTy->toString() + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        const auto &methods = customTy->getMethods();
        auto it = std::find_if(methods.begin(), methods.end(), [&](const CustomType::Method &m)
            { return m.name == node->methodName; });

        if (it == methods.end())
        {
            log(*node, "struct '" + customTy->getName() + "' has no method '" + node->methodName + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        if (it->isStatic)
        {
            log(*node, "'" + node->methodName + "' is a static method; use '::' to call it.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        // params[0] is self, remaining are the user-supplied args
        size_t expectedArgs = it->params.size() > 0 ? it->params.size() - 1 : 0;
        if (node->args.size() != expectedArgs)
            log(*node, "method '" + node->methodName + "' expects " + std::to_string(expectedArgs) + " arguments, got " + std::to_string(node->args.size()) + ".");

        // Method symbols are registered under the *origin* struct's name
        // (e.g. `Range::next`, not `Range$i32::next`), and MIR mangles them the
        // same way — so the callee name is 2-part `<Struct>::<method>`. The
        // trait part was dropped to stay consistent (the MIR mangleName ignores
        // associatedTrait, so a 3-part name here would never resolve).
        std::string baseName = customTy->getOriginName();
        std::string symName = baseName + "::" + node->methodName;
        Symbol *symbol = SymbolTable::getInstance().lookupSymbol(symName);

        if (!symbol)
        {
            log(*node, "method '" + node->methodName + "' not found on '" + baseName + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        auto newTy = std::static_pointer_cast<FunctionType>(symbol->type);

        // If the receiver is an instantiation of a generic struct, substitute
        // the struct's gParams -> concrete args throughout the method signature
        // before further processing.
        std::unordered_map<std::string, std::shared_ptr<Type>> structSubst;
        if (!customTy->getGenericArgs().empty() && customTy->genericOrigin)
        {
            const auto &gps = customTy->genericOrigin->getGenericParams();
            const auto &gas = customTy->getGenericArgs();
            for (size_t i = 0; i < gps.size() && i < gas.size(); ++i)
            {
                auto gp = std::static_pointer_cast<GenericParamType>(gps[i]);
                structSubst[gp->getParamName()] = gas[i];
            }
            // An impl-level bound (`impl<T: Copy> Foo<T> { fn bar(self) }`) is a
            // property of THIS instantiation, and the substitution below is what
            // erases the evidence — so check it here.
            if (!checkMethodGenericBounds(newTy, structSubst, *node, customTy->getOriginName() + "::" + node->methodName))
            {
                node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                return;
            }
            newTy = std::static_pointer_cast<FunctionType>(substituteType(newTy, structSubst));
        }

        // Concrete struct args, in the struct's generic-param order. For a
        // method of a generic struct (`impl<T> Foo<T> { fn bar(self) }`), the
        // MIR method now carries these params, so the call must carry the args
        // for monomorphization to substitute them (Item B-b).
        std::vector<std::shared_ptr<Type>> structArgs = customTy->getGenericArgs();

        // The receiver is the generic DEFINITION itself (`self.grow(...)` inside
        // `impl Vec`): its arguments are the definition's own parameters (the `T`
        // in scope), so the method's struct-level generics are ALREADY known.
        // Without this, inference had to find `T` among the VALUE parameters
        // (`grow` takes only an i32) and always failed with "failed to infer
        // generic method params" — which made every generic-struct method with
        // arguments uncallable from inside the type.
        std::vector<std::shared_ptr<Type>> definitionArgs;
        if (structArgs.empty() && customTy->isGeneric())
            for (const auto &gp : customTy->getGenericParams())
                definitionArgs.push_back(gp);

        std::shared_ptr<FunctionType> instantiatedFuncType;
        if (newTy->isGeneric())
        {
            const auto &genericParams = newTy->getGenericParams();
            std::vector<std::shared_ptr<Type>> genericArgs;

            if (!node->genericParams.empty())
            {
                for (const auto &rawTy : node->genericParams)
                {
                    auto ty = resolveType(rawTy, *node);
                    if (!ty)
                    {
                        log(*node, "invalid generic param");
                        return;
                    }
                    genericArgs.push_back(ty);
                }
                if (genericArgs.size() != genericParams.size())
                {
                    log(*node, "generic param count mismatch");
                    return;
                }
            }
            else if (!definitionArgs.empty() && definitionArgs.size() == genericParams.size())
            {
                // The definition's parameters ARE the arguments (see above); no
                // inference is possible or needed.
                genericArgs = definitionArgs;
            }
            else
            {
                std::vector<std::shared_ptr<Type>> methodParams = newTy->getParams();
                if (!methodParams.empty()) methodParams.erase(methodParams.begin());

                genericArgs = inferGenericArguments(genericParams, methodParams, node->args);
                if (genericArgs.size() != genericParams.size())
                {
                    log(*node, "failed to infer generic method params");
                    return;
                }
            }

            node->typedGenericParams = structArgs;
            node->typedGenericParams.insert(node->typedGenericParams.end(),
                genericArgs.begin(),
                genericArgs.end());
            instantiatedFuncType = instantiateGenericFunction(newTy, genericArgs);
        }
        else
        {
            node->typedGenericParams = structArgs;
            instantiatedFuncType = newTy;
        }

        // The self param kind is known before the args so the receiver borrow can
        // be registered first (a `&self` / `&mut self` receiver borrows the place
        // for the call; by-value `self` moves it).
        auto selfParamTy = instantiatedFuncType->getParams()[0];

        // Borrow-check: register the receiver borrow BEFORE the args, so an arg
        // that conflicts with the receiver (e.g. `x.m(x)` moving x into an arg)
        // is caught. A `&mut self` receiver must be a mutable place.
        if (auto refTy = std::dynamic_pointer_cast<ReferenceType>(selfParamTy))
        {
            std::string rroot;
            std::vector<std::string> rpath;
            if (extractRootAndPath(node->object.get(), rroot, rpath))
            {
                if (auto *rsym = SymbolTable::getInstance().lookupSymbol(rroot))
                {
                    if (refTy->isMutableRef())
                    {
                        bool mut = rsym->isMutable.has_value() && *rsym->isMutable;
                        if (auto rty = std::dynamic_pointer_cast<ReferenceType>(rsym->type))
                            mut = mut || rty->isMutableRef();
                        if (!mut)
                            log(*node, "cannot borrow '" + rroot + "' as mutable because it is not mutable", E_CannotBorrowAsMutable);
                    }
                    registerBorrow(rroot, rpath, refTy->isMutableRef(), /*isPromoted=*/false, *node, /*isTwoPhase=*/true);
                }
            }
        }

        // params[0] is the receiver, which is not an argument.
        checkCallArgs(node->args, instantiatedFuncType->getParams(),
            /*paramOffset=*/1, *node, /*explainUninferredGeneric=*/false);

        // Insert the receiver as arg[0]. A `&self` / `&mut self` method borrows
        // the receiver (HIRRef); a BY-VALUE `self` method (`fn drop(self)`)
        // consumes it — the receiver is moved in directly (MIR's placeToOperand
        // marks it moved). Guard the self-param kind first: the old unchecked
        // static_pointer_cast<ReferenceType> was UB for by-value self.
        if (auto refTy = std::dynamic_pointer_cast<ReferenceType>(selfParamTy))
        {
            // If the receiver is ALREADY a reference (`m.add(5)` where `m: &mut S`),
            // pass its VALUE directly — the `&mut self` / `&self` param must receive
            // the pointer to the referent. Wrapping it as `&m` would hand over a
            // pointer to the reference SLOT (a `&&mut S`), so mutations never reach
            // the referent. A mutable reference may also serve an immutable `&self`
            // (both are `ptr` at runtime). The reverse (`&S` receiver for a `&mut`
            // self) was already rejected above by the mutability check.
            auto objRef = std::dynamic_pointer_cast<ReferenceType>(node->object->type);
            if (objRef && (objRef->isMutableRef() || !refTy->isMutableRef()))
            {
                node->args.insert(node->args.begin(), std::move(node->object));
            }
            else
            {
                auto refExpr = std::make_unique<HIRRef>();
                refExpr->expr = std::move(node->object);
                refExpr->isMutable = refTy->isMutableRef();
                refExpr->type = selfParamTy;
                node->args.insert(node->args.begin(), std::move(refExpr));
            }
        }
        else
        {
            if (node->object->type && !typesCompatible(selfParamTy, node->object->type))
                log(*node, "receiver type mismatch for by-value self method.");
            // By-value self consumes the receiver (`x.drop()` moves x).
            HIRExpr *obj = node->object.get();
            handleMoveSource(obj, *node);
            node->args.insert(node->args.begin(), std::move(node->object));
        }

        node->type = instantiatedFuncType->getReturnType();

        std::string funcName = baseName + "::" + node->methodName;
        auto callee = std::make_unique<HIRNameRef>();
        callee->name = funcName;
        callee->symbol = SymbolTable::getInstance().lookupSymbol(funcName);
        if (callee->symbol) callee->type = callee->symbol->type;
        node->callee = std::move(callee);
        break;
    }

    // ---- Static method call  Type::method(args) -------------------------
    case HIRCall::CallKind::Static:
    {
        // Module-aware type lookup: bare name → selective-import alias symbol
        // (forwarding to the module's internal type) → current module prefix.
        auto custom = context->typeContext->getCustom(node->staticTypeName);
        if (!custom.has_value() && !isInternalName(node->staticTypeName) && !currentModule_.empty())
            custom = context->typeContext->getCustom(internalName(currentModule_, node->staticTypeName));
        if (!custom.has_value())
        {
            if (auto *sym = lookupModuleAware(node->staticTypeName))
            {
                if (sym->type && sym->type->getKind() == Type::Kind::Custom)
                    custom = std::static_pointer_cast<CustomType>(sym->type);
            }
        }
        if (!custom.has_value())
        {
            log(*node, "cannot find type '" + node->staticTypeName + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        // `Vec<i32>::new()`: the class type carries use-site generic arguments,
        // so resolve name + args as ONE type (resolveType runs the declared-bound
        // check) and work with the INSTANTIATION from here on. An instantiation
        // keeps the origin's method list (getMethods) and origin name, while
        // getGenericArgs() yields the struct's concrete arguments — exactly what
        // the signature substitution and monomorphization below need. Without
        // this a static call on a generic struct stayed a bare definition and
        // every call died with "failed to infer generic static method params".
        std::shared_ptr<Type> classTy = custom.value();
        if (!node->staticGenericArgs.empty())
        {
            HIRRawType rawClassTy;
            rawClassTy.isPresent = true;
            rawClassTy.isPrimitive = false;
            rawClassTy.name = node->staticTypeName;
            rawClassTy.genericArgs = node->staticGenericArgs;
            classTy = resolveType(rawClassTy, *node);
        }

        auto customTy = std::dynamic_pointer_cast<CustomType>(classTy);
        if (!customTy)
        {
            // resolveType's own instantiation failures come back as VOID and were
            // already reported; only "not a struct at all" is new here.
            if (!classTy || classTy->getKind() != Type::Kind::Primitive)
                log(*node, "type '" + node->staticTypeName + "' is not a struct.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        const auto &methods = customTy->getMethods();
        auto it = std::find_if(methods.begin(), methods.end(), [&](const CustomType::Method &m)
            { return m.name == node->methodName; });

        if (it == methods.end())
        {
            log(*node, "struct '" + node->staticTypeName + "' has no method '" + node->methodName + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        if (!it->isStatic)
        {
            log(*node, "'" + node->methodName + "' is not static; use '.' to call it.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        // The method symbol is keyed by the type's INTERNAL name (e.g.
        // `string$String::from_lit`), not the source spelling. The ORIGIN name:
        // an instantiation is mangled (`Vec$i32`), and methods live on the
        // definition (same rule as the Method branch).
        std::string funcName = customTy->getOriginName() + "::" + node->methodName;
        Symbol *symbol = SymbolTable::getInstance().lookupSymbol(funcName);
        if (!symbol)
        {
            log(*node, "static method symbol not found: " + funcName);
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }
        auto funcType = std::static_pointer_cast<FunctionType>(symbol->type);

        // A static method of a generic struct is written against the struct's
        // parameters (`fn new() -> Vec<T>`), so substitute params → the
        // instantiation's args before anything reads the signature. Mirrors the
        // Method branch; the substituted type is non-generic, which is why the
        // call's generic args are the STRUCT's below.
        std::unordered_map<std::string, std::shared_ptr<Type>> structSubst;
        if (!customTy->getGenericArgs().empty() && customTy->genericOrigin)
        {
            const auto &gps = customTy->genericOrigin->getGenericParams();
            const auto &gas = customTy->getGenericArgs();
            for (size_t i = 0; i < gps.size() && i < gas.size(); ++i)
            {
                auto gp = std::static_pointer_cast<GenericParamType>(gps[i]);
                structSubst[gp->getParamName()] = gas[i];
            }
            // Same impl-level bound check as the Method branch (and before the
            // substitution, which drops the generic params).
            if (!checkMethodGenericBounds(funcType, structSubst, *node, customTy->getOriginName() + "::" + node->methodName))
            {
                node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                return;
            }
            funcType = std::static_pointer_cast<FunctionType>(substituteType(funcType, structSubst));
        }

        // ===================== 泛型参数推断与填充 =====================
        // Mirror the Method branch: always carry the struct's concrete generic
        // args, appending the method's own args when the method is generic.
        // Without this, a static call on a generic struct never reaches
        // monomorphization. Concrete class args, in the struct's generic-param
        // order — the
        // monomorphization carrier for a static method of a generic struct
        // (`Vec<i32>::new()` → [i32]). Empty for a bare-name call on the
        // definition, where the struct's args still have to be inferred from the
        // value arguments (`box::new(10)` → [i32]) exactly as before.
        std::vector<std::shared_ptr<Type>> structArgs = customTy->getGenericArgs();

        std::shared_ptr<FunctionType> instantiatedFuncType;
        if (funcType->isGeneric())
        {
            const auto &genericParams = funcType->getGenericParams();
            std::vector<std::shared_ptr<Type>> genericArgs;

            if (!node->genericParams.empty())
            {
                for (const auto &rawTy : node->genericParams)
                {
                    auto ty = resolveType(rawTy, *node);
                    if (!ty)
                    {
                        log(*node, "invalid generic param");
                        return;
                    }
                    genericArgs.push_back(ty);
                }
                if (genericArgs.size() != genericParams.size())
                {
                    log(*node, "generic param count mismatch");
                    return;
                }
            }
            else
            {
                genericArgs = inferGenericArguments(genericParams, funcType->getParams(), node->args);
                if (genericArgs.size() != genericParams.size())
                {
                    log(*node, "failed to infer generic static method params");
                    return;
                }
            }

            node->typedGenericParams = structArgs;
            node->typedGenericParams.insert(node->typedGenericParams.end(),
                genericArgs.begin(),
                genericArgs.end());
            instantiatedFuncType = instantiateGenericFunction(funcType, genericArgs);
        }
        else
        {
            node->typedGenericParams = structArgs;
            instantiatedFuncType = funcType;
        }

        if (node->args.size() != it->params.size())
            log(*node, "static method '" + node->methodName + "' expects " + std::to_string(it->params.size()) + " arguments, got " + std::to_string(node->args.size()) + ".");

        checkCallArgs(node->args, instantiatedFuncType->getParams(),
            /*paramOffset=*/0, *node, /*explainUninferredGeneric=*/false);

        node->type = instantiatedFuncType->getReturnType();

        auto callee = std::make_unique<HIRNameRef>();
        callee->name = funcName;
        callee->symbol = SymbolTable::getInstance().lookupSymbol(funcName);
        if (callee->symbol) callee->type = callee->symbol->type;
        node->callee = std::move(callee);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRMemberAccess *node)
{
    analyzeExpr(node->object.get());
    auto objTy = node->object->type;
    if (!objTy) return;

    if (auto ref = std::dynamic_pointer_cast<ReferenceType>(objTy))
        objTy = ref->getBaseType();

    // No invented type on the failure paths below. An expression whose
    // analysis failed has NO type: null is this file's established 'analysis
    // failed' state (see the `if (!objTy) return;` above), and
    // typesCompatible() treats a null operand as compatible — so one real
    // error does not cascade into a second, misleading one at every
    // enclosing use (`ret g(s.nope)` used to add a bogus "argument type
    // mismatch" because the fallback was a hard-coded i32), and the LSP has
    // nothing wrong to show on hover.
    auto ct = std::dynamic_pointer_cast<CustomType>(objTy);
    if (!ct)
    {
        log(*node, "member access on non-struct type '" + objTy->toString() + "'.");
        return;
    }

    // For instantiations, the fields stored on the instantiation are already substituted
    // (TypeContext::instantiateCustom did this). So we can read them directly.
    const auto &fields = ct->getFields();
    auto it = std::find(fields.begin(), fields.end(), node->memberName);
    if (it != fields.end())
    {
        // Visibility: a private field is only reachable from a non-static method
        // of the declaring type.
        checkFieldAccess(*it, ct, *node);
        node->type = it->type;
    }
    else
        log(*node, "struct '" + ct->getName() + "' has no field '" + node->memberName + "'.");

    // Borrow-check: a Copy field read conflicts with active &mut borrows. The
    // access is resolved to its PLACE — alias-resolved, so a read through a
    // reference (`*r`, `(*r).a`) is checked against the referent, not against the
    // reference binding. Non-Copy field moves are checked at consuming sites.
    std::string root;
    std::vector<std::string> path;
    if (node->type && node->type->isCopyable()
        && resolvePlace(node, root, path))
        checkBorrowUse(root, path, BorrowUseKind::Read, *node);
}

// ---------------------------------------------------------------------------
// `obj[i]` on a user type: the index operator traits. Kept next to
// resolveOperatorMethod and built the same way (symbol under the ORIGIN name +
// struct-argument substitution), so indexing and the binary operators share one
// mental model.
// An impl's bounds are checked at the CALL SITE, not by conformance: implTrait
// is registered per TYPE (with the struct's parameter inside, later substituted
// per instantiation), so `Vec<String>` still "implements" Index — the bound on
// the impl's re-declared T is what says that instantiation may not use it.
bool HIRSemanticAnalyzer::checkMethodGenericBounds(
    const std::shared_ptr<FunctionType> &fnType,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &subst,
    HIRNode &errNode, const std::string &owner)
{
    if (!fnType) return true;

    for (const auto &gpRaw : fnType->getGenericParams())
    {
        auto gp = std::dynamic_pointer_cast<GenericParamType>(gpRaw);
        if (!gp || gp->getConstraints().empty()) continue;

        // Not instantiated at this call site (the parameter is still generic,
        // e.g. a method of a generic function's body): its own caller checks it.
        auto it = subst.find(gp->getParamName());
        if (it == subst.end() || !it->second) continue;
        const std::shared_ptr<Type> &arg = it->second;

        for (const auto &constraint : gp->getConstraints())
        {
            bool satisfied = false;
            for (const auto &impl : arg->implTrait)
                if (impl->equals(constraint))
                {
                    satisfied = true;
                    break;
                }
            if (satisfied) continue;

            log(errNode, "type '" + arg->toString() + "' does not implement trait '"
                             + displayName(constraint->getName()) + "' required by '" + owner + "'.");
            return false;
        }
    }
    return true;
}

bool HIRSemanticAnalyzer::resolveIndexMethod(HIRIndexAccess *node,
    const std::shared_ptr<CustomType> &ct,
    bool forWrite)
{
    const char *traitName = forWrite ? "IndexMut" : "Index";
    const char *methodName = forWrite ? "set" : "at";

    if (!ct->implementsTrait(traitName)) return false;

    std::string baseName = ct->getOriginName();
    std::string symName = baseName + "::" + methodName;
    Symbol *symbol = SymbolTable::getInstance().lookupSymbol(symName);
    if (!symbol)
    {
        log(*node, "type '" + baseName + "' implements '" + traitName + "' but no method '"
                       + methodName + "' is registered on it.");
        return true; // reported; do not also emit the "not indexable" error
    }

    auto newTy = std::static_pointer_cast<FunctionType>(symbol->type);

    // A generic instantiation (`Vec$i32`) needs its gParams substituted into the
    // method signature before the operand check below (mirrors
    // resolveOperatorMethod).
    std::unordered_map<std::string, std::shared_ptr<Type>> structSubst;
    if (!ct->getGenericArgs().empty() && ct->genericOrigin)
    {
        const auto &gps = ct->genericOrigin->getGenericParams();
        const auto &gas = ct->getGenericArgs();
        for (size_t i = 0; i < gps.size() && i < gas.size(); ++i)
            structSubst[std::static_pointer_cast<GenericParamType>(gps[i])->getParamName()] = gas[i];

        // The impl's own bounds first (and BEFORE the substitution, which drops
        // the generic params): `impl<T: Copy> Index<T> for Vec<T>` must not be
        // usable on `Vec<String>` — the trait hands the element out BY VALUE, so
        // a non-Copy element would be moved out while the container still owned
        // it (a double drop at teardown).
        if (!checkMethodGenericBounds(newTy, structSubst, *node, symName))
        {
            log(*node, "indexing '" + ct->toString()
                           + "' is only allowed for Copy elements: the index operator returns the element by value. Use at_ref/at_mut (they lend a reference) or a move-out method (pop/remove) instead.");
            return true; // reported; do not also emit the "not indexable" error
        }

        newTy = std::static_pointer_cast<FunctionType>(substituteType(newTy, structSubst));
    }

    const size_t expected = forWrite ? 3 : 2;
    if (newTy->getParams().size() != expected)
    {
        log(*node, std::string("index method '") + methodName + "' must take "
                       + (forWrite ? "3 parameters (self, i, v)" : "2 parameters (self, i)")
                       + ", got " + std::to_string(newTy->getParams().size()) + ".");
        return true;
    }

    auto selfRef = std::dynamic_pointer_cast<ReferenceType>(newTy->getParams()[0]);
    if (!selfRef)
    {
        log(*node, std::string("index method '") + methodName + "' must take a reference receiver.");
        return true;
    }
    if (forWrite && !selfRef->isMutableRef())
    {
        log(*node, "index method 'set' must take '&mut Self' (writing through the index mutates the container).");
        return true;
    }
    if (!selfRef->getBaseType()->equals(ct))
    {
        log(*node, std::string("index method '") + methodName + "' receiver type does not match '" + baseName + "'.");
        return true;
    }

    auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    if (node->index->type && !node->index->type->equals(i32Ty))
        log(*node->index, "array index must be of type 'i32', got '" + node->index->type->toString() + "'.");

    // `v[i]` denotes a PLACE of the element type in both directions: the write
    // form yields `set`'s third parameter (`T`), not `set`'s void return —
    // otherwise `v[0] = 50` reported "assignment type mismatch" (void vs i32).
    node->type = forWrite ? newTy->getParams()[2] : newTy->getReturnType();

    if (forWrite)
    {
        node->setMethodName = symName;
        node->setMethodType = newTy;
        node->setStructArgs = ct->getGenericArgs();
    }
    else
    {
        node->indexMethod = symbol;
        node->indexMethodName = symName;
        node->indexMethodType = newTy;
        node->indexStructArgs = ct->getGenericArgs();
    }
    return true;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRIndexAccess *node)
{
    analyzeExpr(node->object.get());
    analyzeExpr(node->index.get());
    auto objTy = node->object->type;
    if (!objTy) return;

    // Same convention as visit(HIRMemberAccess): no invented type, null it is.
    // (The old `void` fallback produced a second, bogus diagnostic —
    // `let y = x[0];` on an int reported 'variable y cannot have type void').

    // The element type comes from the place UNDER the references: an array
    // [T; N] yields T (bounds-checked), a RAW POINTER *T / *mut T yields T as
    // unchecked C pointer arithmetic — legal only in the standard library.
    // A reference to either auto-derefs (all layers: MIR's buildIndexAccess
    // walks the full chain, and sema used to stop at one, causing `&&T`
    // divergence), but a reference to a PRIMITIVE is no longer a buffer.
    bool viaReference = false;
    while (auto ref = std::dynamic_pointer_cast<ReferenceType>(objTy))
    {
        objTy = ref->getBaseType();
        viaReference = true;
    }

    // A USER TYPE is indexed through the index operator traits — the only
    // user-extensible spelling of `v[i]`. Arrays and raw pointers never come
    // here: they keep the built-in projection path below, unchanged.
    if (auto ct = std::dynamic_pointer_cast<CustomType>(objTy))
    {
        // While analyzing an ASSIGNMENT TARGET, `v[i] = x` resolves IndexMut::set
        // instead of Index::at (visit(HIRAssign) sets inAssignTarget_ around the
        // target). The node type is the element type either way, so the existing
        // assignment type check still applies.
        if (resolveIndexMethod(node, ct, inAssignTarget_))
        {
            // A COMPOUND assignment target reads the element before writing it
            // (`v[i] += x` is Index::at then IndexMut::set), so the read side
            // needs its own resolution too — an assignment target alone fills the
            // set* fields only, and the MIR read call would have no signature.
            // Both resolve to the element type, so node->type is unaffected.
            if (inAssignTarget_ && inCompoundAssignTarget_)
                resolveIndexMethod(node, ct, /*forWrite=*/false);

            // The receiver behaves like a READ of the container, not like a
            // borrow of it: the access CHECKS against the borrows already live
            // (so `let r = &mut v; let x = v[0];` is still rejected, and
            // `v.push(v[0])` passes through the receiver's two-phase
            // reservation) but CREATES none — exactly what reading a Copy
            // element does for an array (visit(HIRMemberAccess) does the same for
            // a Copy field). A created borrow would outlive the read to the end
            // of the statement and collide with the statement's own write:
            // `v[2] = v[0] + 1;` was rejected as "cannot assign to 'v[2]'
            // because it is borrowed" (E4003) by the read on its right-hand side.
            //
            // An assignment target (`v[0] = x` → IndexMut::set, `&mut self`)
            // does not even check here: visit(HIRAssign) runs the write check on
            // this very place, which is the same protection.
            if (!inAssignTarget_)
            {
                std::string rroot;
                std::vector<std::string> rpath;
                if (resolvePlace(node->object.get(), rroot, rpath))
                    checkBorrowUse(rroot, rpath, BorrowUseKind::Read, *node);
            }
        }
        else
        {
            // Keeps the "is not indexable" phrasing the reference-to-struct
            // rejection has always used (and the test asserts): an instantiation
            // reaches this same branch, and the actionable part is the trait.
            log(*node, "type '" + objTy->toString()
                           + "' is not indexable; implement the 'Index<T>' trait to make it indexable.");
        }
        return;
    }

    std::shared_ptr<Type> elemTy;
    if (auto arrTy = std::dynamic_pointer_cast<ArrayType>(objTy))
        elemTy = arrTy->getElementType(); // bounds-checked in codegen
    else if (auto ptrTy = std::dynamic_pointer_cast<PointerType>(objTy))
    {
        // C-style pointer indexing `p[i]`: unchecked address arithmetic. That is
        // what a raw pointer IS, which is why it is confined to the standard
        // library (the audited heap implementation) and why only a raw pointer
        // may do it — a `&i8` must no longer silently become a buffer.
        if (!inStdLib())
        {
            log(*node, "indexing the raw pointer '" + ptrTy->toString() + "' is only allowed inside the standard library: it is unchecked C pointer arithmetic.", E_PointerOpOutsideStdlib);
            return;
        }
        // Only primitives (a byte/small buffer), arrays or a GENERIC PARAMETER
        // are legal pointees; a pointer to a struct would hit an
        // llvm_unreachable in codegen and would otherwise let `p[0].f = x`
        // write through a shared pointer.
        //
        // The generic-param case is what a container buffer needs
        // (`data: *mut T` inside the standard library): the pointee is opaque
        // until monomorphization substitutes it, at which point the GEP/load
        // below are ordinary. It stays confined to the standard library anyway
        // (the E3014 gate above).
        const auto pointeeKind = ptrTy->getBaseType()->getKind();
        if (pointeeKind != Type::Kind::Primitive
            && pointeeKind != Type::Kind::Array
            && pointeeKind != Type::Kind::GenericParam)
        {
            log(*node, "cannot index '" + ptrTy->toString() + "' (only pointers to primitives, arrays or a generic parameter).");
            return;
        }
        elemTy = ptrTy->getBaseType();
    }
    else if (viaReference)
    {
        log(*node, "a reference to '" + objTy->toString() + "' is not indexable: C-style pointer indexing requires a raw pointer ('*" + objTy->toString() + "'), and only the standard library may do it.");
        return;
    }
    else
    {
        log(*node, "indexing a non-array type '" + objTy->toString() + "'.");
        return;
    }

    // Index must be i32.
    auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    if (node->index->type && !node->index->type->equals(i32Ty))
        log(*node->index, "array index must be of type 'i32', got '" + node->index->type->toString() + "'.");

    node->type = elemTy;

    // A Copy element read conflicts with active &mut borrows (same rule as a
    // Copy field read in visit(HIRMemberAccess)). The path carries a CONSTANT
    // index segment ("[0]") when the index is a literal — so a borrow of `a[0]`
    // does not block `a[1]` — and the wildcard "[*]" when it is not, which
    // overlaps every element (sound: the value is not known).
    std::string root;
    std::vector<std::string> path;
    if (node->type && node->type->isCopyable()
        && resolvePlace(node, root, path))
        checkBorrowUse(root, path, BorrowUseKind::Read, *node);
}

// ---------------------------------------------------------------------------
// `*p` — dereference of a REFERENCE. The result is a place, not a value: the
// read/write rules (Copy read, E3017 for a moving read, `&mut` required to
// write) are enforced where the place is USED — handleMoveSource for a moving
// read and visit(HIRAssign) for a write.
void HIRSemanticAnalyzer::visit(HIRDeref *node)
{
    analyzeExpr(node->operand.get());
    if (!node->operand->type) return; // the real error was already reported

    if (auto ref = std::dynamic_pointer_cast<ReferenceType>(node->operand->type))
    {
        node->type = ref->getBaseType();

        // A Copy read THROUGH the deref conflicts with a live exclusive borrow of
        // the same place (`let s = &mut *r; let v = *r;` — Rust rejects this with
        // E0503 while `s` is live). The WRITE case is handled by visit(HIRAssign)
        // with the same resolved place, and a non-Copy read is E3017 in
        // handleMoveSource.
        if (!inAssignTarget_ && node->type->isCopyable())
        {
            // Reading `*r` USES r and then reads the referent: both are checked,
            // and the referent check exempts r's own borrow — the very borrow
            // that makes the read legal.
            std::string holderRoot;
            std::vector<std::string> holderPath;
            bool viaHolder = derefHolderOf(node, holderRoot, holderPath);
            if (viaHolder)
                checkBorrowUse(holderRoot, holderPath, BorrowUseKind::Read, *node);

            std::vector<std::string> exempt = viaHolder ? exemptionChain(holderRoot)
                                                        : std::vector<std::string>{};
            std::string root;
            std::vector<std::string> path;
            if (resolvePlace(node, root, path))
                checkBorrowUse(root, path, BorrowUseKind::Read, *node,
                    exempt.empty() ? nullptr : &exempt);
        }
        return;
    }

    // A raw pointer is deliberately NOT dereferenceable here: `*p` on a `*T`
    // would be an unchecked load with no lifetime, which is exactly what the
    // stdlib-only `__deref` / `__deref_mut` bridge exists to keep auditable
    // (E3013/E3014). Everything else has no pointee at all.
    if (std::dynamic_pointer_cast<PointerType>(node->operand->type))
        log(*node, "cannot dereference the raw pointer '" + node->operand->type->toString()
                       + "': use the standard library's '__deref'/'__deref_mut' (user code cannot turn an address into a value).");
    else
        log(*node, "cannot dereference a value of type '" + node->operand->type->toString()
                       + "': '*p' requires a reference ('&T' or '&mut T').");
}
// ---------------------------------------------------------------------------
// Prefix VALUE operators: -x, !x, ~x (2026-09-19).
//
// All three are defined on PRIMITIVES only for now (user-type overloading needs
// a Neg/Not trait plus the operator-method plumbing, which lands separately):
//   * -  : the numeric types (i8..i64, f32/f64) -> the operand type.
//   * !  : bool -> bool.
//   * ~  : the integer types (i8..i64) -> the operand type.
// char participates in none of them (it only compares), and structs/enums are
// rejected here rather than reaching codegen.
void HIRSemanticAnalyzer::visit(HIRUnaryOp *node)
{
    analyzeExpr(node->operand.get());
    if (!node->operand->type) return; // the real error was already reported

    const auto &operandTy = node->operand->type;
    const char *op = node->opKind == HIRUnaryOp::OpKind::Neg      ? "-"
                     : node->opKind == HIRUnaryOp::OpKind::Not    ? "!"
                                                                  : "~";

    bool ok = false;
    switch (node->opKind)
    {
    case HIRUnaryOp::OpKind::Neg:
        ok = operandTy->getKind() == Type::Kind::Primitive
             && (std::static_pointer_cast<PrimitiveType>(operandTy)->isInteger()
                 || std::static_pointer_cast<PrimitiveType>(operandTy)->isFloat());
        break;
    case HIRUnaryOp::OpKind::Not:
        ok = operandTy->getKind() == Type::Kind::Primitive
             && std::static_pointer_cast<PrimitiveType>(operandTy)->getPrimKind() == PrimitiveType::PrimKind::BOOL;
        break;
    case HIRUnaryOp::OpKind::BitNot:
        ok = operandTy->getKind() == Type::Kind::Primitive
             && std::static_pointer_cast<PrimitiveType>(operandTy)->isInteger();
        break;
    }

    if (!ok)
    {
        log(*node, std::string("operator '") + op + "' cannot be applied to type '" + operandTy->toString() + "'.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    node->type = node->opKind == HIRUnaryOp::OpKind::Not
                     ? context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL)
                     : operandTy;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRArrayLiteral *node)
{
    // A never-valued element ([panic("..."), 1, 2]) contributes no element type
    // and is compatible with whatever the other elements say (bottom), exactly
    // like a never-valued initialiser or argument.
    std::shared_ptr<Type> elemTy;
    for (auto &e : node->elements)
    {
        analyzeExpr(e.get());
        if (!e->type || isNever(e->type))
            continue;
        if (!elemTy)
            elemTy = e->type;
        else if (!e->type->equals(elemTy))
            log(*e, "array literal elements must all have the same type ('" + elemTy->toString() + "' vs '" + e->type->toString() + "').");
    }
    if (!elemTy)
    {
        // `[]` — no element type to infer from, and `[T; 0]` is rejected too.
        log(*node, node->isRepeat
                       ? "array repeat element has no value to copy (its type is 'never')."
                       : "empty array literal has no element type; write an explicit `[T; N]` with N > 0.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    // `[v; N]`: N is a compile-time literal (the parser enforces that) and obeys
    // exactly the size rules of the `[T; N]` TYPE, so `[v; 0]` and `[v; 2000000]`
    // are rejected the same way `[i32; 0]` / `[i32; 2000000]` are.
    if (node->isRepeat)
    {
        if (node->repeatCount <= 0)
        {
            log(*node, "array repeat count must be a positive integer (got " + std::to_string(node->repeatCount) + ").");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }
        if ((size_t)node->repeatCount > MAX_ARRAY_ELEMENTS)
        {
            log(*node, "array repeat count " + std::to_string(node->repeatCount) + " exceeds the limit of " + std::to_string(MAX_ARRAY_ELEMENTS) + " elements.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }
    }

    if (isReferenceType(elemTy) || elemTy->getKind() == Type::Kind::Pointer)
    {
        log(*node, "array literal element type '" + elemTy->toString() + "' cannot be a reference or a raw pointer (indirection elements are not supported yet).");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }
    if (!elemTy->isCopyable())
    {
        log(*node, "array literal element type '" + elemTy->toString() + "' must be Copy (arrays of non-Copy types are not supported yet).");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }
    node->type = context->typeContext->getArray(
        elemTy, node->isRepeat ? (size_t)node->repeatCount : node->elements.size());
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRStructInit *node)
{
    auto *sym = lookupModuleAware(node->structName);
    if (!sym || sym->kind != SymbolKind::Struct)
    {
        log(*node, "unknown struct '" + node->structName + "'.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    auto structTy = std::dynamic_pointer_cast<CustomType>(sym->type);
    node->structSymbol = sym;

    // Analyse all member initialisers up front so we have their types for inference.
    for (auto &[mname, mval] : node->members)
        analyzeExpr(mval.get());

    std::shared_ptr<CustomType> finalTy = structTy;

    if (structTy->isGeneric())
    {
        std::vector<std::shared_ptr<Type>> typeArgs;

        if (!node->genericArgs.empty())
        {
            // Explicit: Foo<i32> { ... }
            for (auto &raw : node->genericArgs)
                typeArgs.push_back(resolveType(raw, *node));
            if (typeArgs.size() != structTy->getGenericParams().size())
            {
                log(*node, "generic argument count mismatch for struct '" + node->structName + "'.");
                node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                return;
            }
        }
        else
        {
            // Infer from member initialiser types by matching against declared field types.
            std::unordered_map<std::string, std::shared_ptr<Type>> genericMap;
            for (auto &[mname, mval] : node->members)
            {
                auto fIt = std::find(structTy->getFields().begin(), structTy->getFields().end(), mname);
                if (fIt == structTy->getFields().end()) continue;
                if (mval->type) matchGenericType(fIt->type, mval->type, genericMap);
            }
            for (auto &gp : structTy->getGenericParams())
            {
                auto gpTy = std::static_pointer_cast<GenericParamType>(gp);
                auto it = genericMap.find(gpTy->getParamName());
                if (it == genericMap.end())
                {
                    log(*node, "cannot infer generic parameter '" + gpTy->getParamName() + "' for struct '" + node->structName + "'; specify it explicitly.");
                    node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
                    return;
                }
                typeArgs.push_back(it->second);
            }
        }

        // Constraint check
        for (size_t i = 0; i < typeArgs.size(); ++i)
        {
            auto gp = std::static_pointer_cast<GenericParamType>(structTy->getGenericParams()[i]);
            for (auto &constraint : gp->getConstraints())
            {
                bool ok = false;
                for (auto &impled : typeArgs[i]->implTrait)
                    if (impled->equals(constraint))
                    {
                        ok = true;
                        break;
                    }
                if (!ok)
                    log(*node, "type '" + typeArgs[i]->toString() + "' does not implement trait '" + constraint->toString() + "'.");
            }
        }

        node->typedGenericParams = typeArgs;
        finalTy = context->typeContext->instantiateCustom(structTy, std::move(typeArgs));
    }

    node->type = finalTy;

    // Validate member initialisers against the *instantiated* field types.
    const auto &fields = finalTy->getFields();
    for (auto &[mname, mval] : node->members)
    {
        auto fIt = std::find(fields.begin(), fields.end(), mname);
        if (fIt == fields.end())
        {
            log(*node, "struct '" + node->structName + "' has no field '" + mname + "'.");
            continue;
        }
        // Visibility applies to CONSTRUCTION too — otherwise a private field
        // could be initialized from outside and its invariant broken at birth.
        checkFieldAccess(*fIt, finalTy, *node);
        if (mval->type && !typesCompatible(fIt->type, mval->type))
            log(*node, "type mismatch for field '" + mname + "': expected '" + fIt->type->toString() + "', got '" + mval->type->toString() + "'.");
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRVariantInit *node)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    auto *sym = lookupModuleAware(node->enumName);
    if (!sym || sym->kind != SymbolKind::Struct)
    {
        log(*node, "unknown enum '" + node->enumName + "'", E_UnknownVariant);
        node->type = voidTy;
        return;
    }
    auto enumTy = std::dynamic_pointer_cast<CustomType>(sym->type);
    if (!enumTy || !enumTy->isEnum())
    {
        log(*node, "'" + node->enumName + "' is not an enum", E_UnknownVariant);
        node->type = voidTy;
        return;
    }
    node->enumSymbol = sym;

    // Analyse all payload args up front so their types are available for inference.
    for (auto &arg : node->args)
        analyzeExpr(arg.get());

    auto findVariant = [](const std::shared_ptr<CustomType> &ct, const std::string &name)
        -> const CustomType::EnumVariantInfo *
    {
        for (const auto &v : ct->getVariants())
            if (v.name == name) return &v;
        return nullptr;
    };

    // The variant on the GENERIC enum (payload types still contain T) — used for
    // generic-arg inference and to report unknown variants before instantiating.
    const auto *genVariant = findVariant(enumTy, node->variantName);
    if (!genVariant)
    {
        log(*node, "enum '" + node->enumName + "' has no variant '" + node->variantName + "'", E_UnknownVariant);
        node->type = voidTy;
        return;
    }

    std::shared_ptr<CustomType> finalEnumTy = enumTy;

    if (enumTy->isGeneric())
    {
        std::vector<std::shared_ptr<Type>> typeArgs;

        if (!node->genericArgs.empty())
        {
            for (auto &raw : node->genericArgs)
                typeArgs.push_back(resolveType(raw, *node));
        }
        else
        {
            // Infer from the payload args: `option::some(5)` → T = i32.
            std::unordered_map<std::string, std::shared_ptr<Type>> genericMap;
            for (size_t i = 0; i < genVariant->payloadTypes.size() && i < node->args.size(); ++i)
                if (node->args[i]->type)
                    matchGenericType(genVariant->payloadTypes[i], node->args[i]->type, genericMap);
            for (auto &gp : enumTy->getGenericParams())
            {
                auto gpTy = std::static_pointer_cast<GenericParamType>(gp);
                auto it = genericMap.find(gpTy->getParamName());
                if (it == genericMap.end())
                {
                    // No payload args to infer T from (e.g. `option::none`). Leave
                    // the value's type as the GENERIC enum so an enclosing context
                    // (a `ret` with a declared instantiated type) can re-instantiate
                    // it via expected-type inference.
                    node->type = enumTy;
                    return;
                }
                typeArgs.push_back(it->second);
            }
        }

        if (typeArgs.size() != enumTy->getGenericParams().size())
        {
            log(*node, "generic argument count mismatch for enum '" + node->enumName + "'.");
            node->type = voidTy;
            return;
        }

        finalEnumTy = context->typeContext->instantiateCustom(enumTy, std::move(typeArgs));
        node->typedGenericParams = finalEnumTy->getGenericArgs();
    }

    // Validate the payload against the INSTANTIATED variant types.
    const auto *variant = findVariant(finalEnumTy, node->variantName);
    if (!variant)
    {
        log(*node, "enum '" + node->enumName + "' has no variant '" + node->variantName + "'", E_UnknownVariant);
        node->type = voidTy;
        return;
    }
    if (variant->payloadTypes.size() != node->args.size())
    {
        log(*node, "variant '" + node->variantName + "' expects " + std::to_string(variant->payloadTypes.size()) + " payload argument(s), got " + std::to_string(node->args.size()), E_VariantArgMismatch);
        node->type = voidTy;
        return;
    }
    for (size_t i = 0; i < node->args.size(); ++i)
    {
        if (node->args[i]->type && !typesCompatible(variant->payloadTypes[i], node->args[i]->type))
            log(*node->args[i], "payload type mismatch for variant '" + node->variantName + "': expected '" + variant->payloadTypes[i]->toString() + "', got '" + node->args[i]->type->toString() + "'.");
    }

    // Non-Copy payload args are moved into the enum value.
    for (auto &arg : node->args)
        handleMoveSource(arg.get(), *node);

    node->type = finalEnumTy;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRRef *node)
{
    // Everything registered from here on belongs to this `&` expression, so a
    // var-decl initialiser can promote the whole group (see HIRRef::borrowMark).
    node->borrowMark = activeBorrows_.size();

    analyzeExpr(node->expr.get());
    if (node->expr->type)
        node->type = context->typeContext->getReference(node->expr->type, node->isMutable);

    // Borrow-check: `&p` / `&mut p` borrows the place p. Register the borrow
    // after checking aliasing and that p hasn't been moved.
    //
    // The place is ALIAS-RESOLVED, which is what makes a borrow taken through a
    // dereference join the same accounting as the original: `&mut *r` (with
    // `r = &mut x`) borrows `x`, so a second `&mut *r`, a `&mut x`, or a plain
    // `*r = v` in between are all conflicts. Before this the deref form was not
    // tracked at all — two live `&mut *r` were accepted.
    // A borrow THROUGH a dereference (`&mut *r`) has two halves: the POINTER has
    // to be usable right now, and the referent is what gets borrowed. The first
    // half is what freezes the parent — a second `&mut *r` while the first
    // reborrow is still live is rejected here (Rust's E0499) — and the second
    // joins the referent place, so `&mut x`, `*r = v` or a plain read of `x`
    // conflict with it.
    std::string holderRoot;
    std::vector<std::string> holderPath;
    bool viaHolder = derefHolderOf(node->expr.get(), holderRoot, holderPath);
    if (viaHolder)
    {
        // Registering the FREEZE is also the "is the pointer usable right now"
        // check — registerBorrow checks the same place with the same kind first,
        // so the diagnostic is reported once and only when the freeze is taken.
        registerBorrow(holderRoot, holderPath, node->isMutable, /*isPromoted=*/false, *node);
    }

    std::string root;
    std::vector<std::string> path;
    if (resolvePlace(node->expr.get(), root, path))
    {
        if (auto *sym = SymbolTable::getInstance().lookupSymbol(root))
        {
            // In `mir` mode the moved-state and the borrow bookkeeping below are
            // MIRBorrowCheck's job (it sees the same place with a Deref projection).
            if (mirBorrowCheck_) return;

            // A whole-value move poisons every borrow of the root.
            if (sym->state == VarState::Moved)
            {
                log(*node, "cannot borrow moved value '" + root + "'", E_CannotBorrowMovedValue);
                return;
            }
            // Field precision: a partial (field) move only poisons the moved
            // field and its descendants. Borrowing a DISJOINT sibling is fine
            // (`let v = p.b; let r = &p.a;`), while `&p.b` / `&p` still fail.
            for (const auto &movedPath : sym->movedFields)
            {
                if (pathsOverlap(movedPath, path))
                {
                    log(*node, "cannot borrow moved value '" + root + "'", E_CannotBorrowMovedValue);
                    return;
                }
            }
            // Temporary borrow by default; `let r = &p` promotes it (visit(HIRVarDecl)).
            std::vector<std::string> exempt = viaHolder ? exemptionChain(holderRoot)
                                                        : std::vector<std::string>{};
            registerBorrow(root, path, node->isMutable, /*isPromoted=*/false, *node,
                /*isTwoPhase=*/false, exempt.empty() ? nullptr : &exempt);
        }
    }
}

// ---------------------------------------------------------------------------
// `expr?` — error propagation over the stdlib Result.
//
// The operator is defined ONLY in terms of `result$Result` (the language has no
// `Try`-style trait to generalise over): the operand must be a Result value and
// the enclosing function must return a Result whose error type is compatible
// with the operand's. That keeps the rewrite a pure match-shaped lowering with
// no implicit conversion (the manual documents the absence of From/Into).
void HIRSemanticAnalyzer::visit(HIRTry *node)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    analyzeExpr(node->expr.get());
    auto operandTy = node->expr ? node->expr->type : nullptr;

    // The stdlib type must exist: `impt result { Result }`.
    if (!context->typeContext->getCustom("result$Result").has_value())
    {
        log(*node, "the '?' operator requires the stdlib 'Result' type (add 'impt result { Result }').", E_TryNotResult);
        node->type = voidTy;
        return;
    }

    auto operandCt = std::dynamic_pointer_cast<CustomType>(operandTy);
    if (!operandCt || operandCt->getOriginName() != "result$Result")
    {
        log(*node, "the '?' operator requires a 'Result' value, got '" + (operandTy ? operandTy->toString() : std::string("?")) + "'.", E_TryNotResult);
        node->type = voidTy;
        return;
    }

    // Locate the Ok/Err shape on the OPERAND (the layout is the stdlib Result's).
    std::shared_ptr<Type> okPayloadTy;
    std::shared_ptr<Type> errPayloadTy;
    for (const auto &v : operandCt->getVariants())
    {
        if (v.name == "Ok" && !v.payloadTypes.empty())
            okPayloadTy = v.payloadTypes[0];
        else if (v.name == "Err" && !v.payloadTypes.empty())
            errPayloadTy = v.payloadTypes[0];
    }
    if (!okPayloadTy || !errPayloadTy)
    {
        log(*node, "'?' requires a Result with 'Ok(T)' and 'Err(E)' variants.", E_TryNotResult);
        node->type = voidTy;
        return;
    }

    // The enclosing function must return a Result, and its error type must accept
    // the operand's (no conversion is inserted).
    auto declaredCt = std::dynamic_pointer_cast<CustomType>(functionInfo.declaredReturnType);
    if (!declaredCt || declaredCt->getOriginName() != "result$Result")
    {
        log(*node, "the '?' operator requires the enclosing function to return 'Result<_, E>' (it returns '"
                + (functionInfo.declaredReturnType ? functionInfo.declaredReturnType->toString() : std::string("void")) + "').",
            E_TryNotInResultFn);
        node->type = okPayloadTy;
        return;
    }

    std::shared_ptr<Type> declaredErrTy;
    for (const auto &v : declaredCt->getVariants())
        if (v.name == "Err" && !v.payloadTypes.empty())
            declaredErrTy = v.payloadTypes[0];

    if (!declaredErrTy || !typesCompatible(declaredErrTy, errPayloadTy))
    {
        log(*node, "the error type of '?' ('" + errPayloadTy->toString()
                + "') does not match the function error type ('"
                + (declaredErrTy ? declaredErrTy->toString() : std::string("?")) + "').",
            E_TryErrorTypeMismatch);
        node->type = okPayloadTy;
        return;
    }

    // `?` CONSUMES the Result: on the error path its payload is moved into the
    // value that is returned, and on the ok path the payload is moved out.
    handleMoveSource(node->expr.get(), *node);
    node->type = okPayloadTy;
}

void HIRSemanticAnalyzer::visit(HIRImport *node)
{
    // Module loading happens in the Parser (each imported file is lexed and
    // parsed into the same Program before sema runs). Nothing to do here yet.
    (void)node;
}
