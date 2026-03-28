/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Analysiser/SemanticAnalyzer.hpp"

#include "Analysiser/Type.hpp"
#include "Core/Debugging.hpp"
#include "Logger/Logger.hpp"

#include <algorithm>
#include <unordered_set>

void SemanticAnalyzer::visit(Program *node)
{
    if (!node) return;

    for (auto &stmt : node->globalStatements)
        if (stmt)
            stmt->accept(this);

    auto scope = SymbolTable::getInstance().getCurrentScope();
}

void SemanticAnalyzer::visit(GlobalVarDef *node)
{
    if (SymbolTable::getInstance().lookupSymbol(node->name))
    {
        log(*node, "the variable '" + node->name + "' is already exists.");
    }

    // 全局变量一定有初始值
    node->initValue->accept(this);
    auto initType = node->initValue->type;

    std::shared_ptr<Type> varType;

    if (node->type.has_value())
    {
        node->type.value()->accept(this);

        varType = node->type.value()->semanticType;

        if (!initType->equals(varType))
        {
            log(*node, "the type '" + initType->toString() + "' is not equals to '" + varType->toString() + "'.");
        }
    }
    else
    {
        varType = initType;
    }

    auto symbol = std::make_unique<Symbol>();
    symbol->kind = SymbolKind::GlobalVar;
    symbol->name = node->name;
    symbol->position = node->position;
    symbol->type = varType;
    symbol->isMutable = true;
    SymbolTable::getInstance().insertSymbol(node->name, std::move(symbol));
}

void SemanticAnalyzer::visit(FunctionDef *node)
{
    if (SymbolTable::getInstance().lookupSymbol(node->name))
    {
        log(*node, "the idenfiter '" + node->name + "' is already exists.");
    }

    auto funcSymbol = std::make_unique<Symbol>();
    funcSymbol->kind = SymbolKind::Function;
    funcSymbol->name = node->name;
    funcSymbol->position = node->position;
    funcSymbol->type = nullptr;
    SymbolTable::getInstance().insertSymbol(node->name, std::move(funcSymbol));

    auto funcScope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(funcScope);

    std::vector<std::shared_ptr<Type>> params;

    for (auto &param : node->params)
    {
        if (!param) continue;

        param->accept(this);
        params.emplace_back(param->semanticType);
    }

    functionInfo.hasReturnValue = node->returnType.has_value();
    functionInfo.declaredReturnType = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    functionInfo.isInFunction = true;

    if (node->returnType.has_value())
    {
        node->returnType.value()->accept(this);
        functionInfo.declaredReturnType = node->returnType.value()->semanticType;

        if (auto func = SymbolTable::getInstance().lookupSymbol(node->name))
            if (func->kind == SymbolKind::Function)
            {
                node->type = func->type = context->typeContext->getFunction(std::move(params), functionInfo.declaredReturnType);
                node->symbol = func;
            }
    }

    node->body->accept(this);

    if (!node->returnType.has_value())
        if (auto func = SymbolTable::getInstance().lookupSymbol(node->name))
            if (func->kind == SymbolKind::Function)
            {
                node->type = func->type = context->typeContext->getFunction(std::move(params), functionInfo.declaredReturnType);
                node->symbol = func;
            }

    functionInfo.isInFunction = false;

    SymbolTable::getInstance().exitScope();
}

void SemanticAnalyzer::visit(Param *node)
{
    if (auto searchResult = SymbolTable::getInstance().lookupSymbol(node->name))
    {
        log(*node, "param name '" + node->name + "' shadows a previous definition.");
    }

    auto paramSymbol = std::make_unique<Symbol>();
    paramSymbol->kind = SymbolKind::Param;
    paramSymbol->name = node->name;
    paramSymbol->position = node->position;
    paramSymbol->type = nullptr;

    if (node->type.has_value())
    {
        node->type.value()->accept(this);
        paramSymbol->type = node->type.value()->semanticType;
        node->semanticType = paramSymbol->type;
    }

    if (node->defaultValue.has_value())
    {
        node->defaultValue.value()->accept(this);

        if (!node->type.has_value())
        {
            paramSymbol->type = node->defaultValue.value()->type;
            node->semanticType = paramSymbol->type;
        }
        else if (!node->semanticType->equals(node->defaultValue.value()->type))
        {
            log(*node, "the type '" + node->semanticType->toString() + "' is not equals to '" + node->defaultValue.value()->type->toString() + "'.");
        }
    }

    if (node->semanticType == nullptr)
    {
        log(*node, "can not deduce the type for param '" + node->name + "'.");
    }

    SymbolTable::getInstance().insertSymbol(node->name, std::move(paramSymbol));
}

