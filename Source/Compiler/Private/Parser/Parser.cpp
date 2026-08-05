/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include "Parser/Parser.hpp"
#include "Lexer/Token.hpp"
#include "Parser/ASTPrinter.hpp"

Token Parser::eofToken_;

void Parser::synchronize()
{
    // Skip tokens until a safe restart point: a `;` (consumed), a `}`, or a
    // statement / declaration-start keyword. This prevents a failed construct
    // from cascading bogus errors into the next one.
    while (!finished())
    {
        TokenCode c = currentToken().code;
        switch (c)
        {
        case TokenCode::SEMI: advance(); return;
        case TokenCode::RBRACE: return;
        case TokenCode::LBRACE:
        case TokenCode::IF: case TokenCode::LET: case TokenCode::WHILE:
        case TokenCode::FOR: case TokenCode::RET: case TokenCode::BREAK:
        case TokenCode::CONTINUE:
        case TokenCode::FN: case TokenCode::STRUCT: case TokenCode::IMPL:
        case TokenCode::TRAIT: case TokenCode::ENUM:
            return;
        default: advance();
        }
    }
}

void Parser::run()
{
    tokenStream = &(context->tokenStream);
    auto &program = context->program;

    // Errors are non-fatal during parsing so all of them surface; the count
    // gates the pipeline at the end. The count is NOT reset here — the Lexer
    // (which runs first and resets per file) may have already logged lexing
    // errors for this file, and they must not be discarded.
    while (!finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();

        auto node = parseGlobalStatement();

        if (node)
            program.globalStatements.push_back(std::move(node));

        // Guarantee forward progress even when a construct failed without
        // consuming a token (avoids an infinite loop on malformed input).
        if (currentPos == beforePos)
            advance();

        // A construct failed — skip to the next statement/declaration boundary
        // so the next construct parses cleanly.
        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
    }

    if (Logger::GetErrorCount() > 0)
        exit(1);

    if (context->args->getArg("print_ast").compare("true") == 0)
    {
        printAST(program);
    }
}

std::unique_ptr<ASTNode> Parser::parseGlobalStatement()
{
    if (check(TokenCode::STRUCT))
    {
        return parseStructDefinition();
    }
    else if (check(TokenCode::IMPL))
    {
        return parseStructImplementation();
    }
    else if (check(TokenCode::FN))
    {
        return parseFunctionDefinition();
    }
    else if (check(TokenCode::LET))
    {
        return parseGlobalVariableDefinition();
    }
    else if (check(TokenCode::TRAIT))
    {
        return parseTraitDefinition();
    }
    else if (check(TokenCode::ENUM))
    {
        return parseEnumDefinition();
    }
    else
    {
        logError(currentToken(), "illegal global statement");
        advance(); // consume the bad token so run() makes progress
    }

    return nullptr;
}

std::unique_ptr<StructDef> Parser::parseStructDefinition()
{
    // create but not bind the node
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::STRUCT);

    auto structDef = std::make_unique<StructDef>();

    // bind the node to recorder
    recorder.bindNode(structDef.get());

    createSnapshot();
    structDef->name = consume(TokenCode::IDENTIFIER, "expect an identifier as the struct name", E_ExpectAnIdentifier).value;

    if (check(TokenCode::LT))
    {
        structDef->genericParams = std::move(parseGenericParams());
    }

    if (knownTypes.count(structDef->name) > 0)
    {
        backToSnapshot();
        consume(TokenCode::UNDEFINED, "mutidefined struct '" + structDef->name + "'", E_MutidefinedStruct);
    }

    consume(TokenCode::LBRACE, "expect a '{' after struct name", E_ExpectALBRACE);

    while (!match(TokenCode::RBRACE) && !finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();
        auto member = parseMemberVariableDefinition();
        if (member)
            structDef->members.push_back(std::move(member));
        if (currentPos == beforePos)
            advance(); // guarantee progress on a member that consumed nothing
        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
    }

    knownTypes.insert(structDef->name);
    return structDef;
}

std::unique_ptr<EnumDef> Parser::parseEnumDefinition()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::ENUM);

    auto enumDef = std::make_unique<EnumDef>();
    recorder.bindNode(enumDef.get());

    createSnapshot();
    enumDef->name = consume(TokenCode::IDENTIFIER, "expect an identifier as the enum name", E_ExpectAnIdentifier).value;

    if (check(TokenCode::LT))
        enumDef->genericParams = std::move(parseGenericParams());

    if (knownTypes.count(enumDef->name) > 0)
    {
        backToSnapshot();
        consume(TokenCode::UNDEFINED, "mutidefined enum '" + enumDef->name + "'", E_MutidefinedStruct);
    }

    consume(TokenCode::LBRACE, "expect a '{' after enum name", E_ExpectALBRACE);

    while (!match(TokenCode::RBRACE) && !finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();
        auto variant = parseEnumVariant();
        if (variant)
            enumDef->variants.push_back(std::move(variant));
        if (currentPos == beforePos)
            advance(); // guarantee progress on a variant that consumed nothing
        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
        if (match(TokenCode::COMMA)) continue; // variants are comma-separated
    }

    knownTypes.insert(enumDef->name);
    context->knownEnums.insert(enumDef->name);
    return enumDef;
}

