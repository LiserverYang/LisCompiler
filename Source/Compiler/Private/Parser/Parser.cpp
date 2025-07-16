#include "Parser/Parser.hpp"
#include "Lexer/Token.hpp"
#include "Parser/ASTPrinter.hpp"

void Parser::run()
{
    tokenStream = &(context->tokenStream);
    auto &program = context->program;

    while (!finished())
    {
        program.globalStatements.push_back(parseGlobalStatement());
    }

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
    else
    {
        Logger::LogInfo logInfo;
        initLogInfo(currentToken(), logInfo, "illegal global statement");
        Logger::Log(Logger::LogLevel::ERROR, logInfo);
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

    if (knownTypes.count(structDef->name) > 0)
    {
        backToSnapshot();
        consume(TokenCode::UNDEFINED, "mutidefined struct '" + structDef->name + "'", E_MutidefinedStruct);
    }

    consume(TokenCode::LBRACE, "expect a '{' after struct name", E_ExpectALBRACE);

    while (!match(TokenCode::RBRACE))
    {
        structDef->members.push_back(parseMemberVariableDefinition());
    }

    knownTypes.insert(structDef->name);
    return structDef;
}

std::unique_ptr<StructImpl> Parser::parseStructImplementation()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::IMPL);

    auto impl = std::make_unique<StructImpl>();
    createSnapshot();
    impl->structName = consume(TokenCode::IDENTIFIER, "expect a struct name after impl", E_ExpectAnIdentifier).value;

    recorder.bindNode(impl.get());

    if (knownTypes.count(impl->structName) == 0)
    {
        backToSnapshot();
        consume(TokenCode::UNDEFINED, "undefined struct '" + impl->structName + "'", E_UndefinedStruct);
    }

    consume(TokenCode::LBRACE, "expect a '{'", E_ExpectALBRACE);

    while (!check(TokenCode::RBRACE))
    {
        impl->methods.push_back(parseMemberFunctionDefinition());
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

std::unique_ptr<Type> Parser::parseType()
{
    PositionRecorder recorder(this, nullptr);

    auto type = std::make_unique<Type>();

    recorder.bindNode(type.get());

    if (match(TokenCode::REFERENCE))
    {
        type->isReference = true;
        type->isMutReference = match(TokenCode::MUT);
    }

    if ((size_t)currentToken().code >= TYPE_KEYWORD_BEGIN && (size_t)currentToken().code <= TYPE_KEYWORD_END)
    {
        type->kind = Type::TypeKind::Primitive;
        type->typeName = currentToken().value;
        advance();
        return type;
    }

    if (check(TokenCode::IDENTIFIER))
    {
        if (knownTypes.count(currentToken().value) == 0)
        {
            consume(TokenCode::UNDEFINED, "undefined type '" + currentToken().value + "'", E_UndefinedType);
        }

        type->kind = Type::TypeKind::Custom;
        type->typeName = currentToken().value;
        advance();
        return type;
    }

    Logger::LogInfo logInfo;
    initLogInfo(currentToken(), logInfo, "expected type", E_ExpectType);
    Logger::Log(Logger::LogLevel::ERROR, logInfo);

    return nullptr;
}

std::unique_ptr<MemberFunctionDef> Parser::parseMemberFunctionDefinition()
{
    PositionRecorder recorder(this, nullptr);

    consume(TokenCode::FN, "expect 'fn' for member function", E_ExpectedKeyword);

    auto func = std::make_unique<MemberFunctionDef>();
    func->name = consume(TokenCode::IDENTIFIER, "expected function name", E_ExpectAnIdentifier).value;

    recorder.bindNode(func.get());

    consume(TokenCode::LPAREN, "expected '(' after function name", E_ExpectALPAREN);

    if (match(TokenCode::SELF))
    {
        auto selfParam = std::make_unique<SelfParam>();
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
    }
    else
    {
        func->params = parseParameterList();
    }

    consume(TokenCode::RPAREN, "expected ')' after parameters", E_ExpectARPAREN);

    if (match(TokenCode::ARROW))
    {
        func->returnType = parseType();
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

    while (!check(TokenCode::RBRACE))
    {
        block->statements.push_back(parseStatement());
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

    // 赋值语句或表达式语句
    auto expr = parseExpression();

    if (match(TokenCode::ASSIGN))
    {
        auto assign = std::make_unique<AssignStmt>();
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

    consume(TokenCode::LPAREN, "expected '(' after 'if'", E_ExpectALPAREN);
    ifStmt->condition = parseExpression();
    consume(TokenCode::RPAREN, "expected ')'", E_ExpectARPAREN);

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

    consume(TokenCode::LPAREN, "expected '(' after 'for'", E_ExpectALPAREN);
    forStmt->loopVar = consume(TokenCode::IDENTIFIER, "expected an identifier as the loop variable", E_ExpectAnIdentifier).value;
    consume(TokenCode::IN, "expected keyword 'in'", E_ExpectedKeyword);
    forStmt->iterable = parseExpression();
    consume(TokenCode::RPAREN, "expected ')'", E_ExpectARPAREN);

    forStmt->body = parseStatement();
    return forStmt;
}

std::unique_ptr<WhileStmt> Parser::parseWhileLoop()
{
    PositionRecorder recorder(this, nullptr);

    match(TokenCode::WHILE);
    auto whileLoop = std::make_unique<WhileStmt>();

    recorder.bindNode(whileLoop.get());

    consume(TokenCode::LPAREN, "expected '(' after 'while'", E_ExpectALPAREN);
    whileLoop->condition = parseExpression();
    consume(TokenCode::RPAREN, "expected ')' after condition", E_ExpectARPAREN);

    whileLoop->body = parseStatement();
    return whileLoop;
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

    if (isTypeStart() && getToken(currentPos + 1).code == TokenCode::LPAREN && knownTypes.count(currentToken().value) > 0)
    {
        auto type = parseType();

        consume(TokenCode::LPAREN, "except '(' for type cast", E_ExpectALPAREN);

        auto expr = parseCastExpression(std::move(type));
        recorder.bindNode(expr.get());
        return expr;
    }

    if (check(TokenCode::IDENTIFIER))
    {
        Token identifier = currentToken();
        advance();

        if (match(TokenCode::LBRACE))
        {
            auto expr = parseStructInitialization(identifier);
            recorder.bindNode(expr.get());
            return expr;
        }

        if (match(TokenCode::LPAREN))
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

    Logger::LogInfo logInfo;
    initLogInfo(currentToken(), logInfo, "expected expression", E_ExpectedExpression);
    Logger::Log(Logger::LogLevel::ERROR, logInfo);

    return nullptr;
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
        literal->type = LiteralExpr::LiteralType::Int;
        break;
    case TokenCode::FLOAT_LITERAL:
        literal->type = LiteralExpr::LiteralType::Float;
        break;
    case TokenCode::STRING_LITERAL:
        literal->type = LiteralExpr::LiteralType::String;
        break;
    case TokenCode::CHAR_LITERAL:
        literal->type = LiteralExpr::LiteralType::Char;
        break;
    case TokenCode::BOOLEAN_TRUE:
    case TokenCode::BOOLEAN_FALSE:
        literal->type = LiteralExpr::LiteralType::Bool;
        break;
    default:
        Logger::LogInfo logInfo;
        initLogInfo(currentToken(), logInfo, "invalid literal type", E_InvalidLiteralType);
        Logger::Log(Logger::LogLevel::ERROR, logInfo);
    }

    advance();
    return literal;
}

bool Parser::isLiteral()
{
    return isOneOf({TokenCode::INT_LITERAL, TokenCode::FLOAT_LITERAL, TokenCode::STRING_LITERAL, TokenCode::CHAR_LITERAL, TokenCode::BOOLEAN_TRUE, TokenCode::BOOLEAN_FALSE});
}

std::unique_ptr<CastExpr> Parser::parseCastExpression(std::unique_ptr<Type> type)
{
    PositionRecorder recorder(this, nullptr);
    auto cast = std::make_unique<CastExpr>();
    recorder.bindNode(cast.get());
    cast->targetType = std::move(type);
    cast->expression = parseExpression();
    consume(TokenCode::RPAREN, "expected ')' after cast expression", E_ExpectARPAREN);
    return cast;
}

std::unique_ptr<StructInitExpr> Parser::parseStructInitialization(Token typeName)
{
    PositionRecorder recorder(this, nullptr);
    auto init = std::make_unique<StructInitExpr>();
    recorder.bindNode(init.get());

    auto type = std::make_unique<Type>();
    type->kind = Type::TypeKind::Custom;
    type->typeName = typeName.value;
    type->col = typeName.col;
    type->line = typeName.line;
    type->lineStart = typeName.lineStart;
    type->length = typeName.value.length();
    init->structType = std::move(type);

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

std::unique_ptr<Expr> Parser::parseFunctionCall(Token name)
{
    PositionRecorder recorder(this, nullptr);

    // 静态成员调用 (Type::method)
    if (match(TokenCode::DOUBLE_COLON))
    {
        auto staticCall = std::make_unique<StaticMemberCall>();
        recorder.bindNode(staticCall.get());

        auto type = std::make_unique<Type>();
        type->kind = Type::TypeKind::Custom;
        type->typeName = name.value;
        type->col = name.col;
        type->line = name.line;
        type->lineStart = name.lineStart;
        type->length = name.value.length();
        staticCall->classType = std::move(type);

        staticCall->methodName = consume(TokenCode::IDENTIFIER, "expected method name", E_ExpectAnIdentifier).value;
        consume(TokenCode::LPAREN, "expected '(' after method name", E_ExpectALPAREN);
        staticCall->arguments = parseArgumentList();
        consume(TokenCode::RPAREN, "expected ')' after arguments", E_ExpectARPAREN);
        return staticCall;
    }

    // 普通函数调用
    auto call = std::make_unique<FunctionCall>();
    recorder.bindNode(call.get());

    call->function = std::make_unique<IdentifierExpr>();
    static_cast<IdentifierExpr *>(call->function.get())->name = name.value;
    call->function->line = name.line;
    call->function->col = name.col;
    call->function->lineStart = name.lineStart;
    call->function->length = name.value.length();
    call->arguments = parseArgumentList();

    consume(TokenCode::RPAREN, "expected ')' after arguments", E_ExpectARPAREN);

    // 检查链式调用 (obj.method())
    if (match(TokenCode::DOT))
    {
        PositionRecorder chainRecorder(this, nullptr);
        auto memberCall = std::make_unique<MemberFunctionCall>();
        chainRecorder.bindNode(memberCall.get());

        memberCall->object = std::move(call);
        memberCall->methodName = consume(TokenCode::IDENTIFIER, "expected method name", E_ExpectAnIdentifier).value;
        consume(TokenCode::LPAREN, "expected '(' after method name", E_ExpectALBRACE);
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

bool Parser::isTypeStart()
{
    return isOneOf({TokenCode::I8, TokenCode::I16, TokenCode::I32, TokenCode::I64, TokenCode::F32, TokenCode::F64, TokenCode::BOOL, TokenCode::CHAR, TokenCode::IDENTIFIER});
}