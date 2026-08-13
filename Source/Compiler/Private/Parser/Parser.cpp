/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include "Parser/Parser.hpp"
#include "Lexer/Lexer.hpp"
#include "Lexer/Token.hpp"
#include "Parser/ASTPrinter.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

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
        case TokenCode::IF:
        case TokenCode::LET:
        case TokenCode::WHILE:
        case TokenCode::FOR:
        case TokenCode::RET:
        case TokenCode::BREAK:
        case TokenCode::CONTINUE:
        case TokenCode::FN:
        case TokenCode::STRUCT:
        case TokenCode::IMPL:
        case TokenCode::TRAIT:
        case TokenCode::ENUM:
        case TokenCode::IMPT:
            return;
        default: advance();
        }
    }
}

void Parser::run()
{
    parseAll();

    if (Logger::GetErrorCount() > 0)
        exit(1);

    if (context->args->getArg("print_ast").compare("true") == 0)
    {
        printAST(context->program);
    }
}

void Parser::parseAll()
{
    tokenStream = &(context->tokenStream);
    auto &program = context->program;

    // Errors are non-fatal during parsing so all of them surface; the count
    // gates the pipeline at the end (see run()). The count is NOT reset here —
    // the Lexer (which runs first and resets per file) may have already logged
    // lexing errors for this file, and they must not be discarded.
    //
    // parseAll() exists as a gate-free entry (mirroring how tests call
    // sema.visit() instead of sema.run()): run() gates with exit(1), which would
    // kill the whole test process, so tests that exercise PARSE errors call
    // parseAll() and inspect Logger::GetErrorCount() instead.
    while (!finished())
    {
        size_t beforePos = currentPos;
        int beforeErr = Logger::GetErrorCount();

        auto node = parseGlobalStatement();

        if (node)
        {
            program.globalStatements.push_back(std::move(node));
            // Record which module/file this top-level statement belongs to, in
            // parallel with globalStatements (HIRBuilder/sema read it).
            StmtAttribution attr;
            attr.modulePath = currentModule_;
            attr.filePath = context->filePath;
            context->stmtAttributions.push_back(std::move(attr));
        }

        // Guarantee forward progress even when a construct failed without
        // consuming a token (avoids an infinite loop on malformed input).
        if (currentPos == beforePos)
            advance();

        // A construct failed — skip to the next statement/declaration boundary
        // so the next construct parses cleanly.
        if (Logger::GetErrorCount() > beforeErr)
            synchronize();
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
    else if (check(TokenCode::IMPT))
    {
        return parseImptStatement();
    }
    else
    {
        logError(currentToken(), "illegal global statement");
        advance(); // consume the bad token so run() makes progress
    }

    return nullptr;
}

std::unique_ptr<ModulePath> Parser::parseModulePath()
{
    auto path = std::make_unique<ModulePath>();
    PositionRecorder recorder(this, path.get());

    do
    {
        path->pathSegments.push_back(consume(TokenCode::IDENTIFIER, "expected module name", E_ExpectAnIdentifier).value);
    } while (match(TokenCode::DOT));

    return path;
}

std::unique_ptr<ImportStmt> Parser::parseImptStatement()
{
    auto stmt = std::make_unique<ImportStmt>();
    PositionRecorder recorder(this, stmt.get());

    match(TokenCode::IMPT);

    stmt->modulePath = parseModulePath();

    // `impt foo.bar;`                → import whole module (alias = last segment)
    // `impt foo.bar as f;`           → import under alias f
    // `impt foo.bar { a, b };`       → selective import (bare names into scope)
    if (match(TokenCode::AS))
    {
        stmt->alias = consume(TokenCode::IDENTIFIER, "expected alias name after 'as'", E_ExpectAnIdentifier).value;
    }
    else if (match(TokenCode::LBRACE))
    {
        std::vector<std::string> symbols;
        while (!check(TokenCode::RBRACE))
        {
            symbols.push_back(consume(TokenCode::IDENTIFIER, "expected symbol name", E_ExpectAnIdentifier).value);
            if (!match(TokenCode::COMMA))
                break;
        }
        consume(TokenCode::RBRACE, "expected '}' after import symbols", E_ExpectARBRACE);
        if (symbols.empty())
            logError(currentToken(), "selective import needs at least one symbol", E_ExpectedExpression);
        stmt->symbols = std::move(symbols);
    }

    consume(TokenCode::SEMI, "expected ';' after import statement", E_ExpectASEMI);

    // ── module-system side effect: bind the import and load the file ──────
    // Join the dot-path into a canonical module path ("foo.bar") and record the
    // binding (alias or last segment) in this module's import table. Then load
    // the module's file, recursively parsing its own imports.
    std::string canonical;
    if (stmt->modulePath)
    {
        for (size_t i = 0; i < stmt->modulePath->pathSegments.size(); ++i)
        {
            if (i > 0) canonical += ".";
            canonical += stmt->modulePath->pathSegments[i];
        }
    }
    if (canonical.empty())
        return stmt;

    std::string boundName = stmt->alias.value_or(canonical.substr(canonical.find_last_of('.') == std::string::npos ? 0 : canonical.find_last_of('.') + 1));

    ImportBinding binding;
    binding.canonicalModule = canonical;
    binding.boundName = boundName;
    binding.selective = stmt->symbols.has_value();
    if (binding.selective)
        binding.symbols = *stmt->symbols;
    context->importsByModule[currentModule_].push_back(binding);
    moduleImports_[boundName] = canonical;

    loadModule(canonical);

    return stmt;
}

std::string Parser::resolveModuleAlias(const std::string &alias) const
{
    auto it = moduleImports_.find(alias);
    return it != moduleImports_.end() ? it->second : std::string();
}

void Parser::loadModule(const std::string &canonical)
{
    // Circular import: this module is already being parsed somewhere up the stack.
    if (context->loadingModules.count(canonical))
    {
        logError(currentToken(), "circular import: module '" + canonical + "'", E_UndefinedIdentifier);
        return;
    }
    // Already fully loaded — only (re)bind, don't re-parse.
    if (context->loadedModules.count(canonical))
        return;

    context->loadingModules.insert(canonical);

    // Resolve the module path to a file: <searchPath>/foo/bar.lis.
    namespace fs = std::filesystem;
    std::string relative = canonical;
    for (auto &c : relative)
        if (c == '.') c = '/';
    relative += ".lis";

    std::string foundPath;
    for (const auto &dir : context->searchPaths)
    {
        fs::path candidate = fs::path(dir) / fs::path(relative);
        if (fs::exists(candidate))
        {
            foundPath = candidate.string();
            break;
        }
    }

    if (foundPath.empty())
    {
        context->loadingModules.erase(canonical);
        logError(currentToken(), "cannot find module '" + canonical + "'", E_UndefinedIdentifier);
        return;
    }

    // Save/restore the single-slot file fields AND the token stream (mirrors
    // CompilePipeline's stdlib preload). The token stream MUST be saved: the
    // recursive Lexer overwrites context->tokenStream, and the CURRENT parser's
    // tokenStream pointer (bound to &context->tokenStream) would otherwise
    // start reading the module file's tokens mid-parse.
    std::string savedFilePath = context->filePath;
    std::string savedFileValue = context->fileValue;
    TokenStream savedStream = std::move(context->tokenStream);

    std::ifstream file(foundPath);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    context->filePath = foundPath;
    context->fileValue = content;
    context->fileContents[foundPath] = content;

    Lexer lexer(context);
    lexer.run();
    Parser parser(context);
    parser.setCurrentModule(canonical);
    parser.parseAll(); // gate-free: parse errors are non-fatal and counted

    context->filePath = savedFilePath;
    context->fileValue = savedFileValue;
    context->tokenStream = std::move(savedStream);

    context->loadingModules.erase(canonical);
    context->loadedModules.insert(canonical);
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

    if (knownTypes.count(internalName(currentModule_, structDef->name)) > 0)
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

    knownTypes.insert(internalName(currentModule_, structDef->name));
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

    if (knownTypes.count(internalName(currentModule_, enumDef->name)) > 0)
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

    knownTypes.insert(internalName(currentModule_, enumDef->name));
    context->knownEnums.insert(internalName(currentModule_, enumDef->name));
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

    // P3: a trait name must be unique across ALL entities (structs, enums AND
    // traits) — the language's "single name, single entity" rule. The old check
    // looked only at knownTraits, which was never populated (dead set), so a
    // struct-then-trait or trait-then-trait name collision slipped through and
    // the two names silently collided in knownTypes. Check both sets so every
    // direction is caught with the same error. A snapshot/backtrack (matching
    // parseStructDefinition / parseEnumDefinition) keeps the token stream
    // aligned after the recoverable error — consuming UNDEFINED from the LIVE
    // position would skip the `{` and desync the rest of the trait body, which
    // crashed the downstream method/param parsing on garbage tokens.
    createSnapshot();
    traitDef->name = consume(TokenCode::IDENTIFIER, "expect an identifier as the trait name", E_ExpectAnIdentifier).value;

    // Note: generic params are parsed AFTER the duplicate-name check (below).
    // On the duplicate path backToSnapshot() rewinds to just before the name,
    // so parsing them here too would be redundant — parseGenericParams() is
    // pure token consumption with no side effects, and the single parse below
    // keeps the stream aligned for BOTH the clean and the recovered path.
    if (knownTraits.count(internalName(currentModule_, traitDef->name)) > 0 || knownTypes.count(internalName(currentModule_, traitDef->name)) > 0)
    {
        backToSnapshot();
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

    knownTypes.insert(internalName(currentModule_, traitDef->name));
    // P3: actually register the trait name so a later trait/struct/enum with the
    // same name is caught (previously this set was never written, making the
    // mutidefined check above a no-op).
    knownTraits.insert(internalName(currentModule_, traitDef->name));
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

    if (knownTypes.count(internalName(currentModule_, impl->structName)) == 0)
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

    // Array type `[T; N]`.
    if (match(TokenCode::LBRACKET))
    {
        type->isArray = true;
        type->elementType = parseType();
        consume(TokenCode::SEMI, "expect ';' in array type", E_ExpectedKeyword);
        if (currentToken().code == TokenCode::INT_LITERAL)
        {
            // A huge literal (e.g. `[i32; 99999999999999999999]`) makes stoll
            // throw std::out_of_range → std::terminate. Catch it and clamp to a
            // sentinel that the SEMANTIC analyzer's size check rejects with a
            // clean error. NOT logged here: a parse error makes Parser::run()
            // exit(1), killing the process before sema can report it (and the
            // test harness with it).
            try
            {
                type->arraySize = std::stoll(currentToken().value);
            }
            catch (const std::exception &)
            {
                type->arraySize = INT64_MAX; // sentinel > MAX_ARRAY_ELEMENTS
            }
            advance();
        }
        else
        {
            logError(currentToken(), "array size must be an integer literal", E_ExpectType);
            advance();
        }
        consume(TokenCode::RBRACKET, "expect ']' after array size", E_ExpectedKeyword);
        return type;
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

        // Module-qualified type: `m::Vec2` / `m::Option<i32>` where `m` is a
        // bound module alias. Bake the internal name into typeName.
        if (check(TokenCode::DOUBLE_COLON) && !resolveModuleAlias(type->typeName).empty())
        {
            std::string canonical = resolveModuleAlias(type->typeName);
            match(TokenCode::DOUBLE_COLON); // consume the '::'
            Token inner = consume(TokenCode::IDENTIFIER, "expected type name after '::'", E_ExpectAnIdentifier);
            type->typeName = internalName(canonical, inner.value);
        }

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

    // P6: create the self param (and its position recorder) ONLY when a self
    // actually follows. The old code created both unconditionally, then
    // (a) manually called ~PositionRecorder() on the stack object and let it
    // destruct a SECOND time at scope exit — after currentPos had moved past
    // the params, return type and body, so the self param's recorded span was
    // overwritten with the whole-function span (double-destruction UB), and
    // (b) in the non-self branch leaked the SelfParam via selfParam.release().
    // With the recorder scoped to the self branch, it destructs exactly once at
    // the end of the branch (after the params are parsed) with the correct span,
    // and a non-self function never allocates a SelfParam at all.
    // Use check() + match() so the recorder is constructed BEFORE the `self`
    // token is consumed: the recorder's recorded span therefore covers the
    // whole `self: &S` parameter (`self` keyword through the type), not just
    // the type annotation after the colon.
    if (check(TokenCode::SELF))
    {
        auto selfParam = std::make_unique<SelfParam>();
        PositionRecorder selfRecorder(this, selfParam.get());
        match(TokenCode::SELF);

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

    // 2026-08-12 (spec decision): default parameter values (`a: i32 = 5`) were
    // parsed then silently IGNORED by every later pass — dead syntax. Removed:
    // a trailing `= expr` is now a clean parse error instead.
    if (match(TokenCode::ASSIGN))
    {
        logError(currentToken(), "default parameter values are not supported", E_ExpectType);
        synchronize();
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

void Parser::parseAttribute()
{
    consume(TokenCode::ATTRIBUTE_START, "expected '#['", E_ExpectALBRACE);

    // Only `#[i_know = "..."]` is defined (2026-08-12 spec decision): it
    // relaxes integer-narrowing cast errors on the next statement to warnings.
    if (check(TokenCode::IDENTIFIER) && currentToken().value == "i_know")
    {
        advance();
        pendingIKnow_ = true;
        if (match(TokenCode::ASSIGN))
        {
            if (!check(TokenCode::STRING_LITERAL))
            {
                logError(currentToken(), "expected a string message in #[i_know = \"...\"]", E_ExpectedExpression);
            }
            else
            {
                advance(); // message content is informational only
            }
        }
    }
    else
    {
        logError(currentToken(), "unknown attribute; only #[i_know] is supported", E_UndefinedIdentifier);
    }

    consume(TokenCode::RBRACKET, "expected ']' to close attribute", E_ExpectARBRACE);
}

void Parser::applyIKnow(Expr *expr)
{
    if (!expr)
        return;

    if (auto cast = dynamic_cast<CastExpr *>(expr))
    {
        cast->iKnow = true;
        applyIKnow(cast->expression.get());
        return;
    }
    if (auto bin = dynamic_cast<BinaryOp *>(expr))
    {
        applyIKnow(bin->left.get());
        applyIKnow(bin->right.get());
        return;
    }
    if (auto paren = dynamic_cast<ParenExpr *>(expr))
    {
        applyIKnow(paren->expression.get());
        return;
    }
    if (auto call = dynamic_cast<FunctionCall *>(expr))
    {
        applyIKnow(call->function.get());
        for (auto &arg : call->arguments)
            applyIKnow(arg.get());
        return;
    }
    if (auto method = dynamic_cast<MemberFunctionCall *>(expr))
    {
        applyIKnow(method->object.get());
        for (auto &arg : method->arguments)
            applyIKnow(arg.get());
        return;
    }
    if (auto access = dynamic_cast<MemberAccess *>(expr))
    {
        applyIKnow(access->object.get());
        return;
    }
    if (auto index = dynamic_cast<IndexAccess *>(expr))
    {
        applyIKnow(index->object.get());
        applyIKnow(index->index.get());
        return;
    }
    if (auto init = dynamic_cast<StructInitExpr *>(expr))
    {
        for (auto &field : init->memberInits)
            applyIKnow(field.second.get());
        return;
    }
    if (auto arr = dynamic_cast<ArrayLiteral *>(expr))
    {
        for (auto &elem : arr->elements)
            applyIKnow(elem.get());
        return;
    }
    if (auto variant = dynamic_cast<VariantInitExpr *>(expr))
    {
        for (auto &arg : variant->arguments)
            applyIKnow(arg.get());
        return;
    }
}

std::unique_ptr<Stmt> Parser::parseStatement()
{
    // Attributes (`#[i_know = "..."]`) attach to the FOLLOWING statement:
    // parse the attribute, recurse for the statement, then mark its casts.
    if (check(TokenCode::ATTRIBUTE_START))
    {
        parseAttribute();
        auto stmt = parseStatement();
        if (pendingIKnow_)
        {
            pendingIKnow_ = false;
            if (auto exprStmt = dynamic_cast<ExprStmt *>(stmt.get()))
                applyIKnow(exprStmt->expression.get());
            else if (auto assign = dynamic_cast<AssignStmt *>(stmt.get()))
                applyIKnow(assign->value.get());
            else if (auto decl = dynamic_cast<DeclStmt *>(stmt.get()))
                applyIKnow(decl->initValue ? decl->initValue->get() : nullptr);
        }
        return stmt;
    }

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

        // Right side at the SAME precedence → equal-precedence operators are
        // left-associative (`104 - 104 + 40` = `(104 - 104) + 40`, not
        // `104 - (104 + 40)`). Only strictly-higher precedence binds tighter.
        auto right = parseBinaryExpression(precedence);

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
    // KNOWN LIMITATION (P13): there is no token for `^`, `<<`, or `>>` — the
    // lexer reports `^` as an unknown character and `<<`/`>>` lex as two separate
    // `<`/`>` tokens. Consequently a HIRBinaryOp with OpKind BitXor/ShiftLeft/
    // ShiftRight can never be produced from source syntax; those opKinds exist
    // only so the operator-overload traits BitXor/Shl/Shr can lower a struct's
    // `bitxor`/`shl`/`shr` method calls, and for the codegen's completeness.
    // This is deliberate — do not add precedence entries for operators the
    // lexer cannot tokenize (they would be dead code).
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

    // Array literal `[a, b, c]`.
    if (match(TokenCode::LBRACKET))
    {
        auto literal = std::make_unique<ArrayLiteral>();
        recorder.bindNode(literal.get());
        if (!check(TokenCode::RBRACKET))
        {
            do
            {
                literal->elements.push_back(parseExpression());
            } while (match(TokenCode::COMMA));
        }
        consume(TokenCode::RBRACKET, "expected ']' after array literal", E_ExpectedKeyword);
        return literal;
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
        if (check(TokenCode::LBRACE) && (knownTypes.count(internalName(currentModule_, identifier.value)) > 0 || !(inForIterable_ || inControlFlowCondition_)))
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

        // `m::X` where `m` is a bound module alias → module-qualified access.
        // This takes priority over enum-variant / static-call `::` so `m::Some`
        // (a variant in module m) and `m::Type::method` resolve against module m.
        if (check(TokenCode::DOUBLE_COLON) && moduleImports_.count(identifier.value))
        {
            auto expr = parseModuleQualified(identifier);
            recorder.bindNode(expr.get());
            return expr;
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

    // KNOWN LIMITATION (P14): unary operators are not implemented. A leading
    // `-`/`!`/`~` reaches here and is reported as "expected expression" (the
    // `-` token is otherwise only a binary minus). `!` is only meaningful in the
    // lexed `!=` pair. Negation must be written `0 - x` (see math.lis's
    // abs/fabs). Deliberate — do not add unary parsing without a language-level
    // decision to support it.
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

std::unique_ptr<Expr> Parser::parseModuleQualified(Token aliasToken)
{
    std::string canonical = resolveModuleAlias(aliasToken.value);
    match(TokenCode::DOUBLE_COLON);
    Token nameToken = consume(TokenCode::IDENTIFIER, "expected name after '::'", E_ExpectAnIdentifier);

    // Bake the module prefix into the name NOW (alias → canonical → internal).
    // The `$` rule guarantees this is never re-prefixed downstream.
    std::string internal = internalName(canonical, nameToken.value);
    Token internalToken = nameToken;
    internalToken.value = internal;

    // `m::A::B` — variant construction (`m::Option::Some`) or a static method
    // (`m::Type::method`). parseVariantInitialization reads A as the enum/type
    // and B as variant/method; HIRBuilder dispatches by knownEnums.
    if (check(TokenCode::DOUBLE_COLON))
        return parseVariantInitialization(internalToken);

    // `m::f<T>(...)` — generic function call (turbofish).
    if (check(TokenCode::LT) && looksLikeCallGenericParams())
        return parseFunctionCall(internalToken);

    // `m::f(...)` — function call.
    if (check(TokenCode::LPAREN))
    {
        match(TokenCode::LPAREN);
        return parseFunctionCall(internalToken);
    }

    // `m::Vec2 { ... }` — module-qualified struct literal.
    if (check(TokenCode::LBRACE))
    {
        match(TokenCode::LBRACE);
        return parseStructInitialization(internalToken);
    }

    // `m::GLOBAL` / `m::fn_ref` — a module symbol (global var / function value).
    auto id = std::make_unique<IdentifierExpr>();
    id->name = internal;
    id->position = nameToken.position;
    id->length = nameToken.value.length();
    return parseMemberAccessChain(std::move(id));
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
    while (true)
    {
        if (match(TokenCode::DOT))
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
        else if (match(TokenCode::LBRACKET))
        {
            // Array / pointer indexing: `a[i]`, `s.data[i]`.
            PositionRecorder recorder(this, nullptr);
            auto index = std::make_unique<IndexAccess>();
            recorder.bindNode(index.get());
            index->object = std::move(left);
            index->index = parseExpression();
            consume(TokenCode::RBRACKET, "expected ']' after index", E_ExpectedKeyword);
            left = std::move(index);
        }
        else
        {
            break;
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