std::unique_ptr<EnumVariant> Parser::parseEnumVariant()
{
    PositionRecorder recorder(this, nullptr);
    auto variant = std::make_unique<EnumVariant>();
    recorder.bindNode(variant.get());

    variant->name = consume(TokenCode::IDENTIFIER, "expect an identifier as the variant name", E_ExpectAnIdentifier).value;

    // Optional payload: `some(T, U)` or a unit variant `none`.
    if (match(TokenCode::LPAREN))
    {
        if (!match(TokenCode::RPAREN))
        {
            do
            {
                variant->payloadTypes.push_back(parseType());
            } while (match(TokenCode::COMMA));
            match(TokenCode::RPAREN); // consume the closing paren
        }
    }
    return variant;
}

std::unique_ptr<TraitDef> Parser::parseTraitDefinition()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::TRAIT);

    auto traitDef = std::make_unique<TraitDef>();

    recorder.bindNode(traitDef.get());

    traitDef->name = consume(TokenCode::IDENTIFIER, "expect an identifier as the trait name", E_ExpectAnIdentifier).value;

    if (knownTraits.count(traitDef->name) > 0)
    {
        consume(TokenCode::UNDEFINED, "mutidefined trait '" + traitDef->name + "'", E_MutidefinedTrait);
    }

    // Generic traits: trait Iterator<T> { ... }
    if (check(TokenCode::LT))
    {
        traitDef->genericParams = std::move(parseGenericParams());
    }

    consume(TokenCode::LBRACE, "expect a '{' after trait name", E_ExpectALBRACE);

    while (!match(TokenCode::RBRACE) && !finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();
        auto it = parseMemberFunctionDefinition();
        if (it)
        {
            it->traitName = traitDef->name;
            traitDef->methods.push_back(std::move(it));
        }
        if (currentPos == beforePos)
            advance(); // guarantee progress
        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
    }

    knownTypes.insert(traitDef->name);
    return traitDef;
}

std::unique_ptr<StructImpl> Parser::parseStructImplementation()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::IMPL);

    auto impl = std::make_unique<StructImpl>();

    if (check(TokenCode::LT))
    {
        impl->genericParams = std::move(parseGenericParams());
    }

    std::string name = consume(TokenCode::IDENTIFIER, "expect a identifer after impl", E_ExpectAnIdentifier).value;

    // Optional generic args after the first name — could be a trait
    // instantiation (impl Iterator<i32> for Range) or a plain struct impl
    // (impl Box<T>). Parse them speculatively, then decide based on `for`.
    std::vector<std::unique_ptr<TypeNode>> firstArgs;
    if (check(TokenCode::LT))
        firstArgs = parseCallGenericParams();

    if (match(TokenCode::FOR))
    {
        impl->traitName = name;
        impl->traitGenericArgs = std::move(firstArgs);
        impl->structName = consume(TokenCode::IDENTIFIER, "expect a struct name after 'for'", E_ExpectAnIdentifier).value;
    }
    else
    {
        impl->structName = name;
        impl->structGenericArgs = std::move(firstArgs);
    }

    if (check(TokenCode::LT))
        impl->structGenericArgs = parseCallGenericParams();

    recorder.bindNode(impl.get());

    if (knownTypes.count(impl->structName) == 0)
    {
        consume(TokenCode::UNDEFINED, "undefined struct '" + impl->structName + "'", E_UndefinedStruct);
    }

    consume(TokenCode::LBRACE, "expect a '{'", E_ExpectALBRACE);

    while (!check(TokenCode::RBRACE) && !finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();
        auto it = parseMemberFunctionDefinition();
        if (it)
        {
            it->structName = impl->structName;
            it->traitName = impl->traitName.has_value() ? impl->traitName.value() : "";
            impl->methods.push_back(std::move(it));
        }
        if (currentPos == beforePos)
            advance(); // guarantee progress
        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
    }

    consume(TokenCode::RBRACE, "expect a '}'", E_ExpectARBRACE);

    return impl;
}

std::unique_ptr<FunctionDef> Parser::parseFunctionDefinition()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::FN);

    auto func = std::make_unique<FunctionDef>();
    func->name = consume(TokenCode::IDENTIFIER, "expect a function name", E_ExpectAnIdentifier).value;

    recorder.bindNode(func.get());

    if (check(TokenCode::LT))
    {
        func->genericParams = parseGenericParams();
    }

    consume(TokenCode::LPAREN, "expect a '(' after function name", E_ExpectALPAREN);
    func->params = parseParameterList();
    consume(TokenCode::RPAREN, "expect a ')' after parameters", E_ExpectARPAREN);

    if (match(TokenCode::ARROW))
    {
        func->returnType = parseType();
    }

    func->body = parseCompoundStatement();
    return func;
}

