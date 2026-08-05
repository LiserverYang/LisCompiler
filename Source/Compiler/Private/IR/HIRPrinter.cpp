/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include <cxxabi.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <typeinfo>
#include <vector>

#include "IR/HIR.hpp"

namespace detail
{
static std::string demangle(const char *mangled)
{
    int status;
    char *demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (status == 0)
    {
        std::string result(demangled);
        free(demangled);
        size_t pos = result.find_last_of(':');
        if (pos != std::string::npos)
        {
            return result.substr(pos + 1);
        }
        return result;
    }
    return mangled;
}

static std::string formatAddress(const void *addr)
{
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(12) << std::setfill('0')
       << reinterpret_cast<uintptr_t>(addr);
    return ss.str();
}

std::string opKindToString(HIRBinaryOp::OpKind kind)
{
    switch (kind)
    {
    case HIRBinaryOp::OpKind::Add: return "+";
    case HIRBinaryOp::OpKind::Sub: return "-";
    case HIRBinaryOp::OpKind::Mul: return "*";
    case HIRBinaryOp::OpKind::Div: return "/";
    case HIRBinaryOp::OpKind::Mod: return "%";
    case HIRBinaryOp::OpKind::Eq: return "==";
    case HIRBinaryOp::OpKind::Ne: return "!=";
    case HIRBinaryOp::OpKind::Lt: return "<";
    case HIRBinaryOp::OpKind::Gt: return ">";
    case HIRBinaryOp::OpKind::Le: return "<=";
    case HIRBinaryOp::OpKind::Ge: return ">=";
    case HIRBinaryOp::OpKind::And: return "&&";
    case HIRBinaryOp::OpKind::Or: return "||";
    case HIRBinaryOp::OpKind::BitAnd: return "&";
    case HIRBinaryOp::OpKind::BitOr: return "|";
    case HIRBinaryOp::OpKind::BitXor: return "^";
    case HIRBinaryOp::OpKind::ShiftLeft: return "<<";
    case HIRBinaryOp::OpKind::ShiftRight: return ">>";
    default: return "unknown";
    }
}

std::string literalKindToString(HIRLiteral::Kind kind)
{
    switch (kind)
    {
    case HIRLiteral::Kind::Int: return "int";
    case HIRLiteral::Kind::Float: return "float";
    case HIRLiteral::Kind::String: return "string";
    case HIRLiteral::Kind::Bool: return "bool";
    case HIRLiteral::Kind::Char: return "char";
    default: return "unknown";
    }
}

// 将循环类型枚举转换为可读字符串
std::string loopKindToString(HIRLoop::Kind kind)
{
    switch (kind)
    {
    case HIRLoop::Kind::While: return "while";
    case HIRLoop::Kind::For: return "for";
    default: return "unknown";
    }
}

std::string typeToString(const std::shared_ptr<Type> &type)
{
    if (!type) return "<unknown type>";

    return type->toString();
}

// 打印 variant 类型的字面量值
std::string literalValueToString(const HIRLiteral &literal)
{
    std::stringstream ss;
    std::visit([&](auto &&val)
        {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string>) {
            ss << "\"" << val << "\"";
        } else if constexpr (std::is_same_v<T, char>) {
            ss << "'" << val << "'";
        } else {
            ss << val;
        } },
        literal.value);
    return ss.str();
}

} // namespace detail

class HIRPrinter
{
public:
    std::ostream &os;

    explicit HIRPrinter(std::ostream &os = std::cout) : os(os) {}

    void printCommon(HIRNode *node)
    {
        std::string nodeType = detail::demangle(typeid(*node).name());
        std::string address = detail::formatAddress(node);

        os << "\033[38;5;10m" << nodeType << "\033[0m";

        os << " <" << node->position.line << ":" << node->position.col
           << ":" << node->length << ">";

           os << " " << "\033[38;5;3m" << address << "\033[0m";
    }

