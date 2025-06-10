/**
 * Copyrigt 2025, LiserverYang. All rights reserved.
 * 此文件定义了语法分析器的函数
 */

#pragma once

#include "Core/Pass.hpp"
#include "Logger/Logger.hpp"
#include "Parser/AST.hpp"

#include <unordered_set>

/**
 * Parser 是语法分析器，它会通过 TokenStream 生成 AST
 */
class Parser : public Pass
{
public:
    Parser() = default;
    Parser(std::shared_ptr<Context> cnt)
    {
        context = cnt;
    }

    ~Parser() {}

    virtual void run() override;

private:
    size_t currentPos = 0;
    size_t snapshot = 0;
    TokenStream *tokenStream = nullptr;

    std::unordered_set<std::string> knownTypes = {"i8", "i16", "i32", "i64", "f32", "f64", "bool", "char"};

    /* 辅助函数 */

    inline void advance()
    {
        currentPos += 1;
    }

    inline bool finished()
    {
        return currentPos >= tokenStream->size();
    }

    inline Token &currentToken()
    {
        if (finished())
        {
            Logger::LogInfo logInfo;
            initLogInfo(tokenStream->at(currentPos - 1), logInfo, "Unexpect finishing");

            Logger::Log(Logger::LogLevel::ERROR, logInfo);
        }

        return tokenStream->at(currentPos);
    }

    inline bool match(TokenCode code)
    {
        if (!finished() && currentToken().code == code)
        {
            advance();
            return true;
        }

        return false;
    }

    inline bool check(TokenCode code)
    {
        return currentToken().code == code;
    }

    inline Token &consume(TokenCode code, Logger::LogInfo &logInfo)
    {
        if (finished() || currentToken().code != code)
        {
            Logger::Log(Logger::LogLevel::ERROR, logInfo);
        }

        Token &result = currentToken();

        return advance(), result;
    }

    inline Token &consume(TokenCode code, std::string msg)
    {
        if (finished() || currentToken().code != code)
        {
            Logger::LogInfo logInfo;
            initLogInfo(currentToken(), logInfo, msg);
            Logger::Log(Logger::LogLevel::ERROR, logInfo);
        }

        Token &result = currentToken();

        return advance(), result;
    }

    inline bool isOneOf(std::initializer_list<TokenCode> tk)
    {
        for (auto it : tk)
        {
            if (it == currentToken().code)
            {
                return true;
            }
        }

        return false;
    }

    inline void initLogInfo(Token &token, Logger::LogInfo &logInfo, std::string msg)
    {
        logInfo.codePath = context->filePath;
        logInfo.code = &context->fileValue;
        logInfo.col = token.col;
        logInfo.line = token.line;
        logInfo.length = token.value.size();
        logInfo.beginPosition = token.lineStart;
        logInfo.msg = msg;
    }

    inline void createSnapshot()
    {
        snapshot = currentPos;
    }

    inline void backToSnapshot()
    {
        currentPos = snapshot;
    }

    bool isTypeStart();
    bool isLiteral();
    
    int getPrecedence(TokenCode type);
    void initLineInformation(std::unique_ptr<ASTNode> &node);

    /* 解析函数 */
    std::vector<std::unique_ptr<Param>> parseParameterList();

    std::unique_ptr<ASTNode> parseGlobalStatement();
    // std::unique_ptr<ImportStmt> parseImptStatement();
    std::unique_ptr<StructDef> parseStructDefinition();
    std::unique_ptr<StructImpl> parseStructImplementation();
    std::unique_ptr<FunctionDef> parseFunctionDefinition();
    std::unique_ptr<GlobalVarDef> parseGlobalVariableDefinition();
    // std::unique_ptr<ModulePath> parseModulePath();
    std::unique_ptr<MemberVarDef> parseMemberVariableDefinition();
    std::unique_ptr<Type> parseType();
    std::unique_ptr<MemberFunctionDef> parseMemberFunctionDefinition();
    std::unique_ptr<Param> parseParameter();
    std::unique_ptr<CompoundStmt> parseCompoundStatement();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<IfStmt> parseIfStmt();
    std::unique_ptr<ReturnStmt> parseReturnStmt();
    std::unique_ptr<DeclStmt> parseDeclarationStatement();
    std::unique_ptr<ForStmt> parseForLoop();
    std::unique_ptr<WhileStmt> parseWhileLoop();
    std::unique_ptr<Expr> parseBinaryExpression(int minPrecedence);
    std::unique_ptr<Expr> parsePrimary();
    std::vector<std::unique_ptr<Expr>> parseArgumentList();
    std::unique_ptr<ParenExpr> parseParenthesized();
    std::unique_ptr<LiteralExpr> parseLiteral();
    std::unique_ptr<CastExpr> parseCastExpression(std::unique_ptr<Type> type);
    std::unique_ptr<StructInitExpr> parseStructInitialization(const std::string &typeName);
    std::unique_ptr<Expr> parseFunctionCall(const std::string &name);
    std::unique_ptr<Expr> parseMemberAccessChain(std::unique_ptr<Expr> left);
};