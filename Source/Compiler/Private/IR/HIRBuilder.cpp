/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/HIRBuilder.hpp"

// Helper: convert an AST TypeNode pointer into an HIRRawType.
static HIRRawType toRaw(const TypeNode *n)
{
    if (!n) return {};
    HIRRawType r;
    r.name = n->typeName;
    r.isRef = n->isReference;
    r.isMutRef = n->isMutReference;
    r.isPresent = true;
    r.isPrimitive = (n->kind == TypeNode::TypeKind::Primitive);
    return r;
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(Program *node)
{
    if (!node) return;

    context->hirProgram = std::make_unique<HIRProgram>();
    context->hirProgram->position = node->position;
    context->hirProgram->length = node->length;

    for (auto &it : node->globalStatements)
    {
        it->accept(this);
        context->hirProgram->items.emplace_back((HIRNode *)nodeStack.top().release());
        nodeStack.pop();
    }
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(TypeNode *node) {} // nothing to push — callers use toRaw()

void HIRBuilder::visit(MemberVarDef *node) {}

void HIRBuilder::visit(Param *node) {} // handled inline by parent visitors

void HIRBuilder::visit(SelfParam *node) {} // handled inline

// ---------------------------------------------------------------------------
void HIRBuilder::visit(StructDef *node)
{
    auto result = std::make_unique<HIRStruct>();
    result->name = node->name;
    result->position = node->position;
    result->length = node->length;

    if (!node->genericParams.empty())
    {
        result->isGeneric = true;

        for (auto &gParam : node->genericParams)
        {
            result->gParams.push_back(std::make_shared<GenericParamType>(gParam->name));
            result->unsolveConstraints[gParam->name] = std::move(gParam->constraints);
        }
    }

    for (auto &m : node->members)
    {
        HIRStruct::Member member;
        member.name = m->name;
        member.isPublic = m->isPublic;
        member.rawType = toRaw(m->type.get());
        result->members.push_back(std::move(member));
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(TraitDef *node)
{
    auto result = std::make_unique<HIRTrait>();
    result->name = node->name;
    result->position = node->position;
    result->length = node->length;

    for (auto &method : node->methods)
    {
        method->accept(this);
        result->methods.emplace_back(
            std::unique_ptr<HIRFunction>(dynamic_cast<HIRFunction *>(nodeStack.top().release())));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(MemberFunctionDef *node)
{
    auto result = std::make_unique<HIRFunction>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isMethod = true;
    result->associatedStruct = node->structName;
    result->associatedTrait = node->traitName;

    // Self param
    if (node->selfParam.has_value())
    {
        result->hasSelf = true;
        result->selfIsRef = node->selfParam.value()->isRef;
        result->selfIsMut = node->selfParam.value()->isMut;
    }
    else
    {
        result->isStatic = true;
    }

    // Return type
    if (node->returnType.has_value())
    {
        result->hasReturnType = true;
        result->rawReturnType = toRaw(node->returnType.value().get());
    }

    if (!node->genericParams.empty())
    {
        result->isGeneric = true;

        for (auto &gParam : node->genericParams)
        {
            result->gParams.push_back(std::make_shared<GenericParamType>(gParam->name));
            result->unsolveConstraints[gParam->name] = std::move(gParam->constraints);
        }
    }

    // Params
    for (auto &param : node->params)
    {
        HIRRawType rt;
        if (param->type.has_value())
            rt = toRaw(param->type.value().get());
        result->rawParams.emplace_back(param->name, rt);
    }

    // Body
    if (node->body.has_value())
    {
        node->body.value()->accept(this);
        result->body.reset(dynamic_cast<HIRBlock *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(StructImpl *node)
{
    auto result = std::make_unique<HIRImpl>();
    result->position = node->position;
    result->length = node->length;
    result->structName = node->structName;
    result->traitName = node->traitName;

    if (!node->genericParams.empty())
    {
        for (auto &gParam : node->genericParams)
        {
            result->gParams.push_back(std::make_shared<GenericParamType>(gParam->name));
            result->unsolveConstraints[gParam->name] = std::move(gParam->constraints);
        }
    }

    for (auto &arg : node->structGenericArgs)
    {
        result->structGenericArgs.push_back(toRaw(arg.get()));
    }

    for (auto &method : node->methods)
    {
        method->accept(this);
        result->methods.emplace_back(
            std::unique_ptr<HIRFunction>(dynamic_cast<HIRFunction *>(nodeStack.top().release())));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(FunctionDef *node)
{
    auto result = std::make_unique<HIRFunction>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isMethod = false;
    result->isStatic = true;
    result->isTraitMethod = false;

    if (node->returnType.has_value())
    {
        result->hasReturnType = true;
        result->rawReturnType = toRaw(node->returnType.value().get());
    }

    if (!node->genericParams.empty())
    {
        result->isGeneric = true;

        for (auto &gParam : node->genericParams)
        {
            result->gParams.push_back(std::make_shared<GenericParamType>(gParam->name));
            result->unsolveConstraints[gParam->name] = std::move(gParam->constraints);
        }
    }

    for (auto &param : node->params)
    {
        HIRRawType rt;
        if (param->type.has_value())
            rt = toRaw(param->type.value().get());
        result->rawParams.emplace_back(param->name, rt);
    }

    if (node->body)
    {
        node->body->accept(this);
        result->body.reset(dynamic_cast<HIRBlock *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(GlobalVarDef *node)
{
    auto result = std::make_unique<HIRVarDecl>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isGlobal = true;
    result->isMutable = true;

    if (node->type.has_value())
    {
        result->hasExplicitType = true;
        result->rawType = toRaw(node->type.value().get());
    }

    if (node->initValue)
    {
        node->initValue->accept(this);
        result->init = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(CompoundStmt *node)
{
    auto result = std::make_unique<HIRBlock>();
    result->position = node->position;
    result->length = node->length;

    for (auto &stmt : node->statements)
    {
        stmt->accept(this);
        result->stmts.emplace_back((HIRStmt *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(IfStmt *node)
{
    auto result = std::make_unique<HIRIf>();
    result->position = node->position;
    result->length = node->length;

    node->condition->accept(this);
    result->cond.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    node->thenBranch->accept(this);
    result->thenBlock.reset(dynamic_cast<HIRBlock *>(nodeStack.top().release()));
    nodeStack.pop();

    if (node->elseBranch.has_value())
    {
        node->elseBranch.value()->accept(this);
        result->elseBlock = std::unique_ptr<HIRBlock>(
            dynamic_cast<HIRBlock *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(ReturnStmt *node)
{
    auto result = std::make_unique<HIRReturn>();
    result->position = node->position;
    result->length = node->length;

    if (node->returnValue.has_value())
    {
        node->returnValue.value()->accept(this);
        result->value = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(DeclStmt *node)
{
    auto result = std::make_unique<HIRVarDecl>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    result->isMutable = node->isMutable;
    result->isGlobal = false;

    if (node->type.has_value())
    {
        result->hasExplicitType = true;
        result->rawType = toRaw(node->type.value().get());
    }

    if (node->initValue.has_value())
    {
        node->initValue.value()->accept(this);
        result->init = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(AssignStmt *node)
{
    auto result = std::make_unique<HIRAssign>();
    result->position = node->position;
    result->length = node->length;

    node->target->accept(this);
    result->target.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    node->value->accept(this);
    result->value.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(ExprStmt *node)
{
    auto result = std::make_unique<HIRExprStmt>();
    result->position = node->position;
    result->length = node->length;

    if (node->expression)
    {
        node->expression->accept(this);
        result->expr.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(ForStmt *node)
{
    auto result = std::make_unique<HIRLoop>();
    result->position = node->position;
    result->length = node->length;
    result->kind = HIRLoop::Kind::For;
    // TODO: full for-loop lowering
    nodeStack.push(std::move(result));
}

void HIRBuilder::visit(WhileStmt *node)
{
    auto result = std::make_unique<HIRLoop>();
    result->position = node->position;
    result->length = node->length;
    result->kind = HIRLoop::Kind::While;

    node->condition->accept(this);
    result->cond = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
    nodeStack.pop();

    node->body->accept(this);
    result->body.reset(dynamic_cast<HIRBlock *>(nodeStack.top().release()));
    nodeStack.pop();

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(LiteralExpr *node)
{
    auto result = std::make_unique<HIRLiteral>();
    result->position = node->position;
    result->length = node->length;

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

// ---------------------------------------------------------------------------
void HIRBuilder::visit(IdentifierExpr *node)
{
    auto result = std::make_unique<HIRNameRef>();
    result->position = node->position;
    result->length = node->length;
    result->name = node->name;
    // symbol / scope / type left null — filled by HIRSemanticAnalyzer
    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(StructInitExpr *node)
{
    auto result = std::make_unique<HIRStructInit>();
    result->position = node->position;
    result->length = node->length;
    result->structName = node->structType->typeName;

    for (auto &arg : node->genericParams)
    {
        result->genericArgs.push_back(toRaw(arg.get()));
    }

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

// ---------------------------------------------------------------------------
void HIRBuilder::visit(StaticMemberCall *node)
{
    auto result = std::make_unique<HIRCall>();
    result->position = node->position;
    result->length = node->length;
    result->callKind = HIRCall::CallKind::Static;
    result->staticTypeName = node->classType->typeName;
    result->methodName = node->methodName;

    for (auto &arg : node->genericParams)
    {
        result->genericParams.push_back(toRaw(arg.get()));
    }

    for (auto &arg : node->arguments)
    {
        arg->accept(this);
        result->args.emplace_back((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(MemberFunctionCall *node)
{
    auto result = std::make_unique<HIRCall>();
    result->position = node->position;
    result->length = node->length;
    result->callKind = HIRCall::CallKind::Method;
    result->methodName = node->methodName;

    node->object->accept(this);
    result->object = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
    nodeStack.pop();

    for (auto &arg : node->genericParams)
    {
        result->genericParams.push_back(toRaw(arg.get()));
    }

    for (auto &arg : node->arguments)
    {
        arg->accept(this);
        result->args.emplace_back((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(FunctionCall *node)
{
    auto result = std::make_unique<HIRCall>();
    result->position = node->position;
    result->length = node->length;
    result->callKind = HIRCall::CallKind::Regular;

    node->function->accept(this);
    result->callee.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    for (auto &arg : node->genericParams)
    {
        result->genericParams.push_back(toRaw(arg.get()));
    }

    for (auto &arg : node->arguments)
    {
        arg->accept(this);
        result->args.emplace_back((HIRExpr *)nodeStack.top().release());
        nodeStack.pop();
    }

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(MemberAccess *node)
{
    auto result = std::make_unique<HIRMemberAccess>();
    result->position = node->position;
    result->length = node->length;
    result->memberName = node->memberName;

    node->object->accept(this);
    result->object.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(BinaryOp *node)
{
    auto result = std::make_unique<HIRBinaryOp>();
    result->position = node->position;
    result->length = node->length;

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

    node->left->accept(this);
    result->left.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    node->right->accept(this);
    result->right.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(CastExpr *node)
{
    auto result = std::make_unique<HIRCast>();
    result->position = node->position;
    result->length = node->length;
    result->rawTargetType = toRaw(node->targetType.get());

    node->expression->accept(this);
    result->expr.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(ParenExpr *node)
{
    if (node->expression)
    {
        node->expression->accept(this);
    }
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(BorrowExpr *node)
{
    auto result = std::make_unique<HIRRef>();
    result->position = node->position;
    result->length = node->length;
    result->isMutable = node->isMutable;

    node->expression->accept(this);
    result->expr.reset(dynamic_cast<HIRExpr *>(nodeStack.top().release()));
    nodeStack.pop();

    nodeStack.push(std::move(result));
}