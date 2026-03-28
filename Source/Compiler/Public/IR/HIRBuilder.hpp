/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * The builder from AST -> HIR.
 */

#pragma once

#include <stack>

#include "Core/Pass.hpp"
#include "Parser/AST.hpp"
#include "Parser/ASTVisitor.hpp"

#include "Core/Debugging.hpp"

class HIRBuilder : public ASTVisitor, public Pass
{
private:
    std::stack<std::unique_ptr<HIRNode>> nodeStack;

public:
    HIRBuilder() = default;
    HIRBuilder(std::shared_ptr<Context> cnt)
    {
        context = cnt;
    }

    ~HIRBuilder() {}

    virtual void run() override
    {
        context->program.accept(this);

        if (context->args->getArg("print_hir").compare("true") == 0)
        {
            printHIR((HIRNode *)context->hirProgram.get());
        }
    }

public:
    // TODO: 目前先不实现模块有关的东西

    virtual void visit(Program *node) override;
    virtual void visit(ModulePath *node) {}
    virtual void visit(TypeNode *node) override;
    virtual void visit(ImportStmt *node) {}
    virtual void visit(MemberVarDef *node) override;
    virtual void visit(StructDef *node) override;
    virtual void visit(Param *node) override;
    virtual void visit(SelfParam *node) override;
    virtual void visit(MemberFunctionDef *node) override;
    virtual void visit(StructImpl *node) override;
    virtual void visit(FunctionDef *node) override;
    virtual void visit(GlobalVarDef *node) override;
    virtual void visit(TraitDef *node) override;

    virtual void visit(CompoundStmt *node) override;
    virtual void visit(IfStmt *node) override;
    virtual void visit(ReturnStmt *node) override;
    virtual void visit(DeclStmt *node) override;
    virtual void visit(AssignStmt *node) override;
    virtual void visit(ExprStmt *node) override;
    virtual void visit(ForStmt *node) override;
    virtual void visit(WhileStmt *node) override;

    virtual void visit(LiteralExpr *node) override;
    virtual void visit(IdentifierExpr *node) override;
    virtual void visit(ModuleIdentifierExpr *node) {}
    virtual void visit(StructInitExpr *node) override;
    virtual void visit(StaticMemberCall *node);
    virtual void visit(MemberFunctionCall *node);
    virtual void visit(FunctionCall *node) override;
    virtual void visit(MemberAccess *node) override;
    virtual void visit(BinaryOp *node) override;
    virtual void visit(CastExpr *node) override;
    virtual void visit(ParenExpr *node) override;
    virtual void visit(BorrowExpr *node) override;
};