    void visit(HIRProgram *node)
    {
        printCommon(node);
        os << " [Program] items_count: " << node->items.size();
    }

    void visit(HIRExpr *node)
    {
        printCommon(node);
        os << " [Expr] type: " << "\033[38;5;14m"
           << detail::typeToString(static_cast<HIRExpr *>(node)->type)
           << "\033[0m";
    }

    void visit(HIRNameRef *node)
    {
        printCommon(node);
        os << " name_ref: \033[38;5;2m'" << node->name << "'\033[0m";
        if (node->symbol)
        {
            os << " symbol: " << detail::formatAddress(node->symbol);
        }
    }

    void visit(HIRLiteral *node)
    {
        printCommon(node);
        os << " literal: " << "\033[38;5;2m"
           << detail::literalValueToString(*node) << "\033[0m"
           << " (" << detail::literalKindToString(node->kind) << ")";
    }

    void visit(HIRBinaryOp *node)
    {
        printCommon(node);
        os << " binary_op: \033[38;5;14m"
           << detail::opKindToString(node->opKind) << "\033[0m";
    }

    void visit(HIRCast *node)
    {
        printCommon(node);
        os << " cast to: \033[38;5;14m"
           << detail::typeToString(node->targetType) << "\033[0m";
    }

    void visit(HIRCall *node)
    {
        printCommon(node);
        os << " call: "
           << "args_count: " << node->args.size();
    }

    void visit(HIRMemberAccess *node)
    {
        printCommon(node);
        os << " member_access: \033[38;5;2m'" << node->memberName << "'\033[0m";
        if (node->memberSymbol)
        {
            os << " symbol: " << detail::formatAddress(node->memberSymbol);
        }
    }

    void visit(HIRStructInit *node)
    {
        printCommon(node);
        os << " struct_init: members_count: " << node->members.size();
        if (node->structSymbol)
        {
            os << " struct_symbol: " << detail::formatAddress(node->structSymbol);
        }
    }

    void visit(HIRStmt *node)
    {
        printCommon(node);
        os << " [Stmt]";
    }

    void visit(HIRBlock *node)
    {
        printCommon(node);
        os << " [Block] stmts_count: " << node->stmts.size()
           << " scope: " << detail::formatAddress(node->scope.get());
    }

    void visit(HIRVarDecl *node)
    {
        printCommon(node);
        os << " var_decl: \033[38;5;2m'" << node->name << "'\033[0m"
           << " type: \033[38;5;14m" << detail::typeToString(node->type) << "\033[0m"
           << (node->isMutable ? " (mutable)" : " (immutable)")
           << (node->isGlobal ? " (global)" : "");
        if (node->varSymbol)
        {
            os << " symbol: " << detail::formatAddress(node->varSymbol);
        }
    }

    void visit(HIRAssign *node)
    {
        printCommon(node);
        os << " [AssignStmt]";
    }

    void visit(HIRIf *node)
    {
        printCommon(node);
        os << " [IfStmt]";
    }

    void visit(HIRLoop *node)
    {
        printCommon(node);
        os << " [LoopStmt] kind: " << detail::loopKindToString(node->kind);
    }

    void visit(HIRReturn *node)
    {
        printCommon(node);
        os << " [ReturnStmt]" << (node->value ? "" : " (void)");
    }

    void visit(HIRBreak *node)
    {
        printCommon(node);
        os << " [BreakStmt]";
    }

    void visit(HIRContinue *node)
    {
        printCommon(node);
        os << " [ContinueStmt]";
    }

    void visit(HIRExprStmt *node)
    {
        printCommon(node);
        os << " [ExprStmt]";
    }

