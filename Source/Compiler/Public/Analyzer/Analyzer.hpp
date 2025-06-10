/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include <string>
#include <vector>

#include "Analyzer/SymbolTable.hpp"
#include "Core/Pass.hpp"

class Analyzer : public Pass
{
public:
    Analyzer() = default;
    Analyzer(std::shared_ptr<Context> cnt)
    {
        context = cnt;
    }

    ~Analyzer() {}

    virtual void run() override;

    // Visitor methods
    void visit(std::unique_ptr<Program> program);
    void visit(std::unique_ptr<StructDef> structDef);
    void visit(std::unique_ptr<FunctionDef> funcDef);
    void visit(std::unique_ptr<GlobalVarDef> globalVar);
    void visit(std::unique_ptr<CompoundStmt> stmt);
    void visit(std::unique_ptr<DeclStmt> decl);
    void visit(std::unique_ptr<AssignStmt> assign);
    void visit(std::unique_ptr<IfStmt> ifStmt);
    void visit(std::unique_ptr<ReturnStmt> ret);
    void visit(std::unique_ptr<WhileStmt> whileStmt);
    void visit(std::unique_ptr<ForStmt> forStmt);
    void visit(std::unique_ptr<ExprStmt> exprStmt);
    void visit(std::unique_ptr<BinaryOp> binop);
    void visit(std::unique_ptr<IdentifierExpr> ident);
    void visit(std::unique_ptr<LiteralExpr> literal);
    void visit(std::unique_ptr<FunctionCall> call);
    void visit(std::unique_ptr<MemberAccess> access);
    void visit(std::unique_ptr<StructInitExpr> init);
    void visit(std::unique_ptr<CastExpr> cast);
    void visit(std::unique_ptr<ParenExpr> paren);

private:
    SymbolTable symbolTable;
    std::vector<std::string> errors;
    std::shared_ptr<Type> currentType;
    bool inGlobalScope = true;

    bool typeCheck(std::shared_ptr<Type> expected, std::shared_ptr<Type> actual, const ASTNode &node);
    bool isAssignable(std::shared_ptr<Type> target, std::shared_ptr<Type> source, const ASTNode &node);
    std::shared_ptr<Type> getTypeFromExpr(Expr &expr);
};