std::unique_ptr<GlobalVarDef> Parser::parseGlobalVariableDefinition()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::LET);

    auto var = std::make_unique<GlobalVarDef>();
    var->isMove = match(TokenCode::MOVE);
    var->name = consume(TokenCode::IDENTIFIER, "expect variable name", E_ExpectAnIdentifier).value;

    recorder.bindNode(var.get());

    if (match(TokenCode::COLON))
    {
        var->type = parseType();
    }

    consume(TokenCode::ASSIGN, "expected '=' in variable definition", E_ExpectAnASSIGN);
    var->initValue = parseExpression();
    consume(TokenCode::SEMI, "expected ';' after variable definition", E_ExpectASEMI);

    return var;
}

std::unique_ptr<MemberVarDef> Parser::parseMemberVariableDefinition()
{
    PositionRecorder recorder(this, nullptr);

    auto member = std::make_unique<MemberVarDef>();
    member->isPublic = match(TokenCode::PUB);
    member->name = consume(TokenCode::IDENTIFIER, "expected a member name", E_ExpectAnIdentifier).value;

    recorder.bindNode(member.get());

    consume(TokenCode::COLON, "expected ':' after member name", E_ExpectACOLON);
    member->type = parseType();
    match(TokenCode::COMMA);

    return member;
}

std::unique_ptr<TypeNode> Parser::parseType()
{
    PositionRecorder recorder(this, nullptr);

    auto type = std::make_unique<TypeNode>();

    recorder.bindNode(type.get());

    if (match(TokenCode::REFERENCE))
    {
        type->isReference = true;
        type->isMutReference = match(TokenCode::MUT);
    }

    if ((size_t)currentToken().code >= TYPE_KEYWORD_BEGIN && (size_t)currentToken().code <= TYPE_KEYWORD_END)
    {
        type->kind = TypeNode::TypeKind::Primitive;
        type->typeName = currentToken().value;
        advance();
        return type;
    }

    if (check(TokenCode::IDENTIFIER))
    {
        type->kind = TypeNode::TypeKind::Custom;
        type->typeName = currentToken().value;

        advance();

        if (match(TokenCode::LT))
        {
            do
            {
                type->genericArgs.push_back(std::move(parseType()));
            } while (match(TokenCode::COMMA));

            consume(TokenCode::GT, "expect '>' after generic args", E_ExpectedKeyword);
        }

        return type;
    }

    logError(currentToken(), "expected type", E_ExpectType);
    advance(); // consume the bad token — generic-arg loops (Foo<,,,>) need progress

    return nullptr;
}

std::unique_ptr<MemberFunctionDef> Parser::parseMemberFunctionDefinition()
{
    PositionRecorder recorder(this, nullptr);

    consume(TokenCode::FN, "expect 'fn' for member function", E_ExpectedKeyword);

    auto func = std::make_unique<MemberFunctionDef>();
    func->name = consume(TokenCode::IDENTIFIER, "expected function name", E_ExpectAnIdentifier).value;

    recorder.bindNode(func.get());

    if (check(TokenCode::LT))
    {
        func->genericParams = parseGenericParams();
    }

    consume(TokenCode::LPAREN, "expected '(' after function name", E_ExpectALPAREN);

    auto selfParam = std::make_unique<SelfParam>();
    PositionRecorder selfRecorder(this, selfParam.get());

    if (match(TokenCode::SELF))
    {
        selfParam->isRef = false;
        selfParam->isMut = false;

        if (match(TokenCode::COLON))
        {
            if (match(TokenCode::REFERENCE))
            {
                selfParam->isRef = true;
                selfParam->isMut = match(TokenCode::MUT);
            }
            selfParam->type = parseType();
        }

        func->selfParam = std::move(selfParam);

        if (match(TokenCode::COMMA))
        {
            func->params = parseParameterList();
        }

        selfRecorder.~PositionRecorder();
    }
    else
    {
        selfParam.release();
        func->params = parseParameterList();
    }

    consume(TokenCode::RPAREN, "expected ')' after parameters", E_ExpectARPAREN);

    if (match(TokenCode::ARROW))
    {
        func->returnType = parseType();
    }

    if (match(TokenCode::SEMI))
    {
        return func;
    }

    func->body = parseCompoundStatement();
    return func;
}

std::vector<std::unique_ptr<Param>> Parser::parseParameterList()
{
    std::vector<std::unique_ptr<Param>> params;

    if (!check(TokenCode::RPAREN))
    {
        do
        {
            params.push_back(parseParameter());
        } while (match(TokenCode::COMMA));
    }

    return params;
}

std::unique_ptr<Param> Parser::parseParameter()
{
    PositionRecorder recorder(this, nullptr);

    auto param = std::make_unique<Param>();
    param->name = consume(TokenCode::IDENTIFIER, "expected parameter name", E_ExpectAnIdentifier).value;

    recorder.bindNode(param.get());

    consume(TokenCode::COLON, "expected ':'", E_ExpectACOLON);

    param->type = parseType();

    if (match(TokenCode::ASSIGN))
    {
        param->defaultValue = parseExpression();
    }

    return param;
}