void SemanticAnalyzer::visit(TypeNode *node)
{
    std::shared_ptr<Type> basic_type;

    if (node->kind == TypeNode::TypeKind::Custom)
    {
        auto custom = context->typeContext->getCustom(node->typeName);

        if (!custom.has_value())
        {
            log(*node, "the type '" + node->typeName + "' can not be found.");
            node->semanticType = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        basic_type = custom.value();
    }
    else
    {
        auto primitive = context->typeContext->getPrimitive(PrimitiveType::getKind(node->typeName));

        basic_type = primitive;
    }

    if (node->isReference)
    {
        node->semanticType = context->typeContext->getReference(basic_type, node->isMutReference);

        return;
    }

    node->semanticType = basic_type;
}

void SemanticAnalyzer::visit(ReturnStmt *node)
{
    if (!functionInfo.isInFunction)
    {
        log(*node, "return statement can only be used in function body.");
        return;
    }

    std::shared_ptr<Type> type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    if (node->returnValue.has_value())
    {
        node->returnValue.value()->accept(this);

        type = node->returnValue.value()->type;

        if (!functionInfo.hasReturnValue)
        {
            functionInfo.hasReturnValue = true;
            functionInfo.declaredReturnType = type;
        }
    }

    if (!functionInfo.declaredReturnType->equals(type))
    {
        log(*node, "the return type '" + type->toString() + "' is not equals to '" + functionInfo.declaredReturnType->toString() + "'.");
    }
}

void SemanticAnalyzer::visit(MemberVarDef *node)
{
    node->type->accept(this);
}

void SemanticAnalyzer::visit(StructDef *node)
{
    if (auto searchResult = SymbolTable::getInstance().lookupSymbol(node->name))
    {
        log(*node, "the idenfiter '" + node->name + "' is already exists.");
    }

    auto strcutSymbol = std::make_unique<Symbol>();
    strcutSymbol->kind = SymbolKind::Struct;
    strcutSymbol->name = node->name;
    strcutSymbol->position = node->position;
    strcutSymbol->type = nullptr;
    SymbolTable::getInstance().insertSymbol(node->name, std::move(strcutSymbol));

    std::unordered_set<std::string> memberNames;
    std::vector<CustomType::Field> fields;

    for (auto &member : node->members)
    {
        member->accept(this);

        if (memberNames.count(member->name))
        {
            log(*node, "the member variable '" + member->name + "' is already exists.");
        }

        memberNames.insert(member->name);

        fields.emplace_back(member->name, member->type->semanticType);
    }

    if (auto stru = SymbolTable::getInstance().lookupSymbol(node->name))
    {
        stru->type = context->typeContext->createCustom(node->name, std::move(fields));
        node->symbol = stru;
    }
}

void SemanticAnalyzer::visit(MemberFunctionDef *node)
{
    auto funcScope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(funcScope);

    // 1. 处理 Self
    if (node->selfParam.has_value())
    {
        node->selfParam.value()->accept(this);
    }

    // 2. 处理 Params
    for (auto &param : node->params)
    {
        param->accept(this);
    }

    functionInfo.hasReturnValue = node->returnType.has_value();
    functionInfo.declaredReturnType = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
    functionInfo.isInFunction = true;

    if (node->returnType.has_value())
    {
        node->returnType.value()->accept(this);
        functionInfo.declaredReturnType = node->returnType.value()->semanticType;
    }

    if (node->body.has_value())
    {
        node->body.value()->accept(this);
    }

    functionInfo.isInFunction = false;

    node->declaredReturnType = functionInfo.declaredReturnType;

    SymbolTable::getInstance().exitScope();
}

void SemanticAnalyzer::visit(SelfParam *node)
{
    bool inStructImpl = (currentStructType != nullptr);
    bool inTraitMethod = isInTraitMethod;

    if (!inStructImpl && !inTraitMethod)
    {
        log(*node, "self used outside of impl block or trait method.");
        return;
    }

    std::shared_ptr<Type> selfType;

    if (inStructImpl)
    {
        selfType = currentStructType;
        if (node->isRef)
        {
            selfType = context->typeContext->getReference(currentStructType, node->isMut);
        }
    }
    else if (inTraitMethod)
    {
        selfType = context->typeContext->createSelf(traitName, node->isMut, node->isRef);
    }

    node->semanticType = selfType;

    auto selfSymbol = std::make_unique<Symbol>();
    selfSymbol->kind = SymbolKind::Param;
    selfSymbol->name = "self";
    selfSymbol->type = selfType;
    selfSymbol->isMutable = node->isMut || !node->isRef;

    SymbolTable::getInstance().insertSymbol("self", std::move(selfSymbol));
}

void SemanticAnalyzer::visit(StructImpl *node)
{
    std::shared_ptr<Type> structType = nullptr;
    std::shared_ptr<TraitType> traitType = nullptr;
    Symbol *structSymbol;

    if (structSymbol = SymbolTable::getInstance().lookupSymbol(node->structName))
    {
        if (structSymbol->kind != SymbolKind::Struct)
        {
            log(*node, "the idenfiter '" + node->structName + "' is not a struct name.");
        }

        structType = structSymbol->type;
    }
    else
    {
        log(*node, "can not find the struct '" + node->structName + "'.");
    }

    if (node->traitName.has_value())
    {
        const std::string &traitName = node->traitName.value();

        auto traitSymbol = SymbolTable::getInstance().lookupSymbol(traitName);
        if (!traitSymbol)
        {
            log(*node, "can not find the trait '" + traitName + "'.");
            return;
        }

        if (traitSymbol->kind != SymbolKind::Trait)
        {
            log(*node, "the identifier '" + traitName + "' is not a trait name.");
            return;
        }

        traitType = std::static_pointer_cast<TraitType>(traitSymbol->type);

        if (!traitType)
        {
            log(*node, "trait '" + traitName + "' has invalid type information.");
            return;
        }

        auto &implementedTraits = structSymbol->implementedTraits;
        if (std::find(implementedTraits.begin(), implementedTraits.end(), traitName) != implementedTraits.end())
        {
            log(*node, "struct '" + node->structName + "' already implements trait '" + traitName + "'.");
            return;
        }
    }

    currentStructType = structType;

    std::vector<CustomType::Method> methods;
    std::unordered_map<std::string, CustomType::Method> methodMap;

    for (auto &method : node->methods)
    {
        method->accept(this);

        std::vector<CustomType::Field> params;

        if (method->selfParam.has_value())
        {
            params.emplace_back("self", method->selfParam.value()->semanticType);
        }

        for (auto &param : method->params)
        {
            params.emplace_back(param->name, param->semanticType);
        }

        CustomType::Method structMethod{
            method->name,
            traitType ? traitType->getName() : "",
            std::move(params),
            functionInfo.declaredReturnType,
            !method->selfParam.has_value(), // 无self参数则为静态方法
            traitType != nullptr,
        };

        methods.push_back(structMethod);

        methodMap[method->name] = structMethod;
    }

    if (traitType)
    {
        const std::string &traitName = node->traitName.value();
        const auto &traitMethods = traitType->getMethods();

        // 4.1 检查Trait的所有方法是否都被实现
        for (const auto &traitMethod : traitMethods)
        {
            auto it = methodMap.find(traitMethod.name);

            if (it == methodMap.end())
            {
                log(*node, "struct '" + node->structName + "' does not implement trait method '" + traitMethod.name + "' from trait '" + traitName + "'.");
                continue; // 继续检查其他方法，输出所有缺失的方法
            }

            // 4.2 检查方法签名（参数数量、类型、返回值）是否匹配
            const auto &structMethod = it->second;

            // 检查返回值
            if (!structMethod.returnType->equals(traitMethod.returnType))
            {
                log(*node, "method '" + traitMethod.name + "' return type mismatch: expected '" + traitMethod.returnType->toString() + "', got '" + structMethod.returnType->toString() + "'.");
            }

            // 检查参数数量
            if (structMethod.params.size() != traitMethod.params.size())
            {
                log(*node, "method '" + traitMethod.name + "' parameter count mismatch: expected " + std::to_string(traitMethod.params.size()) + ", got " + std::to_string(structMethod.params.size()) + ".");
                continue;
            }

            // 检查每个参数的类型
            for (size_t i = 0; i < traitMethod.params.size(); ++i)
            {
                const auto &traitParam = traitMethod.params[i];
                const auto &structParam = structMethod.params[i];

                std::shared_ptr<Type> newTraitType = traitParam.type;

                if (traitParam.type->getKind() == Type::Kind::Self)
                {
                    auto traitType = std::static_pointer_cast<SelfType>(traitParam.type);

                    std::shared_ptr<Type> base = structSymbol->type;

                    if (traitType->isReference())
                    {
                        base = context->typeContext->getReference(base, traitType->isMutable());
                    }

                    newTraitType = base;
                }

                if (!structParam.type->equals(newTraitType))
                {
                    log(*node, "method '" + traitMethod.name + "' parameter " + std::to_string(i + 1) + " type mismatch: expected '" + newTraitType->toString() + "', got '" + structParam.type->toString() + "'.");
                }
            }

            // 检查是否为静态方法（Trait方法如果要求非静态，结构体实现也必须非静态）
            if (traitMethod.isStatic != structMethod.isStatic)
            {
                log(*node, "method '" + traitMethod.name + "' static modifier mismatch: expected " + (traitMethod.isStatic ? "static" : "non-static") + ", got " + (structMethod.isStatic ? "static" : "non-static") + ".");
            }
        }

        // 4.3 更新符号关联信息（双向绑定）
        auto structSymbol = SymbolTable::getInstance().lookupSymbol(node->structName);
        structSymbol->implementedTraits.push_back(traitName);

        auto traitSymbol = SymbolTable::getInstance().lookupSymbol(traitName);
        traitSymbol->structsImplementing.push_back(node->structName);
    }

    if (auto *customTy = dynamic_cast<CustomType *>(structType.get()))
    {
        customTy->addMethods(methods);
    }

    currentStructType = nullptr;
}

void SemanticAnalyzer::visit(TraitDef *node)
{
    if (SymbolTable::getInstance().lookupSymbol(node->name))
    {
        log(*node, "trait '" + node->name + "' already exists.");
        return;
    }

    auto traitSymbol = std::make_unique<Symbol>();
    traitSymbol->kind = SymbolKind::Trait;
    traitSymbol->name = node->name;
    traitSymbol->type = nullptr;
    SymbolTable::getInstance().insertSymbol(node->name, std::move(traitSymbol));

    isInTraitMethod = true;
    traitName = node->name;

    std::vector<TraitType::Method> traitMethods;
    for (auto &methodNode : node->methods)
    {
        // 2.1 访问方法节点，完成方法签名的语义分析（推导参数/返回值类型）
        methodNode->accept(this);

        // 2.2 构建方法参数列表（复用CustomType::Field结构）
        std::vector<CustomType::Field> methodParams;

        if (methodNode->selfParam.has_value())
        {
            methodParams.emplace_back("self", methodNode->selfParam.value()->semanticType);
        }

        for (auto &paramNode : methodNode->params) // 方法参数节点
        {
            // 校验参数类型是否有效
            if (!paramNode->semanticType)
            {
                log(*paramNode, "parameter '" + paramNode->name + "' in trait method '" + methodNode->name + "' has invalid type.");
                continue;
            }
            methodParams.emplace_back(paramNode->name, paramNode->semanticType);
        }

        // 2.3 处理返回类型（无返回值则默认为void）
        std::shared_ptr<Type> returnType = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        if (methodNode->returnType.has_value())
        {
            auto &returnTypeNode = methodNode->returnType.value();
            if (returnTypeNode->semanticType)
            {
                returnType = returnTypeNode->semanticType;
            }
            else
            {
                log(*returnTypeNode, "return type of trait method '" + methodNode->name + "' is invalid.");
            }
        }

        TraitType::Method traitMethod{
            methodNode->name,
            node->name,
            std::move(methodParams),
            returnType,
            !methodNode->selfParam.has_value(),
            true};

        traitMethods.push_back(traitMethod);
    }

    if (auto trait = SymbolTable::getInstance().lookupSymbol(node->name))
    {
        trait->type = context->typeContext->createTrait(
            node->name,
            std::move(traitMethods));

        node->symbol = trait;
    }

    isInTraitMethod = false;
    traitName = "";
}

void SemanticAnalyzer::visit(CompoundStmt *node)
{
    if (!node) return;

    // 进入新的作用域
    auto scope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(scope);

    node->scope = scope;

    for (auto &stmt : node->statements)
    {
        if (stmt) stmt->accept(this);
    }

    SymbolTable::getInstance().exitScope();
}

void SemanticAnalyzer::visit(IfStmt *node)
{
    node->condition->accept(this);
    // 检查 condition 是否为 bool

    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);

    if (!node->condition->type->equals(boolTy))
    {
        log(*node->condition, "if condition must be bool.");
    }

    node->thenBranch->accept(this);

    if (node->elseBranch.has_value())
    {
        node->elseBranch.value()->accept(this);
    }
}

