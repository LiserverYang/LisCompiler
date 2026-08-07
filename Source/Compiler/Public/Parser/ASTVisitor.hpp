/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * Definations of ASTVisitor
 */

#pragma once

class Program;
class ModulePath;
class TypeNode;
class ImportStmt;
class MemberVarDef;
class StructDef;
class EnumVariant;
class EnumDef;
class TraitDef;
class Param;
class SelfParam;
class MemberFunctionDef;
class StructImpl;
class FunctionDef;
class GlobalVarDef;
class CompoundStmt;
class IfStmt;
class ReturnStmt;
class BreakStmt;
class ContinueStmt;
class DeclStmt;
class AssignStmt;
class ExprStmt;
class ForStmt;
class WhileStmt;
class Pattern;
class MatchArm;
class MatchExpr;
class LiteralExpr;
class IdentifierExpr;
class ModuleIdentifierExpr;
class StructInitExpr;
class StaticMemberCall;
class VariantInitExpr;
class MemberFunctionCall;
class FunctionCall;
class MemberAccess;
class IndexAccess;
class ArrayLiteral;
class BinaryOp;
class CastExpr;
class ParenExpr;
class BorrowExpr;
class GenericParam;

/**
 * ASTVisitor can visitor the whole AST tree
 */
class ASTVisitor
{
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Program *node) = 0;

    virtual void visit(ModulePath *node) = 0;
    virtual void visit(TypeNode *node) = 0;
    virtual void visit(ImportStmt *node) = 0;
    virtual void visit(MemberVarDef *node) = 0;
    virtual void visit(StructDef *node) = 0;
    virtual void visit(EnumVariant *node) = 0;
    virtual void visit(EnumDef *node) = 0;
    virtual void visit(Param *node) = 0;
    virtual void visit(SelfParam *node) = 0;
    virtual void visit(MemberFunctionDef *node) = 0;
    virtual void visit(StructImpl *node) = 0;
    virtual void visit(FunctionDef *node) = 0;
    virtual void visit(GlobalVarDef *node) = 0;
    virtual void visit(TraitDef *node) = 0;

    virtual void visit(CompoundStmt *node) = 0;
    virtual void visit(IfStmt *node) = 0;
    virtual void visit(ReturnStmt *node) = 0;
    virtual void visit(BreakStmt *node) = 0;
    virtual void visit(ContinueStmt *node) = 0;
    virtual void visit(DeclStmt *node) = 0;
    virtual void visit(AssignStmt *node) = 0;
    virtual void visit(ExprStmt *node) = 0;
    virtual void visit(ForStmt *node) = 0;
    virtual void visit(WhileStmt *node) = 0;
    virtual void visit(Pattern *node) = 0;
    virtual void visit(MatchArm *node) = 0;
    virtual void visit(MatchExpr *node) = 0;

    virtual void visit(LiteralExpr *node) = 0;
    virtual void visit(IdentifierExpr *node) = 0;
    virtual void visit(ModuleIdentifierExpr *node) = 0;
    virtual void visit(StructInitExpr *node) = 0;
    virtual void visit(StaticMemberCall *node) = 0;
    virtual void visit(VariantInitExpr *node) = 0;
    virtual void visit(MemberFunctionCall *node) = 0;
    virtual void visit(FunctionCall *node) = 0;
    virtual void visit(MemberAccess *node) = 0;
    virtual void visit(IndexAccess *node) = 0;
    virtual void visit(ArrayLiteral *node) = 0;
    virtual void visit(BinaryOp *node) = 0;
    virtual void visit(CastExpr *node) = 0;
    virtual void visit(ParenExpr *node) = 0;
    virtual void visit(BorrowExpr *node) = 0;
    virtual void visit(GenericParam *node) = 0;
};