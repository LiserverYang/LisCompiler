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

#include "ASTVisitor.hpp"
#include "Analysiser/Type.hpp"
#include "Core/SourcePosition.hpp"

class Scope;

// The base class of AST, all kinds of ASTNode should inheritance it
class ASTNode
{
public:
    SourcePosition position;
    size_t length;
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor *visitor) = 0;
};

class Expr : public ASTNode
{
public:
    std::shared_ptr<Type> type;
};

class Stmt : public ASTNode
{
};

class TypeNode;
class FunctionDef;
class StructImpl;

/**
 * Program is the root of AST
 */
class Program : public ASTNode
{
public:
    std::vector<std::unique_ptr<ASTNode>> globalStatements;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class ModulePath : public ASTNode
{
public:
    std::vector<std::string> pathSegments;
};

class TypeNode : public ASTNode
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
    std::string typeName; // base type name or idenfiter
    std::unique_ptr<ModulePath> modulePath;
    std::shared_ptr<Type> semanticType;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class ImportStmt : public ASTNode
{
public:
    std::unique_ptr<ModulePath> modulePath;
    std::optional<std::vector<std::string>> symbols; // nullopt means import all
    std::optional<std::string> alias;                // nullopt mean import as global

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class MemberVarDef : public ASTNode
{
public:
    bool isPublic;
    std::string name;
    std::unique_ptr<TypeNode> type;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class StructDef : public ASTNode
{
public:
    std::string name;
    std::vector<std::unique_ptr<MemberVarDef>> members;
    Symbol *symbol;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class TraitDef : public ASTNode
{
public:
    std::string name;
    std::vector<std::unique_ptr<MemberFunctionDef>> methods;
    Symbol *symbol;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class Param : public ASTNode
{
public:
    std::string name;
    std::optional<std::unique_ptr<TypeNode>> type;
    std::optional<std::unique_ptr<Expr>> defaultValue;
    std::shared_ptr<Type> semanticType;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class SelfParam : public ASTNode
{
public:
    bool isRef;
    bool isMut;
    std::optional<std::unique_ptr<TypeNode>> type;
    std::shared_ptr<Type> semanticType;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class MemberFunctionDef : public ASTNode
{
public:
    std::string name;
    std::string sturctName;
    std::string traitName;
    std::optional<std::unique_ptr<SelfParam>> selfParam;
    std::vector<std::unique_ptr<Param>> params;
    std::optional<std::unique_ptr<TypeNode>> returnType;
    std::optional<std::unique_ptr<Stmt>> body; // CompoundStmt, trait can be null
    std::shared_ptr<Type> declaredReturnType;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class StructImpl : public ASTNode
{
public:
    std::string structName;
    std::vector<std::unique_ptr<MemberFunctionDef>> methods;
    std::optional<std::string> traitName;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class FunctionDef : public ASTNode
{
public:
    std::string name;
    std::vector<std::unique_ptr<Param>> params;
    std::optional<std::unique_ptr<TypeNode>> returnType;
    std::unique_ptr<Stmt> body; // CompoundStmt
    Symbol *symbol;
    std::shared_ptr<Type> type;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class GlobalVarDef : public ASTNode
{
public:
    bool isMove;
    std::string name;
    std::optional<std::unique_ptr<TypeNode>> type;
    std::unique_ptr<Expr> initValue;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class CompoundStmt : public Stmt
{
public:
    std::vector<std::unique_ptr<Stmt>> statements;
    std::shared_ptr<Scope> scope;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class IfStmt : public Stmt
{
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::optional<std::unique_ptr<Stmt>> elseBranch;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class ReturnStmt : public Stmt
{
public:
    std::optional<std::unique_ptr<Expr>> returnValue;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class DeclStmt : public Stmt
{
public:
    bool isMutable;
    std::string name;
    std::optional<std::unique_ptr<TypeNode>> type;
    std::optional<std::unique_ptr<Expr>> initValue;
    Symbol *symbol;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class AssignStmt : public Stmt
{
public:
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class ExprStmt : public Stmt
{
public:
    std::unique_ptr<Expr> expression;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class ForStmt : public Stmt
{
public:
    std::string loopVar;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<Stmt> body;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class WhileStmt : public Stmt
{
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
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

    LiteralType kind;
    std::string value;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class IdentifierExpr : public Expr
{
public:
    std::string name;
    Symbol *symbol;
    std::shared_ptr<Scope> scope;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class ModuleIdentifierExpr : public Expr
{
public:
    std::unique_ptr<ModulePath> modulePath;
    std::string name;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class StructInitExpr : public Expr
{
public:
    std::unique_ptr<TypeNode> structType;
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> memberInits;
    Symbol *structSymbol;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class StaticMemberCall : public Expr
{
public:
    std::unique_ptr<TypeNode> classType;
    std::string methodName;
    std::vector<std::unique_ptr<Expr>> arguments;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class MemberFunctionCall : public Expr
{
public:
    std::unique_ptr<Expr> object;
    std::string methodName;
    std::vector<std::unique_ptr<Expr>> arguments;
    const CustomType::Method *method;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class FunctionCall : public Expr
{
public:
    std::unique_ptr<Expr> function;
    std::vector<std::unique_ptr<Expr>> arguments;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class MemberAccess : public Expr
{
public:
    std::unique_ptr<Expr> object;
    std::string memberName;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class BinaryOp : public Expr
{
public:
    std::unique_ptr<Expr> left;
    std::string op; // "+", "==", "<=" and so on
    std::unique_ptr<Expr> right;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class CastExpr : public Expr
{
public:
    std::unique_ptr<TypeNode> targetType;
    std::unique_ptr<Expr> expression;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class ParenExpr : public Expr
{
public:
    std::unique_ptr<Expr> expression;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

class BorrowExpr : public Expr
{
public:
    std::unique_ptr<Expr> expression;
    bool isMutable;

    void accept(ASTVisitor *visitor) override
    {
        visitor->visit(this);
    }
};