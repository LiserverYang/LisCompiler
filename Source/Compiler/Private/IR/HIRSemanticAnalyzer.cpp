/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/HIRSemanticAnalyzer.hpp"

#include <algorithm>
#include <unordered_set>

// ============================================================
//  Helpers
// ============================================================

std::shared_ptr<Type>
HIRSemanticAnalyzer::resolveType(const HIRRawType &raw, HIRNode &errorNode)
{
    if (!raw.isPresent)
        return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    std::shared_ptr<Type> base;

    if (raw.isPrimitive)
    {
        base = context->typeContext->getPrimitive(PrimitiveType::getKind(raw.name));
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
                log(errorNode, "the type '" + raw.name + "' cannot be found.");
                return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            }
            base = custom.value();
        }
    }

    // Generic instantiation: Foo<T1, T2, ...>
    if (!raw.genericArgs.empty())
    {
        auto custom = std::dynamic_pointer_cast<CustomType>(base);
        if (!custom)
        {
            log(errorNode, "type '" + raw.name + "' is not a struct and cannot take generic arguments.");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        if (!custom->isGeneric())
        {
            log(errorNode, "type '" + raw.name + "' is not generic.");
            return context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        }
        if (custom->getGenericParams().size() != raw.genericArgs.size())
        {
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
    if (stmt) stmt->accept(this);
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
        if (ctParam->getName() != ctArg->getName()) return;
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

// 类型替换（辅助函数）
std::shared_ptr<Type> HIRSemanticAnalyzer::substituteType(
    std::shared_ptr<Type> ty,
    const std::unordered_map<std::string, std::shared_ptr<Type>> &subst)
{
    if (!ty) return ty;

    if (auto gp = std::dynamic_pointer_cast<GenericParamType>(ty))
    {
        auto it = subst.find(gp->getParamName());
        return (it != subst.end()) ? it->second : ty;
    }
    if (auto ref = std::dynamic_pointer_cast<ReferenceType>(ty))
    {
        auto base = substituteType(ref->getBaseType(), subst);
        return context->typeContext->getReference(base, ref->isMutableRef());
    }
    if (auto ct = std::dynamic_pointer_cast<CustomType>(ty))
    {
        if (ct->getGenericArgs().empty()) return ty;
        std::vector<std::shared_ptr<Type>> newArgs;
        bool changed = false;
        for (auto &a : ct->getGenericArgs())
        {
            auto na = substituteType(a, subst);
            if (na.get() != a.get()) changed = true;
            newArgs.push_back(na);
        }
        if (!changed) return ty;
        auto origin = ct->genericOrigin ? ct->genericOrigin : ct;
        return context->typeContext->instantiateCustom(origin, std::move(newArgs));
    }
    if (auto ft = std::dynamic_pointer_cast<FunctionType>(ty))
    {
        std::vector<std::shared_ptr<Type>> newParams;
        for (auto &p : ft->getParams())
            newParams.push_back(substituteType(p, subst));
        auto newRet = substituteType(ft->getReturnType(), subst);
        return context->typeContext->getFunction(newParams, newRet);
    }
    return ty;
}

// ============================================================
//  Pre-registration pass (forward declarations for the global scope)
// ============================================================

void HIRSemanticAnalyzer::preRegister(HIRNode *item)
{
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
    else if (auto *f = dynamic_cast<HIRFunction *>(item))
    {
        if (SymbolTable::getInstance().lookupSymbol(f->name)) return;
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Function;
        sym->name = f->name;
        std::vector<std::shared_ptr<Type>> paramTypes;
        if (f->hasReturnType)
        {
            std::vector<std::shared_ptr<Type>> paramTypes;
            for (auto &[pname, rawTy] : f->rawParams)
            {
                auto ty = resolveType(rawTy, *f);
                paramTypes.push_back(ty);
            }
            auto retTy = resolveType(f->rawReturnType, *f);
            std::shared_ptr<Type> funcType = nullptr;
            if (f->isGeneric)
                funcType = context->typeContext->getGenericFunction(f->gParams, paramTypes, retTy);
            else
                funcType = context->typeContext->getFunction(paramTypes, retTy);
            sym->type = funcType;
            f->type = funcType;
        }
        SymbolTable::getInstance().insertSymbol(f->name, std::move(sym));
    }
}

// ============================================================
//  Program
// ============================================================

void HIRSemanticAnalyzer::visit(HIRProgram *node)
{
    if (!node) return;

    // Pass 1: register all top-level names so forward references work
    for (auto &item : node->items)
        preRegister(item.get());

    // Pass 2: full analysis
    for (auto &item : node->items)
        item->accept(this);
}

// ============================================================
//  Top-level declarations
// ============================================================

void HIRSemanticAnalyzer::visit(HIRStruct *node)
{
    auto sym = SymbolTable::getInstance().lookupSymbol(node->name);
    if (!sym)
    {
        log(*node, "internal: struct symbol '" + node->name + "' was not pre-registered.");
        return;
    }

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
                for (auto &tn : cIt->second)
                {
                    auto *traitSym = SymbolTable::getInstance().lookupSymbol(tn);
                    if (!traitSym || traitSym->kind != SymbolKind::Trait)
                    {
                        log(*node, "trait constraint '" + tn + "' is not a trait name.");
                        continue;
                    }
                    traits.push_back(std::static_pointer_cast<TraitType>(traitSym->type));
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

    if (node->isGeneric)
    {
        std::vector<std::shared_ptr<Type>> gParamTypes(node->gParams.begin(), node->gParams.end());
        sym->type = context->typeContext->createGenericCustom(node->name, std::move(gParamTypes), std::move(fields));
    }
    else
    {
        sym->type = context->typeContext->createCustom(node->name, std::move(fields));
    }
    node->structSymbol = sym;

    structGParams.clear();
    isInStruct = false;
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

    sym->type = context->typeContext->createTrait(node->name, std::move(traitMethods));
    node->traitSymbol = sym;

    isInTraitMethod = false;
    traitName = "";
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
        // NOTE: selfTypeForMethods stays as baseStruct (the generic definition).
        // `self` inside methods has type Box (with T referring to structGParams).
        // At call sites, the Method branch in visit(HIRCall) substitutes T -> concrete.
    }

    currentStructType = selfTypeForMethods;

    std::shared_ptr<TraitType> traitType = nullptr;
    if (node->traitName.has_value())
    {
        const std::string &tn = node->traitName.value();
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
        std::vector<std::shared_ptr<Type>> paramTypes;
        for (auto &[n, t] : method->params)
            paramTypes.push_back(t);
        auto funcType = context->typeContext->getFunction(paramTypes, method->returnType);

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

            if (!sm.returnType->equals(tm.returnType))
                log(*node, "method '" + tm.name + "' return type mismatch: expected '" + tm.returnType->toString() + "', got '" + sm.returnType->toString() + "'.");

            if (sm.params.size() != tm.params.size())
            {
                log(*node, "method '" + tm.name + "' param count mismatch.");
                continue;
            }

            for (size_t i = 0; i < tm.params.size(); ++i)
            {
                std::shared_ptr<Type> expected = tm.params[i].type;
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

    functionInfo.isInFunction = true;
    functionInfo.gParams.clear();

    for (auto &it : node->gParams)
    {
        std::vector<std::shared_ptr<TraitType>> types;

        for (auto &trait : node->unsolveConstraints[it->getParamName()])
        {
            Symbol *traitSym = SymbolTable::getInstance().lookupSymbol(trait);

            if (!traitSym)
            {
                log(*node, "can not find the trait '" + it->getParamName() + "' for trait constraint.");
                continue;
            }

            if (traitSym->kind != SymbolKind::Trait)
            {
                log(*node, "trait constraint '" + trait + "' is not a trait name");
                continue;
            }

            assert(traitSym->type->getKind() == Type::Kind::Trait);

            types.push_back(std::static_pointer_cast<TraitType>(traitSym->type));
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
        if (SymbolTable::getInstance().lookupSymbol(pname))
            log(*node, "param '" + pname + "' shadows a previous definition.");

        auto resolvedTy = resolveType(rawTy, *node);
        if (!rawTy.isPresent)
            log(*node, "cannot deduce type for param '" + pname + "'.");

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
        functionInfo.declaredReturnType = node->returnType;
    }
    else
    {
        node->returnType = functionInfo.declaredReturnType;
    }

    // --- body ---
    if (node->body)
        visit(node->body.get());

    functionInfo.isInFunction = false;

    // If return type was inferred from return statements, update node
    if (!node->hasReturnType)
        node->returnType = functionInfo.declaredReturnType;

    // Build FunctionType (self type is already in node->params for methods)
    // For the function symbol type we include all params (including self)
    std::vector<std::shared_ptr<Type>> allParamTypes;
    for (auto &[n, t] : node->params)
        allParamTypes.push_back(t);

    if (!node->hasReturnType)
    {
        if (node->isGeneric)
        {
            node->type = context->typeContext->getGenericFunction(node->gParams, allParamTypes, node->returnType);
        }
        else
        {
            node->type = context->typeContext->getFunction(allParamTypes, node->returnType);
        }
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

    for (auto &stmt : node->stmts)
        analyzeStmt(stmt.get());

    SymbolTable::getInstance().exitScope();
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRVarDecl *node)
{
    if (SymbolTable::getInstance().lookupSymbol(node->name))
        log(*node, "variable '" + node->name + "' already exists in this scope.");

    std::shared_ptr<Type> initType;

    if (node->init.has_value())
    {
        analyzeExpr(node->init.value().get());
        initType = node->init.value()->type;

        // Move semantics check
        if (auto *nameRef = dynamic_cast<HIRNameRef *>(node->init.value().get()))
        {
            auto *sym = SymbolTable::getInstance().lookupSymbol(nameRef->name);
            if (sym)
            {
                if (sym->state == VarState::Moved)
                    log(*node, "use of moved value: '" + nameRef->name + "'");
                if (sym->type && sym->type->getKind() == Type::Kind::Custom)
                    sym->state = VarState::Moved;
            }
        }
    }

    if (node->hasExplicitType)
    {
        node->type = resolveType(node->rawType, *node);
        if (initType && !initType->equals(node->type))
            log(*node, "type mismatch in variable declaration.");
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
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRAssign *node)
{
    analyzeExpr(node->target.get());
    analyzeExpr(node->value.get());

    if (node->target->type && node->value->type && !node->target->type->equals(node->value->type))
        log(*node, "assignment type mismatch.");

    // Move semantics
    if (auto *nameRef = dynamic_cast<HIRNameRef *>(node->value.get()))
    {
        auto *sym = SymbolTable::getInstance().lookupSymbol(nameRef->name);
        if (sym)
        {
            if (sym->state == VarState::Moved)
                log(*node, "use of moved value: '" + nameRef->name + "'");
            if (sym->type && sym->type->getKind() == Type::Kind::Custom)
                sym->state = VarState::Moved;
        }
    }
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
void HIRSemanticAnalyzer::visit(HIRLoop *node)
{
    if (node->cond.has_value())
    {
        analyzeExpr(node->cond.value().get());
        auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        if (node->cond.value()->type && !node->cond.value()->type->equals(boolTy))
            log(*node, "loop condition must be bool.");
    }
    if (node->body) visit(node->body.get());
}

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
    }

    if (functionInfo.declaredReturnType && !functionInfo.declaredReturnType->equals(retTy))
        log(*node, "return type '" + retTy->toString() + "' does not match declared '" + functionInfo.declaredReturnType->toString() + "'.");
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
        // TODO: string type
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
        log(*node, "undefined identifier '" + node->name + "'.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }
    node->symbol = sym;
    node->type = sym->type;
    node->scope = SymbolTable::getInstance().getCurrentScope();
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRBinaryOp *node)
{
    analyzeExpr(node->left.get());
    analyzeExpr(node->right.get());

    if (node->left->type && node->right->type && !node->left->type->equals(node->right->type))
        log(*node, "operands of binary operator must have the same type.");

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
            if (!tType->isFloat() && !tType->isInteger())
            {
                log(*node, "integer can only be cast to float or integer.");
                break;
            }
            if (tType->isInteger() && (size_t)rType->getPrimKind() > (size_t)tType->getPrimKind())
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
void HIRSemanticAnalyzer::visit(HIRCall *node)
{
    switch (node->callKind)
    {
    // ---- Regular function call -------------------------------------------
    case HIRCall::CallKind::Regular:
    {
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
            if (!node->args[i]->type->equals(instantiatedFuncType->getParams()[i]))
                log(*node->args[i], "argument type mismatch.");
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

        std::string symName = customTy->getName() + "::" + (it->isTraitImpl ? (it->traitName + "::") : "") + node->methodName;
        Symbol *symbol = SymbolTable::getInstance().lookupSymbol(symName);

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

            node->typedGenericParams = genericArgs;
            instantiatedFuncType = instantiateGenericFunction(newTy, genericArgs);
        }
        else
        {
            instantiatedFuncType = newTy;
        }

        for (size_t i = 0; i < node->args.size() && i + 1 < instantiatedFuncType->getParams().size() + 1; ++i)
        {
            analyzeExpr(node->args[i].get());
            if (!node->args[i]->type->equals(instantiatedFuncType->getParams()[i + 1]))
                log(*node->args[i], "argument type mismatch.");
        }

        auto refExpr = std::make_unique<HIRRef>();
        refExpr->expr = std::move(node->object);
        refExpr->isMutable = std::static_pointer_cast<ReferenceType>(instantiatedFuncType->getParams()[0])->isMutableRef();
        refExpr->type = instantiatedFuncType->getParams()[0];
        node->args.insert(node->args.begin(), std::move(refExpr));

        node->type = instantiatedFuncType->getReturnType();

        std::string funcName = customTy->getName() + "::" + (it->isTraitImpl ? (it->traitName + "::") : "") + node->methodName;
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

            node->typedGenericParams = genericArgs;
            instantiatedFuncType = instantiateGenericFunction(funcType, genericArgs);
        }
        else
        {
            instantiatedFuncType = funcType;
        }

        if (node->args.size() != it->params.size())
            log(*node, "static method '" + node->methodName + "' expects " + std::to_string(it->params.size()) + " arguments, got " + std::to_string(node->args.size()) + ".");

        for (size_t i = 0; i < node->args.size() && i < it->params.size(); ++i)
        {
            analyzeExpr(node->args[i].get());
            if (!node->args[i]->type->equals(instantiatedFuncType->getParams()[i]))
                log(*node->args[i], "argument type mismatch.");
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
        if (mval->type && !mval->type->equals(fIt->type))
            log(*node, "type mismatch for field '" + mname + "': expected '" + fIt->type->toString() + "', got '" + mval->type->toString() + "'.");
    }
}

// ---------------------------------------------------------------------------
void HIRSemanticAnalyzer::visit(HIRRef *node)
{
    analyzeExpr(node->expr.get());
    if (node->expr->type)
        node->type = context->typeContext->getReference(node->expr->type, node->isMutable);
}