std::unique_ptr<CompoundStmt> Parser::parseCompoundStatement()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::LBRACE);

    auto block = std::make_unique<CompoundStmt>();

    recorder.bindNode(block.get());

    while (!check(TokenCode::RBRACE) && !finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();

        auto stmt = parseStatement();

        if (stmt)
            block->statements.push_back(std::move(stmt));

        if (currentPos == beforePos)
            advance(); // guarantee progress

        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
    }

    consume(TokenCode::RBRACE, "expected '}' after compound statement", E_ExpectARBRACE);
    return block;
}

std::unique_ptr<Stmt> Parser::parseStatement()
{
    if (check(TokenCode::LBRACE))
    {
        return parseCompoundStatement();
    }
    else if (check(TokenCode::IF))
    {
        return parseIfStmt();
    }
    else if (check(TokenCode::RET))
    {
        return parseReturnStmt();
    }
    else if (check(TokenCode::LET))
    {
        return parseDeclarationStatement();
    }
    else if (check(TokenCode::FOR))
    {
        return parseForLoop();
    }
    else if (check(TokenCode::WHILE))
    {
        return parseWhileLoop();
    }
    else if (check(TokenCode::MATCH))
    {
        // A match is an expression; as a statement it needs no trailing ';'
        // (like if/while). Wrap it in an ExprStmt.
        auto expr = parseMatchExpression();
        auto stmt = std::make_unique<ExprStmt>();
        stmt->expression = std::move(expr);
        return stmt;
    }
    else if (check(TokenCode::BREAK))
    {
        return parseBreakStmt();
    }
    else if (check(TokenCode::CONTINUE))
    {
        return parseContinueStmt();
    }

    // 赋值语句或表达式语句
    auto expr = parseExpression();

    if (match(TokenCode::ASSIGN))
    {
        auto assign = std::make_unique<AssignStmt>();
        // Anchor the statement at its target expression so diagnostics (e.g.
        // "cannot assign to immutable variable") point at the right location.
        assign->position = expr->position;
        assign->length = expr->length;
        assign->target = std::move(expr);
        assign->value = parseExpression();
        consume(TokenCode::SEMI, "expected ';' after assignment", E_ExpectASEMI);
        return assign;
    }

    auto exprStmt = std::make_unique<ExprStmt>();
    exprStmt->expression = std::move(expr);
    consume(TokenCode::SEMI, "expected ';' after expression", E_ExpectASEMI);
    return exprStmt;
}

std::unique_ptr<IfStmt> Parser::parseIfStmt()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::IF);
    auto ifStmt = std::make_unique<IfStmt>();

    recorder.bindNode(ifStmt.get());

    // now if statement needn't '(' and ')'
    inControlFlowCondition_ = true;
    ifStmt->condition = parseExpression();
    inControlFlowCondition_ = false;
    ifStmt->thenBranch = parseStatement();

    if (match(TokenCode::ELSE))
    {
        ifStmt->elseBranch = parseStatement();
    }

    return ifStmt;
}

std::unique_ptr<ReturnStmt> Parser::parseReturnStmt()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::RET);
    auto returnStmt = std::make_unique<ReturnStmt>();

    recorder.bindNode(returnStmt.get());

    if (!check(TokenCode::SEMI))
    {
        returnStmt->returnValue = parseExpression();
    }

    consume(TokenCode::SEMI, "expected ';' after return", E_ExpectASEMI);
    return returnStmt;
}

std::unique_ptr<BreakStmt> Parser::parseBreakStmt()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::BREAK);
    auto stmt = std::make_unique<BreakStmt>();

    recorder.bindNode(stmt.get());

    consume(TokenCode::SEMI, "expected ';' after break", E_ExpectASEMI);
    return stmt;
}

std::unique_ptr<ContinueStmt> Parser::parseContinueStmt()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::CONTINUE);
    auto stmt = std::make_unique<ContinueStmt>();

    recorder.bindNode(stmt.get());

    consume(TokenCode::SEMI, "expected ';' after continue", E_ExpectASEMI);
    return stmt;
}

std::unique_ptr<DeclStmt> Parser::parseDeclarationStatement()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::LET);
    auto decl = std::make_unique<DeclStmt>();

    decl->isMutable = match(TokenCode::MUT);
    decl->name = consume(TokenCode::IDENTIFIER, "expected an identifier as the variable name", E_ExpectAnIdentifier).value;

    recorder.bindNode(decl.get());

    if (match(TokenCode::COLON))
    {
        decl->type = parseType();
    }

    if (match(TokenCode::ASSIGN))
    {
        decl->initValue = parseExpression();
    }

    consume(TokenCode::SEMI, "expected ';' after let statement", E_ExpectASEMI);
    return decl;
}

std::unique_ptr<ForStmt> Parser::parseForLoop()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::FOR);
    auto forStmt = std::make_unique<ForStmt>();

    recorder.bindNode(forStmt.get());

    forStmt->loopVar = consume(TokenCode::IDENTIFIER, "expected an identifier as the loop variable", E_ExpectAnIdentifier).value;
    consume(TokenCode::IN, "expected keyword 'in'", E_ExpectedKeyword);

    // A bare-identifier iterable followed by `{` is the loop body, not a struct
    // literal — but a known-struct-type literal is still allowed as the iterable.
    inForIterable_ = true;
    forStmt->iterable = parseExpression();
    inForIterable_ = false;

    forStmt->body = parseStatement();
    return forStmt;
}

