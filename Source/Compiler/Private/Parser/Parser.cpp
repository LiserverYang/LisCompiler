#include "Parser/Parser.hpp"
#include "Lexer/Token.hpp"

void Parser::initLineInformation(std::unique_ptr<ASTNode> &node)
{
    node->col = currentToken().col;
    node->line = currentToken().line;
    node->lineStart = currentToken().lineStart;
}

void Parser::run()
{
    tokenStream = &(context->tokenStream);
    auto &program = context->program;

    while (!finished())
    {
        program.globalStatements.push_back(parseGlobalStatement());
    }
}

std::unique_ptr<ASTNode> Parser::parseGlobalStatement()
{
    // 移除导入语句(IMPT)相关代码
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

// 移除 parseImptStatement() 函数

std::unique_ptr<StructDef> Parser::parseStructDefinition()
{
    match(TokenCode::STRUCT);

    auto structDef = std::make_unique<StructDef>();

    createSnapshot();
    structDef->name = consume(TokenCode::IDENTIFIER, "expect an identifier as the struct name").value;

    if (knownTypes.count(structDef->name) > 0)
    {
        backToSnapshot();
        consume(TokenCode::UNDEFINED, "mutidefined struct '" + structDef->name + "'");
    }

    consume(TokenCode::LBRACE, "expect a '{' after struct name");

    while (!match(TokenCode::RBRACE))
    {
        structDef->members.push_back(parseMemberVariableDefinition());
    }

    knownTypes.insert(structDef->name);
    return structDef;
}

std::unique_ptr<StructImpl> Parser::parseStructImplementation()
{
    match(TokenCode::IMPL);

    auto impl = std::make_unique<StructImpl>();
    createSnapshot();
    impl->structName = consume(TokenCode::IDENTIFIER, "expect a struct name after impl").value;

    if (knownTypes.count(impl->structName) == 0)
    {
        backToSnapshot();
        consume(TokenCode::UNDEFINED, "undefined struct '" + impl->structName + "'");
    }

    consume(TokenCode::LBRACE, "expect a '{'");

    while (!check(TokenCode::RBRACE))
    {
        impl->methods.push_back(parseMemberFunctionDefinition());
    }

    consume(TokenCode::RBRACE, "expect a '}'");

    return impl;
}

std::unique_ptr<FunctionDef> Parser::parseFunctionDefinition()
{
    match(TokenCode::FN);

    auto func = std::make_unique<FunctionDef>();
    func->name = consume(TokenCode::IDENTIFIER, "expect a function name").value;

    consume(TokenCode::LPAREN, "expect a '(' after function name");
    func->params = parseParameterList();
    consume(TokenCode::RPAREN, "expect a ')' after parameters");

    if (match(TokenCode::ARROW))
    {
        func->returnType = parseType();
    }

    func->body = parseCompoundStatement();
    return func;
}

std::unique_ptr<GlobalVarDef> Parser::parseGlobalVariableDefinition()
{
    auto var = std::make_unique<GlobalVarDef>();
    var->isMove = match(TokenCode::MOVE);
    var->name = consume(TokenCode::IDENTIFIER, "expect variable name").value;

    if (match(TokenCode::COLON))
    {
        var->type = parseType();
    }

    consume(TokenCode::ASSIGN, "expected '=' in variable definition");
    var->initValue = parseExpression();
    consume(TokenCode::SEMI, "expected ';' after variable definition");

    return var;
}

// 移除 parseModulePath() 函数

std::unique_ptr<MemberVarDef> Parser::parseMemberVariableDefinition()
{
    auto member = std::make_unique<MemberVarDef>();
    member->isPublic = match(TokenCode::PUB);
    member->name = consume(TokenCode::IDENTIFIER, "expected a member name").value;

    consume(TokenCode::COLON, "expected ':' after member name");
    member->type = parseType();
    match(TokenCode::COMMA); // 可选逗号

    return member;
}

std::unique_ptr<Type> Parser::parseType()
{
    auto type = std::make_unique<Type>();

    if (match(TokenCode::REFERENCE))
    {
        type->isReference = true;
        type->isMutReference = match(TokenCode::MUT);
    }

    // 移除模块限定类型相关代码
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
            consume(TokenCode::UNDEFINED, "undefined type '" + currentToken().value + "'");
        }

        type->kind = Type::TypeKind::Custom;
        type->typeName = currentToken().value;
        advance();
        return type;
    }

    Logger::LogInfo logInfo;
    initLogInfo(currentToken(), logInfo, "expected type");
    Logger::Log(Logger::LogLevel::ERROR, logInfo);

    return nullptr;
}