void SemanticAnalyzer::visit(DeclStmt *node)
{
    if (SymbolTable::getInstance().lookupSymbol(node->name))
    {
        log(*node, "variable '" + node->name + "' already exists in this scope.");
    }

    std::shared_ptr<Type> varType = nullptr;
    std::shared_ptr<Type> initType = nullptr;

    if (node->initValue.has_value())
    {
        node->initValue.value()->accept(this);
        initType = node->initValue.value()->type;

        if (auto identExpr = dynamic_cast<IdentifierExpr *>(node->initValue.value().get()))
        {
            // 查找右值变量的符号
            auto sym = SymbolTable::getInstance().lookupSymbol(identExpr->name);
            if (sym)
            {
                // 检查是否已被移动
                if (sym->state == VarState::Moved)
                {
                    log(*node, "use of moved value: '" + identExpr->name + "'");
                }
                // 非 Copy 类型，标记为已移动
                if (sym->type->getKind() == Type::Kind::Custom)
                {
                    sym->state = VarState::Moved;
                }
            }
        }
    }

    if (node->type.has_value())
    {
        node->type.value()->accept(this);
        varType = node->type.value()->semanticType;
        if (initType && !initType->equals(varType))
        {
            log(*node, "type mismatch in initialization.");
        }
    }
    else
    {
        if (!initType)
        {
            log(*node, "cannot infer type for '" + node->name + "'.");
            return;
        }
        varType = initType;
    }

    auto symbol = std::make_unique<Symbol>();
    symbol->kind = SymbolKind::LocalVar;
    symbol->name = node->name;
    symbol->type = varType;
    symbol->isMutable = node->isMutable;

    SymbolTable::getInstance().insertSymbol(node->name, std::move(symbol));

    node->symbol = SymbolTable::getInstance().lookupSymbol(node->name);
}