std::unique_ptr<WhileStmt> Parser::parseWhileLoop()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::WHILE);
    auto whileLoop = std::make_unique<WhileStmt>();

    recorder.bindNode(whileLoop.get());

    inControlFlowCondition_ = true;
    whileLoop->condition = parseExpression();
    inControlFlowCondition_ = false;

    whileLoop->body = parseStatement();
    return whileLoop;
}

std::unique_ptr<MatchExpr> Parser::parseMatchExpression()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::MATCH);
    auto matchExpr = std::make_unique<MatchExpr>();
    recorder.bindNode(matchExpr.get());

    // Block the struct-literal `x { ... }` path: the `{` after the scrutinee is
    // the match body's opening brace, not a struct initializer.
    inControlFlowCondition_ = true;
    matchExpr->scrutinee = parseExpression();
    inControlFlowCondition_ = false;

    consume(TokenCode::LBRACE, "expect '{' after match scrutinee", E_ExpectALBRACE);

    while (!match(TokenCode::RBRACE) && !finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();
        auto arm = parseMatchArm();
        if (arm)
            matchExpr->arms.push_back(std::move(arm));
        if (currentPos == beforePos)
            advance(); // guarantee progress
        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
        if (match(TokenCode::COMMA)) continue; // arms are comma-separated
    }

    return matchExpr;
}

std::unique_ptr<MatchArm> Parser::parseMatchArm()
{
    PositionRecorder recorder(this, nullptr);
    auto arm = std::make_unique<MatchArm>();
    recorder.bindNode(arm.get());

    arm->pattern = parsePattern();
    consume(TokenCode::DOUBLE_ARROW, "expect '=>' after pattern", E_ExpectedKeyword);

    // An arm body is either a block `{ ... }` (statement match, void) or a tail
    // expression `v + 1` (value match).
    if (check(TokenCode::LBRACE))
        arm->body = parseCompoundStatement();
    else
        arm->tailValue = parseExpression();
    return arm;
}

std::unique_ptr<Pattern> Parser::parsePattern()
{
    PositionRecorder recorder(this, nullptr);
    auto pattern = std::make_unique<Pattern>();
    recorder.bindNode(pattern.get());

    // `_` wildcard.
    if (check(TokenCode::IDENTIFIER) && currentToken().value == "_")
    {
        advance();
        pattern->isWildcard = true;
        return pattern;
    }

    // Variant pattern: `none` (unit) or `some(v, w)` (payload bindings).
    pattern->variantName = consume(TokenCode::IDENTIFIER, "expected a pattern", E_ExpectAnIdentifier).value;
    if (match(TokenCode::LPAREN))
    {
        if (!match(TokenCode::RPAREN))
        {
            do
            {
                pattern->bindings.push_back(
                    consume(TokenCode::IDENTIFIER, "expected a binding name", E_ExpectAnIdentifier).value);
            } while (match(TokenCode::COMMA));
            match(TokenCode::RPAREN);
        }
    }
    return pattern;
}

// Parse expression
std::unique_ptr<Expr> Parser::parseExpression()
{
    PositionRecorder recorder(this, nullptr);

    auto expr = parseBinaryExpression(0);
    recorder.bindNode(expr.get());

    return expr;
}

std::unique_ptr<Expr> Parser::parseBinaryExpression(int minPrecedence)
{
    PositionRecorder exprRecorder(this, nullptr);
    PositionRecorder opRecorder(this, nullptr);

    auto left = parsePrimary();

    if (match(TokenCode::AS))
    {
        left = parseCastExpression(std::move(left));
    }

    left = parseMemberAccessChain(std::move(left));

    while (true)
    {
        Token opToken = currentToken();
        int precedence = getPrecedence(opToken.code);

        if (precedence <= minPrecedence)
            break;

        advance();

        auto right = parseBinaryExpression(precedence - 1);

        auto binary = std::make_unique<BinaryOp>();

        binary->left = std::move(left);
        binary->op = opToken.value;
        binary->right = std::move(right);

        opRecorder.bindNode(binary.get());
        opRecorder.~PositionRecorder();

        left = std::move(binary);
    }

    exprRecorder.bindNode(left.get());
    return left;
}

int Parser::getPrecedence(TokenCode type)
{
    switch (type)
    {
    case TokenCode::STAR:
    case TokenCode::SLASH:
    case TokenCode::MOD:
        return 8;
    case TokenCode::PLUS:
    case TokenCode::MINUS:
        return 7;
    case TokenCode::LT:
    case TokenCode::LT_EQ:
    case TokenCode::GT:
    case TokenCode::GT_EQ:
        return 6;
    case TokenCode::EQ_EQ:
    case TokenCode::NOT_EQ:
        return 5;
    case TokenCode::REFERENCE:
        return 4;
    case TokenCode::BOR:
        return 3;
    case TokenCode::AND:
        return 2;
    case TokenCode::OR:
        return 1;
    default:
        return 0;
    }
}

