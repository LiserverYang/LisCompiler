/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Analysiser/SemanticAnalyzer.hpp"

#include "Analysiser/Type.hpp"
#include "Logger/Logger.hpp"

#include <algorithm>
#include <unordered_set>

void SemanticAnalyzer::visit(Program *node)
{
    if (!node) return;

    for (auto &stmt : node->globalStatements)
        if (stmt)
            stmt->accept(this);
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

    std::vector<FunctionType::Param> params;

    for (auto &param : node->params)
    {
        if (!param) continue;

        param->accept(this);
        params.emplace_back(param->name, param->semanticType);
    }

    functionInfo.hasReturnValue = node->returnType.has_value();
    functionInfo.declaredReturnType = typeContext.getPrimitive(PrimitiveType::PrimKind::VOID);
    functionInfo.isInFunction = true;

    if (node->returnType.has_value())
    {
        node->returnType.value()->accept(this);
        functionInfo.declaredReturnType = node->returnType.value()->semanticType;

        if (auto func = SymbolTable::getInstance().lookupSymbol(node->name))
            if (func->kind == SymbolKind::Function)
                func->type = typeContext.getFunction(node->name, std::move(params), functionInfo.declaredReturnType);
    }

    node->body->accept(this);

    if (!node->returnType.has_value())
        if (auto func = SymbolTable::getInstance().lookupSymbol(node->name))
            if (func->kind == SymbolKind::Function)
                func->type = typeContext.getFunction(node->name, std::move(params), functionInfo.declaredReturnType);

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
        auto custom = typeContext.getCustom(node->typeName);

        if (!custom.has_value())
        {
            log(*node, "the type '" + node->typeName + "' can not be found.");
            node->semanticType = typeContext.getPrimitive(PrimitiveType::PrimKind::VOID);
            return;
        }

        basic_type = custom.value();
    }
    else
    {
        auto primitive = typeContext.getPrimitive(PrimitiveType::getKind(node->typeName));

        basic_type = primitive;
    }

    if (node->isReference)
    {
        node->semanticType = typeContext.getReference(basic_type, node->isMutReference);

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

    std::shared_ptr<Type> type = typeContext.getPrimitive(PrimitiveType::PrimKind::VOID);

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
        stru->type = typeContext.getCustom(node->name, std::move(fields));
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

    // 3. Return & Body
    functionInfo.isInFunction = true;
    functionInfo.declaredReturnType = typeContext.getPrimitive(PrimitiveType::PrimKind::VOID);

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

    SymbolTable::getInstance().exitScope();
}

void SemanticAnalyzer::visit(SelfParam *node)
{
    // SelfParam 只有在 MemberFunctionDef 且在 StructImpl 中处理时才有意义
    // 我们需要把 self 变量插入当前作用域
    if (!currentStructType)
    {
        log(*node, "self used outside of impl block.");
        return;
    }

    std::shared_ptr<Type> selfType = currentStructType;

    if (node->isRef)
    {
        selfType = typeContext.getReference(currentStructType, node->isMut);
    }

    node->semanticType = selfType;

    auto selfSymbol = std::make_unique<Symbol>();
    selfSymbol->kind = SymbolKind::Param; // 或者专门的 Self
    selfSymbol->name = "self";
    selfSymbol->type = selfType;
    selfSymbol->isMutable = node->isMut || !node->isRef; // 值类型 self 通常可修改
    SymbolTable::getInstance().insertSymbol("self", std::move(selfSymbol));
}

void SemanticAnalyzer::visit(StructImpl *node)
{
    std::shared_ptr<Type> structType = nullptr;

    if (auto structSymbol = SymbolTable::getInstance().lookupSymbol(node->structName))
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
        if (auto traitSymbol = SymbolTable::getInstance().lookupSymbol(node->traitName.value()))
        {
            if (traitSymbol->kind != SymbolKind::Trait)
            {
                log(*node, "the idenfiter '" + node->traitName.value() + "' is not a trait name.");
            }
        }
        else
        {
            log(*node, "can not find the struct '" + node->traitName.value() + "'.");
        }
    }

    // TODO: 实现 trait 检查

    currentStructType = structType;

    std::vector<CustomType::Method> methods;

    for (auto &method : node->methods)
    {
        method->accept(this);

        std::vector<CustomType::Field> params;

        for (auto &param : method->params)
        {
            params.emplace_back(param->name, param->semanticType);
        }

        if (method->selfParam.has_value())
        {
            params.emplace_back("self", method->selfParam.value()->semanticType);
        }

        methods.emplace_back(method->name,
            std::move(params),
            method->returnType.has_value() ? method->returnType.value()->semanticType : typeContext.getPrimitive(PrimitiveType::PrimKind::VOID),
            !method->selfParam.has_value());
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
    SymbolTable::getInstance().insertSymbol(node->name, std::move(traitSymbol));

    // 可以在这里分析 trait 内部的方法签名
}

void SemanticAnalyzer::visit(CompoundStmt *node)
{
    if (!node) return;

    // 进入新的作用域
    auto scope = SymbolTable::getInstance().getCurrentScope()->createChild();
    SymbolTable::getInstance().enterScope(scope);

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

    auto boolTy = typeContext.getPrimitive(PrimitiveType::PrimKind::BOOL);

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
}

void SemanticAnalyzer::visit(AssignStmt *node)
{
    node->target->accept(this);
    node->value->accept(this);

    if (!node->target->type->equals(node->value->type))
    {
        log(*node, "assignment type mismatch.");
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
    auto elemType = typeContext.getPrimitive(PrimitiveType::PrimKind::I32); // 占位

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
    auto boolTy = typeContext.getPrimitive(PrimitiveType::PrimKind::BOOL);

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
        node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::I32);
        break;
    case LiteralExpr::LiteralType::Float:
        node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::F64);
        break;
    case LiteralExpr::LiteralType::Bool:
        node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::BOOL);
        break;
    case LiteralExpr::LiteralType::String:
        // TODO: std.string
        // node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::);
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
        node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::VOID);
        return;
    }

    node->type = sym->type;
}

void SemanticAnalyzer::visit(StructInitExpr *node)
{
    node->structType->accept(this);
    std::shared_ptr<CustomType> structTy = std::dynamic_pointer_cast<CustomType>(node->structType->semanticType);
    node->type = structTy;

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

    // TODO: 这里简化处理，假设 function 是 IdentifierExpr 且类型是 FunctionType
    // 实际需要检查 node->function->type 是否为 FunctionType
    auto funcType = std::dynamic_pointer_cast<FunctionType>(node->function->type);

    if (!funcType)
    {
        log(*node, "trying to call a non-function.");
        node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::VOID);
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
        if (!node->arguments[i]->type->equals(funcType->getParams()[i].type))
        {
            log(*node->arguments[i], "argument type mismatch.");
        }
    }

    node->type = funcType->getReturnType();
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

    // 查找成员 (逻辑依赖于你的 Type 系统)
    node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::I32); // 占位

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
        node->type = typeContext.getPrimitive(PrimitiveType::PrimKind::BOOL);
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
    // TODO: 这里应该添加类型转换合法性检查
}

void SemanticAnalyzer::visit(ParenExpr *node)
{
    node->expression->accept(this);

    node->type = node->expression->type;
}