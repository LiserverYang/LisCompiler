/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/HIRBuilder.hpp"
#include "Analysiser/SymbolTable.hpp"

void HIRBuilder::visit(Program *node)
{
    if (!node)
    {
        return;
    }

    context->hirProgram = std::make_unique<HIRProgram>();
    context->hirProgram->position = node->position;
    context->hirProgram->length = node->length;

    // here we generite all member functions

    auto &symbols = SymbolTable::getInstance().getCurrentScope()->getSymbols();

    for (auto &[name, symbol] : symbols)
    {
        if (symbol->kind == SymbolKind::Struct)
        {
            std::shared_ptr<CustomType> newTy = std::static_pointer_cast<CustomType>(symbol->type);

            for (auto &method : newTy->getMethods())
            {
                std::vector<std::shared_ptr<Type>> params;

                for (CustomType::Field param : method.params)
                {
                    params.push_back(param.type);
                }

                auto type = context->typeContext->getFunction(std::move(params), method.returnType);

                auto funcSymbol = std::make_unique<Symbol>();
                std::string funcName = name + "::" + (method.isTraitImpl ? (method.traitName + "::") : "") + method.name;

                funcSymbol->kind = SymbolKind::Function;
                funcSymbol->name = funcName;
                funcSymbol->position = node->position;
                funcSymbol->type = type;
                SymbolTable::getInstance().insertSymbol(funcName, std::move(funcSymbol));
            }
        }
    }

    for (auto &it : node->globalStatements)
    {
        it->accept(this);

        context->hirProgram->items.emplace_back((HIRExpr *)nodeStack.top().release());

        nodeStack.pop();
    }
}

void HIRBuilder::visit(TypeNode *node) {}

void HIRBuilder::visit(MemberVarDef *node) {}