std::unique_ptr<Expr> Parser::parsePrimary()
{
    PositionRecorder recorder(this, nullptr);

    if (match(TokenCode::LPAREN))
    {
        auto expr = parseParenthesized();
        recorder.bindNode(expr.get());
        return expr;
    }

    if (isLiteral())
    {
        auto expr = parseLiteral();
        recorder.bindNode(expr.get());
        return expr;
    }

    // `match ...` is an expression (`let y = match o { ... }`).
    if (check(TokenCode::MATCH))
    {
        auto expr = parseMatchExpression();
        recorder.bindNode(expr.get());
        return expr;
    }

    if (check(TokenCode::IDENTIFIER))
    {
        Token identifier = currentToken();
        advance();

        // `Name { ... }` is a struct literal. Normally any identifier works
        // (cross-file types like the stdlib's Option aren't in knownTypes). In a
        // for-loop iterable or an if/while condition the `{` is ambiguous with a
        // block/body, so there we require Name to be a known struct type.
        if (check(TokenCode::LBRACE) && (knownTypes.count(identifier.value) > 0
            || !(inForIterable_ || inControlFlowCondition_)))
        {
            match(TokenCode::LBRACE);
            auto expr = parseStructInitialization(identifier);
            recorder.bindNode(expr.get());
            return expr;
        }

        if (check(TokenCode::LT))
        {
            if (looksLikeCallGenericParams())
            {
                auto expr = parseFunctionCall(identifier);
                recorder.bindNode(expr.get());
                return expr;
            }
            else
            {
                auto id = std::make_unique<IdentifierExpr>();
                recorder.bindNode(id.get());
                id->name = identifier.value;
                return parseMemberAccessChain(std::move(id));
            }
        }

        // `Name::X[(args)]` — parsed as a variant construction; HIRBuilder
        // dispatches to enum-variant-init or a static method call using the
        // shared knownEnums (a stdlib enum may be parsed after its first use).
        if (check(TokenCode::DOUBLE_COLON))
        {
            auto expr = parseVariantInitialization(identifier);
            recorder.bindNode(expr.get());
            return expr;
        }

        if (match(TokenCode::LPAREN) || check(TokenCode::DOUBLE_COLON))
        {
            auto expr = parseFunctionCall(identifier);
            recorder.bindNode(expr.get());
            return expr;
        }

        auto id = std::make_unique<IdentifierExpr>();
        recorder.bindNode(id.get());
        id->name = identifier.value;
        return parseMemberAccessChain(std::move(id));
    }

    if (match(TokenCode::SELF))
    {
        auto self = std::make_unique<IdentifierExpr>();
        recorder.bindNode(self.get());
        self->name = "self";
        return parseMemberAccessChain(std::move(self));
    }

    if (match(TokenCode::REFERENCE))
    {
        auto expr = parseBorrowExpression();
        recorder.bindNode(expr.get());
        return expr;
    }

    logError(currentToken(), "expected expression", E_ExpectedExpression);
    advance(); // consume the bad token — guarantees callers make progress

    // Return a placeholder so callers (parseStatement's `expr->position`, the
    // binary-op loop, etc.) don't dereference null. The Parser gates on the
    // error count before HIRBuilder ever sees this AST.
    auto placeholder = std::make_unique<IdentifierExpr>();
    placeholder->name = "<error>";
    return placeholder;
}

std::unique_ptr<BorrowExpr> Parser::parseBorrowExpression()
{
    PositionRecorder recorder(this, nullptr);
    auto expr = std::make_unique<BorrowExpr>();
    if (match(TokenCode::MUT))
        expr->isMutable = true;
    expr->expression = parseExpression();
    return expr;
}

std::unique_ptr<ParenExpr> Parser::parseParenthesized()
{
    PositionRecorder recorder(this, nullptr);
    auto expr = std::make_unique<ParenExpr>();
    recorder.bindNode(expr.get());
    expr->expression = parseExpression();
    consume(TokenCode::RPAREN, "expected ')' after expression", E_ExpectARPAREN);
    return expr;
}

std::unique_ptr<LiteralExpr> Parser::parseLiteral()
{
    PositionRecorder recorder(this, nullptr);
    auto literal = std::make_unique<LiteralExpr>();
    recorder.bindNode(literal.get());
    literal->value = currentToken().value;

    switch (currentToken().code)
    {
    case TokenCode::INT_LITERAL:
        literal->kind = LiteralExpr::LiteralType::Int;
        break;
    case TokenCode::FLOAT_LITERAL:
        literal->kind = LiteralExpr::LiteralType::Float;
        break;
    case TokenCode::STRING_LITERAL:
        literal->kind = LiteralExpr::LiteralType::String;
        break;
    case TokenCode::CHAR_LITERAL:
        literal->kind = LiteralExpr::LiteralType::Char;
        break;
    case TokenCode::BOOLEAN_TRUE:
    case TokenCode::BOOLEAN_FALSE:
        literal->kind = LiteralExpr::LiteralType::Bool;
        break;
    default:
        logError(currentToken(), "invalid literal kind", E_InvalidLiteralType);
    }

    advance();
    return literal;
}

