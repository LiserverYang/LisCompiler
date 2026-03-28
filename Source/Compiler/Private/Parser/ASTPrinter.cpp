/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include <cxxabi.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <typeinfo>
#include <vector>

#include "Parser/AST.hpp"
#include "Parser/ASTPrinter.hpp"

namespace detail
{
std::string demangle(const char *mangled)
{
    int status;
    char *demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (status == 0)
    {
        std::string result(demangled);
        free(demangled);
        // remove namespace
        size_t pos = result.find_last_of(':');
        if (pos != std::string::npos)
        {
            return result.substr(pos + 1);
        }
        return result;
    }
    return mangled;
}

std::string formatAddress(const void *addr)
{
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(12) << std::setfill('0')
       << reinterpret_cast<uintptr_t>(addr);
    return ss.str();
}
} // namespace detail

void ASTPrinter::printCommon(ASTNode *node)
{
    std::string nodeType = detail::demangle(typeid(*node).name());
    std::string address = detail::formatAddress(node);
    os << "\033[38;5;10m" << nodeType << "\033[0m"
       << " <" << node->position.line << ":" << node->position.col << ":" << node->length << ">"
       << " "
       << "\033[38;5;3m" << address << "\033[0m";
}

void ASTPrinter::visit(Program *node)
{
    printCommon(node);
    os << " [Program]";
}

void ASTPrinter::visit(ModulePath *node)
{
    printCommon(node);
    os << " path: \033[38;5;2m'";
    for (size_t i = 0; i < node->pathSegments.size(); ++i)
    {
        if (i > 0)
            os << "::";
        os << node->pathSegments[i];
    }
    os << "'\033[0m";
}

void ASTPrinter::visit(TypeNode *node)
{
    printCommon(node);
    os << " ";
    os << "\033[38;5;14m";

    std::string reference = node->isReference ? (" \033[38;5;14mreference\033[0m ") : "";

    switch (node->kind)
    {
    case TypeNode::TypeKind::Primitive:
        os << "PrimitiveType\033[0m" << reference << "\033[38;5;2m'" << node->typeName << "'";
        break;
    case TypeNode::TypeKind::Custom:
        os << "CustomType\033[0m \033[38;5;2m'" << node->typeName << "'";
        break;
    case TypeNode::TypeKind::ModuleQualified:
        os << "ModuleQualifiedType\033[0m \033[38;5;2m'";
        if (node->modulePath)
        {
            for (auto &seg : node->modulePath->pathSegments)
            {
                os << seg << "::";
            }
        }
        os << node->typeName << "'\033[0m";
        break;
    }
}

void ASTPrinter::visit(ImportStmt *node)
{
    printCommon(node);
    os << " import: \033[38;5;2m'";
    if (node->modulePath)
    {
        for (auto &seg : node->modulePath->pathSegments)
        {
            os << seg << "::";
        }
    }
    os << "'\033[0m";
    if (node->symbols)
    {
        os << " symbols: \033[38;5;14m[";
        for (auto &sym : *node->symbols)
        {
            os << sym << ", ";
        }
        os << "]\033[0m";
    }
    if (node->alias)
    {
        os << " alias: \033[38;5;2m" << *node->alias << "\033[0m";
    }
}

void ASTPrinter::visit(MemberVarDef *node)
{
    printCommon(node);
    os << " " << "\033[38;5;14m" << (node->isPublic ? "public " : "private ") << "\033[0m"
       << "\033[38;5;2m" << node->name << "\033[0m" << ": ";
    if (node->type)
    {
        node->type->accept(this);
    }
}

void ASTPrinter::visit(StructDef *node)
{
    printCommon(node);
    os << " struct \033[38;5;2m" << node->name << "\033[0m";
}

void ASTPrinter::visit(Param *node)
{
    printCommon(node);
    os << " " << node->name << ": ";
    if (node->type && *node->type)
    {
        (*node->type)->accept(this);
    }
    else
    {
        os << "<inferred>";
    }
}

void ASTPrinter::visit(SelfParam *node)
{
    printCommon(node);
    os << " self: " << (node->isRef ? "ref " : "")
       << (node->isMut ? "mut " : "");
    if (node->type && *node->type)
    {
        (*node->type)->accept(this);
    }
}

void ASTPrinter::visit(MemberFunctionDef *node)
{
    printCommon(node);
    os << " fn " << node->name << "()";
}

void ASTPrinter::visit(StructImpl *node)
{
    printCommon(node);
    os << " impl " << node->structName;

    if (node->traitName)
    {
        os << " trait " << node->traitName.value();
    }
}

void ASTPrinter::visit(TraitDef *node)
{
    printCommon(node);
    os << " trait " << node->name;
}

void ASTPrinter::visit(FunctionDef *node)
{
    printCommon(node);
    os << " fn " << node->name << "()";
}

void ASTPrinter::visit(GlobalVarDef *node)
{
    printCommon(node);
    os << " " << (node->isMove ? "move " : "") << node->name << ": ";
    if (node->type && *node->type)
    {
        (*node->type)->accept(this);
    }
    else
    {
        os << "<inferred>";
    }
}

