/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The definations of all kinds of AST type
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The base class of AST, all kinds of ASTNode should inheritance it
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

class Program : public ASTNode
{
public:
    std::vector<std::unique_ptr<ASTNode>> globalStatements;
};

class ModulePath : public ASTNode
{
public:
    std::vector<std::string> pathSegments;
};

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
    std::string typeName;                   // base type name or idenfiter
    std::unique_ptr<ModulePath> modulePath;
};

class ImportStmt : public ASTNode
{
public:
    std::unique_ptr<ModulePath> modulePath;
    std::optional<std::vector<std::string>> symbols; // nullopt means import all
    std::optional<std::string> alias; // nullopt mean import as global
};

class MemberVarDef : public ASTNode
{
public:
    bool isPublic;
    std::string name;
    std::unique_ptr<Type> type;
};

class StructDef : public ASTNode
{
public:
    std::string name;
    std::vector<std::unique_ptr<MemberVarDef>> members;
};

class Param : public ASTNode
{
public:
    std::string name;
    std::optional<std::unique_ptr<Type>> type;
    std::optional<std::unique_ptr<Expr>> defaultValue;
};

class SelfParam : public ASTNode
{
public:
    bool isRef;
    bool isMut;
    std::optional<std::unique_ptr<Type>> type;
};

class MemberFunctionDef : public ASTNode
{
public:
    std::string name;
    std::optional<std::unique_ptr<SelfParam>> selfParam;
    std::vector<std::unique_ptr<Param>> params;
    std::optional<std::unique_ptr<Type>> returnType;
    std::unique_ptr<Stmt> body; // CompoundStmt
};

class StructImpl : public ASTNode
{
public:
    std::string structName;
    std::vector<std::unique_ptr<MemberFunctionDef>> methods;
};

class FunctionDef : public ASTNode
{
public:
    std::string name;
    std::vector<std::unique_ptr<Param>> params;
    std::optional<std::unique_ptr<Type>> returnType;
    std::unique_ptr<Stmt> body; // CompoundStmt
};

class GlobalVarDef : public ASTNode
{
public:
    bool isMove;
    std::string name;
    std::optional<std::unique_ptr<Type>> type;
    std::unique_ptr<Expr> initValue;
};

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
    std::string op; // "+", "==", "<=" and so on
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