void HIRBuilder::visit(StructDef *node)
{
    auto result = std::make_unique<HIRStruct>();

    for (auto &it : node->members)
    {
        result->members.emplace_back(it->name, it->type->semanticType, it->isPublic);
    }

    result->name = node->name;
    result->position = node->position;
    result->length = node->length;
    result->structSymbol = node->symbol;

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(Param *node) {}

void HIRBuilder::visit(SelfParam *node) {}

void HIRBuilder::visit(MemberFunctionDef *node)
{
    auto result = std::make_unique<HIRFunction>();

    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isMethod = true;
    result->returnType = node->declaredReturnType;

    if (node->body.has_value())
    {
        node->body.value()->accept(this);
        result->body.reset((HIRBlock *)nodeStack.top().release());
        nodeStack.pop();
    }

    result->associatedStruct = node->sturctName;
    result->associatedTrait = node->traitName;

    if (node->selfParam.has_value())
    {
        result->params.emplace_back("self", node->selfParam.value()->semanticType);
    }
    else
    {
        result->isStatic = true;
    }

    for (auto &param : node->params)
    {
        param->accept(this);
        result->params.emplace_back(param->name, param->semanticType);
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(StructImpl *node)
{
    auto result = std::make_unique<HIRImpl>();
    result->position = node->position;
    result->length = node->length;
    result->structName = node->structName;
    result->traitName = node->traitName;

    for (auto &method : node->methods)
    {
        method->accept(this);
        result->methods.emplace_back(
            std::unique_ptr<HIRFunction>(dynamic_cast<HIRFunction *>(nodeStack.top().release())));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(FunctionDef *node)
{
    auto result = std::make_unique<HIRFunction>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isMethod = false;
    result->isStatic = true;
    result->isTraitMethod = false;

    // 处理返回类型
    result->returnType = node->returnType.has_value()
                             ? node->returnType.value()->semanticType
                             : context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);

    // 处理参数
    for (auto &param : node->params)
    {
        param->accept(this);
        result->params.emplace_back(param->name, param->semanticType);
    }

    // 处理函数体
    if (node->body)
    {
        node->body->accept(this);
        result->body.reset(dynamic_cast<HIRBlock *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    result->funcSymbol = node->symbol;
    result->type = node->type;

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(GlobalVarDef *node)
{
    auto result = std::make_unique<HIRVarDecl>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isGlobal = true;
    result->isMutable = true;

    // 处理变量类型
    if (node->type.has_value())
    {
        node->type.value()->accept(this);
        result->type = node->type.value()->semanticType;
    }
    else
    {
        result->type = node->initValue->type;
    }

    // 处理初始化值
    if (node->initValue)
    {
        node->initValue->accept(this);
        result->init = std::move(std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release()));
        nodeStack.pop();
    }

    // 设置变量符号
    auto varSymbol = SymbolTable::getInstance().lookupSymbol(node->name);
    if (varSymbol && varSymbol->kind == SymbolKind::GlobalVar)
    {
        result->varSymbol = varSymbol;
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(TraitDef *node)
{
    auto result = std::make_unique<HIRTrait>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->traitSymbol = node->symbol;

    // 转换 trait 方法
    for (auto &method : node->methods)
    {
        method->accept(this);
        result->methods.emplace_back(
            std::unique_ptr<HIRFunction>(dynamic_cast<HIRFunction *>(nodeStack.top().release())));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(CompoundStmt *node)
{
    auto result = std::make_unique<HIRBlock>();
    result->position = node->position;
    result->length = node->length;
    result->scope = node->scope;

    // 转换块内语句
    for (auto &stmt : node->statements)
    {
        stmt->accept(this);
        result->stmts.emplace_back(
            std::unique_ptr<HIRStmt>((HIRStmt *)nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(IfStmt *node)
{
    auto result = std::make_unique<HIRIf>();
    result->position = node->position;
    result->length = node->length;

    // 处理条件表达式
    if (node->condition)
    {
        node->condition->accept(this);
        result->cond.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    // 处理 then 分支
    if (node->thenBranch)
    {
        node->thenBranch->accept(this);
        result->thenBlock.reset(dynamic_cast<HIRBlock *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    // 处理 else 分支
    if (node->elseBranch.has_value())
    {
        node->elseBranch.value()->accept(this);
        result->elseBlock = std::unique_ptr<HIRBlock>(
            dynamic_cast<HIRBlock *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(ReturnStmt *node)
{
    auto result = std::make_unique<HIRReturn>();
    result->position = node->position;
    result->length = node->length;

    // 处理返回值
    if (node->returnValue.has_value())
    {
        node->returnValue.value()->accept(this);
        result->value = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(DeclStmt *node)
{
    auto result = std::make_unique<HIRVarDecl>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isMutable = node->isMutable;
    result->isGlobal = false;

    // 处理变量类型
    if (node->type.has_value())
    {
        node->type.value()->accept(this);
        result->type = node->type.value()->semanticType;
    }
    else if (node->initValue.has_value())
    {
        result->type = node->initValue.value()->type;
    }

    // 处理初始化值
    if (node->initValue.has_value())
    {
        node->initValue.value()->accept(this);
        result->init = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    // 设置变量符号
    result->varSymbol = node->symbol;

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(AssignStmt *node)
{
    auto result = std::make_unique<HIRAssign>();
    result->position = node->position;
    result->length = node->length;

    // 处理赋值目标（左值）
    if (node->target)
    {
        node->target->accept(this);
        result->target.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    // 处理赋值值
    if (node->value)
    {
        node->value->accept(this);
        result->value.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(ExprStmt *node)
{
    auto result = std::make_unique<HIRExprStmt>();
    result->position = node->position;
    result->length = node->length;

    // 处理表达式
    if (node->expression)
    {
        node->expression->accept(this);
        result->expr.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(ForStmt *node)
{
    auto result = std::make_unique<HIRLoop>();
    result->position = node->position;
    result->length = node->length;
    result->kind = HIRLoop::Kind::For;

    // TODO

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(WhileStmt *node)
{
    // TODO
}

void HIRBuilder::visit(LiteralExpr *node)
{
    auto result = std::make_unique<HIRLiteral>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;

    // 转换字面量类型和值
    switch (node->kind)
    {
    case LiteralExpr::LiteralType::Int:
        result->kind = HIRLiteral::Kind::Int;
        result->value = std::stoll(node->value);
        break;
    case LiteralExpr::LiteralType::Float:
        result->kind = HIRLiteral::Kind::Float;
        result->value = std::stod(node->value);
        break;
    case LiteralExpr::LiteralType::String:
        result->kind = HIRLiteral::Kind::String;
        result->value = node->value;
        break;
    case LiteralExpr::LiteralType::Bool:
        result->kind = HIRLiteral::Kind::Bool;
        result->value = (node->value == "true");
        break;
    case LiteralExpr::LiteralType::Char:
        result->kind = HIRLiteral::Kind::Char;
        result->value = node->value.empty() ? '\0' : node->value[0];
        break;
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(IdentifierExpr *node)
{
    auto result = std::make_unique<HIRNameRef>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->type = node->type;

    // 获取符号和作用域
    result->symbol = node->symbol;
    result->scope = node->scope;

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(StructInitExpr *node)
{
    auto result = std::make_unique<HIRStructInit>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;

    // 获取结构体符号
    result->structSymbol = node->structSymbol;
    result->type = node->structSymbol->type;

    // 转换成员初始化
    for (auto &member : node->memberInits)
    {
        member.second->accept(this);
        result->members.emplace_back(
            member.first,
            std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(StaticMemberCall *node)
{
    auto result = std::make_unique<HIRCall>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;

    auto callee = std::make_unique<HIRNameRef>();
    callee->name = node->classType->typeName + "::" + node->methodName;
    callee->symbol = SymbolTable::getInstance().lookupSymbol(node->classType->typeName);
    callee->type = callee->symbol->type;
    result->callee = std::move(callee);

    // 转换参数
    for (auto &arg : node->arguments)
    {
        arg->accept(this);
        result->args.emplace_back(std::unique_ptr<HIRExpr>(static_cast<HIRExpr *>(nodeStack.top().release())));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(MemberFunctionCall *node)
{
    auto result = std::make_unique<HIRCall>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;

    // 处理调用对象
    node->object->accept(this);
    auto objectExpr = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
    nodeStack.pop();

    auto callee = std::make_unique<HIRNameRef>();
    callee->name = std::static_pointer_cast<CustomType>(objectExpr->type)->getName() + "::" + (node->method->isTraitImpl ? (node->method->traitName + "::") : "") + node->methodName;
    callee->symbol = SymbolTable::getInstance().lookupSymbol(callee->name);
    callee->type = callee->symbol->type;
    result->callee = std::move(callee);

    auto newTy = std::static_pointer_cast<FunctionType>(result->callee->type);

    if (newTy->getParams()[0]->getKind() == Type::Kind::Custom)
    {
        result->args.emplace_back(std::move(objectExpr));
    }
    else
    {
        auto refExpr = std::make_unique<HIRRef>();
        refExpr->expr = std::move(objectExpr);
        refExpr->isMutable = std::static_pointer_cast<ReferenceType>(newTy->getParams()[0])->isMutableRef();
        refExpr->type = newTy->getParams()[0];
        result->args.emplace_back(std::move(refExpr));
    }

    // 转换参数
    for (auto &arg : node->arguments)
    {
        arg->accept(this);
        result->args.emplace_back(std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(FunctionCall *node)
{
    auto result = std::make_unique<HIRCall>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;

    // 处理被调用函数
    if (node->function)
    {
        node->function->accept(this);
        result->callee.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    // 转换参数
    for (auto &arg : node->arguments)
    {
        arg->accept(this);
        result->args.emplace_back(std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(MemberAccess *node)
{
    auto result = std::make_unique<HIRMemberAccess>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;
    result->memberName = node->memberName;

    // 处理访问对象
    if (node->object)
    {
        node->object->accept(this);
        result->object.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    // 获取成员符号
    auto objType = result->object->type;
    if (auto refType = std::dynamic_pointer_cast<ReferenceType>(objType))
    {
        objType = refType->getBaseType();
    }
    if (auto customType = std::dynamic_pointer_cast<CustomType>(objType))
    {
        result->memberSymbol = SymbolTable::getInstance().lookupSymbol(
            customType->getName() + "." + node->memberName);
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(BinaryOp *node)
{
    auto result = std::make_unique<HIRBinaryOp>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;

    // 转换操作符类型
    if (node->op == "+")
        result->opKind = HIRBinaryOp::OpKind::Add;
    else if (node->op == "-")
        result->opKind = HIRBinaryOp::OpKind::Sub;
    else if (node->op == "*")
        result->opKind = HIRBinaryOp::OpKind::Mul;
    else if (node->op == "/")
        result->opKind = HIRBinaryOp::OpKind::Div;
    else if (node->op == "%")
        result->opKind = HIRBinaryOp::OpKind::Mod;
    else if (node->op == "==")
        result->opKind = HIRBinaryOp::OpKind::Eq;
    else if (node->op == "!=")
        result->opKind = HIRBinaryOp::OpKind::Ne;
    else if (node->op == "<")
        result->opKind = HIRBinaryOp::OpKind::Lt;
    else if (node->op == ">")
        result->opKind = HIRBinaryOp::OpKind::Gt;
    else if (node->op == "<=")
        result->opKind = HIRBinaryOp::OpKind::Le;
    else if (node->op == ">=")
        result->opKind = HIRBinaryOp::OpKind::Ge;
    else if (node->op == "&&")
        result->opKind = HIRBinaryOp::OpKind::And;
    else if (node->op == "||")
        result->opKind = HIRBinaryOp::OpKind::Or;
    else if (node->op == "&")
        result->opKind = HIRBinaryOp::OpKind::BitAnd;
    else if (node->op == "|")
        result->opKind = HIRBinaryOp::OpKind::BitOr;
    else if (node->op == "^")
        result->opKind = HIRBinaryOp::OpKind::BitXor;
    else if (node->op == "<<")
        result->opKind = HIRBinaryOp::OpKind::ShiftLeft;
    else if (node->op == ">>")
        result->opKind = HIRBinaryOp::OpKind::ShiftRight;

    // 处理左操作数
    if (node->left)
    {
        node->left->accept(this);
        result->left.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    // 处理右操作数
    if (node->right)
    {
        node->right->accept(this);
        result->right.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(CastExpr *node)
{
    auto result = std::make_unique<HIRCast>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;

    // 处理目标类型
    result->targetType = node->targetType->semanticType;

    // 处理被转换表达式
    if (node->expression)
    {
        node->expression->accept(this);
        result->expr.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(ParenExpr *node)
{
    if (node->expression)
    {
        node->expression->accept(this);
    }
}

void HIRBuilder::visit(BorrowExpr *node)
{
    auto result = std::make_unique<HIRRef>();
    result->position = node->position;
    result->length = node->length;
    result->type = node->type;
    result->isMutable = node->isMutable;

    node->expression->accept(this);
    result->expr.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    nodeStack.push(std::move(result));
}