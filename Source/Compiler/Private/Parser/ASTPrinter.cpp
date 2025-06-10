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

// 辅助函数：获取可读的类型名
std::string demangle(const char *mangled)
{
    int status;
    char *demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (status == 0)
    {
        std::string result(demangled);
        free(demangled);
        // 移除命名空间
        size_t pos = result.find_last_of(':');
        if (pos != std::string::npos)
        {
            return result.substr(pos + 1);
        }
        return result;
    }
    return mangled;
}

// 辅助函数：格式化指针地址
std::string formatAddress(const void *addr)
{
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(12) << std::setfill('0')
       << reinterpret_cast<uintptr_t>(addr);
    return ss.str();
}

void printAST(const ASTNode *node, const std::string &prefix = "", bool isLast = true);

// 处理类型节点
void printTypeInfo(const Type *type, std::ostream &os)
{
    if (!type)
    {
        os << "<null-type>";
        return;
    }

    os << "\033[38;5;14m";

    const std::string reference = type->isReference ? (" \033[38;5;14mreference\033[0m ") : "";

    switch (type->kind)
    {
    case Type::TypeKind::Primitive:
        os << "PrimitiveType\033[0m" << reference << "\033[38;5;2m'" << type->typeName << "'";
        break;
    case Type::TypeKind::Custom:
        os << "CustomType\033[0m \033[38;5;2m'" << type->typeName << "'";
        break;
    case Type::TypeKind::ModuleQualified:
        os << "ModuleQualifiedType\033[0m \033[38;5;2m'";
        if (type->modulePath)
        {
            for (const auto &seg : type->modulePath->pathSegments)
            {
                os << seg << "::";
            }
        }
        os << type->typeName << "'\033[0m";
        break;
    }
}

