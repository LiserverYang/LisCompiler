/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/HIRBuilder.hpp"
#include "Logger/Logger.hpp"

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
    // Recursively preserve generic args (e.g. Option<i32>), otherwise a typed
    // reference like `Option<i32>` silently degrades to the un-instantiated `Option`.
    for (auto &ga : n->genericArgs)
        r.genericArgs.push_back(toRaw(ga.get()));
    return r;
}

// Convert AST GenericConstraints (with TypeNode args) to HIRGenericConstraints.
static std::vector<HIRGenericConstraint> toHIRConstraints(const std::vector<GenericConstraint> &cs)
{
    std::vector<HIRGenericConstraint> out;
    for (const auto &c : cs)
    {
        HIRGenericConstraint hc;
        hc.traitName = c.name;
        for (const auto &arg : c.args)
            hc.args.push_back(toRaw(arg.get()));
        out.push_back(std::move(hc));
    }
    return out;
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
            result->unsolveConstraints[gParam->name] = toHIRConstraints(gParam->constraints);
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

    if (!node->genericParams.empty())
    {
        result->isGeneric = true;

        for (auto &gParam : node->genericParams)
        {
            result->gParams.push_back(std::make_shared<GenericParamType>(gParam->name));
            result->unsolveConstraints[gParam->name] = toHIRConstraints(gParam->constraints);
        }
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
            result->unsolveConstraints[gParam->name] = toHIRConstraints(gParam->constraints);
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
            result->unsolveConstraints[gParam->name] = toHIRConstraints(gParam->constraints);
        }
    }

    for (auto &arg : node->traitGenericArgs)
    {
        result->traitGenericArgs.push_back(toRaw(arg.get()));
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
            result->unsolveConstraints[gParam->name] = toHIRConstraints(gParam->constraints);
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
    // Globals are mutable (the grammar has no `mut` keyword for them, but
    // assignment to a global is the only shared-mutable-state mechanism).
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
void HIRBuilder::visit(BreakStmt *node)
{
    auto result = std::make_unique<HIRBreak>();
    result->position = node->position;
    result->length = node->length;
    nodeStack.push(std::move(result));
}

// ---------------------------------------------------------------------------
void HIRBuilder::visit(ContinueStmt *node)
{
    auto result = std::make_unique<HIRContinue>();
    result->position = node->position;
    result->length = node->length;
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
// Desugar `for x in iterable { body }` into existing HIR constructs. The fetch
// happens at the TOP of the loop so that `continue` (which jumps to the loop
// header) re-fetches the next element instead of looping on a stale value:
//     {
//         let mut __it = iterable;
//         while true {
//             let __opt = __it.next();
//             if __opt.is_some {
//                 let x = __opt.value;
//                 <body>
//             } else {
//                 break;
//             }
//         }
//     }
// The iterator protocol (next() -> Option<T>) lives in the stdlib. The internal
// `break` is the same HIRBreak node user code produces.
// ---------------------------------------------------------------------------

void HIRBuilder::visit(ForStmt *node)
{
    auto position = node->position;
    auto length = node->length;

    // Unique per-loop temporaries so nested for-loops don't collide through
    // the MIRBuilder's flat varMap_ (which is not scope-aware on block exit).
    std::string itName = "__it_" + std::to_string(forLoopCtr_);
    std::string optName = "__opt_" + std::to_string(forLoopCtr_);
    forLoopCtr_++;

    auto nameRef = [&](const std::string &name) {
        auto n = std::make_unique<HIRNameRef>();
        n->position = position;
        n->length = length;
        n->name = name;
        return n;
    };

    auto callNext = [&](const std::string &receiverName) {
        auto c = std::make_unique<HIRCall>();
        c->position = position;
        c->length = length;
        c->callKind = HIRCall::CallKind::Method;
        c->object = nameRef(receiverName);
        c->methodName = "next";
        return c;
    };

    auto memberOf = [&](const std::string &objName, const std::string &field) {
        auto m = std::make_unique<HIRMemberAccess>();
        m->position = position;
        m->length = length;
        m->object = nameRef(objName);
        m->memberName = field;
        return m;
    };

    // The iterable expression, evaluated once into the iterator local.
    node->iterable->accept(this);
    auto iterExpr = std::unique_ptr<HIRExpr>((HIRExpr *)nodeStack.top().release());
    nodeStack.pop();

    // User body as a flat list of statements.
    node->body->accept(this);
    auto bodyStmt = std::unique_ptr<HIRStmt>((HIRStmt *)nodeStack.top().release());
    nodeStack.pop();

    std::vector<std::unique_ptr<HIRStmt>> bodyStmts;
    if (auto *blk = dynamic_cast<HIRBlock *>(bodyStmt.get()))
        bodyStmts = std::move(blk->stmts);
    else
        bodyStmts.push_back(std::move(bodyStmt));

    // let mut __it = <iterable>;
    auto itDecl = std::make_unique<HIRVarDecl>();
    itDecl->position = position;
    itDecl->length = length;
    itDecl->name = itName;
    itDecl->isMutable = true;
    itDecl->isGlobal = false;
    itDecl->init = std::move(iterExpr);

    // while true {
    //     let __opt = __it.next();            // fetch at the TOP — continue lands here
    //     if __opt.is_some {
    //         let <loopVar> = __opt.value;
    //         <user body>                     // user break → exit, continue → refetch
    //     } else {
    //         break;                          // internal break on exhaustion
    //     }
    // }
    auto loop = std::make_unique<HIRLoop>();
    loop->position = position;
    loop->length = length;
    loop->kind = HIRLoop::Kind::While;

    auto trueLit = std::make_unique<HIRLiteral>();
    trueLit->position = position;
    trueLit->length = length;
    trueLit->kind = HIRLiteral::Kind::Bool;
    trueLit->value = true;
    loop->cond = std::move(trueLit);

    auto loopBody = std::make_unique<HIRBlock>();
    loopBody->position = position;
    loopBody->length = length;

    // let __opt = __it.next();
    auto optDecl = std::make_unique<HIRVarDecl>();
    optDecl->position = position;
    optDecl->length = length;
    optDecl->name = optName;
    optDecl->isMutable = false;
    optDecl->isGlobal = false;
    optDecl->init = callNext(itName);
    loopBody->stmts.push_back(std::move(optDecl));

    // if __opt.is_some { ... } else { break; }
    auto ifStmt = std::make_unique<HIRIf>();
    ifStmt->position = position;
    ifStmt->length = length;
    ifStmt->cond = memberOf(optName, "is_some");

    auto thenBlock = std::make_unique<HIRBlock>();
    thenBlock->position = position;
    thenBlock->length = length;

    // let <loopVar> = __opt.value;
    auto varDecl = std::make_unique<HIRVarDecl>();
    varDecl->position = position;
    varDecl->length = length;
    varDecl->name = node->loopVar;
    varDecl->isMutable = false;
    varDecl->isGlobal = false;
    varDecl->init = memberOf(optName, "value");
    thenBlock->stmts.push_back(std::move(varDecl));

    for (auto &s : bodyStmts)
        thenBlock->stmts.push_back(std::move(s));
    ifStmt->thenBlock = std::move(thenBlock);

    auto elseBlock = std::make_unique<HIRBlock>();
    elseBlock->position = position;
    elseBlock->length = length;
    auto brk = std::make_unique<HIRBreak>();
    brk->position = position;
    brk->length = length;
    elseBlock->stmts.push_back(std::move(brk));
    ifStmt->elseBlock = std::move(elseBlock);

    loopBody->stmts.push_back(std::move(ifStmt));
    loop->body = std::move(loopBody);

    // { let mut __it; while ... }
    auto outer = std::make_unique<HIRBlock>();
    outer->position = position;
    outer->length = length;
    outer->stmts.push_back(std::move(itDecl));
    outer->stmts.push_back(std::move(loop));

    nodeStack.push(std::move(outer));
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
    else
    {
        // Unreachable through the normal parser path, but don't silently
        // produce Add for an unknown operator.
        Logger::LogInfo logInfo;
        logInfo.code = &context->fileValue;
        logInfo.codePath = context->filePath;
        logInfo.line = node->position.line;
        logInfo.col = node->position.col;
        logInfo.length = node->length;
        logInfo.beginPosition = node->position.lineStart;
        logInfo.msg = "unknown binary operator '" + node->op + "'";
        Logger::Log(Logger::LogLevel::ERROR, logInfo);

        result->opKind = HIRBinaryOp::OpKind::Add;
    }

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