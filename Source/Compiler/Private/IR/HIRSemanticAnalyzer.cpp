/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/HIRSemanticAnalyzer.hpp"
#include "IR/BuiltinNames.hpp"

#include <algorithm>
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
    auto ce = std::dynamic_pointer_cast<CustomType>(expected);
    auto ca = std::dynamic_pointer_cast<CustomType>(actual);
    if (ce && ca && ce->getOriginName() == ca->getOriginName())
        return true;
    auto re = std::dynamic_pointer_cast<ReferenceType>(expected);
    auto ra = std::dynamic_pointer_cast<ReferenceType>(actual);
    if (re && ra && !re->isMutableRef() && ra->isMutableRef()
        && re->getBaseType()->equals(ra->getBaseType()))
        return true;
    return expected->equals(actual);
}

/// Decompose a member-access chain (p.a.b) into the root variable name and the
/// field path ["a","b"]. Returns false if `expr` is not a simple chain.
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
        // Any index maps to the wildcard segment "[*]": a[0] and a[1] both become
        // path ["[*]"] (conservative — disjoint-element borrows are rejected, but
        // the checker stays sound; a whole-array borrow `&a` still overlaps every
        // element). Refined per-index tracking is a future improvement.
        if (!extractRootAndPath(ia->object.get(), root, path)) return false;
        path.push_back("[*]");
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
    std::string s;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (i) s += ".";
        s += path[i];
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
                log(errorNode, "array size " + std::to_string(raw.arraySize)
                    + " exceeds the limit of " + std::to_string(MAX_ARRAY_ELEMENTS) + " elements.");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        auto elemTy = raw.element ? resolveType(*raw.element, errorNode)
                                  : context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        if (elemTy && isReferenceType(elemTy))
        {
            if (!suppressTypeErrors_)
                log(errorNode, "array element type '" + elemTy->toString()
                    + "' cannot be a reference (reference elements are not supported yet).");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        if (elemTy && !elemTy->isCopyable())
        {
            if (!suppressTypeErrors_)
                log(errorNode, "array element type '" + elemTy->toString()
                    + "' must be Copy (arrays of non-Copy types are not supported yet).");
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
            if (raw.isRef)
                base = context->typeContext->getReference(base, raw.isMutRef);
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
                auto custom = context->typeContext->getCustom(raw.name);
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
            typeArgs.push_back(resolveType(g, errorNode));

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
    if (auto ctParam = std::dynamic_pointer_cast<CustomType>(paramTy))
    {
        auto ctArg = std::dynamic_pointer_cast<CustomType>(argTy);
        if (!ctArg) return;
        // Compare *origin* names: mangled names (Option$T vs Option$int32)
        // differ by generic args, which we recurse into below.
        auto pName = ctParam->getOriginName();
        auto aName = ctArg->getOriginName();
        if (pName != aName) return;
        const auto &pa = ctParam->getGenericArgs();
        const auto &aa = ctArg->getGenericArgs();
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
        if (isReservedFunctionName(f->name))
        {
            log(*f, "function name '" + f->name + "' is reserved by the compiler.");
            return;
        }
        if (SymbolTable::getInstance().lookupSymbol(f->name)) return;
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Function;
        sym->name = f->name;
        sym->type = nullptr;
        SymbolTable::getInstance().insertSymbol(f->name, std::move(sym));
    }
}

// ---------------------------------------------------------------------------
// Best-effort function signature resolution for the pre-registration pass.
// Struct CustomTypes have already been built by buildStructType by the time
// this runs, so struct params/returns resolve correctly. Type errors found
// here are suppressed — the full analysis pass reports the authoritative ones.
// ---------------------------------------------------------------------------

void HIRSemanticAnalyzer::preRegisterFunctionType(HIRFunction *f,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &inferredReturns)
{
    auto *sym = SymbolTable::getInstance().lookupSymbol(f->name);
    if (!sym) return;

    suppressTypeErrors_ = true;

    // Bring the function's own generic params into scope so `it: T` resolves
    // to the generic param T rather than a silent VOID. resolveType only
    // consults functionInfo.gParams when isInFunction is set — mimic that here.
    auto savedGParams = std::move(functionInfo.gParams);
    bool savedInFunction = functionInfo.isInFunction;
    functionInfo.isInFunction = true;
    for (auto &gp : f->gParams)
        functionInfo.gParams[gp->getParamName()] = gp;

    std::vector<std::shared_ptr<Type>> paramTypes;
    for (auto &[pname, rawTy] : f->rawParams)
        paramTypes.push_back(resolveType(rawTy, *f));

    // If the return type is inferred from the body, use the best-effort value
    // from the pass-1c-1 scan (or VOID) so a forward call still sees a
    // callable symbol with a usable signature; visit(HIRFunction) corrects it.
    std::shared_ptr<Type> retTy;
    if (f->hasReturnType)
    {
        retTy = resolveType(f->rawReturnType, *f);
    }
    else
    {
        auto it = inferredReturns.find(f->name);
        retTy = (it != inferredReturns.end() && it->second)
            ? it->second
            : context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    }

    functionInfo.gParams = std::move(savedGParams);
    functionInfo.isInFunction = savedInFunction;

    std::shared_ptr<Type> funcType = nullptr;
    if (f->isGeneric)
        funcType = context->typeContext->getGenericFunction(f->gParams, paramTypes, retTy);
    else
        funcType = context->typeContext->getFunction(paramTypes, retTy);

    sym->type = funcType;
    f->type = funcType;

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
        case H::Eq: case H::Ne: case H::Lt: case H::Gt: case H::Le: case H::Ge:
        case H::And: case H::Or:
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
    HIRImpl *node, const std::shared_ptr<TraitType> &traitType, std::unordered_map<std::string, std::shared_ptr<Type>> *outSubst)
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
    const HIRGenericConstraint &con, HIRNode &errNode, bool silent)
{
    auto *traitSym = SymbolTable::getInstance().lookupSymbol(con.traitName);
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
            log(errNode, "trait constraint '" + con.traitName + "' expects "
                + std::to_string(traitTy->getGenericParams().size())
                + " generic argument(s), got " + std::to_string(argTypes.size()) + ".");
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

void HIRSemanticAnalyzer::visit(HIRProgram *node)
{
    if (!node) return;

    // Pass 1: register all top-level names so forward references work.
    for (auto &item : node->items)
        preRegister(item.get());

    // Pre-registration type-building is best-effort; the full analysis pass is
    // authoritative, so suppress its diagnostics.
    suppressTypeErrors_ = true;

    // Pass 1b: register every struct as an EMPTY CustomType shell so forward /
    // recursive / cross-file references (e.g. `struct Node { next: Option<Node> }`,
    // or an impl in a later alphabetically-loaded stdlib file) resolve before any
    // fields are resolved. visit(HIRStruct) fills the fields in pass 2.
    for (auto &item : node->items)
    {
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

    // Pass 1b': build trait types so struct/function trait bounds resolve
    // regardless of declaration order. visit(HIRTrait) is idempotent (traits are
    // cached by name), so re-running it in pass 2 is harmless.
    for (auto &item : node->items)
    {
        if (auto *t = dynamic_cast<HIRTrait *>(item.get()))
            visit(t);
    }

    // Pass 1b'': fill struct fields. Every shell now exists, so forward field
    // references resolve to the shell type (correct identity); setFields fills
    // the origin. This must complete before any struct-init / impl body is
    // analyzed in pass 2 (those read struct fields).
    for (auto &item : node->items)
    {
        if (auto *s = dynamic_cast<HIRStruct *>(item.get()))
            buildStructType(s);
        else if (auto *e = dynamic_cast<HIREnum *>(item.get()))
            buildEnumType(e);
    }

    // Pass 1b''': register every impl's trait conformance on its struct BEFORE
    // any body/call analysis, so bound checks are order-independent (an impl
    // may appear after its first use in file order).
    for (auto &item : node->items)
    {
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
        for (auto &item : node->items)
        {
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
    for (auto &item : node->items)
    {
        if (auto *f = dynamic_cast<HIRFunction *>(item.get()))
            preRegisterFunctionType(f, inferredReturns);
    }

    suppressTypeErrors_ = false;

    // Pass 2: full analysis
    for (auto &item : node->items)
        item->accept(this);
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

        member.type = resolveType(member.rawType, *node);
        fields.emplace_back(member.name, member.type);
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
    if (node->name == "Numeric" || node->name == "Integer" || isOperatorTrait(node->name))
    {
        auto traitTy = std::static_pointer_cast<TraitType>(sym->type);
        auto seed = [&](PrimitiveType::PrimKind k)
        {
            auto p = context->typeContext->getPrimitive(k);
            if (!p->implementsTrait(node->name))
                p->implTrait.push_back(traitTy);
        };
        const std::string &n = node->name;
        bool isNumeric = (n == "Numeric");
        bool isInteger = (n == "Integer");
        bool isCmp = (n == "PartialEq" || n == "PartialOrd");
        bool isArith = (n == "Add" || n == "Sub" || n == "Mul" || n == "Div" || n == "Rem");
        bool isBitwise = (n == "BitAnd" || n == "BitOr" || n == "BitXor" || n == "Shl" || n == "Shr");

        // ints get every family (Numeric/Integer markers + all operator traits).
        if (isNumeric || isInteger || isCmp || isArith || isBitwise)
        {
            seed(PrimitiveType::PrimKind::I8);
            seed(PrimitiveType::PrimKind::I16);
            seed(PrimitiveType::PrimKind::I32);
            seed(PrimitiveType::PrimKind::I64);
        }
        // floats get Numeric + arithmetic + comparison, NOT Integer / bitwise.
        if (isNumeric || isCmp || isArith)
        {
            seed(PrimitiveType::PrimKind::F32);
            seed(PrimitiveType::PrimKind::F64);
        }
        if (isNumeric || isCmp)
            seed(PrimitiveType::PrimKind::CHAR); // char compares via i32
        if (isCmp)
            seed(PrimitiveType::PrimKind::BOOL); // == / < work on bool
    }

    isInTraitMethod = false;
    traitName = "";
    traitGParams.clear();
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRImpl *node)
{
    auto *structSym = SymbolTable::getInstance().lookupSymbol(node->structName);
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
        for (auto &gp : node->gParams)
            structGParams[gp->getParamName()] = gp;
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
        if (tn == "Numeric" || tn == "Integer")
        {
            log(*node, "builtin marker trait '" + tn + "' is primitives-only and cannot be implemented by a struct; implement the operator traits (Add, Sub, ...) instead (operator overloading).");
            currentStructType = nullptr;
            structGParams.clear();
            return;
        }
        auto *traitSym = SymbolTable::getInstance().lookupSymbol(tn);
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
        auto funcType = method->type;

        auto funcSym = std::make_unique<Symbol>();
        funcSym->kind = SymbolKind::Function;
        funcSym->name = funcName;
        funcSym->type = funcType;
        SymbolTable::getInstance().insertSymbol(funcName, std::move(funcSym));
        method->funcSymbol = SymbolTable::getInstance().lookupSymbol(funcName);
    }

    // Methods always go on the origin (baseStruct), not the self instantiation.
    baseStruct->addMethods(methods);

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
        setupParamOrigin(sym.get());
        SymbolTable::getInstance().insertSymbol("self", std::move(sym));
    }

    // --- regular params ---
    std::vector<std::shared_ptr<Type>> paramTypes; // for building FunctionType

    for (auto &[pname, rawTy] : node->rawParams)
    {
        if (SymbolTable::getInstance().lookupSymbol(pname))
            log(*node, "param '" + pname + "' shadows a previous definition.");

        auto resolvedTy = resolveType(rawTy, *node);
        if (!rawTy.isPresent)
            log(*node, "cannot deduce type for param '" + pname + "'.");
        if (resolvedTy && resolvedTy->getKind() == Type::Kind::Array)
        {
            log(*node, "array type cannot be a function parameter yet (pass a reference instead).");
            resolvedTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }

        node->params.emplace_back(pname, resolvedTy);
        paramTypes.push_back(resolvedTy);

        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Param;
        sym->name = pname;
        sym->type = resolvedTy;
        setupParamOrigin(sym.get());
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
            bool present = false;
            for (const auto &existing : genericParams)
                if (existing->getParamName() == gpTy->getParamName()) { present = true; break; }
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
    for (auto &stmt : node->stmts)
        analyzeStmt(stmt.get());
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
    size_t n = std::min(a.size(), b.size());
    return std::equal(a.begin(), a.begin() + n, b.begin());
}

void HIRSemanticAnalyzer::borrowConflictInfo(std::string &msg, size_t &errorId,
    const std::string &name, BorrowUseKind kind)
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
    const std::vector<std::string> &path, BorrowUseKind kind, HIRNode &errNode)
{
    if (activeBorrows_.empty()) return true;
    bool ok = true;
    std::string name = root + (path.empty() ? "" : "." + joinPath(path));

    for (const auto &b : activeBorrows_)
    {
        if (b.root != root || !pathsOverlap(b.path, path)) continue;

        bool conflict = false;
        switch (kind)
        {
        case BorrowUseKind::BorrowMut:    conflict = true;    break; // any borrow blocks &mut
        case BorrowUseKind::BorrowShared: conflict = b.isMut; break; // &mut blocks &; & allows &
        case BorrowUseKind::Write:        conflict = true;    break; // any borrow blocks write
        case BorrowUseKind::Move:         conflict = true;    break; // any borrow blocks move
        case BorrowUseKind::Read:         conflict = b.isMut; break; // &mut blocks read; & allows
        }

        if (conflict)
        {
            // NLL: a conflict against a PROMOTED (variable) borrow is deferred to
            // the end of the function — the borrow may have already ended at its
            // holder's last use before this use. Moves stay inline (conservative:
            // moving a borrowed place would leave the reference dangling).
            if (b.isPromoted && kind != BorrowUseKind::Move)
            {
                pendingBorrowConflicts_.push_back(
                    PendingConflict{root, path, kind, stmtOrdinal_, errNode.position, errNode.length});
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

void HIRSemanticAnalyzer::logAtPosition(const SourcePosition &pos, size_t length,
    const std::string &msg, size_t errorId)
{
    Logger::LogInfo info{};
    info.codePath = context->filePath;
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
    for (const auto &pc : pendingBorrowConflicts_)
    {
        for (const auto &b : promotedBorrows_)
        {
            if (b.root != pc.root || !pathsOverlap(b.path, pc.path)) continue;

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
                std::string name = pc.root + (pc.path.empty() ? "" : "." + joinPath(pc.path));
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
    const std::vector<std::string> &path, bool isMut, bool isPromoted, HIRNode &errNode)
{
    if (!checkBorrowUse(root, path,
            isMut ? BorrowUseKind::BorrowMut : BorrowUseKind::BorrowShared, errNode))
        return false;
    activeBorrows_.push_back(Borrow{root, "", path, isMut, isPromoted, 0, errNode.position});
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

RefOrigin HIRSemanticAnalyzer::originOfBinding(Symbol *sym)
{
    if (!sym || !sym->type) return RefOrigin::Unknown;
    if (sym->kind == SymbolKind::GlobalVar) return RefOrigin::Global;
    if (sym->kind == SymbolKind::Param) return RefOrigin::Param;
    return sym->refOrigin.value_or(RefOrigin::Unknown); // LocalVar
}

RefOrigin HIRSemanticAnalyzer::placeStorageOrigin(const std::string &root,
    const std::vector<std::string> &path)
{
    auto *sym = SymbolTable::getInstance().lookupSymbol(root);
    if (!sym || !sym->type) return RefOrigin::Unknown;
    if (sym->kind == SymbolKind::GlobalVar) return RefOrigin::Global;
    // `&x` / `&param` — the bare slot lives in this function's frame.
    if (path.empty()) return RefOrigin::Local;
    // `&r.v` with r reference-typed — the field lives where r points.
    if (isReferenceType(sym->type)) return originOfBinding(sym);
    // `&s.v` with s a by-value struct — its fields are a local frame copy.
    return RefOrigin::Local;
}

RefOrigin HIRSemanticAnalyzer::fieldValueOrigin(const std::string &root,
    const std::vector<std::string> &path)
{
    if (path.size() != 1) return RefOrigin::Unknown; // nested fields not tracked (conservative)
    auto *sym = SymbolTable::getInstance().lookupSymbol(root);
    if (!sym || !sym->type) return RefOrigin::Unknown;

    // The reference value lives in the struct the root binding points to.
    if (isReferenceType(sym->type))
    {
        if (sym->kind == SymbolKind::Param) return RefOrigin::Param;       // caller's struct
        if (sym->kind == SymbolKind::GlobalVar) return RefOrigin::Global;  // static struct
        // Local reference → resolve its target to find the holding struct.
        if (sym->refTarget.has_value())
        {
            const auto &[tRoot, tPath] = *sym->refTarget;
            if (tPath.empty())
            {
                auto *tSym = SymbolTable::getInstance().lookupSymbol(tRoot);
                if (tSym)
                {
                    if (tSym->kind == SymbolKind::Param) return RefOrigin::Param;
                    if (tSym->kind == SymbolKind::GlobalVar) return RefOrigin::Global;
                    if (tSym->kind == SymbolKind::LocalVar)
                    {
                        auto it = tSym->refFieldOrigins.find(path[0]);
                        return it != tSym->refFieldOrigins.end() ? it->second : RefOrigin::Unknown;
                    }
                }
            }
        }
        return RefOrigin::Unknown;
    }

    // Direct struct binding stores the field value.
    switch (sym->kind)
    {
    case SymbolKind::GlobalVar: return RefOrigin::Global;
    case SymbolKind::Param: return RefOrigin::Param;
    case SymbolKind::LocalVar:
        {
            auto it = sym->refFieldOrigins.find(path[0]);
            return it != sym->refFieldOrigins.end() ? it->second : RefOrigin::Unknown;
        }
    default: return RefOrigin::Unknown;
    }
}

RefOrigin HIRSemanticAnalyzer::originOfReferenceValue(HIRExpr *expr)
{
    if (!expr) return RefOrigin::Unknown;
    if (auto *ref = dynamic_cast<HIRRef *>(expr))
    {
        std::string root;
        std::vector<std::string> path;
        if (!extractRootAndPath(ref->expr.get(), root, path)) return RefOrigin::Unknown;
        return placeStorageOrigin(root, path);
    }
    if (auto *nr = dynamic_cast<HIRNameRef *>(expr))
        return originOfBinding(SymbolTable::getInstance().lookupSymbol(nr->name));
    if (auto *ma = dynamic_cast<HIRMemberAccess *>(expr))
    {
        std::string root;
        std::vector<std::string> path;
        if (!extractRootAndPath(ma, root, path)) return RefOrigin::Unknown;
        return fieldValueOrigin(root, path);
    }
    if (auto *lit = dynamic_cast<HIRLiteral *>(expr))
        return lit->kind == HIRLiteral::Kind::String ? RefOrigin::Global : RefOrigin::Unknown;
    // Calls / binary / casts: origin unknown → conservative allow.
    return RefOrigin::Unknown;
}

void HIRSemanticAnalyzer::setupParamOrigin(Symbol *sym)
{
    if (!sym || !sym->type) return;
    if (isReferenceType(sym->type))
    {
        sym->refOrigin = RefOrigin::Param;
        return;
    }
    if (auto ct = std::dynamic_pointer_cast<CustomType>(sym->type))
    {
        if (!structHasRefFields(ct)) return;
        // A by-value struct param is a caller-provided copy; its reference fields
        // point to caller-owned memory, so they are safe to return.
        for (const auto &f : ct->getFields())
            if (isReferenceType(f.type))
                sym->refFieldOrigins[f.name] = RefOrigin::Param;
    }
}

void HIRSemanticAnalyzer::setupBindingOrigins(HIRVarDecl *node)
{
    auto *sym = node->varSymbol;
    if (!sym || !sym->type) return;
    if (sym->kind != SymbolKind::LocalVar) return; // globals derived from kind at use time
    if (!node->init.has_value()) return;
    auto *init = node->init.value().get();
    if (!init->type) return;

    if (isReferenceType(sym->type))
    {
        sym->refOrigin = originOfReferenceValue(init);
        // Target (where the reference points) for through-reference field reads.
        sym->refTarget.reset();
        if (auto *ref = dynamic_cast<HIRRef *>(init))
        {
            std::string root;
            std::vector<std::string> path;
            if (extractRootAndPath(ref->expr.get(), root, path))
                sym->refTarget = std::make_pair(root, path);
        }
        else if (auto *nr = dynamic_cast<HIRNameRef *>(init))
        {
            auto *other = SymbolTable::getInstance().lookupSymbol(nr->name);
            if (other && other->refTarget) sym->refTarget = other->refTarget;
        }
        return;
    }

    // Struct-typed local with reference fields: per-field origins.
    auto ct = std::dynamic_pointer_cast<CustomType>(sym->type);
    if (!ct || !structHasRefFields(ct)) return;

    if (auto *sinit = dynamic_cast<HIRStructInit *>(init))
    {
        for (auto &[fname, mexpr] : sinit->members)
        {
            auto fit = std::find(ct->getFields().begin(), ct->getFields().end(), fname);
            if (fit == ct->getFields().end()) continue;
            if (!isReferenceType(fit->type)) continue;
            sym->refFieldOrigins[fname] = originOfReferenceValue(mexpr.get());
        }
        return;
    }
    if (auto *nr = dynamic_cast<HIRNameRef *>(init))
    {
        auto *other = SymbolTable::getInstance().lookupSymbol(nr->name);
        if (!other) return;
        if (other->kind == SymbolKind::LocalVar)
            sym->refFieldOrigins = other->refFieldOrigins; // struct copy
        else if (other->kind == SymbolKind::Param)
            for (const auto &f : ct->getFields())
                if (isReferenceType(f.type)) sym->refFieldOrigins[f.name] = RefOrigin::Param;
        else if (other->kind == SymbolKind::GlobalVar)
            for (const auto &f : ct->getFields())
                if (isReferenceType(f.type)) sym->refFieldOrigins[f.name] = RefOrigin::Global;
        return;
    }
    // Calls / member access / other: not tracked → empty map → Unknown.
}

void HIRSemanticAnalyzer::updateAssignOrigins(HIRAssign *node)
{
    auto *target = node->target.get();
    auto *value = node->value.get();
    if (!target || !value) return;

    // Whole-binding assignment: `r = <ref>` or `s = <struct>`.
    if (auto *nr = dynamic_cast<HIRNameRef *>(target))
    {
        auto *sym = SymbolTable::getInstance().lookupSymbol(nr->name);
        if (!sym || sym->kind != SymbolKind::LocalVar) return;
        if (!value->type) return;

        if (isReferenceType(sym->type))
        {
            sym->refOrigin = originOfReferenceValue(value);
            sym->refTarget.reset();
            if (auto *ref = dynamic_cast<HIRRef *>(value))
            {
                std::string root;
                std::vector<std::string> path;
                if (extractRootAndPath(ref->expr.get(), root, path))
                    sym->refTarget = std::make_pair(root, path);
            }
            else if (auto *vn = dynamic_cast<HIRNameRef *>(value))
            {
                auto *other = SymbolTable::getInstance().lookupSymbol(vn->name);
                if (other && other->refTarget) sym->refTarget = other->refTarget;
            }
            return;
        }

        auto ct = std::dynamic_pointer_cast<CustomType>(sym->type);
        if (!ct || !structHasRefFields(ct)) return;
        // Whole-struct re-assignment re-derives field origins like a declaration.
        sym->refFieldOrigins.clear();
        if (auto *sinit = dynamic_cast<HIRStructInit *>(value))
        {
            for (auto &[fname, mexpr] : sinit->members)
            {
                auto fit = std::find(ct->getFields().begin(), ct->getFields().end(), fname);
                if (fit == ct->getFields().end()) continue;
                if (!isReferenceType(fit->type)) continue;
                sym->refFieldOrigins[fname] = originOfReferenceValue(mexpr.get());
            }
        }
        else if (auto *vn = dynamic_cast<HIRNameRef *>(value))
        {
            auto *other = SymbolTable::getInstance().lookupSymbol(vn->name);
            if (!other) return;
            if (other->kind == SymbolKind::LocalVar)
                sym->refFieldOrigins = other->refFieldOrigins;
            else if (other->kind == SymbolKind::Param)
                for (const auto &f : ct->getFields())
                    if (isReferenceType(f.type)) sym->refFieldOrigins[f.name] = RefOrigin::Param;
            else if (other->kind == SymbolKind::GlobalVar)
                for (const auto &f : ct->getFields())
                    if (isReferenceType(f.type)) sym->refFieldOrigins[f.name] = RefOrigin::Global;
        }
        return;
    }

    // Field assignment `s.r = <ref>` refreshes the storing struct's field origin.
    if (auto *ma = dynamic_cast<HIRMemberAccess *>(target))
    {
        std::string root;
        std::vector<std::string> path;
        if (!extractRootAndPath(ma, root, path)) return;
        if (path.size() != 1) return;                 // nested: not tracked
        if (!isReferenceType(ma->type)) return;       // only reference-typed fields carry origins
        auto *sym = SymbolTable::getInstance().lookupSymbol(root);
        if (!sym || sym->kind != SymbolKind::LocalVar) return;

        // If the store goes THROUGH a local reference (`r.v = &x`, r = &mut h),
        // the field belongs to r's target struct.
        Symbol *store = sym;
        if (isReferenceType(sym->type))
        {
            if (!sym->refTarget.has_value()) return;
            const auto &[tRoot, tPath] = *sym->refTarget;
            if (!tPath.empty()) return;
            store = SymbolTable::getInstance().lookupSymbol(tRoot);
            if (!store || store->kind != SymbolKind::LocalVar) return;
        }
        store->refFieldOrigins[path[0]] = originOfReferenceValue(value);
    }
}

void HIRSemanticAnalyzer::checkDanglingReturn(HIRReturn *node)
{
    if (!node->value.has_value()) return;
    auto *value = node->value.value().get();
    auto declared = functionInfo.declaredReturnType;
    if (!declared) return;

    if (isReferenceType(declared))
    {
        RefOrigin o = originOfReferenceValue(value);
        if (o == RefOrigin::Local)
        {
            // Name the referenced place for the message (`ret &x` → 'x').
            std::string name = "this value";
            std::string root;
            std::vector<std::string> path;
            HIRExpr *place = value;
            if (auto *ref = dynamic_cast<HIRRef *>(value))
                place = ref->expr.get();
            if (extractRootAndPath(place, root, path))
                name = root + (path.empty() ? "" : "." + joinPath(path));
            log(*node,
                "cannot return reference to '" + name + "': it does not live long enough (it points into this function's stack frame)",
                E_BorrowDoesNotLiveLongEnough);
        }
        return;
    }

    auto ct = std::dynamic_pointer_cast<CustomType>(declared);
    if (ct && structHasRefFields(ct))
        checkStructReturn(value, ct, *node);
}

void HIRSemanticAnalyzer::checkStructReturn(HIRExpr *value,
    const std::shared_ptr<CustomType> &declaredStruct, HIRNode &errNode)
{
    if (!declaredStruct || !structHasRefFields(declaredStruct)) return;

    auto logLocal = [&](const std::string &fname) {
        log(errNode, "cannot return struct: reference field '" + fname + "' does not live long enough",
            E_BorrowDoesNotLiveLongEnough);
    };

    // `ret S { r: &x }` — check each reference-typed member directly.
    if (auto *init = dynamic_cast<HIRStructInit *>(value))
    {
        auto valueStruct = std::dynamic_pointer_cast<CustomType>(init->type);
        const auto &fields = valueStruct ? valueStruct->getFields() : declaredStruct->getFields();
        for (auto &[fname, mexpr] : init->members)
        {
            auto fit = std::find(fields.begin(), fields.end(), fname);
            if (fit == fields.end()) continue;
            if (!isReferenceType(fit->type)) continue;
            if (originOfReferenceValue(mexpr.get()) == RefOrigin::Local)
                logLocal(fname);
        }
        return;
    }

    // `ret h` — a local struct binding's tracked field origins.
    if (auto *nr = dynamic_cast<HIRNameRef *>(value))
    {
        auto *sym = SymbolTable::getInstance().lookupSymbol(nr->name);
        if (!sym) return;
        if (sym->kind == SymbolKind::LocalVar)
            for (const auto &[f, o] : sym->refFieldOrigins)
                if (o == RefOrigin::Local)
                    logLocal(f);
        // Params / globals: their reference fields point to caller / static storage.
        return;
    }

    // Member access / call / other: nested struct fields not tracked → allow.
}

// ---------------------------------------------------------------------------
// Move semantics of consuming `source` (whole variable or field path).
// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::handleMoveSource(HIRExpr *source, HIRNode &errNode)
{
    // Borrow-check: moving a place that is currently borrowed would leave the
    // borrow dangling — reject it. Only genuinely non-Copy sources are moved;
    // a Copy source is a read (handled by the NameRef/MemberAccess read check
    // with NLL liveness).
    std::string root;
    std::vector<std::string> path;
    if (extractRootAndPath(source, root, path)
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
            log(errNode, "use of moved value: '" + nameRef->name + "' (partially moved)",
                E_UseOfMovedValue);
            return;
        }
        if (sym->state == VarState::Moved)
            log(errNode, "use of moved value: '" + nameRef->name + "'", E_UseOfMovedValue);
        if (sym->type && !sym->type->isCopyable())
            sym->state = VarState::Moved;
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
            log(errNode, "use of moved value: '" + root + "." + joinPath(path) + "'",
                E_UseOfMovedValue);
            return;
        }
        // Copy fields never move (mirrors MIR's isCopyType).
        if (ma->type && ma->type->isCopyable()) return;

        // Reject re-reading a moved field or an ancestor of it.
        for (const auto &existing : sym->movedFields)
        {
            bool existingIsPrefix = existing.size() <= path.size()
                && std::equal(existing.begin(), existing.end(), path.begin());
            bool pathIsPrefix = path.size() <= existing.size()
                && std::equal(path.begin(), path.end(), existing.begin());
            if (existingIsPrefix || pathIsPrefix)
            {
                log(errNode, "use of moved value: '" + root + "." + joinPath(path) + "'",
                    E_UseOfMovedValue);
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
    if (SymbolTable::getInstance().lookupSymbol(node->name))
        log(*node, "variable '" + node->name + "' already exists in this scope.");

    std::shared_ptr<Type> initType;

    if (node->init.has_value())
    {
        analyzeExpr(node->init.value().get());
        initType = node->init.value()->type;

        // Globals live in static storage: only a literal initializer can be
        // embedded in the object file. Anything else (a call, an array built at
        // runtime, a reference) would silently lower to zeroed memory — reject
        // it rather than miscompile.
        if (node->isGlobal && !dynamic_cast<HIRLiteral *>(node->init.value().get()))
        {
            log(*node, "global variable initializer must be a literal (not '"
                + (initType ? initType->toString() : std::string("?")) + "').");
        }

        // Move semantics check (whole-variable or field-path move source).
        handleMoveSource(node->init.value().get(), *node);

        // Borrow promotion: `let r = &p` binds the borrow to the variable r, so
        // it survives the statement. Under NLL its liveness runs to r's last use;
        // record the holder + creation ordinal and keep a function-scoped copy.
        if (dynamic_cast<HIRRef *>(node->init.value().get()) && !activeBorrows_.empty())
        {
            activeBorrows_.back().isPromoted = true;
            activeBorrows_.back().holderName = node->name;
            activeBorrows_.back().createStmt = stmtOrdinal_;
            promotedBorrows_.push_back(activeBorrows_.back());
        }
    }

    if (node->hasExplicitType)
    {
        node->type = resolveType(node->rawType, *node);
        if (initType && !initType->equals(node->type))
            log(*node, "type mismatch in variable declaration.", E_TypeMismatch);
    }
    else
    {
        if (!initType)
        {
            log(*node, "cannot infer type for '" + node->name + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        else
        {
            node->type = initType;
        }
    }

    auto sym = std::make_unique<Symbol>();
    sym->kind = node->isGlobal ? SymbolKind::GlobalVar : SymbolKind::LocalVar;
    sym->name = node->name;
    sym->type = node->type;
    sym->isMutable = node->isMutable;
    SymbolTable::getInstance().insertSymbol(node->name, std::move(sym));

    node->varSymbol = SymbolTable::getInstance().lookupSymbol(node->name);

    // Stage 3: derive this binding's reference / ref-field origins (locals only).
    setupBindingOrigins(node);
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRAssign *node)
{
    analyzeExpr(node->target.get());
    analyzeExpr(node->value.get());

    // Borrow-check: writing to a borrowed place is forbidden. A write THROUGH a
    // reference (`r.v = 5` where r = &mut x) roots on `r`, which is not itself
    // a borrowed binding, so it passes here and is governed by mutability below.
    std::string targetRoot;
    std::vector<std::string> targetPath;
    if (extractRootAndPath(node->target.get(), targetRoot, targetPath))
        checkBorrowUse(targetRoot, targetPath, BorrowUseKind::Write, *node);

    if (node->target->type && node->value->type && !node->target->type->equals(node->value->type))
        log(*node, "assignment type mismatch.", E_TypeMismatch);

    // Mutability check: the assignment target must be writable. A plain
    // variable must be `let mut`; a field access inherits mutability from its
    // root binding OR from a `&mut T` reference held in that binding
    // (e.g. `let r = &mut x; r.f = 1;` is legal even though `r` is not `mut`).
    // NOTE: Symbol::isMutable is std::optional<bool>; `!sym->isMutable` would
    // only test whether the optional is engaged. Use value_or to test the flag.
    auto isMutableBinding = [](const Symbol *sym) {
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
            log(*node, "cannot assign to immutable variable '" + nameRef->name + "'",
                E_AssignToImmutable);
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
                log(*node, "cannot assign to field of immutable variable '" + rootRef->name + "'",
                    E_AssignToImmutable);
                return;
            }
        }
    }
    else if (auto *idx = dynamic_cast<HIRIndexAccess *>(node->target.get()))
    {
        // `a[i] = ...` / `s.data[i] = ...`.
        //
        // Write-through: when the indexed object is itself a `&mut T` reference
        // (`s.data: &mut i8`), writing through it is allowed even if the root
        // binding `s` is not `mut` — the write targets the referent, not the
        // struct field. A shared reference is rejected. Otherwise the root
        // binding's mutability rules.
        if (auto refTy = std::dynamic_pointer_cast<ReferenceType>(idx->object->type))
        {
            if (!refTy->isMutableRef())
            {
                log(*node, "cannot assign through a shared reference.",
                    E_AssignToImmutable);
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
                    log(*node, "cannot assign to element of immutable variable '" + rootRef->name + "'",
                        E_AssignToImmutable);
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
        }
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
                    std::remove_if(tsym->movedFields.begin(), tsym->movedFields.end(),
                        [&](const std::vector<std::string> &existing)
                        {
                            if (existing.size() < path.size()) return false;
                            return std::equal(path.begin(), path.end(), existing.begin());
                        }),
                    tsym->movedFields.end());
            }
        }
    }

    // Stage 3: re-assignment may change what a reference / ref field points to.
    updateAssignOrigins(node);
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRIf *node)
{
    analyzeExpr(node->cond.get());

    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
    if (node->cond->type && !node->cond->type->equals(boolTy))
        log(*node, "if condition must be bool.");

    if (node->thenBlock) visit(node->thenBlock.get());
    if (node->elseBlock.has_value()) visit(node->elseBlock.value().get());
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRMatch *node)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    analyzeExpr(node->scrutinee.get());
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

    for (auto &arm : node->arms)
    {
        auto armScope = SymbolTable::getInstance().getCurrentScope()->createChild();
        SymbolTable::getInstance().enterScope(armScope);

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
                    log(*node, "variant '" + arm.variantName + "' pattern expects " + std::to_string(variant->payloadTypes.size())
                        + " binding(s), got " + std::to_string(arm.bindings.size()), E_VariantPatternMismatch);
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
            if (matchResultType && !matchResultType->equals(tailTy))
                log(*node, "match arms have inconsistent types: '" + matchResultType->toString() + "' vs '" + tailTy->toString() + "'.");
            else
                matchResultType = tailTy;
        }
        else
        {
            log(*node, "match arm has neither a block body nor a value.");
        }
        SymbolTable::getInstance().exitScope(); // armScope
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
                log(*node, "match is not exhaustive: variant '" + v.name + "' is not covered (add an arm or a '_' wildcard)",
                    E_NonExhaustiveMatch);
                break;
            }
        }
    }

    node->type = matchResultType ? matchResultType : voidTy; // VOID for statement match

    SymbolTable::getInstance().exitScope(); // matchScope
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRLoop *node)
{
    if (node->cond.has_value())
    {
        analyzeExpr(node->cond.value().get());
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

    loopDepth_++;
    if (node->body) visit(node->body.get());
    loopDepth_--;

    for (auto s = SymbolTable::getInstance().getCurrentScope(); s; s = s->getParent())
        for (const auto &[name, sym] : s->getSymbols())
        {
            if (!sym->type || sym->type->isCopyable()) continue;
            auto it = preBodyMoves.find(name);
            if (it == preBodyMoves.end()) continue; // declared in the body, not enclosing
            bool nowMoved = sym->state == VarState::Moved || !sym->movedFields.empty();
            bool wasMoved = it->second.first || it->second.second;
            if (nowMoved && !wasMoved)
                log(*node, "value '" + name + "' is moved inside this loop without being "
                    "re-assigned; the next iteration would move it again.", E_UseOfMovedValue);
        }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRBreak *node)
{
    if (loopDepth_ == 0)
        log(*node, "break can only be used inside a loop.");
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRContinue *node)
{
    if (loopDepth_ == 0)
        log(*node, "continue can only be used inside a loop.");
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
    else if (functionInfo.declaredReturnType)
    {
        // Stage 3: reject returning a reference that points into this function's
        // frame (and structs whose reference fields do). Skipped on type mismatch
        // — the program is already invalid; the type error is the real one.
        checkDanglingReturn(node);
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRExprStmt *node)
{
    analyzeExpr(node->expr.get());
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
                /*errorId=*/1, Logger::LogLevel::WARNING);
        }
        break;
    case HIRLiteral::Kind::Char:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::CHAR);
        break;
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRNameRef *node)
{
    auto *sym = SymbolTable::getInstance().lookupSymbol(node->name);
    if (!sym)
    {
        log(*node, "undefined identifier '" + node->name + "'.", E_UndefinedIdentifier);
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }
    node->symbol = sym;
    node->type = sym->type;
    node->scope = SymbolTable::getInstance().getCurrentScope();

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

    // Borrow-check: a Copy read of a whole variable conflicts with active &mut
    // borrows. Non-Copy uses are moves and are checked at the consuming sites
    // (handleMoveSource). Also close the read-after-move gap in non-consuming
    // positions (e.g. `if x == 0` after `let y = x;`).
    if (sym->type && sym->type->isCopyable())
        checkBorrowUse(node->name, {}, BorrowUseKind::Read, *node);
    if (sym->state == VarState::Moved)
        log(*node, "use of moved value: '" + node->name + "'", E_UseOfMovedValue);
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
        "Add", "Sub", "Mul", "Div", "Rem",
        "PartialEq", "PartialOrd",
        "BitAnd", "BitOr", "BitXor", "Shl", "Shr"};
    return ops.count(name) != 0;
}

bool HIRSemanticAnalyzer::resolveOperatorMethod(HIRBinaryOp *node,
    const std::shared_ptr<CustomType> &ct, const char *opMethod, const char *opTrait)
{
    std::string baseName = ct->getOriginName();
    std::string symName = baseName + "::" + opMethod;
    Symbol *symbol = SymbolTable::getInstance().lookupSymbol(symName);
    if (!symbol)
    {
        log(*node, "operator '" + std::string(node->opToString()) + "' resolves to method '" + opMethod
            + "' but no such method is registered on '" + baseName + "'.");
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
        log(*node, std::string("operator method '") + opMethod + "' must take 2 parameters (self, other), got "
            + std::to_string(newTy->getParams().size()) + ".");
        return false;
    }
    if (!node->left->type->equals(newTy->getParams()[0])
        || !node->right->type->equals(newTy->getParams()[1]))
    {
        log(*node, "operator '" + std::string(node->opToString()) + "' operand types do not match the '"
            + opTrait + "' method signature.");
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
    const std::shared_ptr<GenericParamType> &gp, const char *opMethod, const char *opTrait)
{
    // Find the operator trait among the generic param's bounds.
    std::shared_ptr<TraitType> matchedTrait = nullptr;
    for (const auto &traitTy : gp->getConstraints())
        if (traitTy->getName() == opTrait)
        {
            matchedTrait = traitTy;
            break;
        }
    if (!matchedTrait)
    {
        log(*node, "generic parameter '" + gp->getParamName() + "' has no '" + opTrait + "' bound; cannot resolve operator '"
            + node->opToString() + "'.");
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
        log(*node, "operator method '" + std::string(opMethod) + "' must take 2 parameters (self, other), got "
            + std::to_string(paramTypes.size()) + ".");
        return false;
    }
    if (!node->left->type->equals(paramTypes[0]) || !node->right->type->equals(paramTypes[1]))
    {
        log(*node, "operator '" + std::string(node->opToString()) + "' operand types do not match the '"
            + opTrait + "' method signature.");
        return false;
    }

    node->operatorMethodType = context->typeContext->getFunction(paramTypes, retTy);
    node->operatorMethodName = "<" + gp->getParamName() + ">::" + opMethod;
    node->operatorStructArgs = {}; // monomorphization fills from the concrete struct
    node->type = retTy;
    return true;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRBinaryOp *node)
{
    analyzeExpr(node->left.get());
    analyzeExpr(node->right.get());

    if (node->left->type && node->right->type && !node->left->type->equals(node->right->type))
        log(*node, "operands of binary operator must have the same type.");

    // Operator type check: every binary op requires its operand types to support
    // it (the codegen would otherwise crash — e.g. ICmp on a struct). Concrete
    // primitives are classified by op family; a generic param must carry a
    // `Numeric`/`Integer` marker bound (or the specific operator trait, e.g.
    // `Add`); a struct/enum must implement the corresponding operator trait
    // (operator overloading), otherwise it is rejected here.
    using K = HIRBinaryOp::OpKind;
    bool isCmp    = node->opKind == K::Eq || node->opKind == K::Ne || node->opKind == K::Lt
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
        // By-value `self`/`other` consume both operands (mirrors call args).
        handleMoveSource(node->left.get(), *node);
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
                    log(*node, "cannot cast integer to a smaller integer type.");
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
                log(*node, "cannot cast integer to a smaller integer type.");
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
    HIRCall *node, std::shared_ptr<GenericParamType> gp)
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
            log(*node, "ambiguous call to '" + node->methodName + "': generic parameter '" +
                       gp->getParamName() + "' is bound by multiple traits that define it ('" +
                       matchedTrait->getName() + "' and '" + traitTy->getName() + "').");
            node->type = voidTy;
            return;
        }
        matchedTrait = traitTy;
    }
    if (!matchedTrait)
    {
        log(*node, "generic parameter '" + gp->getParamName() + "' has no method '" +
                   node->methodName + "' (no matching trait bound).");
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
                        registerBorrow(rroot, rpath, refTy->isMutableRef(), /*isPromoted=*/false, *node);
                }
            }
        }

        // Type-check args against params[1..].
        for (size_t i = 0; i < node->args.size() && i + 1 < paramTypes.size(); ++i)
        {
            analyzeExpr(node->args[i].get());
            if (node->args[i]->type && !typesCompatible(paramTypes[i + 1], node->args[i]->type))
                log(*node->args[i], "argument type mismatch.");
            // By-value non-Copy arg consumes the source.
            handleMoveSource(node->args[i].get(), *node);
        }

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
    if (name == "print_str")      argTy = i8PtrTy;
    else if (name == "print_int") argTy = i32Ty;
    else if (name == "print_float") argTy = f64Ty;
    else if (name == "print_bool") argTy = boolTy;
    else if (name == "print_char") argTy = charTy;
    else if (name == "println")   { argTy = nullptr; argCount = 0; }
    else return false; // not a builtin print

    if (node->args.size() != argCount)
    {
        log(*node, "builtin '" + name + "' expects " + std::to_string(argCount)
            + " argument(s), got " + std::to_string(node->args.size()) + ".");
    }

    for (auto &arg : node->args)
    {
        analyzeExpr(arg.get());
        if (argTy && arg->type && !typesCompatible(argTy, arg->type))
            log(*arg, "builtin '" + name + "' expects an argument of type '"
                + argTy->toString() + "', got '" + arg->type->toString() + "'.");
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
    else return false; // not an input builtin

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
    auto mutI8Ptr = context->typeContext->getReference(i8Ty, /*isMutable=*/true);
    auto shI8Ptr = context->typeContext->getReference(i8Ty, /*isMutable=*/false);

    std::shared_ptr<Type> retTy = nullptr;
    std::vector<std::shared_ptr<Type>> argTys;
    if (name == "__alloc")
    {
        retTy = mutI8Ptr;           // &mut i8 — the caller gets a writable buffer
        argTys = {i32Ty};           // size
    }
    else if (name == "__free")
    {
        retTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        argTys = {shI8Ptr};         // &i8 — free only needs the address
    }
    else if (name == "__memcpy")
    {
        retTy = mutI8Ptr;           // returns dst (unused in .lis)
        argTys = {shI8Ptr, shI8Ptr, i32Ty}; // dst, src, n — both shared pointers
    }
    else if (name == "__strlen")
    {
        retTy = i32Ty;              // length (truncated from size_t)
        argTys = {shI8Ptr};         // &i8
    }
    else return false; // not a heap builtin

    if (node->args.size() != argTys.size())
    {
        log(*node, "builtin '" + name + "' expects " + std::to_string(argTys.size())
            + " argument(s), got " + std::to_string(node->args.size()) + ".");
    }
    for (size_t i = 0; i < node->args.size() && i < argTys.size(); ++i)
    {
        analyzeExpr(node->args[i].get());
        if (node->args[i]->type && !typesCompatible(argTys[i], node->args[i]->type))
            log(*node->args[i], "builtin '" + name + "' expects argument of type '"
                + argTys[i]->toString() + "', got '" + node->args[i]->type->toString() + "'.");
    }

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
    if (name == "to_string_i32")      argTy = i32Ty;
    else if (name == "to_string_i64") argTy = i64Ty;
    else if (name == "to_string_f64") argTy = f64Ty;
    else if (name == "to_string_bool") argTy = boolTy;
    else if (name == "to_string_char") argTy = charTy;
    else return false; // not a to_string builtin

    // The return type is the stdlib String struct (preloaded before user code).
    auto strOpt = context->typeContext->getCustom("String");
    if (!strOpt.has_value())
    {
        log(*node, "builtin '" + name + "' requires the stdlib 'String' type, which was not found.");
        return false;
    }

    if (node->args.size() != 1)
        log(*node, "builtin '" + name + "' expects 1 argument, got " + std::to_string(node->args.size()) + ".");
    if (!node->args.empty())
    {
        analyzeExpr(node->args[0].get());
        if (node->args[0]->type && !node->args[0]->type->equals(argTy))
            log(*node->args[0], "builtin '" + name + "' expects an argument of type '"
                + argTy->toString() + "', got '" + node->args[0]->type->toString() + "'.");
    }

    node->type = strOpt.value();
    if (auto *nr = dynamic_cast<HIRNameRef *>(node->callee.get()))
        nr->type = strOpt.value();
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
            case BuiltinCategory::ToString:
                handleToStringBuiltin(node, nr->name);
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

        // 参数类型检查（使用实例化后的具体类型）
        for (size_t i = 0; i < node->args.size() && i < instantiatedFuncType->getParams().size(); ++i)
        {
            analyzeExpr(node->args[i].get());
            if (!typesCompatible(instantiatedFuncType->getParams()[i], node->args[i]->type))
                log(*node->args[i], "argument type mismatch.");
            // A by-value non-Copy arg consumes the source (`foo(p)` moves p).
            handleMoveSource(node->args[i].get(), *node);
        }

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
            newTy = std::static_pointer_cast<FunctionType>(substituteType(newTy, structSubst));
        }

        // Concrete struct args, in the struct's generic-param order. For a
        // method of a generic struct (`impl<T> Foo<T> { fn bar(self) }`), the
        // MIR method now carries these params, so the call must carry the args
        // for monomorphization to substitute them (Item B-b).
        std::vector<std::shared_ptr<Type>> structArgs = customTy->getGenericArgs();

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
                                            genericArgs.begin(), genericArgs.end());
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
                            log(*node, "cannot borrow '" + rroot + "' as mutable because it is not mutable",
                                E_CannotBorrowAsMutable);
                    }
                    registerBorrow(rroot, rpath, refTy->isMutableRef(), /*isPromoted=*/false, *node);
                }
            }
        }

        for (size_t i = 0; i < node->args.size() && i + 1 < instantiatedFuncType->getParams().size(); ++i)
        {
            analyzeExpr(node->args[i].get());
            // typesCompatible (not equals): a `&mut T` argument satisfies a
            // `&T` param (`s.push_str(&mut x)`), matching the builtin /
            // dispatch / regular call paths.
            if (!typesCompatible(instantiatedFuncType->getParams()[i + 1], node->args[i]->type))
                log(*node->args[i], "argument type mismatch.");
            // By-value non-Copy arg consumes the source.
            handleMoveSource(node->args[i].get(), *node);
        }

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
        auto custom = context->typeContext->getCustom(node->staticTypeName);
        if (!custom.has_value())
        {
            log(*node, "cannot find type '" + node->staticTypeName + "'.");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        auto customTy = std::dynamic_pointer_cast<CustomType>(custom.value());
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

        std::string funcName = node->staticTypeName + "::" + node->methodName;
        Symbol *symbol = SymbolTable::getInstance().lookupSymbol(funcName);
        if (!symbol)
        {
            log(*node, "static method symbol not found: " + funcName);
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }
        auto funcType = std::static_pointer_cast<FunctionType>(symbol->type);

        // ===================== 泛型参数推断与填充 =====================
        // Mirror the Method branch: always carry the struct's concrete generic
        // args (empty for a bare-name static call on a generic definition),
        // appending the method's own args when the method is generic. Without
        // this, a static call on a generic struct never reaches monomorphization.
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
                                            genericArgs.begin(), genericArgs.end());
            instantiatedFuncType = instantiateGenericFunction(funcType, genericArgs);
        }
        else
        {
            node->typedGenericParams = structArgs;
            instantiatedFuncType = funcType;
        }

        if (node->args.size() != it->params.size())
            log(*node, "static method '" + node->methodName + "' expects " + std::to_string(it->params.size()) + " arguments, got " + std::to_string(node->args.size()) + ".");

        for (size_t i = 0; i < node->args.size() && i < it->params.size(); ++i)
        {
            analyzeExpr(node->args[i].get());
            if (!typesCompatible(instantiatedFuncType->getParams()[i], node->args[i]->type))
                log(*node->args[i], "argument type mismatch.");
            // By-value non-Copy arg consumes the source.
            handleMoveSource(node->args[i].get(), *node);
        }

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

    node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32); // fallback

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
        node->type = it->type;
    else
        log(*node, "struct '" + ct->getName() + "' has no field '" + node->memberName + "'.");

    // Borrow-check: a Copy field read conflicts with active &mut borrows. The
    // access root is the object's root binding (a reference root means the read
    // goes THROUGH a borrow, which is allowed — the root name differs from any
    // borrowed binding). Non-Copy field moves are checked at consuming sites.
    std::string root;
    std::vector<std::string> path;
    if (node->type && node->type->isCopyable()
        && extractRootAndPath(node, root, path))
        checkBorrowUse(root, path, BorrowUseKind::Read, *node);
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRIndexAccess *node)
{
    analyzeExpr(node->object.get());
    analyzeExpr(node->index.get());
    auto objTy = node->object->type;
    if (!objTy) return;

    node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    // The element type: an array `[T; N]` yields T; a REFERENCE to a primitive
    // (e.g. `s.data: &mut i8` — a heap pointer) is indexed as a C-style pointer
    // to T elements (`s.data[i]` → i8). A reference to an array auto-derefs.
    // Deref ALL reference layers (MIR's buildIndexAccess walks the full chain;
    // sema used to stop at one, causing `&&T` divergence).
    bool isPointer = false;
    while (auto ref = std::dynamic_pointer_cast<ReferenceType>(objTy))
    {
        objTy = ref->getBaseType();
        isPointer = true;
    }

    std::shared_ptr<Type> elemTy;
    if (auto arrTy = std::dynamic_pointer_cast<ArrayType>(objTy))
        elemTy = arrTy->getElementType();
    else if (isPointer)
    {
        // C-style pointer indexing `s.data[i]`. Only primitives (a byte/small
        // buffer) or arrays are legal pointees; a reference to a struct would
        // hit an llvm_unreachable in codegen (getElementType) and a reference
        // to a struct would otherwise let `r[0].f = x` write through a shared
        // reference.
        if (objTy->getKind() != Type::Kind::Primitive && objTy->getKind() != Type::Kind::Array)
        {
            log(*node, "cannot index a reference to type '" + objTy->toString()
                + "' (only references to primitives or arrays).");
            return;
        }
        elemTy = objTy;
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
    // Copy field read in visit(HIRMemberAccess)); the path carries the "[*]"
    // index segment from extractRootAndPath.
    std::string root;
    std::vector<std::string> path;
    if (node->type && node->type->isCopyable()
        && extractRootAndPath(node, root, path))
        checkBorrowUse(root, path, BorrowUseKind::Read, *node);
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRArrayLiteral *node)
{
    std::shared_ptr<Type> elemTy;
    for (auto &e : node->elements)
    {
        analyzeExpr(e.get());
        if (!elemTy)
            elemTy = e->type;
        else if (e->type && !e->type->equals(elemTy))
            log(*e, "array literal elements must all have the same type ('"
                + elemTy->toString() + "' vs '" + e->type->toString() + "').");
    }
    if (!elemTy)
    {
        // `[]` — no element type to infer from, and `[T; 0]` is rejected too.
        log(*node, "empty array literal has no element type; write an explicit `[T; N]` with N > 0.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    if (isReferenceType(elemTy))
    {
        log(*node, "array literal element type '" + elemTy->toString()
            + "' cannot be a reference (reference elements are not supported yet).");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }
    if (!elemTy->isCopyable())
    {
        log(*node, "array literal element type '" + elemTy->toString()
            + "' must be Copy (arrays of non-Copy types are not supported yet).");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }
    node->type = context->typeContext->getArray(elemTy, node->elements.size());
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRStructInit *node)
{
    auto *sym = SymbolTable::getInstance().lookupSymbol(node->structName);
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
        if (mval->type && !typesCompatible(fIt->type, mval->type))
            log(*node, "type mismatch for field '" + mname + "': expected '" + fIt->type->toString() + "', got '" + mval->type->toString() + "'.");
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRVariantInit *node)
{
    auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    auto *sym = SymbolTable::getInstance().lookupSymbol(node->enumName);
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
        log(*node, "variant '" + node->variantName + "' expects " + std::to_string(variant->payloadTypes.size())
            + " payload argument(s), got " + std::to_string(node->args.size()), E_VariantArgMismatch);
        node->type = voidTy;
        return;
    }
    for (size_t i = 0; i < node->args.size(); ++i)
    {
        if (node->args[i]->type && !typesCompatible(variant->payloadTypes[i], node->args[i]->type))
            log(*node->args[i], "payload type mismatch for variant '" + node->variantName + "': expected '"
                + variant->payloadTypes[i]->toString() + "', got '" + node->args[i]->type->toString() + "'.");
    }

    // Non-Copy payload args are moved into the enum value.
    for (auto &arg : node->args)
        handleMoveSource(arg.get(), *node);

    node->type = finalEnumTy;
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRRef *node)
{
    analyzeExpr(node->expr.get());
    if (node->expr->type)
        node->type = context->typeContext->getReference(node->expr->type, node->isMutable);

    // Borrow-check: `&p` / `&mut p` borrows the place p. Register the borrow
    // after checking aliasing and that p hasn't been moved.
    std::string root;
    std::vector<std::string> path;
    if (extractRootAndPath(node->expr.get(), root, path))
    {
        if (auto *sym = SymbolTable::getInstance().lookupSymbol(root))
        {
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
            registerBorrow(root, path, node->isMutable, /*isPromoted=*/false, *node);
        }
    }
}