// 处理各种AST节点
void printNodeInfo(const ASTNode *node, std::ostream &os)
{
    if (!node)
    {
        os << "<null-node>";
        return;
    }

    std::string nodeType = demangle(typeid(*node).name());
    std::string address = formatAddress(node);

    os << "\033[38;5;10m" << nodeType << "\033[0m" << " " << "\033[38;5;3m" << address << "\033[0m";

    // 根据节点类型添加附加信息
    if (auto p = dynamic_cast<const Program *>(node))
    {
        os << " [Program]";
    }
    else if (auto m = dynamic_cast<const ModulePath *>(node))
    {
        os << " path: \033[38;5;2m'";
        for (size_t i = 0; i < m->pathSegments.size(); ++i)
        {
            if (i > 0)
                os << "::";
            os << m->pathSegments[i];
        }
        os << "'\033[0m";
    }
    else if (auto t = dynamic_cast<const Type *>(node))
    {
        os << " ";
        printTypeInfo(t, os);
    }
    else if (auto imp = dynamic_cast<const ImportStmt *>(node))
    {
        os << " import: \033[38;5;2m'";
        if (imp->modulePath)
        {
            for (const auto &seg : imp->modulePath->pathSegments)
            {
                os << seg << "::";
            }
        }
        os << "'\033[0m";
        if (imp->symbols)
        {
            os << " symbols: \033[38;5;14m[";
            for (const auto &sym : *imp->symbols)
            {
                os << sym << ", ";
            }
            os << "]\033[0m";
        }
        if (imp->alias)
        {
            os << " alias: \033[38;5;2m" << *imp->alias << "\033[0m";
        }
    }
    else if (auto mv = dynamic_cast<const MemberVarDef *>(node))
    {
        os << " " << "\033[38;5;14m" << (mv->isPublic ? "public " : "private ") << "\033[0m"
           << "\033[38;5;2m" << mv->name << "\033[0m" << ": ";
        printTypeInfo(mv->type.get(), os);
    }
    else if (auto sd = dynamic_cast<const StructDef *>(node))
    {
        os << " struct \033[38;5;2m" << sd->name << "\033[0m";
    }
    else if (auto p = dynamic_cast<const Param *>(node))
    {
        os << " " << p->name << ": ";
        if (p->type && *p->type)
        {
            printTypeInfo(p->type->get(), os);
        }
        else
        {
            os << "<inferred>";
        }
    }
    else if (auto sp = dynamic_cast<const SelfParam *>(node))
    {
        os << " self: " << (sp->isRef ? "ref " : "")
           << (sp->isMut ? "mut " : "");
        if (sp->type && *sp->type)
        {
            printTypeInfo(sp->type->get(), os);
        }
    }
    else if (auto mf = dynamic_cast<const MemberFunctionDef *>(node))
    {
        os << " fn " << mf->name << "()";
    }
    else if (auto si = dynamic_cast<const StructImpl *>(node))
    {
        os << " impl " << si->structName;
    }
    else if (auto fd = dynamic_cast<const FunctionDef *>(node))
    {
        os << " fn " << fd->name << "()";
    }
    else if (auto gv = dynamic_cast<const GlobalVarDef *>(node))
    {
        os << " " << (gv->isMove ? "move " : "") << gv->name << ": ";
        if (gv->type && *gv->type)
        {
            printTypeInfo(gv->type->get(), os);
        }
        else
        {
            os << "<inferred>";
        }
    }
    else if (auto cs = dynamic_cast<const CompoundStmt *>(node))
    {
        os << " [CompoundStmt]";
    }
    else if (auto is = dynamic_cast<const IfStmt *>(node))
    {
        os << " [IfStmt]";
    }
    else if (auto rs = dynamic_cast<const ReturnStmt *>(node))
    {
        os << " [ReturnStmt]";
    }
    else if (auto ds = dynamic_cast<const DeclStmt *>(node))
    {
        os << " " << (ds->isMutable ? "mut " : "") << ds->name << ": ";
        if (ds->type && *ds->type)
        {
            printTypeInfo(ds->type->get(), os);
        }
        else
        {
            os << "<inferred>";
        }
    }
    else if (auto as = dynamic_cast<const AssignStmt *>(node))
    {
        os << " [AssignStmt]";
    }
    else if (auto es = dynamic_cast<const ExprStmt *>(node))
    {
        os << " [ExprStmt]";
    }
    else if (auto fs = dynamic_cast<const ForStmt *>(node))
    {
        os << " for " << fs->loopVar;
    }
    else if (auto ws = dynamic_cast<const WhileStmt *>(node))
    {
        os << " [WhileStmt]";
    }
    else if (auto lit = dynamic_cast<const LiteralExpr *>(node))
    {
        os << " literal: " << lit->value << " (";
        switch (lit->type)
        {
        case LiteralExpr::LiteralType::Int: os << "int"; break;
        case LiteralExpr::LiteralType::Float: os << "float"; break;
        case LiteralExpr::LiteralType::String: os << "string"; break;
        case LiteralExpr::LiteralType::Bool: os << "bool"; break;
        case LiteralExpr::LiteralType::Char: os << "char"; break;
        }
        os << ")";
    }
    else if (auto id = dynamic_cast<const IdentifierExpr *>(node))
    {
        os << " identifier: " << id->name;
    }
    else if (auto mid = dynamic_cast<const ModuleIdentifierExpr *>(node))
    {
        os << " module_id: ";
        if (mid->modulePath)
        {
            for (const auto &seg : mid->modulePath->pathSegments)
            {
                os << seg << "::";
            }
        }
        os << mid->name;
    }
    else if (auto si = dynamic_cast<const StructInitExpr *>(node))
    {
        os << " struct_init: ";
        printTypeInfo(si->structType.get(), os);
    }
    else if (auto smc = dynamic_cast<const StaticMemberCall *>(node))
    {
        os << " static_call: " << smc->methodName;
    }
    else if (auto mfc = dynamic_cast<const MemberFunctionCall *>(node))
    {
        os << " method_call: " << mfc->methodName;
    }
    else if (auto fc = dynamic_cast<const FunctionCall *>(node))
    {
        os << " function_call";
    }
    else if (auto ma = dynamic_cast<const MemberAccess *>(node))
    {
        os << " member_access: " << ma->memberName;
    }
    else if (auto bin = dynamic_cast<const BinaryOp *>(node))
    {
        os << " binary_op: " << bin->op;
    }
    else if (auto cast = dynamic_cast<const CastExpr *>(node))
    {
        os << " cast: ";
        printTypeInfo(cast->targetType.get(), os);
    }
    else if (auto paren = dynamic_cast<const ParenExpr *>(node))
    {
        os << " [ParenExpr]";
    }
}