void ASTPrinter::visit(CompoundStmt *node)
{
    printCommon(node);
    os << " [CompoundStmt]";
}

void ASTPrinter::visit(IfStmt *node)
{
    printCommon(node);
    os << " [IfStmt]";
}

void ASTPrinter::visit(ReturnStmt *node)
{
    printCommon(node);
    os << " [ReturnStmt]";
}

void ASTPrinter::visit(DeclStmt *node)
{
    printCommon(node);
    os << " " << (node->isMutable ? "mut " : "") << node->name << ": ";
    if (node->type && *node->type)
    {
        (*node->type)->accept(this);
    }
    else
    {
        os << "<inferred>";
    }
}

void ASTPrinter::visit(AssignStmt *node)
{
    printCommon(node);
    os << " [AssignStmt]";
}

void ASTPrinter::visit(ExprStmt *node)
{
    printCommon(node);
    os << " [ExprStmt]";
}

void ASTPrinter::visit(ForStmt *node)
{
    printCommon(node);
    os << " for " << node->loopVar;
}

void ASTPrinter::visit(WhileStmt *node)
{
    printCommon(node);
    os << " [WhileStmt]";
}

void ASTPrinter::visit(LiteralExpr *node)
{
    printCommon(node);
    os << " literal: " << node->value << " (";
    switch (node->kind)
    {
    case LiteralExpr::LiteralType::Int: os << "int"; break;
    case LiteralExpr::LiteralType::Float: os << "float"; break;
    case LiteralExpr::LiteralType::String: os << "string"; break;
    case LiteralExpr::LiteralType::Bool: os << "bool"; break;
    case LiteralExpr::LiteralType::Char: os << "char"; break;
    }
    os << ")";
}

void ASTPrinter::visit(IdentifierExpr *node)
{
    printCommon(node);
    os << " identifier: " << node->name;
}

void ASTPrinter::visit(ModuleIdentifierExpr *node)
{
    printCommon(node);
    os << " module_id: ";
    if (node->modulePath)
    {
        for (auto &seg : node->modulePath->pathSegments)
        {
            os << seg << "::";
        }
    }
    os << node->name;
}

void ASTPrinter::visit(StructInitExpr *node)
{
    printCommon(node);
    os << " struct_init: ";
    if (node->structType)
    {
        node->structType->accept(this);
    }
}

void ASTPrinter::visit(StaticMemberCall *node)
{
    printCommon(node);
    os << " static_call: " << node->methodName;
}

void ASTPrinter::visit(MemberFunctionCall *node)
{
    printCommon(node);
    os << " method_call: " << node->methodName;
}

void ASTPrinter::visit(FunctionCall *node)
{
    printCommon(node);
    os << " function_call";
}

void ASTPrinter::visit(MemberAccess *node)
{
    printCommon(node);
    os << " member_access: " << node->memberName;
}

void ASTPrinter::visit(BinaryOp *node)
{
    printCommon(node);
    os << " binary_op: " << node->op;
}

void ASTPrinter::visit(CastExpr *node)
{
    printCommon(node);
    os << " cast: ";
    if (node->targetType)
    {
        node->targetType->accept(this);
    }
}

void ASTPrinter::visit(ParenExpr *node)
{
    printCommon(node);
    os << " [ParenExpr]";
}

void ASTPrinter::visit(BorrowExpr *node)
{
    printCommon(node);
    os << " borrow";

    if (node->isMutable)
    {
        os << "(mutable)";
    }
}