std::unique_ptr<MemberFunctionDef> Parser::parseMemberFunctionDefinition()
{
    consume(TokenCode::FN, "expect 'fn' for member function");

    auto func = std::make_unique<MemberFunctionDef>();
    func->name = consume(TokenCode::IDENTIFIER, "expected function name").value;

    consume(TokenCode::LPAREN, "expected '(' after function name");

    // 修复：self参数是可选的
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

    consume(TokenCode::RPAREN, "expected ')' after parameters");

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
    auto param = std::make_unique<Param>();
    param->name = consume(TokenCode::IDENTIFIER, "expected parameter name").value;

    // 修复：移除错误的参数名覆盖
    consume(TokenCode::COLON, "expected ':'");

    param->type = parseType();

    if (match(TokenCode::ASSIGN))
    {
        param->defaultValue = parseExpression();
    }

    return param;
}

std::unique_ptr<CompoundStmt> Parser::parseCompoundStatement()
{
    match(TokenCode::LBRACE);

    auto block = std::make_unique<CompoundStmt>();

    while (!check(TokenCode::RBRACE))
    {
        block->statements.push_back(parseStatement());
    }

    consume(TokenCode::RBRACE, "expected '}' after compound statement");
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
        consume(TokenCode::SEMI, "expected ';' after assignment");
        return assign;
    }

    auto exprStmt = std::make_unique<ExprStmt>();
    exprStmt->expression = std::move(expr);
    consume(TokenCode::SEMI, "expected ';' after expression");
    return exprStmt;
}

std::unique_ptr<IfStmt> Parser::parseIfStmt()
{
    match(TokenCode::IF);
    auto ifStmt = std::make_unique<IfStmt>();

    consume(TokenCode::LPAREN, "expected '(' after 'if'");
    ifStmt->condition = parseExpression();
    consume(TokenCode::RPAREN, "expected ')'");

    ifStmt->thenBranch = parseStatement();

    if (match(TokenCode::ELSE))
    {
        ifStmt->elseBranch = parseStatement();
    }

    return ifStmt;
}

std::unique_ptr<ReturnStmt> Parser::parseReturnStmt()
{
    match(TokenCode::RET);
    auto returnStmt = std::make_unique<ReturnStmt>();

    // 修复：正确处理无分号的返回语句
    if (!check(TokenCode::SEMI))
    {
        returnStmt->returnValue = parseExpression();
    }

    consume(TokenCode::SEMI, "expected ';' after return");
    return returnStmt;
}

std::unique_ptr<DeclStmt> Parser::parseDeclarationStatement()
{
    match(TokenCode::LET);
    auto decl = std::make_unique<DeclStmt>();

    decl->isMutable = match(TokenCode::MUT);
    decl->name = consume(TokenCode::IDENTIFIER, "expected an identifier as the variable name").value;

    if (match(TokenCode::COLON))
    {
        decl->type = parseType();
    }

    if (match(TokenCode::ASSIGN))
    {
        decl->initValue = parseExpression();
    }

    consume(TokenCode::SEMI, "expected ';' after let statement");
    return decl;
}

std::unique_ptr<ForStmt> Parser::parseForLoop()
{
    match(TokenCode::FOR);
    auto forStmt = std::make_unique<ForStmt>();

    consume(TokenCode::LPAREN, "expected '(' after 'for'");
    forStmt->loopVar = consume(TokenCode::IDENTIFIER, "expected an identifier as the loop variable").value;
    consume(TokenCode::IN, "expected keyword 'in'");
    forStmt->iterable = parseExpression();
    consume(TokenCode::RPAREN, "expected ')'");

    forStmt->body = parseStatement();
    return forStmt;
}

std::unique_ptr<WhileStmt> Parser::parseWhileLoop()
{
    match(TokenCode::WHILE);
    auto whileLoop = std::make_unique<WhileStmt>();

    consume(TokenCode::LPAREN, "expected '(' after 'while'");
    whileLoop->condition = parseExpression();
    consume(TokenCode::RPAREN, "expected ')' after condition");

    whileLoop->body = parseStatement();
    return whileLoop;
}

// 表达式解析
std::unique_ptr<Expr> Parser::parseExpression()
{
    return parseBinaryExpression(0);
}

