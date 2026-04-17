/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include "Analysiser/SymbolTable.hpp"
#include "Analysiser/TypeContext.hpp"
#include "Core/Pass.hpp"
#include "IR/HIR.hpp"
#include "IR/HIRVisitor.hpp"
#include "Logger/Logger.hpp"

#include <unordered_set>

class HIRSemanticAnalyzer : public HIRVisitor, public Pass
{
private:
    // -----------------------------------------------------------------------
    struct FunctionInfo
    {
        std::shared_ptr<Type> declaredReturnType;
        std::unordered_map<std::string, std::shared_ptr<GenericParamType>> gParams;
        bool hasReturnValue = false;
        bool isInFunction = false;
    } functionInfo;

    std::shared_ptr<Type> currentStructType;
    std::string traitName;

    bool isInTraitMethod = false;
    bool isInStruct = false;
    std::unordered_map<std::string, std::shared_ptr<GenericParamType>> traitGParams;
    std::unordered_map<std::string, std::shared_ptr<GenericParamType>> structGParams;

    // -----------------------------------------------------------------------
    void log(HIRNode &node, const std::string &msg, size_t errorId = 1, Logger::LogLevel level = Logger::LogLevel::ERROR)
    {
        Logger::LogInfo info{};
        info.codePath = context->filePath;
        info.code = &context->fileValue;
        info.col = node.position.col;
        info.line = node.position.line;
        info.length = node.length;
        info.beginPosition = node.position.lineStart;
        info.msg = msg;
        info.errorId = errorId;
        info.exit = false;
        Logger::Log(level, info);
    }

    std::shared_ptr<Type> resolveType(const HIRRawType &raw, HIRNode &errorNode);

    // Dispatch helpers
    void analyzeExpr(HIRExpr *expr);
    void analyzeStmt(HIRStmt *stmt);

    // First pass: register top-level names so forward refs work
    void preRegister(HIRNode *item);

public:
    HIRSemanticAnalyzer() = default;
    HIRSemanticAnalyzer(std::shared_ptr<Context> cnt)
    {
        context = cnt;
        SymbolTable::getInstance().initGlobalScope();
    }

    virtual void run() override
    {
        visit(context->hirProgram.get());

        if (context->args->getArg("print_hir") == "true")
            printHIR((HIRNode *)context->hirProgram.get());

        if (context->args->getArg("print_typetable") == "true")
            context->typeContext->printTypeTable();
    }

    virtual void visit(HIRProgram *node) override;
    virtual void visit(HIRStruct *node) override;
    virtual void visit(HIRTrait *node) override;
    virtual void visit(HIRImpl *node) override;
    virtual void visit(HIRFunction *node) override;
    virtual void visit(HIRBlock *node) override;
    virtual void visit(HIRVarDecl *node) override;
    virtual void visit(HIRAssign *node) override;
    virtual void visit(HIRIf *node) override;
    virtual void visit(HIRLoop *node) override;
    virtual void visit(HIRReturn *node) override;
    virtual void visit(HIRExprStmt *node) override;
    virtual void visit(HIRNameRef *node) override;
    virtual void visit(HIRLiteral *node) override;
    virtual void visit(HIRBinaryOp *node) override;
    virtual void visit(HIRCast *node) override;
    virtual void visit(HIRCall *node) override;
    virtual void visit(HIRMemberAccess *node) override;
    virtual void visit(HIRStructInit *node) override;
    virtual void visit(HIRRef *node) override;

    std::vector<std::shared_ptr<Type>> inferGenericArguments(
        const std::vector<std::shared_ptr<Type>> &genericParams,
        const std::vector<std::shared_ptr<Type>> &paramTypes,
        const std::vector<std::unique_ptr<HIRExpr>> &args);

    void matchGenericType(
        std::shared_ptr<Type> paramTy,
        std::shared_ptr<Type> argTy,
        std::unordered_map<std::string, std::shared_ptr<Type>> &genericMap);

    std::shared_ptr<FunctionType> instantiateGenericFunction(
        std::shared_ptr<FunctionType> genericFunc,
        const std::vector<std::shared_ptr<Type>> &genericArgs);

    std::shared_ptr<Type> substituteType(
        std::shared_ptr<Type> ty,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &subst);
};