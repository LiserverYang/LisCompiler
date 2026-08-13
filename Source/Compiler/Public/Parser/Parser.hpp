/**
 * Copyrigt 2025, LiserverYang. All rights reserved.
 * The definations of grammer parser
 */

#pragma once

#include "Core/ModuleUtils.hpp"
#include "Core/Pass.hpp"
#include "Logger/ErrorID.hpp"
#include "Logger/Logger.hpp"
#include "Parser/AST.hpp"

#include <cstdint>
#include <unordered_map>
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

    /// Parse every global statement WITHOUT the exit(1) gate that run() applies
    /// when errors are logged. Tests exercising PARSE errors call this (run()
    /// would kill the test process via exit), then check GetErrorCount().
    void parseAll();

protected:
    size_t currentPos = 0;
    size_t snapshot = 0;
    TokenStream *tokenStream = nullptr;

    /// Sentinel returned by currentToken()/getToken() for an empty stream.
    static Token eofToken_;

    /** While parsing a `for x in <iterable>`, a bare identifier followed by `{`
     *  is ambiguous (struct literal vs loop body). In that context we require
     *  the identifier to be a known struct type for it to be a struct literal. */
    bool inForIterable_ = false;

    /** While parsing an if/while CONDITION, a bare identifier followed by `{` is
     *  the body block, not a struct literal (same disambiguation as for-loop
     *  iterables). */
    bool inControlFlowCondition_ = false;

    /** Set while parsing `#[i_know = "..."]`; applied to the next statement
     *  (marks every CastExpr in its expression tree, relaxing the
     *  integer-narrowing ERROR to a warning). */
    bool pendingIKnow_ = false;

    /** Consume a `#[...]` attribute; currently only `#[i_know]` is defined. */
    void parseAttribute();
    /** Mark every CastExpr in *expr* (and its children) with iKnow. */
    void applyIKnow(Expr *expr);

    std::unordered_set<std::string> knownTypes = {"i8", "i16", "i32", "i64", "f32", "f64", "bool", "char", "void"};
    std::unordered_set<std::string> knownTraits = {};
    // Enum type names live on the shared context (see Context::knownEnums) so a
    // parser instance sees enums registered by EARLIER files in the same unit.

    // ── module state ──────────────────────────────────────────────────────
    /// Module path of the file this Parser instance is parsing ("" = root/main).
    std::string currentModule_;
    /// Import bindings visible in the current module: alias/last-segment → canonical
    /// module path (for `alias::sym` resolution).
    std::unordered_map<std::string, std::string> moduleImports_;

    /// Set this Parser's module path (used when recursively loading a module).
    void setCurrentModule(const std::string &mod)
    {
        currentModule_ = mod;
    }

    /** Load the module at `canonical` (dot-separated path) and parse its file,
     *  recursively loading its own imports. Idempotent; detects cycles. */
    void loadModule(const std::string &canonical);

    /** Resolve a `alias::sym` — the alias must be a bound import in the current
     *  module. Returns the canonical module path, or empty if `alias` is unknown. */
    std::string resolveModuleAlias(const std::string &alias) const;

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
            if (tokenStream->empty())
                return eofToken_; // empty stream — nothing to log against
            Logger::LogInfo logInfo;
            // Guard against currentPos == 0: currentPos - 1 would wrap to SIZE_MAX.
            // A failed consume() can push currentPos PAST the end (size+1), so
            // clamp anchor to the last valid token — at(size()) throws.
            size_t anchor = currentPos > 0 ? currentPos - 1 : 0;
            if (anchor >= tokenStream->size())
                anchor = tokenStream->size() - 1;
            initLogInfo(tokenStream->at(anchor), logInfo, "Unexpeced finish", E_UnexpectFinish);
            logInfo.exit = false;

            Logger::Log(Logger::LogLevel::ERROR, logInfo);
            return tokenStream->at(anchor);
        }

        return tokenStream->at(pos);
    }

    inline Token &currentToken()
    {
        if (finished())
        {
            if (tokenStream->empty())
                return eofToken_; // empty stream — nothing to log against
            Logger::LogInfo logInfo;
            // Guard against currentPos == 0: currentPos - 1 would wrap to SIZE_MAX.
            // A failed consume() can push currentPos PAST the end (size+1), so
            // clamp anchor to the last valid token — at(size()) throws.
            size_t anchor = currentPos > 0 ? currentPos - 1 : 0;
            if (anchor >= tokenStream->size())
                anchor = tokenStream->size() - 1;
            initLogInfo(tokenStream->at(anchor), logInfo, "Unexpect finish", E_UnexpectFinish);
            logInfo.exit = false;

            Logger::Log(Logger::LogLevel::ERROR, logInfo);
            return tokenStream->at(anchor);
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
            logInfo.exit = false; // recoverable — continue past the error
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
            logInfo.exit = false; // recoverable
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
        logInfo.col = token.position.col;
        logInfo.line = token.position.line;
        logInfo.length = token.value.size();
        logInfo.beginPosition = token.position.lineStart;
        logInfo.msg = msg;
        logInfo.errorId = errorId;
    }

    /// Log a recoverable (non-fatal) parse error at `token`. Recovery skips to
    /// the next statement boundary and keeps going so all errors surface in one
    /// run; the Parser gates on the error count at the end (see run()).
    inline void logError(Token &token, const std::string &msg, size_t errorId = 1)
    {
        Logger::LogInfo logInfo;
        initLogInfo(token, logInfo, msg, errorId);
        logInfo.exit = false;
        Logger::Log(Logger::LogLevel::ERROR, logInfo);
    }

    /** Skip tokens until a safe restart point: a `;` (consumed), a `}`, or a
     *  statement/declaration-start keyword. Used after a construct fails so the
     *  next construct parses cleanly instead of cascading bogus errors. */
    void synchronize();

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
        node->position = currentToken().position;
        node->length = currentToken().value.length();
    }

    void copyNodePosition(ASTNode *dest, const ASTNode *src)
    {
        dest->position = src->position;
        dest->length = src->length;
    }

    bool isLiteral();

    int getPrecedence(TokenCode type);

    /* Parser functions */
    std::vector<std::unique_ptr<Param>> parseParameterList();
    std::unique_ptr<ASTNode> parseGlobalStatement();
    std::unique_ptr<ImportStmt> parseImptStatement();
    std::unique_ptr<ModulePath> parseModulePath();
    std::unique_ptr<StructDef> parseStructDefinition();
    std::unique_ptr<EnumDef> parseEnumDefinition();
    std::unique_ptr<EnumVariant> parseEnumVariant();
    std::unique_ptr<StructImpl> parseStructImplementation();
    std::unique_ptr<FunctionDef> parseFunctionDefinition();
    std::unique_ptr<GlobalVarDef> parseGlobalVariableDefinition();
    std::unique_ptr<MemberVarDef> parseMemberVariableDefinition();
    std::unique_ptr<TypeNode> parseType();
    std::unique_ptr<MemberFunctionDef> parseMemberFunctionDefinition();
    std::unique_ptr<Param> parseParameter();
    std::unique_ptr<CompoundStmt> parseCompoundStatement();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<IfStmt> parseIfStmt();
    std::unique_ptr<ReturnStmt> parseReturnStmt();
    std::unique_ptr<BreakStmt> parseBreakStmt();
    std::unique_ptr<ContinueStmt> parseContinueStmt();
    std::unique_ptr<DeclStmt> parseDeclarationStatement();
    std::unique_ptr<ForStmt> parseForLoop();
    std::unique_ptr<WhileStmt> parseWhileLoop();
    std::unique_ptr<MatchExpr> parseMatchExpression();
    std::unique_ptr<MatchArm> parseMatchArm();
    std::unique_ptr<Pattern> parsePattern();
    std::unique_ptr<Expr> parseBinaryExpression(int minPrecedence);
    std::unique_ptr<Expr> parsePrimary();
    std::vector<std::unique_ptr<Expr>> parseArgumentList();
    std::unique_ptr<ParenExpr> parseParenthesized();
    std::unique_ptr<LiteralExpr> parseLiteral();
    std::unique_ptr<CastExpr> parseCastExpression(std::unique_ptr<Expr> expr);
    std::unique_ptr<StructInitExpr> parseStructInitialization(Token typeName);
    std::unique_ptr<VariantInitExpr> parseVariantInitialization(Token enumName);
    std::unique_ptr<Expr> parseModuleQualified(Token aliasToken);
    std::unique_ptr<Expr> parseFunctionCall(Token name);
    std::unique_ptr<Expr> parseMemberAccessChain(std::unique_ptr<Expr> left);
    std::unique_ptr<TraitDef> parseTraitDefinition();
    std::unique_ptr<BorrowExpr> parseBorrowExpression();
    std::vector<std::unique_ptr<GenericParam>> parseGenericParams();
    std::vector<std::unique_ptr<TypeNode>> parseCallGenericParams();
    std::vector<GenericConstraint> parseGenericConstraints();

    bool looksLikeCallGenericParams();
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
            position_ = startToken.position;
        }
    }

    ~PositionRecorder()
    {
        // get the length of this node
        if (parser_->tokenStream && node_)
        {
            node_->position = position_;

            // the end token is the token before current token
            size_t endIndex = parser_->currentPos - 1;

            if (endIndex >= startIndex_ && endIndex < parser_->tokenStream->size())
            {
                const Token &startToken = parser_->tokenStream->at(startIndex_);
                const Token &endToken = parser_->tokenStream->at(endIndex);

                size_t startPos = startToken.position.lineStart + (startToken.position.col - 1);
                size_t endPos = endToken.position.lineStart + (endToken.position.col - 1) + endToken.value.length();

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
    size_t startIndex_;
    SourcePosition position_;
};