    void visit(HIRFunction *node)
    {
        printCommon(node);
        os << " function: \033[38;5;2m'" << node->name << "'\033[0m"
           << " params_count: " << node->params.size()
           << " return_type: \033[38;5;14m" << detail::typeToString(node->returnType) << "\033[0m"
           << (node->isMethod ? " (method)" : "")
           << (node->isStatic ? " (static)" : "");
        if (node->funcSymbol)
        {
            os << " symbol: " << detail::formatAddress(node->funcSymbol);
        }
    }

    void visit(HIRStruct *node)
    {
        printCommon(node);
        os << " struct: \033[38;5;2m'" << node->name << "'\033[0m"
           << " members_count: " << node->members.size()
           << " traits_count: " << node->implementedTraits.size();
        if (node->structSymbol)
        {
            os << " symbol: " << detail::formatAddress(node->structSymbol);
        }
    }

    void visit(HIRTrait *node)
    {
        printCommon(node);
        os << " trait: \033[38;5;2m'" << node->name << "'\033[0m"
           << " methods_count: " << node->methods.size();
        if (node->traitSymbol)
        {
            os << " symbol: " << detail::formatAddress(node->traitSymbol);
        }
    }

    void visit(HIRImpl *node)
    {
        printCommon(node);
        os << " impl: \033[38;5;2m'" << node->structName << "'\033[0m";
        if (node->traitName)
        {
            os << " for trait: \033[38;5;2m'" << *node->traitName << "'\033[0m";
        }
        os << " methods_count: " << node->methods.size();
    }

    void visit(HIRImport *node)
    {
        printCommon(node);
        os << " import: \033[38;5;2m'";
        for (size_t i = 0; i < node->path.size(); ++i)
        {
            if (i > 0) os << "::";
            os << node->path[i];
        }
        os << "'\033[0m";
        if (node->symbols)
        {
            os << " symbols: [";
            for (size_t i = 0; i < node->symbols->size(); ++i)
            {
                if (i > 0) os << ", ";
                os << (*node->symbols)[i];
            }
            os << "]";
        }
        if (node->alias)
        {
            os << " alias: \033[38;5;2m'" << *node->alias << "'\033[0m";
        }
    }

    // 通用访问入口（分派到具体的 visit 方法）
    void visit(HIRNode *node)
    {
        if (auto p = dynamic_cast<HIRProgram *>(node))
            visit(p);
        else if (auto e = dynamic_cast<HIRNameRef *>(node))
            visit(e);
        else if (auto e = dynamic_cast<HIRLiteral *>(node))
            visit(e);
        else if (auto e = dynamic_cast<HIRBinaryOp *>(node))
            visit(e);
        else if (auto e = dynamic_cast<HIRCast *>(node))
            visit(e);
        else if (auto e = dynamic_cast<HIRCall *>(node))
            visit(e);
        else if (auto e = dynamic_cast<HIRMemberAccess *>(node))
            visit(e);
        else if (auto e = dynamic_cast<HIRStructInit *>(node))
            visit(e);
        else if (auto s = dynamic_cast<HIRBlock *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRVarDecl *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRAssign *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRIf *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRLoop *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRReturn *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRBreak *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRContinue *>(node))
            visit(s);
        else if (auto s = dynamic_cast<HIRExprStmt *>(node))
            visit(s);
        else if (auto f = dynamic_cast<HIRFunction *>(node))
            visit(f);
        else if (auto s = dynamic_cast<HIRStruct *>(node))
            visit(s);
        else if (auto t = dynamic_cast<HIRTrait *>(node))
            visit(t);
        else if (auto i = dynamic_cast<HIRImpl *>(node))
            visit(i);
        else if (auto i = dynamic_cast<HIRImport *>(node))
            visit(i);
        else if (auto e = dynamic_cast<HIRExpr *>(node))
            visit(e); // 通用 Expr
        else if (auto s = dynamic_cast<HIRStmt *>(node))
            visit(s); // 通用 Stmt
        else
            printCommon(node); // 兜底
    }
};

