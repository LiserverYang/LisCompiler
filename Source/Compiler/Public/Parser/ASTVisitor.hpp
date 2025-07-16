/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * Definations of ASTVisitor
 */

#include "Parser/AST.hpp"

/**
 * ASTVisitor can visitor the whole AST tree
 */
class ASTVisitor
{
    virtual ~ASTVisitor() = default;

    virtual void visit(Program *node) = 0;

    virtual void visit(ModulePath *node) = 0;
    virtual void visit(Type *node) = 0;
    virtual void visit(ImportStmt *node) = 0;
    virtual void visit(MemberVarDef *node) = 0;
    virtual void visit(StructDef *node) = 0;
    virtual void visit(Param *node) = 0;
    virtual void visit(SelfParam *node) = 0;
    virtual void visit(MemberFunctionDef *node) = 0;
    virtual void visit(StructImpl *node) = 0;
    virtual void visit(FunctionDef *node) = 0;
    virtual void visit(GlobalVarDef *node) = 0;

    virtual void visit(CompoundStmt *node) = 0;
    virtual void visit(IfStmt *node) = 0;
    virtual void visit(ReturnStmt *node) = 0;
    virtual void visit(DeclStmt *node) = 0;
    virtual void visit(AssignStmt *node) = 0;
    virtual void visit(ExprStmt *node) = 0;
    virtual void visit(ForStmt *node) = 0;
    virtual void visit(WhileStmt *node) = 0;

    virtual void visit(LiteralExpr *node) = 0;
    virtual void visit(IdentifierExpr *node) = 0;
    virtual void visit(ModuleIdentifierExpr *node) = 0;
    virtual void visit(StructInitExpr *node) = 0;
    virtual void visit(StaticMemberCall *node) = 0;
    virtual void visit(MemberFunctionCall *node) = 0;
    virtual void visit(FunctionCall *node) = 0;
    virtual void visit(MemberAccess *node) = 0;
    virtual void visit(BinaryOp *node) = 0;
    virtual void visit(CastExpr *node) = 0;
    virtual void visit(ParenExpr *node) = 0;
};