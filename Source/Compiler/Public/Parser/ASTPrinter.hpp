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
#include "Parser/ASTVisitor.hpp"

class ASTPrinter : public ASTVisitor
{
private:
    std::ostream &os;
    void printCommon(ASTNode *node);

public:
    ASTPrinter(std::ostream &os = std::cout) : os(os) {}

    void visit(Program *node) override;
    void visit(ModulePath *node) override;
    void visit(TypeNode *node) override;
    void visit(ImportStmt *node) override;
    void visit(MemberVarDef *node) override;
    void visit(StructDef *node) override;
    void visit(Param *node) override;
    void visit(SelfParam *node) override;
    void visit(MemberFunctionDef *node) override;
    void visit(StructImpl *node) override;
    void visit(FunctionDef *node) override;
    void visit(GlobalVarDef *node) override;
    void visit(CompoundStmt *node) override;
    void visit(IfStmt *node) override;
    void visit(ReturnStmt *node) override;
    void visit(BreakStmt *node) override;
    void visit(ContinueStmt *node) override;
    void visit(DeclStmt *node) override;
    void visit(AssignStmt *node) override;
    void visit(ExprStmt *node) override;
    void visit(ForStmt *node) override;
    void visit(WhileStmt *node) override;
    void visit(LiteralExpr *node) override;
    void visit(IdentifierExpr *node) override;
    void visit(ModuleIdentifierExpr *node) override;
    void visit(StructInitExpr *node) override;
    void visit(StaticMemberCall *node) override;
    void visit(MemberFunctionCall *node) override;
    void visit(FunctionCall *node) override;
    void visit(MemberAccess *node) override;
    void visit(BinaryOp *node) override;
    void visit(CastExpr *node) override;
    void visit(ParenExpr *node) override;
    void visit(TraitDef *node) override;
    void visit(BorrowExpr *node) override;
    void visit(GenericParam *node) override;
};

std::vector<ASTNode *> getChildren(ASTNode *node);

void printAST(ASTNode *node, std::string prefix, bool isLast);
void printAST(Program &program);