// 获取 HIR 节点的子节点列表（递归遍历的核心）
std::vector<HIRNode *> getHIRChildren(HIRNode *node)
{
    std::vector<HIRNode *> children;
    if (!node) return children;

    // 按节点类型提取子节点
    if (auto p = dynamic_cast<HIRProgram *>(node))
    {
        for (auto &item : p->items)
        {
            children.push_back(item.get());
        }
    }
    else if (auto e = dynamic_cast<HIRBinaryOp *>(node))
    {
        if (e->left) children.push_back(e->left.get());
        if (e->right) children.push_back(e->right.get());
    }
    else if (auto e = dynamic_cast<HIRCast *>(node))
    {
        if (e->expr) children.push_back(e->expr.get());
    }
    else if (auto e = dynamic_cast<HIRCall *>(node))
    {
        if (e->callee) children.push_back(e->callee.get());
        for (auto &arg : e->args)
        {
            children.push_back(arg.get());
        }
    }
    else if (auto e = dynamic_cast<HIRMemberAccess *>(node))
    {
        if (e->object) children.push_back(e->object.get());
    }
    else if (auto e = dynamic_cast<HIRStructInit *>(node))
    {
        for (auto &[name, expr] : e->members)
        {
            children.push_back(expr.get());
        }
    }
    else if (auto s = dynamic_cast<HIRBlock *>(node))
    {
        for (auto &stmt : s->stmts)
        {
            children.push_back(stmt.get());
        }
    }
    else if (auto s = dynamic_cast<HIRVarDecl *>(node))
    {
        if (s->init) children.push_back((*s->init).get());
    }
    else if (auto s = dynamic_cast<HIRAssign *>(node))
    {
        if (s->target) children.push_back(s->target.get());
        if (s->value) children.push_back(s->value.get());
    }
    else if (auto s = dynamic_cast<HIRIf *>(node))
    {
        if (s->cond) children.push_back(s->cond.get());
        if (s->thenBlock) children.push_back(s->thenBlock.get());
        if (s->elseBlock) children.push_back((*s->elseBlock).get());
    }
    else if (auto s = dynamic_cast<HIRLoop *>(node))
    {
        if (s->cond) children.push_back((*s->cond).get());
        if (s->body) children.push_back(s->body.get());
    }
    else if (auto s = dynamic_cast<HIRReturn *>(node))
    {
        if (s->value) children.push_back((*s->value).get());
    }
    else if (auto s = dynamic_cast<HIRExprStmt *>(node))
    {
        if (s->expr) children.push_back(s->expr.get());
    }
    else if (auto f = dynamic_cast<HIRFunction *>(node))
    {
        if (f->body) children.push_back(f->body.get());
    }
    else if (auto i = dynamic_cast<HIRImpl *>(node))
    {
        for (auto &method : i->methods)
        {
            children.push_back(method.get());
        }
    }
    else if (auto t = dynamic_cast<HIRTrait *>(node))
    {
        for (auto &method : t->methods)
        {
            children.push_back(method.get());
        }
    }

    return children;
}

// 递归打印树状 HIR 结构
void printHIR(HIRNode *node, const std::string &prefix = "", bool isLast = true)
{
    if (!node) return;

    // 打印树形前缀（蓝色）
    std::cout << "\033[38;5;4m" << prefix;
    std::cout << (isLast ? "`-" : "|-") << "\033[0m";

    // 打印节点内容
    HIRPrinter printer;
    printer.visit(node);
    std::cout << std::endl;

    // 递归打印子节点
    auto children = getHIRChildren(node);
    std::string newPrefix = prefix + (isLast ? "  " : "| ");

    for (size_t i = 0; i < children.size(); ++i)
    {
        bool lastChild = (i == children.size() - 1);
        printHIR(children[i], newPrefix, lastChild);
    }

    std::cout << "\033[0m";
}

void printHIR(HIRProgram &program)
{
    printHIR(&program, "", true);
}

void printHIR(HIRNode *node)
{
    printHIR(node, "", true);
}