/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include "Core/Pass.hpp"
#include "Parser/ASTVisitor.hpp"
#include "Analysiser/SymbolTable.hpp"
#include "Analysiser/TypeContext.hpp"
#include "Logger/Logger.hpp"

class SemanticAnalyzer : public ASTVisitor, public Pass
{
private:
    TypeContext typeContext;

    struct FunctionInfo
    {
        std::shared_ptr<Type> declaredReturnType;   // 可能为空（待推断）
        bool hasReturnValue;                        // 函数定义是否显示声明返回类型
        bool isInFunction;                          // 当前是否正在分析函数
    } functionInfo;

    std::shared_ptr<Type> currentStructType;

    inline void log(ASTNode &node, std::string msg, size_t errorId = 1, Logger::LogLevel level = Logger::LogLevel::ERROR)
    {
        Logger::LogInfo logInfo{};

        logInfo.codePath = context->filePath;
        logInfo.code = &context->fileValue;
        logInfo.col = node.position.col;
        logInfo.line = node.position.line;
        logInfo.length = node.length;
        logInfo.beginPosition = node.position.lineStart;
        logInfo.msg = msg;
        logInfo.errorId = errorId;
        logInfo.exit = false;

        Logger::Log(level, logInfo);
    }

    inline void logToken(Token &node, std::string msg, size_t errorId = 1, Logger::LogLevel level = Logger::LogLevel::ERROR)
    {
        Logger::LogInfo logInfo{};

        logInfo.codePath = context->filePath;
        logInfo.code = &context->fileValue;
        logInfo.col = node.position.col;
        logInfo.line = node.position.line;
        logInfo.length = node.value.length();
        logInfo.beginPosition = node.position.lineStart;
        logInfo.msg = msg;
        logInfo.errorId = errorId;
        logInfo.exit = false;

        Logger::Log(level, logInfo);
    }

    bool isNumericType(const std::shared_ptr<Type>& type)
    {
        return type->getKind() == Type::Kind::Primitive && int(std::dynamic_pointer_cast<PrimitiveType>(type)->getPrimKind()) <= 5; 
    }

public:

    SemanticAnalyzer() = default;
    SemanticAnalyzer(std::shared_ptr<Context> cnt)
    {
        context = cnt;
        SymbolTable::getInstance().initGlobalScope();
    }

    ~SemanticAnalyzer() {}

    virtual void run() override
    {
        context->program.accept(this);
    }

public:
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
    virtual void visit(StaticMemberCall *node) {}
    virtual void visit(MemberFunctionCall *node) {}
    virtual void visit(FunctionCall *node) override;
    virtual void visit(MemberAccess *node) override;
    virtual void visit(BinaryOp *node) override;
    virtual void visit(CastExpr *node) override;
    virtual void visit(ParenExpr *node) override;
};