bool Parser::isLiteral()
{
    return isOneOf({TokenCode::INT_LITERAL, TokenCode::FLOAT_LITERAL, TokenCode::STRING_LITERAL, TokenCode::CHAR_LITERAL, TokenCode::BOOLEAN_TRUE, TokenCode::BOOLEAN_FALSE});
}

std::unique_ptr<CastExpr> Parser::parseCastExpression(std::unique_ptr<Expr> expr)
{
    PositionRecorder recorder(this, nullptr);
    auto cast = std::make_unique<CastExpr>();
    recorder.bindNode(cast.get());
    cast->expression = std::move(expr);
    cast->targetType = parseType();
    return cast;
}

std::unique_ptr<StructInitExpr> Parser::parseStructInitialization(Token typeName)
{
    PositionRecorder recorder(this, nullptr);
    auto init = std::make_unique<StructInitExpr>();
    recorder.bindNode(init.get());

    auto type = std::make_unique<TypeNode>();
    type->kind = TypeNode::TypeKind::Custom;
    type->position = typeName.position;
    type->typeName = typeName.value;
    type->length = typeName.value.length();
    init->structType = std::move(type);

    if (check(TokenCode::LT))
    {
        init->genericParams = std::move(parseCallGenericParams());
    }

    if (!match(TokenCode::RBRACE))
    {
        do
        {
            std::string name = consume(TokenCode::IDENTIFIER, "expected member name", E_ExpectAnIdentifier).value;
            consume(TokenCode::COLON, "expected ':' after member name", E_ExpectACOLON);
            auto expr = parseExpression();
            init->memberInits.emplace_back(name, std::move(expr));
        } while (match(TokenCode::COMMA));

        consume(TokenCode::RBRACE, "expected '}' after struct initializer", E_ExpectARBRACE);
    }

    return init;
}

std::unique_ptr<VariantInitExpr> Parser::parseVariantInitialization(Token enumName)
{
    PositionRecorder recorder(this, nullptr);
    auto init = std::make_unique<VariantInitExpr>();
    recorder.bindNode(init.get());

    auto type = std::make_unique<TypeNode>();
    type->kind = TypeNode::TypeKind::Custom;
    type->typeName = enumName.value;
    type->position = enumName.position;
    type->length = enumName.value.length();
    if (check(TokenCode::LT))
        type->genericArgs = std::move(parseCallGenericParams());
    init->enumType = std::move(type);

    match(TokenCode::DOUBLE_COLON);
    init->variantName = consume(TokenCode::IDENTIFIER, "expected variant name after '::'", E_ExpectAnIdentifier).value;

    // Optional payload: `some(5, 6)` or a unit `red`.
    if (match(TokenCode::LPAREN))
    {
        init->arguments = parseArgumentList();
        consume(TokenCode::RPAREN, "expected ')' after variant arguments", E_ExpectARPAREN);
    }

    return init;
}

std::unique_ptr<Expr> Parser::parseFunctionCall(Token name)
{
    PositionRecorder recorder(this, nullptr);

    std::vector<std::unique_ptr<TypeNode>> genericParams;

    if (check(TokenCode::LT))
    {
        genericParams = std::move(parseCallGenericParams());
        match(TokenCode::LPAREN);
    }

    // (TypeNode::method)
    if (match(TokenCode::DOUBLE_COLON))
    {
        auto staticCall = std::make_unique<StaticMemberCall>();
        recorder.bindNode(staticCall.get());

        auto type = std::make_unique<TypeNode>();
        type->kind = TypeNode::TypeKind::Custom;
        type->typeName = name.value;
        type->position = name.position;
        type->length = name.value.length();
        staticCall->classType = std::move(type);

        staticCall->methodName = consume(TokenCode::IDENTIFIER, "expected method name", E_ExpectAnIdentifier).value;
        if (check(TokenCode::LT))
        {
            genericParams = std::move(parseCallGenericParams());
            staticCall->genericParams = std::move(genericParams);
        }
        consume(TokenCode::LPAREN, "expected '(' after method name", E_ExpectALPAREN);
        staticCall->arguments = parseArgumentList();
        consume(TokenCode::RPAREN, "expected ')' after arguments", E_ExpectARPAREN);
        return staticCall;
    }

    auto call = std::make_unique<FunctionCall>();
    recorder.bindNode(call.get());

    call->function = std::make_unique<IdentifierExpr>();
    static_cast<IdentifierExpr *>(call->function.get())->name = name.value;
    call->function->position = name.position;
    call->function->length = name.value.length();
    if (!genericParams.empty()) call->genericParams = std::move(genericParams);
    call->arguments = parseArgumentList();

    consume(TokenCode::RPAREN, "expected ')' after arguments", E_ExpectARPAREN);

    // (obj.method())
    if (match(TokenCode::DOT))
    {
        PositionRecorder chainRecorder(this, nullptr);
        auto memberCall = std::make_unique<MemberFunctionCall>();
        chainRecorder.bindNode(memberCall.get());

        memberCall->object = std::move(call);
        memberCall->methodName = consume(TokenCode::IDENTIFIER, "expected method name", E_ExpectAnIdentifier).value;
        consume(TokenCode::LPAREN, "expected '(' after method name", E_ExpectALBRACE);
        if (check(TokenCode::LT)) memberCall->genericParams = parseCallGenericParams();
        memberCall->arguments = parseArgumentList();

        consume(TokenCode::RPAREN, "expected ')' after arguments", E_ExpectARBRACE);
        return memberCall;
    }

    return call;
}

