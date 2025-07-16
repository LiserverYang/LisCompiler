/**
 * Copyrigt 2025, LiserverYang. All rights reserved.
 * The definations of grammer parser
 */

#pragma once

#include "Core/Pass.hpp"
#include "Logger/ErrorID.hpp"
#include "Logger/Logger.hpp"
#include "Parser/AST.hpp"

#include <unordered_set>

/**
 * Parser is a grammer parser，it will generate AST from TokenStream
 */
class Parser : public Pass
{
    friend class PositionRecorder;

public:
    Parser() = default;
    Parser(std::shared_ptr<Context> cnt)
    {
        context = cnt;
    }

    ~Parser() {}

    virtual void run() override;

protected:
    size_t currentPos = 0;
    size_t snapshot = 0;
    TokenStream *tokenStream = nullptr;

    std::unordered_set<std::string> knownTypes = {"i8", "i16", "i32", "i64", "f32", "f64", "bool", "char"};

    /* Helper functions */

    inline void advance()
    {
        currentPos += 1;
    }

    inline bool finished()
    {
        return currentPos >= tokenStream->size();
    }

    inline Token &getToken(size_t pos)
    {
        if (pos >= tokenStream->size())
        {
            Logger::LogInfo logInfo;
            initLogInfo(tokenStream->at(currentPos - 1), logInfo, "Unexpect finishing", E_UnexpectFinishing);

            Logger::Log(Logger::LogLevel::ERROR, logInfo);
        }

        return tokenStream->at(pos);
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

    inline Token &consume(TokenCode code, std::string msg, size_t errorID = 1)
    {
        if (finished() || currentToken().code != code)
        {
            Logger::LogInfo logInfo;
            initLogInfo(currentToken(), logInfo, msg, errorID);
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

    inline void initLogInfo(Token &token, Logger::LogInfo &logInfo, std::string msg, size_t errorId = 1)
    {
        logInfo.codePath = context->filePath;
        logInfo.code = &context->fileValue;
        logInfo.col = token.col;
        logInfo.line = token.line;
        logInfo.length = token.value.size();
        logInfo.beginPosition = token.lineStart;
        logInfo.msg = msg;
        logInfo.errorId = errorId;
    }

    inline void createSnapshot()
    {
        snapshot = currentPos;
    }

    inline void backToSnapshot()
    {
        currentPos = snapshot;
    }

    void setNodePosition(ASTNode *node)
    {
        node->line = currentToken().line;
        node->col = currentToken().col;
        node->lineStart = currentToken().lineStart;
        node->length = currentToken().value.length();
    }

    void copyNodePosition(ASTNode *dest, const ASTNode *src)
    {
        dest->line = src->line;
        dest->col = src->col;
        dest->lineStart = src->lineStart;
        dest->length = src->length;
    }

    bool isTypeStart();
    bool isLiteral();

    int getPrecedence(TokenCode type);

    /* Parser functions */
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
    std::unique_ptr<StructInitExpr> parseStructInitialization(Token typeName);
    std::unique_ptr<Expr> parseFunctionCall(Token name);
    std::unique_ptr<Expr> parseMemberAccessChain(std::unique_ptr<Expr> left);
};

/**
 * PositionRecorder is a helper class to record positon informations for ASTNode
 *
 * The usage:
 * Create a recorder object before parse, then bind the recorder to the ASTNode.
 * Then, when the function finishied, the recorder will record informations automatically.
 */
class PositionRecorder
{
public:
    PositionRecorder(Parser *parser, ASTNode *node)
        : parser_(parser), node_(node), startIndex_(parser->currentPos)
    {
        // record the first token
        if (parser_->tokenStream && startIndex_ < parser_->tokenStream->size())
        {
            const Token &startToken = parser_->currentToken();
            line_ = startToken.line;
            col_ = startToken.col;
            lineStart_ = startToken.lineStart;
        }
    }

    ~PositionRecorder()
    {
        // get the length of this node
        if (parser_->tokenStream && node_)
        {
            node_->line = line_;
            node_->lineStart = lineStart_;
            node_->col = col_;

            // the end token is the token before current token
            size_t endIndex = parser_->currentPos - 1;

            if (endIndex >= startIndex_ && endIndex < parser_->tokenStream->size())
            {
                const Token &startToken = parser_->tokenStream->at(startIndex_);
                const Token &endToken = parser_->tokenStream->at(endIndex);

                size_t startPos = startToken.lineStart + (startToken.col - 1);
                size_t endPos = endToken.lineStart + (endToken.col - 1) + endToken.value.length();

                node_->length = endPos - startPos;
            }
        }
    }

    void bindNode(ASTNode *node)
    {
        this->node_ = node;
    }

private:
    Parser *parser_;
    ASTNode *node_;
    size_t startIndex_, line_, col_, lineStart_;
};