void SemanticAnalyzer::visit(AssignStmt *node)
{
    node->target->accept(this);
    node->value->accept(this);

    if (!node->target->type->equals(node->value->type))
    {
        log(*node, "assignment type mismatch.");
    }

    if (auto identExpr = dynamic_cast<IdentifierExpr *>(node->value.get()))
    {
        // 查找右值变量的符号
        auto sym = SymbolTable::getInstance().lookupSymbol(identExpr->name);
        if (sym)
        {
            // 检查是否已被移动
            if (sym->state == VarState::Moved)
            {
                log(*node, "use of moved value: '" + identExpr->name + "'");
            }
            // 非 Copy 类型，标记为已移动
            if (sym->type->getKind() == Type::Kind::Custom)
            {
                sym->state = VarState::Moved;
            }
        }
    }

    // TODO: 这里还应该检查左值是否可修改 (mutable)
}

void SemanticAnalyzer::visit(ExprStmt *node)
{
    if (node->expression) node->expression->accept(this);
}

void SemanticAnalyzer::visit(ForStmt *node)
{
    // 简化处理：创建作用域，分析 iterable，分析 body
    node->iterable->accept(this);

    auto loopScope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(loopScope);

    // TODO: 这里应该从 iterable 类型中提取元素类型
    auto elemType = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32); // 占位

    auto loopSym = std::make_unique<Symbol>();
    loopSym->kind = SymbolKind::LocalVar;
    loopSym->name = node->loopVar;
    loopSym->type = elemType;
    loopSym->isMutable = false;
    SymbolTable::getInstance().insertSymbol(node->loopVar, std::move(loopSym));

    node->body->accept(this);

    SymbolTable::getInstance().exitScope();
}