// 获取节点的子节点列表
std::vector<ASTNode *> getChildren(ASTNode *node)
{
    std::vector<ASTNode *> children;

    if (!node)
        return children;

    if (auto p = dynamic_cast<Program *>(node))
    {
        for (auto &stmt : p->globalStatements)
        {
            children.push_back(stmt.get());
        }
    }
    else if (auto t = dynamic_cast<TypeNode *>(node))
    {
        if (t->modulePath)
        {
            children.push_back(t->modulePath.get());
        }
    }
    else if (auto imp = dynamic_cast<ImportStmt *>(node))
    {
        if (imp->modulePath)
        {
            children.push_back(imp->modulePath.get());
        }
    }
    else if (auto sd = dynamic_cast<StructDef *>(node))
    {
        for (auto &member : sd->members)
        {
            children.push_back(member.get());
        }
    }
    else if (auto mf = dynamic_cast<MemberFunctionDef *>(node))
    {
        if (mf->selfParam && *mf->selfParam)
        {
            children.push_back(mf->selfParam->get());
        }
        for (auto &param : mf->params)
        {
            children.push_back(param.get());
        }
        if (mf->returnType && *mf->returnType)
        {
            children.push_back(mf->returnType->get());
        }
        if (mf->body && mf->body.value())
        {
            children.push_back(mf->body.value().get());
        }
    }
    else if (auto si = dynamic_cast<StructImpl *>(node))
    {
        for (auto &method : si->methods)
        {
            children.push_back(method.get());
        }
    }
    else if (auto fd = dynamic_cast<FunctionDef *>(node))
    {
        for (auto &param : fd->params)
        {
            children.push_back(param.get());
        }
        if (fd->returnType && *fd->returnType)
        {
            children.push_back(fd->returnType->get());
        }
        if (fd->body)
        {
            children.push_back(fd->body.get());
        }
    }
    else if (auto gv = dynamic_cast<GlobalVarDef *>(node))
    {
        if (gv->type && *gv->type)
        {
            children.push_back(gv->type->get());
        }
        if (gv->initValue)
        {
            children.push_back(gv->initValue.get());
        }
    }
    else if (auto cs = dynamic_cast<CompoundStmt *>(node))
    {
        for (auto &stmt : cs->statements)
        {
            children.push_back(stmt.get());
        }
    }
    else if (auto is = dynamic_cast<IfStmt *>(node))
    {
        if (is->condition)
            children.push_back(is->condition.get());
        if (is->thenBranch)
            children.push_back(is->thenBranch.get());
        if (is->elseBranch && *is->elseBranch)
        {
            children.push_back(is->elseBranch->get());
        }
    }
    else if (auto rs = dynamic_cast<ReturnStmt *>(node))
    {
        if (rs->returnValue && *rs->returnValue)
        {
            children.push_back(rs->returnValue->get());
        }
    }
    else if (auto ds = dynamic_cast<DeclStmt *>(node))
    {
        if (ds->type && *ds->type)
        {
            children.push_back(ds->type->get());
        }
        if (ds->initValue && ds->initValue.value())
        {
            children.push_back(ds->initValue.value().get());
        }
    }
    else if (auto as = dynamic_cast<AssignStmt *>(node))
    {
        if (as->target)
            children.push_back(as->target.get());
        if (as->value)
            children.push_back(as->value.get());
    }
    else if (auto es = dynamic_cast<ExprStmt *>(node))
    {
        if (es->expression)
            children.push_back(es->expression.get());
    }
    else if (auto fs = dynamic_cast<ForStmt *>(node))
    {
        if (fs->iterable)
            children.push_back(fs->iterable.get());
        if (fs->body)
            children.push_back(fs->body.get());
    }
    else if (auto ws = dynamic_cast<WhileStmt *>(node))
    {
        if (ws->condition)
            children.push_back(ws->condition.get());
        if (ws->body)
            children.push_back(ws->body.get());
    }
    else if (auto si = dynamic_cast<StructInitExpr *>(node))
    {
        children.push_back(si->structType.get());
        for (auto &[name, expr] : si->memberInits)
        {
            children.push_back(expr.get());
        }
    }
    else if (auto smc = dynamic_cast<StaticMemberCall *>(node))
    {
        children.push_back(smc->classType.get());
        for (auto &arg : smc->arguments)
        {
            children.push_back(arg.get());
        }
    }
    else if (auto mfc = dynamic_cast<MemberFunctionCall *>(node))
    {
        if (mfc->object)
            children.push_back(mfc->object.get());
        for (auto &arg : mfc->arguments)
        {
            children.push_back(arg.get());
        }
    }
    else if (auto fc = dynamic_cast<FunctionCall *>(node))
    {
        if (fc->function)
            children.push_back(fc->function.get());
        for (auto &arg : fc->arguments)
        {
            children.push_back(arg.get());
        }
    }
    else if (auto ma = dynamic_cast<MemberAccess *>(node))
    {
        if (ma->object)
            children.push_back(ma->object.get());
    }
    else if (auto bin = dynamic_cast<BinaryOp *>(node))
    {
        if (bin->left)
            children.push_back(bin->left.get());
        if (bin->right)
            children.push_back(bin->right.get());
    }
    else if (auto cast = dynamic_cast<CastExpr *>(node))
    {
        children.push_back(cast->targetType.get());
        if (cast->expression)
            children.push_back(cast->expression.get());
    }
    else if (auto paren = dynamic_cast<ParenExpr *>(node))
    {
        if (paren->expression)
            children.push_back(paren->expression.get());
    }
    else if (auto td = dynamic_cast<TraitDef *>(node))
    {
        for (auto &method : td->methods)
        {
            children.push_back(method.get());
        }
    }
    else if (auto borrow = dynamic_cast<BorrowExpr *>(node))
    {
        if (borrow->expression)
            children.push_back(borrow->expression.get());
    }

    return children;
}

void printAST(ASTNode *node, std::string prefix, bool isLast)
{
    if (!node)
        return;

    std::cout << "\033[38;5;4m" << prefix;
    std::cout << (isLast ? "`-" : "|-") << "\033[0m";

    ASTPrinter printer;
    node->accept(&printer);
    std::cout << std::endl;

    auto children = getChildren(node);

    std::string newPrefix = prefix + (isLast ? "  " : "| ");
    for (size_t i = 0; i < children.size(); ++i)
    {
        bool lastChild = (i == children.size() - 1);
        printAST(children[i], newPrefix, lastChild);
    }

    std::cout << "\033[0m";
}

void printAST(Program &program)
{
    for (auto &node : program.globalStatements)
    {
        printAST(node.get(), "", true);
    }
}