std::vector<std::unique_ptr<Expr>> Parser::parseArgumentList()
{
    std::vector<std::unique_ptr<Expr>> args;

    if (!check(TokenCode::RPAREN))
    {
        do
        {
            args.push_back(parseExpression());
        } while (match(TokenCode::COMMA));
    }

    return args;
}

std::unique_ptr<Expr> Parser::parseMemberAccessChain(std::unique_ptr<Expr> left)
{
    while (match(TokenCode::DOT))
    {
        PositionRecorder recorder(this, nullptr);
        Token member = consume(TokenCode::IDENTIFIER, "expected member name after '.'", E_ExpectAnIdentifier);

        if (match(TokenCode::LPAREN))
        {
            auto call = std::make_unique<MemberFunctionCall>();
            recorder.bindNode(call.get());
            call->object = std::move(left);
            call->methodName = member.value;
            if (check(TokenCode::LT)) call->genericParams = parseCallGenericParams();
            call->arguments = parseArgumentList();
            consume(TokenCode::RPAREN, "expected ')' after arguments", E_ExpectAnIdentifier);
            left = std::move(call);
        }
        else
        {
            auto access = std::make_unique<MemberAccess>();
            recorder.bindNode(access.get());
            access->object = std::move(left);
            access->memberName = member.value;
            left = std::move(access);
        }
    }
    return left;
}

std::vector<GenericConstraint> Parser::parseGenericConstraints()
{
    std::vector<GenericConstraint> result;

    do
    {
        GenericConstraint c;
        c.name = consume(TokenCode::IDENTIFIER, "expected an identifier for constraint trait name", E_ExpectAnIdentifier).value;

        // Optional concrete args: T: Iterator<i32>
        if (match(TokenCode::LT))
        {
            do
            {
                c.args.push_back(std::move(parseType()));
            } while (match(TokenCode::COMMA));
            match(TokenCode::GT);
        }

        result.push_back(std::move(c));
    } while (match(TokenCode::PLUS));

    return result;
}

std::vector<std::unique_ptr<GenericParam>> Parser::parseGenericParams()
{
    consume(TokenCode::LT, "expected '<' before generic type params", E_ExpectAnIdentifier);

    std::vector<std::unique_ptr<GenericParam>> params;

    if (!check(TokenCode::GT))
    {
        do
        {
            PositionRecorder recorder(this, nullptr);
            auto genericParam = std::make_unique<GenericParam>();
            recorder.bindNode(genericParam.get());
            genericParam->name = consume(TokenCode::IDENTIFIER, "expected a idenfiter for the generic type name", E_ExpectAnIdentifier).value;
            if (match(TokenCode::COLON))
                genericParam->constraints = Parser::parseGenericConstraints();
            params.push_back(std::move(genericParam));
        } while (match(TokenCode::COMMA));
    }

    consume(TokenCode::GT, "expected '>' after generic type params", E_ExpectAnIdentifier);

    return params;
}

std::vector<std::unique_ptr<TypeNode>> Parser::parseCallGenericParams()
{
    consume(TokenCode::LT, "expected '<' before generic type params", E_ExpectAnIdentifier);

    std::vector<std::unique_ptr<TypeNode>> params;

    if (!check(TokenCode::GT))
    {
        do
        {
            params.push_back(std::move(parseType()));
        } while (match(TokenCode::COMMA));
    }

    consume(TokenCode::GT, "expected '>' after generic type params", E_ExpectAnIdentifier);

    return params;
}

bool Parser::looksLikeCallGenericParams()
{
    createSnapshot();

    if (!match(TokenCode::LT))
    {
        backToSnapshot();
        return false;
    }

    int depth = 1;
    while (!finished() && depth > 0)
    {
        if (match(TokenCode::LT))
            depth++;
        else if (match(TokenCode::GT))
            depth--;
        else if (check(TokenCode::SEMI) || check(TokenCode::LBRACE) || check(TokenCode::RBRACE) || check(TokenCode::ASSIGN))
        {
            // Looks like a comparison expression, not generics
            backToSnapshot();
            return false;
        }
        else
            advance();
    }

    // Only treat it as generic params if immediately followed by '(' or '::'
    bool result = check(TokenCode::LPAREN) || check(TokenCode::DOUBLE_COLON);
    backToSnapshot();
    return result;
}