void SemanticAnalyzer::visit(WhileStmt *node)
{
    node->condition->accept(this);
    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);

    if (!node->condition->type->equals(boolTy))
    {
        log(*node->condition, "if condition must be bool.");
    }

    node->body->accept(this);
}

void SemanticAnalyzer::visit(LiteralExpr *node)
{
    // 根据字面量类型设置 Type
    switch (node->kind)
    {
    case LiteralExpr::LiteralType::Int:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
        break;
    case LiteralExpr::LiteralType::Float:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::F64);
        break;
    case LiteralExpr::LiteralType::Bool:
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        break;
    case LiteralExpr::LiteralType::String:
        // TODO: std.string
        // node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::);
        break;
        // Char...
    }
}

void SemanticAnalyzer::visit(IdentifierExpr *node)
{
    auto sym = SymbolTable::getInstance().lookupSymbol(node->name);

    if (!sym)
    {
        log(*node, "undefined identifier '" + node->name + "'.");
        // 容错
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    node->type = sym->type;
    node->symbol = sym;
    node->scope = SymbolTable::getInstance().getCurrentScope();
}

void SemanticAnalyzer::visit(StructInitExpr *node)
{
    node->structType->accept(this);
    std::shared_ptr<CustomType> structTy = std::dynamic_pointer_cast<CustomType>(node->structType->semanticType);
    node->type = structTy;
    node->structSymbol = SymbolTable::getInstance().lookupSymbol(structTy->getName());

    const std::vector<CustomType::Field> &fields = structTy->getFields();

    for (auto &member : node->memberInits)
    {
        member.second->accept(this);

        const CustomType::Field key = CustomType::Field(member.first, member.second->type);
        const auto iter = std::find(fields.begin(), fields.end(), key);

        if (iter == fields.end())
        {
            log(*node, "bad member init expr of member '" + member.first + "'.");
        }
    }
}

void SemanticAnalyzer::visit(FunctionCall *node)
{
    node->function->accept(this);

    auto funcType = std::dynamic_pointer_cast<FunctionType>(node->function->type);

    if (!funcType)
    {
        log(*node, "trying to call a non-function.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    // 检查参数数量
    if (node->arguments.size() != funcType->getParams().size())
    {
        log(*node, "argument count mismatch.");
    }

    // 检查每个参数类型
    for (size_t i = 0; i < node->arguments.size() && i < funcType->getParams().size(); ++i)
    {
        node->arguments[i]->accept(this);
        if (!node->arguments[i]->type->equals(funcType->getParams()[i]))
        {
            log(*node->arguments[i], "argument type mismatch.");
        }
    }

    node->type = funcType->getReturnType();
}

void SemanticAnalyzer::visit(MemberFunctionCall *node)
{
    if (!node->object)
    {
        log(*node, "member function call has no object expression.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    node->object->accept(this);
    std::shared_ptr<Type> objType = node->object->type;

    std::shared_ptr<Type> baseObjType = objType;

    if (auto refType = std::dynamic_pointer_cast<ReferenceType>(objType))
    {
        baseObjType = refType->getBaseType();
    }

    auto customType = std::dynamic_pointer_cast<CustomType>(baseObjType);

    if (!customType)
    {
        log(*node, "cannot call member function on non-struct type '" + objType->toString() + "'.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    if (auto identExpr = dynamic_cast<IdentifierExpr *>(node->object.get()))
    {
        auto symbol = SymbolTable::getInstance().lookupSymbol(identExpr->name);
        if (symbol && (symbol->kind == SymbolKind::Struct || symbol->kind == SymbolKind::Trait))
        {
            log(*node, "cannot call instance method on type name '" + objType->toString() + "', did you mean to create an instance first or use static method?");
            node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }
    }

    const auto &methods = customType->getMethods();

    auto methodIt = std::find_if(methods.begin(), methods.end(), [&](const CustomType::Method &method)
        { return method.name == node->methodName; });

    if (methodIt == methods.end())
    {
        log(*node, "struct '" + customType->getName() + "' has no member function named '" + node->methodName + "'.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    const CustomType::Method &targetMethod = *methodIt;

    node->method = &targetMethod;

    std::vector<std::shared_ptr<Type>> expectedParamTypes;

    if (targetMethod.isStatic)
    {
        log(*node, "the method '" + targetMethod.name + "' of '" + node->methodName + "' is static method, use '::' to call it.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    expectedParamTypes.push_back(targetMethod.params.empty()
                                     ? objType
                                     : targetMethod.params[0].type);

    for (size_t i = 1; i < targetMethod.params.size(); ++i)
    {
        expectedParamTypes.push_back(targetMethod.params[i].type);
    }

    if (node->arguments.size() != (expectedParamTypes.size() - 1))
    {
        log(*node,
            "member function '" + node->methodName + "' expects " + std::to_string(expectedParamTypes.size() - 1) + " arguments, but got " + std::to_string(node->arguments.size()) + ".");
    }

    for (size_t i = 0; i < node->arguments.size() && i < expectedParamTypes.size() - 1; ++i)
    {
        if (!node->arguments[i])
        {
            log(*node, "argument at position " + std::to_string(i + 1) + " is null.");
            continue;
        }

        node->arguments[i]->accept(this);
        std::shared_ptr<Type> argType = node->arguments[i]->type;

        if (!argType->equals(expectedParamTypes[i + 1]))
        {
            log(*node->arguments[i],
                "argument type mismatch: expected '" + expectedParamTypes[i]->toString() + "', but got '" + argType->toString() + "'.");
        }
    }

    node->type = targetMethod.returnType;
}

void SemanticAnalyzer::visit(StaticMemberCall *node)
{
    if (!node->classType)
    {
        log(*node, "static member function call has no object expression.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    node->classType->accept(this);
    std::shared_ptr<Type> objType = node->classType->semanticType;

    auto customType = std::dynamic_pointer_cast<CustomType>(objType);

    if (!customType)
    {
        log(*node, "cannot call member function on non-struct type '" + objType->toString() + "'.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    const auto &methods = customType->getMethods();

    auto methodIt = std::find_if(methods.begin(), methods.end(), [&](const CustomType::Method &method)
        { return method.name == node->methodName; });

    if (methodIt == methods.end())
    {
        log(*node, "struct '" + customType->getName() + "' has no member function named '" + node->methodName + "'.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    const CustomType::Method &targetMethod = *methodIt;

    std::vector<std::shared_ptr<Type>> expectedParamTypes;

    if (!targetMethod.isStatic)
    {
        log(*node, "the method '" + targetMethod.name + "' of '" + node->methodName + "' is not a static method, use '.' to call it.");
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    for (size_t i = 0; i < targetMethod.params.size(); ++i)
    {
        expectedParamTypes.push_back(targetMethod.params[i].type);
    }

    // 6. 检查参数数量是否匹配
    if (node->arguments.size() != (expectedParamTypes.size()))
    {
        log(*node,
            "member function '" + node->methodName + "' expects " + std::to_string(expectedParamTypes.size()) + " arguments, but got " + std::to_string(node->arguments.size()) + ".");
    }

    // 7. 检查每个参数的类型是否匹配
    for (size_t i = 0; i < node->arguments.size() && i < expectedParamTypes.size(); ++i)
    {
        if (!node->arguments[i])
        {
            log(*node, "argument at position " + std::to_string(i + 1) + " is null.");
            continue;
        }

        node->arguments[i]->accept(this);
        std::shared_ptr<Type> argType = node->arguments[i]->type;

        if (!argType->equals(expectedParamTypes[i]))
        {
            log(*node->arguments[i],
                "argument type mismatch: expected '" + expectedParamTypes[i]->toString() + "', but got '" + argType->toString() + "'.");
        }
    }

    node->type = targetMethod.returnType;
}

void SemanticAnalyzer::visit(MemberAccess *node)
{
    node->object->accept(this);
    auto objType = node->object->type;

    // 如果是引用，获取基底类型
    if (auto refType = std::dynamic_pointer_cast<ReferenceType>(objType))
    {
        objType = refType->getBaseType();
    }

    // 查找成员
    node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32); // 占位

    if (auto custom = dynamic_cast<CustomType *>(objType.get()))
    {
        auto field = std::find(custom->getFields().begin(), custom->getFields().end(), node->memberName);

        if (field != custom->getFields().end())
        {
            node->type = field->type;
        }
    }
}

void SemanticAnalyzer::visit(BinaryOp *node)
{
    node->left->accept(this);
    node->right->accept(this);

    auto lty = node->left->type;
    auto rty = node->right->type;

    // 简单的类型检查
    if (!lty->equals(rty))
    {
        log(*node, "operands of binary operator must have the same type.");
    }

    // 确定结果类型
    if (node->op == "==" || node->op == "!=" || node->op == "<" || node->op == ">")
    {
        node->type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
    }
    else
    {
        // 算术运算，结果类型与操作数相同
        node->type = lty;
    }
}

void SemanticAnalyzer::visit(CastExpr *node)
{
    node->expression->accept(this);
    node->targetType->accept(this);
    node->type = node->targetType->semanticType;

    std::shared_ptr<Type> fromType = node->expression->type;
    std::shared_ptr<Type> targetType = node->targetType->semanticType;

    if (fromType->equals(targetType))
    {
        log(*node, "useless type translating from '" + fromType->toString() + "' to '" + targetType->toString() + "'.", 1, Logger::LogLevel::INFO);

        // 相同类型直接返回，不用检查了
        return;
    }

    switch (fromType->getKind())
    {
    case Type::Kind::Primitive:
    {
        std::shared_ptr<PrimitiveType> rType = std::dynamic_pointer_cast<PrimitiveType>(fromType);

        switch (rType->getPrimKind())
        {
        case PrimitiveType::PrimKind::I8:
        case PrimitiveType::PrimKind::I16:
        case PrimitiveType::PrimKind::I32:
        case PrimitiveType::PrimKind::I64:
        {
            // 只允许转换为浮点类型或 int16
            if (targetType->getKind() != Type::Kind::Primitive)
            {
                log(*node, "the integer can only be cast to primitive type, not '" + targetType->toString() + "'.");
                break;
            }

            std::shared_ptr<PrimitiveType> tType = std::dynamic_pointer_cast<PrimitiveType>(targetType);

            if (!tType->isFloat() && !tType->isInteger())
            {
                log(*node, "the integer can only be cast to float or higer integer type, not '" + targetType->toString() + "'.");
                break;
            }

            // 检查是否是大转小
            if (tType->isInteger() && ((size_t)rType->getPrimKind() > (size_t)tType->getPrimKind()))
            {
                log(*node, "the integer can only be cast higer integer type, can not cast to '" + targetType->toString() + "'.");
            }

            break;
        }
        case PrimitiveType::PrimKind::F32:
        {
            if (targetType->getKind() != Type::Kind::Primitive || std::dynamic_pointer_cast<PrimitiveType>(targetType)->getPrimKind() != PrimitiveType::PrimKind::F64)
            {
                log(*node, "the float32 can only be cast to f64, not '" + targetType->toString() + "'.");
            }
            break;
        }
        case PrimitiveType::PrimKind::F64:
        {
            log(*node, "the float64 is not allowed to type translating.");
            break;
        }
        case PrimitiveType::PrimKind::BOOL:
        {
            if (targetType->getKind() != Type::Kind::Primitive || !std::dynamic_pointer_cast<PrimitiveType>(targetType)->isInteger())
            {
                log(*node, "the bool can only be cast to integer, not '" + targetType->toString() + "'.");
            }
            break;
        }
        case PrimitiveType::PrimKind::CHAR:
        {
            if (targetType->getKind() != Type::Kind::Primitive || !std::dynamic_pointer_cast<PrimitiveType>(targetType)->isInteger())
            {
                log(*node, "the char can only be cast to integer, not '" + targetType->toString() + "'.");
            }
            break;
        }
        case PrimitiveType::PrimKind::VOID:
        {
            log(*node, "void can not cast to any type.");
            break;
        }
        default:
            break;
        }
    }
    }
}

void SemanticAnalyzer::visit(ParenExpr *node)
{
    node->expression->accept(this);

    node->type = node->expression->type;
}

void SemanticAnalyzer::visit(BorrowExpr *node)
{
    node->expression->accept(this);

    node->type = context->typeContext->getReference(node->expression->type, node->isMutable);
}