// 获取节点的子节点列表
std::vector<const ASTNode *> getChildren(const ASTNode *node)
{
    std::vector<const ASTNode *> children;

    if (!node)
        return children;

    if (auto p = dynamic_cast<const Program *>(node))
    {
        for (const auto &stmt : p->globalStatements)
        {
            children.push_back(stmt.get());
        }
    }
    else if (auto t = dynamic_cast<const Type *>(node))
    {
        if (t->modulePath)
        {
            children.push_back(t->modulePath.get());
        }
    }
    else if (auto imp = dynamic_cast<const ImportStmt *>(node))
    {
        if (imp->modulePath)
        {
            children.push_back(imp->modulePath.get());
        }
    }
    else if (auto sd = dynamic_cast<const StructDef *>(node))
    {
        for (const auto &member : sd->members)
        {
            children.push_back(member.get());
        }
    }
    else if (auto mf = dynamic_cast<const MemberFunctionDef *>(node))
    {
        if (mf->selfParam && *mf->selfParam)
        {
            children.push_back(mf->selfParam->get());
        }
        for (const auto &param : mf->params)
        {
            children.push_back(param.get());
        }
        if (mf->returnType && *mf->returnType)
        {
            children.push_back(mf->returnType->get());
        }
        if (mf->body)
        {
            children.push_back(mf->body.get());
        }
    }
    else if (auto si = dynamic_cast<const StructImpl *>(node))
    {
        for (const auto &method : si->methods)
        {
            children.push_back(method.get());
        }
    }
    else if (auto fd = dynamic_cast<const FunctionDef *>(node))
    {
        for (const auto &param : fd->params)
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
    else if (auto gv = dynamic_cast<const GlobalVarDef *>(node))
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
    else if (auto cs = dynamic_cast<const CompoundStmt *>(node))
    {
        for (const auto &stmt : cs->statements)
        {
            children.push_back(stmt.get());
        }
    }
    else if (auto is = dynamic_cast<const IfStmt *>(node))
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
    else if (auto rs = dynamic_cast<const ReturnStmt *>(node))
    {
        if (rs->returnValue && *rs->returnValue)
        {
            children.push_back(rs->returnValue->get());
        }
    }
    else if (auto ds = dynamic_cast<const DeclStmt *>(node))
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
    else if (auto as = dynamic_cast<const AssignStmt *>(node))
    {
        if (as->target)
            children.push_back(as->target.get());
        if (as->value)
            children.push_back(as->value.get());
    }
    else if (auto es = dynamic_cast<const ExprStmt *>(node))
    {
        if (es->expression)
            children.push_back(es->expression.get());
    }
    else if (auto fs = dynamic_cast<const ForStmt *>(node))
    {
        if (fs->iterable)
            children.push_back(fs->iterable.get());
        if (fs->body)
            children.push_back(fs->body.get());
    }
    else if (auto ws = dynamic_cast<const WhileStmt *>(node))
    {
        if (ws->condition)
            children.push_back(ws->condition.get());
        if (ws->body)
            children.push_back(ws->body.get());
    }
    else if (auto si = dynamic_cast<const StructInitExpr *>(node))
    {
        children.push_back(si->structType.get());
        for (const auto &[name, expr] : si->memberInits)
        {
            children.push_back(expr.get());
        }
    }
    else if (auto smc = dynamic_cast<const StaticMemberCall *>(node))
    {
        children.push_back(smc->classType.get());
        for (const auto &arg : smc->arguments)
        {
            children.push_back(arg.get());
        }
    }
    else if (auto mfc = dynamic_cast<const MemberFunctionCall *>(node))
    {
        if (mfc->object)
            children.push_back(mfc->object.get());
        for (const auto &arg : mfc->arguments)
        {
            children.push_back(arg.get());
        }
    }
    else if (auto fc = dynamic_cast<const FunctionCall *>(node))
    {
        if (fc->function)
            children.push_back(fc->function.get());
        for (const auto &arg : fc->arguments)
        {
            children.push_back(arg.get());
        }
    }
    else if (auto ma = dynamic_cast<const MemberAccess *>(node))
    {
        if (ma->object)
            children.push_back(ma->object.get());
    }
    else if (auto bin = dynamic_cast<const BinaryOp *>(node))
    {
        if (bin->left)
            children.push_back(bin->left.get());
        if (bin->right)
            children.push_back(bin->right.get());
    }
    else if (auto cast = dynamic_cast<const CastExpr *>(node))
    {
        children.push_back(cast->targetType.get());
        if (cast->expression)
            children.push_back(cast->expression.get());
    }
    else if (auto paren = dynamic_cast<const ParenExpr *>(node))
    {
        if (paren->expression)
            children.push_back(paren->expression.get());
    }

    return children;
}

// 主打印函数
void printAST(const ASTNode *node, const std::string &prefix, bool isLast)
{
    if (!node)
        return;

    // 打印当前节点
    std::cout << "\033[38;5;4m" << prefix;
    std::cout << (isLast ? "`-" : "|-") << "\033[0m";

    std::stringstream info;
    printNodeInfo(node, info);
    std::cout << info.str() << std::endl;

    // 获取子节点
    auto children = getChildren(node);

    // 递归打印子节点
    std::string newPrefix = prefix + (isLast ? "  " : "| ");
    for (size_t i = 0; i < children.size(); ++i)
    {
        bool lastChild = (i == children.size() - 1);
        printAST(children[i], newPrefix, lastChild);
    }

    std::cout << "\033[0m";
}

// 入口函数
void printAST(const Program &program)
{
    printAST(&program, "", true);
}