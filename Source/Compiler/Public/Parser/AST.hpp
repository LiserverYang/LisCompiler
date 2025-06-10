/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 此文件定义了所有抽象语法树的节点
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// 基类
class ASTNode
{
public:
    size_t line, col, lineStart;
    virtual ~ASTNode() = default;
};

class Expr : public ASTNode
{
};
class Stmt : public ASTNode
{
};

class Type;
class FunctionDef;
class StructImpl;

// 程序节点
class Program : public ASTNode
{
public:
    std::vector<std::unique_ptr<ASTNode>> globalStatements;
};

// 模块路径节点
class ModulePath : public ASTNode
{
public:
    std::vector<std::string> pathSegments;
};

// 类型节点
class Type : public ASTNode
{
public:
    enum class TypeKind
    {
        Primitive,
        Custom,
        ModuleQualified
    };

    bool isReference = false;
    bool isMutReference = false;
    TypeKind kind;
    std::string typeName;                   // 基础类型名或标识符
    std::unique_ptr<ModulePath> modulePath; // 仅当是模块限定类型时使用
};

// 导入语句节点
class ImportStmt : public ASTNode
{
public:
    std::unique_ptr<ModulePath> modulePath;
    std::optional<std::vector<std::string>> symbols; // nullopt 表示导入全部
    std::optional<std::string> alias; // nullopt 表示通配符导入
};

// 结构体成员变量节点
class MemberVarDef : public ASTNode
{
public:
    bool isPublic;
    std::string name;
    std::unique_ptr<Type> type;
};

// 结构体定义节点
class StructDef : public ASTNode
{
public:
    std::string name;
    std::vector<std::unique_ptr<MemberVarDef>> members;
};

// 函数参数节点
class Param : public ASTNode
{
public:
    std::string name;
    std::optional<std::unique_ptr<Type>> type;
    std::optional<std::unique_ptr<Expr>> defaultValue;
};

// Self参数节点
class SelfParam : public ASTNode
{
public:
    bool isRef;
    bool isMut;
    std::optional<std::unique_ptr<Type>> type;
};

// 成员函数定义节点
class MemberFunctionDef : public ASTNode
{
public:
    std::string name;
    std::optional<std::unique_ptr<SelfParam>> selfParam;
    std::vector<std::unique_ptr<Param>> params;
    std::optional<std::unique_ptr<Type>> returnType;
    std::unique_ptr<Stmt> body; // CompoundStmt
};

// 结构体实现节点
class StructImpl : public ASTNode
{
public:
    std::string structName;
    std::vector<std::unique_ptr<MemberFunctionDef>> methods;
};

// 函数定义节点
class FunctionDef : public ASTNode
{
public:
    std::string name;
    std::vector<std::unique_ptr<Param>> params;
    std::optional<std::unique_ptr<Type>> returnType;
    std::unique_ptr<Stmt> body; // CompoundStmt
};

// 全局变量定义节点
class GlobalVarDef : public ASTNode
{
public:
    bool isMove;
    std::string name;
    std::optional<std::unique_ptr<Type>> type;
    std::unique_ptr<Expr> initValue;
};

// === 语句节点 ===
class CompoundStmt : public Stmt
{
public:
    std::vector<std::unique_ptr<Stmt>> statements;
};

class IfStmt : public Stmt
{
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::optional<std::unique_ptr<Stmt>> elseBranch;
};

class ReturnStmt : public Stmt
{
public:
    std::optional<std::unique_ptr<Expr>> returnValue;
};

class DeclStmt : public Stmt
{
public:
    bool isMutable;
    std::string name;
    std::optional<std::unique_ptr<Type>> type;
    std::optional<std::unique_ptr<Expr>> initValue;
};

class AssignStmt : public Stmt
{
public:
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

class ExprStmt : public Stmt
{
public:
    std::unique_ptr<Expr> expression;
};

class ForStmt : public Stmt
{
public:
    std::string loopVar;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<Stmt> body;
};

class WhileStmt : public Stmt
{
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

// === 表达式节点 ===
class LiteralExpr : public Expr
{
public:
    enum class LiteralType
    {
        Int,
        Float,
        String,
        Bool,
        Char
    };

    LiteralType type;
    std::string value;
};

class IdentifierExpr : public Expr
{
public:
    std::string name;
};

class ModuleIdentifierExpr : public Expr
{
public:
    std::unique_ptr<ModulePath> modulePath;
    std::string name;
};

class StructInitExpr : public Expr
{
public:
    std::unique_ptr<Type> structType;
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> memberInits;
};

class StaticMemberCall : public Expr
{
public:
    std::unique_ptr<Type> classType;
    std::string methodName;
    std::vector<std::unique_ptr<Expr>> arguments;
};

class MemberFunctionCall : public Expr
{
public:
    std::unique_ptr<Expr> object;
    std::string methodName;
    std::vector<std::unique_ptr<Expr>> arguments;
};

class FunctionCall : public Expr
{
public:
    std::unique_ptr<Expr> function;
    std::vector<std::unique_ptr<Expr>> arguments;
};

class MemberAccess : public Expr
{
public:
    std::unique_ptr<Expr> object;
    std::string memberName;
};

class BinaryOp : public Expr
{
public:
    std::unique_ptr<Expr> left;
    std::string op; // "+", "==", "<=" 等
    std::unique_ptr<Expr> right;
};

class CastExpr : public Expr
{
public:
    std::unique_ptr<Type> targetType;
    std::unique_ptr<Expr> expression;
};

class ParenExpr : public Expr
{
public:
    std::unique_ptr<Expr> expression;
};