std::unique_ptr<Expr> Parser::parseBinaryExpression(int minPrecedence)
{
    auto left = parsePrimary();
    left = parseMemberAccessChain(std::move(left));

    while (true)
    {
        Token opToken = currentToken();
        int precedence = getPrecedence(opToken.code);

        if (precedence <= minPrecedence)
            break;

        advance(); // 消耗运算符
        auto right = parseBinaryExpression(precedence + 1);

        auto binary = std::make_unique<BinaryOp>();
        binary->left = std::move(left);
        binary->op = opToken.value;
        binary->right = std::move(right);
        left = std::move(binary);
    }

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
    if (match(TokenCode::LPAREN))
    {
        return parseParenthesized();
    }

    if (isLiteral())
    {
        return parseLiteral();
    }

    if (isTypeStart() && knownTypes.count(currentToken().value) != 0)
    {
        auto type = parseType();

        consume(TokenCode::LPAREN, "except '(' for type cast");

        return parseCastExpression(std::move(type));
    }

    if (check(TokenCode::IDENTIFIER))
    {
        Token identifier = currentToken();
        advance();

        if (match(TokenCode::LBRACE))
        {
            return parseStructInitialization(identifier.value);
        }

        if (match(TokenCode::LPAREN))
        {
            return parseFunctionCall(identifier.value);
        }

        auto id = std::make_unique<IdentifierExpr>();
        id->name = identifier.value;
        return parseMemberAccessChain(std::move(id));
    }

    if (match(TokenCode::SELF))
    {
        auto self = std::make_unique<IdentifierExpr>();
        self->name = "self";
        return parseMemberAccessChain(std::move(self));
    }

    Logger::LogInfo logInfo;
    initLogInfo(currentToken(), logInfo, "expected expression");
    Logger::Log(Logger::LogLevel::ERROR, logInfo);

    return nullptr;
}

std::unique_ptr<ParenExpr> Parser::parseParenthesized()
{
    auto expr = std::make_unique<ParenExpr>();
    expr->expression = parseExpression();
    consume(TokenCode::RPAREN, "expected ')' after expression");
    return expr;
}

std::unique_ptr<LiteralExpr> Parser::parseLiteral()
{
    auto literal = std::make_unique<LiteralExpr>();
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
        initLogInfo(currentToken(), logInfo, "invalid literal type");
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
    auto cast = std::make_unique<CastExpr>();
    cast->targetType = std::move(type);
    cast->expression = parseExpression();
    consume(TokenCode::RPAREN, "expected ')' after cast expression");
    return cast;
}

std::unique_ptr<StructInitExpr> Parser::parseStructInitialization(const std::string &typeName)
{
    auto init = std::make_unique<StructInitExpr>();

    auto type = std::make_unique<Type>();
    type->kind = Type::TypeKind::Custom;
    type->typeName = typeName;
    init->structType = std::move(type);

    if (!match(TokenCode::RBRACE))
    {
        do
        {
            std::string name = consume(TokenCode::IDENTIFIER, "expected member name").value;
            consume(TokenCode::COLON, "expected ':' after member name");
            auto expr = parseExpression();
            init->memberInits.emplace_back(name, std::move(expr));
        } while (match(TokenCode::COMMA));

        consume(TokenCode::RBRACE, "expected '}' after struct initializer");
    }

    return init;
}

std::unique_ptr<Expr> Parser::parseFunctionCall(const std::string &name)
{
    // 静态成员调用 (Type::method)
    if (match(TokenCode::DOUBLE_COLON))
    {
        auto staticCall = std::make_unique<StaticMemberCall>();

        auto type = std::make_unique<Type>();
        type->kind = Type::TypeKind::Custom;
        type->typeName = name;
        staticCall->classType = std::move(type);

        staticCall->methodName = consume(TokenCode::IDENTIFIER, "expected method name").value;
        consume(TokenCode::LPAREN, "expected '(' after method name");
        staticCall->arguments = parseArgumentList();
        consume(TokenCode::RPAREN, "expected ')' after arguments");
        return staticCall;
    }

    // 普通函数调用
    auto call = std::make_unique<FunctionCall>();
    call->function = std::make_unique<IdentifierExpr>();
    static_cast<IdentifierExpr *>(call->function.get())->name = name;
    call->arguments = parseArgumentList();
    consume(TokenCode::RPAREN, "expected ')' after arguments");

    // 检查链式调用 (obj.method())
    if (match(TokenCode::DOT))
    {
        auto memberCall = std::make_unique<MemberFunctionCall>();
        memberCall->object = std::move(call);
        memberCall->methodName = consume(TokenCode::IDENTIFIER, "expected method name").value;
        consume(TokenCode::LPAREN, "expected '(' after method name");
        memberCall->arguments = parseArgumentList();
        consume(TokenCode::RPAREN, "expected ')' after arguments");
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
        Token member = consume(TokenCode::IDENTIFIER, "expected member name after '.'");

        if (match(TokenCode::LPAREN))
        {
            auto call = std::make_unique<MemberFunctionCall>();
            call->object = std::move(left);
            call->methodName = member.value;
            call->arguments = parseArgumentList();
            consume(TokenCode::RPAREN, "expected ')' after arguments");
            left = std::move(call);
        }
        else
        {
            auto access = std::make_unique<MemberAccess>();
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