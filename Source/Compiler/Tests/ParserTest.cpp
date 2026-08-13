/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include <gtest/gtest.h>

#include "Core/Context.hpp"
#include "Lexer/Lexer.hpp"
#include "Logger/Logger.hpp"
#include "Parser/ASTPrinter.hpp"
#include "Parser/Parser.hpp"

class ParserTest : public ::testing::Test
{
protected:
    std::shared_ptr<Context> context;

    void SetUp() override
    {
        context = std::make_shared<Context>();
    }

    void parseSource(const std::string &source)
    {
        context->fileValue = source;
        Lexer lexer(context);
        lexer.run();
        Parser parser(context);
        // parseAll() (not run()): run() calls exit(1) on any parse error, which
        // would kill the whole test process. Gate-free parseAll() lets the
        // parse-error regression tests below assert on the error count instead.
        parser.parseAll();
    }

    inline void checkPosition(ASTNode *node, size_t line, size_t col, size_t lineStart, size_t length)
    {
        EXPECT_EQ(node->position.line, line);
        EXPECT_EQ(node->position.col, col);
        EXPECT_EQ(node->position.lineStart, lineStart);
        EXPECT_EQ(node->length, length);
    }

    inline void checkTokenPosition(Token token, size_t line, size_t col, size_t lineStart, size_t length)
    {
        EXPECT_EQ(token.position.line, line);
        EXPECT_EQ(token.position.col, col);
        EXPECT_EQ(token.position.lineStart, lineStart);
        EXPECT_EQ(token.value.length(), length);
    }
};

// ── P3 regression: trait names must be unique (single name, single entity) ─────
// parseTraitDefinition used to check the never-populated `knownTraits` set and
// register the name only in `knownTypes`, so duplicate traits and
// struct-then-trait collisions were silently accepted.

TEST_F(ParserTest, DuplicateTraitRejected)
{
    parseSource("trait A { fn f(); } trait A { fn g(); }");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "duplicate trait name must be rejected";
}

TEST_F(ParserTest, TraitThenStructSameNameRejected)
{
    parseSource("trait A { fn f(); } struct A { x: i32 }");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "struct after trait with same name must be rejected";
}

TEST_F(ParserTest, StructThenTraitSameNameRejected)
{
    parseSource("struct A { x: i32 } trait A { fn f(); }");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "trait after struct with same name must be rejected";
}

TEST_F(ParserTest, DistinctTraitNamesAccepted)
{
    parseSource("trait A { fn f(); } trait B { fn g(); }");
    EXPECT_EQ(Logger::GetErrorCount(), 0) << "distinct trait names must parse cleanly";
}

// ── P6 regression: self-param span must NOT cover the whole function ───────────
// The old parseMemberFunctionDefinition created a stack PositionRecorder for the
// self param, manually called ~PositionRecorder() (double-destruction UB), and
// let the second (natural) destruction run after currentPos had passed the body
// — overwriting the self param's span to the whole function. This pins the
// fixed behavior: the span covers exactly `self: &S` (line 3, col 13, len 8).

TEST_F(ParserTest, MethodSelfParamPosition)
{
    std::string source =
        "struct S { v: i32 }\n"
        "impl S {\n"
        "    fn read(self: &S) -> i32 { ret self.v; }\n"
        "}";

    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 2);

    auto impl = dynamic_cast<StructImpl *>(globalStmts[1].get());
    ASSERT_NE(impl, nullptr);
    ASSERT_EQ(impl->methods.size(), 1);

    auto method = impl->methods[0].get();
    ASSERT_TRUE(method->selfParam.has_value());
    auto &selfParam = method->selfParam.value();

    // Line 1 = "struct S { v: i32 }\n" (19 chars incl. the space before `}`, +1
    // newline = 20), line 2 = "impl S {\n" (8 + 1 = 9), so line 3 starts at index
    // 29; `self` is col 13 and "self: &S" is 8 chars. The recorder is now built
    // before `self` is consumed so the span covers the whole self parameter.
    checkPosition(selfParam.get(), 3, 13, 29, 8);
    EXPECT_EQ(selfParam->isRef, true);
    EXPECT_EQ(selfParam->isMut, false);
}

// ── P15: `impt` (import) syntax ───────────────────────────────────────────────
// The three `impt` forms (whole-module, alias, selective) must parse into an
// ImportStmt AST node. Module LOADING is a later phase (ParserTest has no
// searchPaths), so only the syntax is exercised here.
TEST_F(ParserTest, ImptThreeFormsParse)
{
    parseSource("impt foo.bar;\n"
                "impt baz.qux as b;\n"
                "impt math { max, min };");
    EXPECT_EQ(Logger::GetErrorCount(), 0) << "all three impt forms must parse cleanly";

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 3);

    // impt foo.bar; — whole module, no alias, no symbols
    auto *i1 = dynamic_cast<ImportStmt *>(globalStmts[0].get());
    ASSERT_NE(i1, nullptr);
    ASSERT_NE(i1->modulePath, nullptr);
    ASSERT_EQ(i1->modulePath->pathSegments.size(), 2);
    EXPECT_EQ(i1->modulePath->pathSegments[0], "foo");
    EXPECT_EQ(i1->modulePath->pathSegments[1], "bar");
    EXPECT_FALSE(i1->alias.has_value());
    EXPECT_FALSE(i1->symbols.has_value());

    // impt baz.qux as b; — alias
    auto *i2 = dynamic_cast<ImportStmt *>(globalStmts[1].get());
    ASSERT_NE(i2, nullptr);
    ASSERT_NE(i2->modulePath, nullptr);
    ASSERT_EQ(i2->modulePath->pathSegments.size(), 2);
    EXPECT_EQ(i2->modulePath->pathSegments[0], "baz");
    EXPECT_EQ(i2->modulePath->pathSegments[1], "qux");
    ASSERT_TRUE(i2->alias.has_value());
    EXPECT_EQ(*i2->alias, "b");

    // impt math { max, min }; — selective
    auto *i3 = dynamic_cast<ImportStmt *>(globalStmts[2].get());
    ASSERT_NE(i3, nullptr);
    ASSERT_NE(i3->modulePath, nullptr);
    ASSERT_EQ(i3->modulePath->pathSegments.size(), 1);
    EXPECT_EQ(i3->modulePath->pathSegments[0], "math");
    ASSERT_TRUE(i3->symbols.has_value());
    ASSERT_EQ(i3->symbols->size(), 2);
    EXPECT_EQ((*i3->symbols)[0], "max");
    EXPECT_EQ((*i3->symbols)[1], "min");
}

TEST_F(ParserTest, ImptTrailingJunkRejected)
{
    // Missing semicolon.
    parseSource("impt foo.bar");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "impt requires a terminating ';'";

    context = std::make_shared<Context>();
    // Empty selective list.
    parseSource("impt math { };");
    EXPECT_GT(Logger::GetErrorCount(), 0) << "selective import needs at least one symbol";
}

TEST_F(ParserTest, GlobalVarDefPosition)
{
    std::string source = "let x = 42;";
    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto globalVar = dynamic_cast<GlobalVarDef *>(globalStmts[0].get());
    ASSERT_NE(globalVar, nullptr);

    // 检查全局变量定义位置
    checkPosition(globalVar, 1, 1, 0, source.size());

    // 检查标识符位置
    ASSERT_NE(globalVar->initValue.get(), nullptr);
    checkPosition(globalVar->initValue.get(), 1, 9, 0, 2);
}

TEST_F(ParserTest, StructDefPosition)
{
    std::string source =
        "struct Point {\n"
        "  pub x: i32,\n"
        "  y: f64\n"
        "}";

    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto structDef = dynamic_cast<StructDef *>(globalStmts[0].get());
    ASSERT_NE(structDef, nullptr);

    // 检查结构体定义位置
    checkPosition(structDef, 1, 1, 0, source.size());

    // 检查成员位置
    ASSERT_EQ(structDef->members.size(), 2);
    checkPosition(structDef->members[0].get(), 2, 3, 15, 11);
    checkPosition(structDef->members[1].get(), 3, 3, 29, 6);
}

TEST_F(ParserTest, FunctionDefPosition)
{
    std::string source =
        "fn add(a: i32, b: i32) -> i32 {\n"
        "  ret a + b;\n"
        "}";

    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto funcDef = dynamic_cast<FunctionDef *>(globalStmts[0].get());
    ASSERT_NE(funcDef, nullptr);

    // 检查函数定义位置
    checkPosition(funcDef, 1, 1, 0, source.size());

    // 检查参数位置
    ASSERT_EQ(funcDef->params.size(), 2);
    checkPosition(funcDef->params[0].get(), 1, 8, 0, 6);
    checkPosition(funcDef->params[1].get(), 1, 16, 0, 6);

    // 检查返回类型位置
    ASSERT_NE(funcDef->returnType, nullptr);
    checkPosition(funcDef->returnType.value().get(), 1, 27, 0, 3);

    // 检查函数体位置
    auto body = dynamic_cast<CompoundStmt *>(funcDef->body.get());
    ASSERT_NE(body, nullptr);
    checkPosition(body, 1, 31, 0, source.size() - 30);

    // 检查返回语句位置
    auto returnStmt = dynamic_cast<ReturnStmt *>(body->statements[0].get());
    ASSERT_NE(returnStmt, nullptr);
    checkPosition(returnStmt, 2, 3, 32, 10);
}

TEST_F(ParserTest, BinaryExpressionPosition)
{
    std::string source = "let result = (a + b) * c;";
    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto globalVar = dynamic_cast<GlobalVarDef *>(globalStmts[0].get());
    ASSERT_NE(globalVar, nullptr);

    // 检查二元表达式位置
    auto binaryOp = dynamic_cast<BinaryOp *>(globalVar->initValue.get());
    ASSERT_NE(binaryOp, nullptr);
    checkPosition(binaryOp, 1, 14, 0, 11);

    // 检查括号表达式位置
    auto parenExpr = dynamic_cast<ParenExpr *>(binaryOp->left.get());
    ASSERT_NE(parenExpr, nullptr);
    checkPosition(parenExpr, 1, 14, 0, 7);

    // 检查加法操作位置
    auto addOp = dynamic_cast<BinaryOp *>(parenExpr->expression.get());
    ASSERT_NE(addOp, nullptr);
    checkPosition(addOp, 1, 15, 0, 5);

    // 检查标识符位置
    auto idA = dynamic_cast<IdentifierExpr *>(addOp->left.get());
    ASSERT_NE(idA, nullptr);
    checkPosition(idA, 1, 15, 0, 1);

    auto idB = dynamic_cast<IdentifierExpr *>(addOp->right.get());
    ASSERT_NE(idB, nullptr);
    checkPosition(idB, 1, 19, 0, 1);

    // 检查乘法右侧标识符位置
    auto idC = dynamic_cast<IdentifierExpr *>(binaryOp->right.get());
    ASSERT_NE(idC, nullptr);
    checkPosition(idC, 1, 24, 0, 1);
}

TEST_F(ParserTest, FunctionCallPosition)
{
    std::string source = "let value = calculate(a, b + c);";
    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto globalVar = dynamic_cast<GlobalVarDef *>(globalStmts[0].get());
    ASSERT_NE(globalVar, nullptr);

    // 检查函数调用位置
    auto funcCall = dynamic_cast<FunctionCall *>(globalVar->initValue.get());
    ASSERT_NE(funcCall, nullptr);
    checkPosition(funcCall, 1, 13, 0, 19);

    // 检查函数名位置
    auto funcId = dynamic_cast<IdentifierExpr *>(funcCall->function.get());
    ASSERT_NE(funcId, nullptr);
    checkPosition(funcId, 1, 13, 0, 9);

    // 检查参数位置
    ASSERT_EQ(funcCall->arguments.size(), 2);

    // 第一个参数 (a)
    auto arg1 = dynamic_cast<IdentifierExpr *>(funcCall->arguments[0].get());
    ASSERT_NE(arg1, nullptr);
    checkPosition(arg1, 1, 23, 0, 1);

    // 第二个参数 (b + c)
    auto arg2 = dynamic_cast<BinaryOp *>(funcCall->arguments[1].get());
    ASSERT_NE(arg2, nullptr);
    checkPosition(arg2, 1, 26, 0, 5);
}

TEST_F(ParserTest, MemberAccessPosition)
{
    std::string source = "let len = point.distance().length;";
    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto globalVar = dynamic_cast<GlobalVarDef *>(globalStmts[0].get());
    ASSERT_NE(globalVar, nullptr);

    // 检查成员访问链位置
    auto memberAccess = dynamic_cast<MemberAccess *>(globalVar->initValue.get());
    ASSERT_NE(memberAccess, nullptr);
    checkPosition(memberAccess, 1, 11, 0, 23);

    // 检查成员函数调用位置
    auto funcCall = dynamic_cast<MemberFunctionCall *>(memberAccess->object.get());
    ASSERT_NE(funcCall, nullptr);
    checkPosition(funcCall, 1, 17, 0, 10);

    // 检查对象位置
    auto objId = dynamic_cast<IdentifierExpr *>(funcCall->object.get());
    ASSERT_NE(objId, nullptr);
    checkPosition(objId, 1, 11, 0, 23);
}

TEST_F(ParserTest, MultiLineExpressionPosition)
{
    std::string source =
        "let total = a +\n"
        "            b *\n"
        "            c;";

    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto globalVar = dynamic_cast<GlobalVarDef *>(globalStmts[0].get());
    ASSERT_NE(globalVar, nullptr);

    // 检查加法操作位置 (跨行)
    auto addOp = dynamic_cast<BinaryOp *>(globalVar->initValue.get());
    ASSERT_NE(addOp, nullptr);
    checkPosition(addOp, 1, 13, 0, source.size() - 13);

    // 检查乘法操作位置 (跨行)
    auto multOp = dynamic_cast<BinaryOp *>(addOp->right.get());
    ASSERT_NE(multOp, nullptr);
    checkPosition(multOp, 2, 13, 16, 17);

    // 检查标识符位置 (不同行)
    auto idA = dynamic_cast<IdentifierExpr *>(addOp->left.get());
    ASSERT_NE(idA, nullptr);
    checkPosition(idA, 1, 13, 0, 1);

    auto idB = dynamic_cast<IdentifierExpr *>(multOp->left.get());
    ASSERT_NE(idB, nullptr);
    checkPosition(idB, 2, 13, 16, 1);

    auto idC = dynamic_cast<IdentifierExpr *>(multOp->right.get());
    ASSERT_NE(idC, nullptr);
    checkPosition(idC, 3, 13, 32, 1);
}

TEST_F(ParserTest, StructInitializationPosition)
{
    std::string source =
        "let point = Point {\n"
        "  x: 10,\n"
        "  y: 20\n"
        "};";

    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto globalVar = dynamic_cast<GlobalVarDef *>(globalStmts[0].get());
    ASSERT_NE(globalVar, nullptr);

    // 检查结构体初始化位置
    auto structInit = dynamic_cast<StructInitExpr *>(globalVar->initValue.get());
    ASSERT_NE(structInit, nullptr);
    checkPosition(structInit, 1, 13, 0, source.size() - 13);

    // 检查类型位置
    auto type = structInit->structType.get();
    ASSERT_NE(type, nullptr);
    checkPosition(type, 1, 13, 0, 5);

    // 检查成员初始化位置
    ASSERT_EQ(structInit->memberInits.size(), 2);

    // 第一个成员初始化
    auto &xInit = structInit->memberInits[0];
    auto xLiteral = dynamic_cast<LiteralExpr *>(xInit.second.get());
    ASSERT_NE(xLiteral, nullptr);
    checkPosition(xLiteral, 2, 6, 20, 2);

    // 第二个成员初始化
    auto &yInit = structInit->memberInits[1];
    auto yLiteral = dynamic_cast<LiteralExpr *>(yInit.second.get());
    ASSERT_NE(yLiteral, nullptr);
    checkPosition(yLiteral, 3, 6, 29, 2);
}

TEST_F(ParserTest, ComplexExpressionPosition)
{
    std::string source =
        "let result = (a + b) * c - d / e;";

    parseSource(source);

    auto &globalStmts = context->program.globalStatements;
    ASSERT_EQ(globalStmts.size(), 1);

    auto globalVar = dynamic_cast<GlobalVarDef *>(globalStmts[0].get());
    ASSERT_NE(globalVar, nullptr);

    // 检查整个表达式位置
    auto expr = globalVar->initValue.get();
    checkPosition(expr, 1, 14, 0, 19);

    // 分解表达式树
    auto subtractOp = dynamic_cast<BinaryOp *>(expr);
    ASSERT_NE(subtractOp, nullptr);

    auto multiplyOp = dynamic_cast<BinaryOp *>(subtractOp->left.get());
    ASSERT_NE(multiplyOp, nullptr);

    auto divideOp = dynamic_cast<BinaryOp *>(subtractOp->right.get());
    ASSERT_NE(divideOp, nullptr);

    auto pattenOp = dynamic_cast<ParenExpr *>(multiplyOp->left.get());
    ASSERT_NE(pattenOp, nullptr);

    auto addOp = dynamic_cast<BinaryOp *>(pattenOp->expression.get());
    ASSERT_NE(addOp, nullptr);

    // 检查各个操作位置
    checkPosition(subtractOp, 1, 14, 0, 19);
    checkPosition(multiplyOp, 1, 14, 0, 11);
    checkPosition(divideOp, 1, 28, 0, 5);
    checkPosition(addOp, 1, 15, 0, 5);

    // 检查标识符位置
    auto idA = dynamic_cast<IdentifierExpr *>(addOp->left.get());
    ASSERT_NE(idA, nullptr);
    checkPosition(idA, 1, 15, 0, 1);

    auto idB = dynamic_cast<IdentifierExpr *>(addOp->right.get());
    ASSERT_NE(idB, nullptr);
    checkPosition(idB, 1, 19, 0, 1);

    auto idC = dynamic_cast<IdentifierExpr *>(multiplyOp->right.get());
    ASSERT_NE(idC, nullptr);
    checkPosition(idC, 1, 24, 0, 1);

    auto idD = dynamic_cast<IdentifierExpr *>(divideOp->left.get());
    ASSERT_NE(idD, nullptr);
    checkPosition(idD, 1, 28, 0, 1);

    auto idE = dynamic_cast<IdentifierExpr *>(divideOp->right.get());
    ASSERT_NE(idE, nullptr);
    checkPosition(idE, 1, 32, 0, 1);
}

// ── D: valid structures parse cleanly (0 errors) ───────────────────────────────

TEST_F(ParserTest, ValidStructDefinition)
{
    parseSource("struct S { a: i32, b: f64 }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
    auto &stmts = context->program.globalStatements;
    ASSERT_EQ(stmts.size(), 1);
    EXPECT_NE(dynamic_cast<StructDef *>(stmts[0].get()), nullptr);
}

TEST_F(ParserTest, ValidEnumDefinition)
{
    parseSource("enum E { A, B(i32), C(f64, f64) }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
    auto &stmts = context->program.globalStatements;
    ASSERT_EQ(stmts.size(), 1);
    EXPECT_NE(dynamic_cast<EnumDef *>(stmts[0].get()), nullptr);
}

TEST_F(ParserTest, ValidImplBlock)
{
    parseSource("struct S { v: i32 } impl S { fn get(self: &S) -> i32 { ret self.v; } }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
    auto &stmts = context->program.globalStatements;
    ASSERT_EQ(stmts.size(), 2);
    EXPECT_NE(dynamic_cast<StructImpl *>(stmts[1].get()), nullptr);
}

TEST_F(ParserTest, ValidTraitDefinition)
{
    parseSource("trait T { fn f(self: &Self) -> i32; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidGenericFunction)
{
    parseSource("fn id<T>(x: T) -> T { ret x; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidGenericStruct)
{
    parseSource("struct Box<T> { v: T }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidGenericEnum)
{
    parseSource("enum Option<T> { Some(T), None }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidIfElse)
{
    parseSource("fn main() -> i32 { if true { ret 1; } else { ret 2; } ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidNestedControlFlow)
{
    parseSource("fn main() -> i32 { if true { if true { ret 1; } } ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidWhileLoop)
{
    parseSource("fn main() -> i32 { let mut i = 0; while i < 3 { i = i + 1; } ret i; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidForLoop)
{
    parseSource("fn main() -> i32 { let mut s = 0; for x in range(1, 5) { s = s + x; } ret s; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMatchExpression)
{
    parseSource("enum E { A, B } fn main() -> i32 { let e = E::A;"
                " match e { A => { ret 0; }, B => { ret 1; } } }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMatchValueArm)
{
    parseSource("enum E { A, B } fn main() -> i32 { let e = E::A;"
                " let y = match e { A => 1, B => 2 }; ret y; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidArrayLiteral)
{
    parseSource("fn main() -> i32 { let a = [1, 2, 3]; ret a[0]; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidArrayType)
{
    parseSource("let g: [i32; 3] = [1, 2, 3]; fn main() -> i32 { ret g[0]; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidGlobalVar)
{
    parseSource("let g = 5; fn main() -> i32 { ret g; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMultipleFunctions)
{
    parseSource("fn a() -> i32 { ret 1; } fn b() -> i32 { ret 2; }"
                " fn c() -> i32 { ret a() + b(); }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidOperatorChain)
{
    parseSource("fn main() -> i32 { ret 1 + 2 * 3 - 4 / 2; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidNestedStructLiteral)
{
    parseSource("struct A { x: i32 } struct B { a: A }"
                " fn main() -> i32 { let b = B { a: A { x: 1 } }; ret b.a.x; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidBreakContinue)
{
    parseSource("fn main() -> i32 { let mut i = 0; while i < 3 { i = i + 1;"
                " if i == 2 { continue; } if i == 3 { break; } } ret i; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidTraitWithGenericParam)
{
    parseSource("trait Iterator<T> { fn next(self: &mut Self) -> Option<T>; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidGlobalMutable)
{
    parseSource("let g = 1; fn main() -> i32 { g = g + 1; ret g; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMultiLineSource)
{
    parseSource("fn main() -> i32 {\n    let x = 1;\n    let y = 2;\n    ret x + y;\n}");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMethodWithMutSelf)
{
    parseSource("struct S { v: i32 } impl S { fn set(self: &mut S, d: i32) { self.v = d; } }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidArrayIndexExpr)
{
    parseSource("fn main() -> i32 { let a = [1, 2]; ret a[1]; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidCharStringMix)
{
    parseSource("fn main() -> i32 { let c = 'a'; let s = \"hi\"; ret c as i32; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidStructInitMemberAccess)
{
    parseSource("struct P { x: i32, y: i32 } fn main() -> i32 {"
                " let p = P { x: 1, y: 2 }; ret p.x + p.y; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidNestedIfElse)
{
    parseSource("fn main() -> i32 { if false { ret 1; } else if true { ret 2; }"
                " else { ret 3; } }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidNegativeNumberExpression)
{
    parseSource("fn main() -> i32 { ret 0 - 5; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

// ── D: malformed structures report errors (no crash) ───────────────────────────

TEST_F(ParserTest, MissingLBraceStruct)
{
    parseSource("struct S a: i32 }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingStructFieldType)
{
    parseSource("struct S { a: }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingRParenFunction)
{
    parseSource("fn f(a: i32 -> i32 { ret a; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingSemicolonLet)
{
    parseSource("fn main() -> i32 { let x = 5 ret x; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingTypeAnnotation)
{
    parseSource("fn f(x) -> i32 { ret x; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, DuplicateStructName)
{
    parseSource("struct S { a: i32 } struct S { b: i32 }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, DuplicateEnumName)
{
    parseSource("enum E { A } enum E { B }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, DuplicateFunctionNameNoParseError)
{
    // Duplicate function names are NOT a parse-time error — the semantic
    // analyzer rejects them (single-name rule). Parser must stay gate-clean.
    parseSource("fn f() -> i32 { ret 1; } fn f() -> i32 { ret 2; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, StructThenEnumSameName)
{
    parseSource("struct S { a: i32 } enum S { A }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, EnumThenStructSameName)
{
    parseSource("enum E { A } struct E { a: i32 }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingOperandInExpression)
{
    parseSource("fn main() -> i32 { ret 1 +; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingFunctionName)
{
    parseSource("fn () -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingCommaInParams)
{
    parseSource("fn f(a: i32 b: i32) -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingStructFieldColon)
{
    parseSource("struct S { a i32 }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingMatchArrow)
{
    parseSource("enum E { A } fn main() -> i32 { let e = E::A; match e { A { ret 0; } } }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingWhileCondition)
{
    parseSource("fn main() -> i32 { while { i = 1; } ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, TrailingGarbageStatement)
{
    parseSource("fn main() -> i32 { ret 0; } @@@");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, EmptyMatchArmsParseAccept)
{
    // An empty `match { }` is a PARSE-time no-op — the semantic analyzer rejects
    // it as non-exhaustive (E5004). The parser must stay gate-clean.
    parseSource("enum E { A } fn main() -> i32 { let e = E::A; match e { } ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

// NOTE: `for x in range(1, 5) }` (missing body) triggers a heap-corruption crash
// in the parser's error-recovery path (SIGTRAP in a TypeNode destructor during
// free). Reported separately — do not add a ParserTest for this input until the
// parser's placeholder/synchronize path for a missing for-body is fixed.

TEST_F(ParserTest, UnbalancedParens)
{
    parseSource("fn main() -> i32 { let x = (1 + 2; ret x; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, StrayRBraceAtTopLevel)
{
    parseSource("} fn main() -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

// ── D: AST structure assertions ────────────────────────────────────────────────

TEST_F(ParserTest, FunctionDefHasBodyAndParams)
{
    parseSource("fn add(a: i32, b: i32) -> i32 { ret a + b; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "add");
    EXPECT_EQ(func->params.size(), 2);
    EXPECT_TRUE(func->returnType.has_value());
    EXPECT_NE(func->body, nullptr);
}

TEST_F(ParserTest, StructDefHasMembers)
{
    parseSource("struct S { a: i32, b: f64 }");
    auto def = dynamic_cast<StructDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->name, "S");
    EXPECT_EQ(def->members.size(), 2);
}

TEST_F(ParserTest, EnumDefHasVariants)
{
    parseSource("enum E { A, B(i32), C(f64, f64) }");
    auto def = dynamic_cast<EnumDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->name, "E");
    EXPECT_EQ(def->variants.size(), 3);
}

TEST_F(ParserTest, IfStmtHasBothBranches)
{
    parseSource("fn main() -> i32 { if true { ret 1; } else { ret 2; } ret 0; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(func, nullptr);
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    ASSERT_NE(body, nullptr);
    auto ifStmt = dynamic_cast<IfStmt *>(body->statements[0].get());
    ASSERT_NE(ifStmt, nullptr);
    EXPECT_TRUE(ifStmt->elseBranch.has_value());
}

TEST_F(ParserTest, IfStmtNoElseHasNoElseBranch)
{
    parseSource("fn main() -> i32 { if true { ret 1; } ret 0; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto ifStmt = dynamic_cast<IfStmt *>(body->statements[0].get());
    ASSERT_NE(ifStmt, nullptr);
    EXPECT_FALSE(ifStmt->elseBranch.has_value());
}

TEST_F(ParserTest, GenericStructHasParams)
{
    parseSource("struct Box<T, U> { a: T, b: U }");
    auto def = dynamic_cast<StructDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->genericParams.size(), 2);
}

TEST_F(ParserTest, MatchHasArms)
{
    parseSource("enum E { A, B } fn main() -> i32 { let e = E::A;"
                " let y = match e { A => 1, B => 2 }; ret y; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[1].get());
    ASSERT_NE(func, nullptr);
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    ASSERT_NE(body, nullptr);
    auto decl = dynamic_cast<DeclStmt *>(body->statements[1].get());
    ASSERT_NE(decl, nullptr);
    auto init = decl->initValue.has_value() ? decl->initValue.value().get() : nullptr;
    auto matchExpr = dynamic_cast<MatchExpr *>(init);
    ASSERT_NE(matchExpr, nullptr);
    EXPECT_EQ(matchExpr->arms.size(), 2);
}

TEST_F(ParserTest, FunctionCallHasArgs)
{
    parseSource("fn main() -> i32 { ret foo(1, 2, 3); }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto retStmt = dynamic_cast<ReturnStmt *>(body->statements[0].get());
    ASSERT_NE(retStmt, nullptr);
    auto call = dynamic_cast<FunctionCall *>(retStmt->returnValue.value().get());
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->arguments.size(), 3);
}

TEST_F(ParserTest, WhileHasConditionAndBody)
{
    parseSource("fn main() -> i32 { while i < 3 { i = i + 1; } ret 0; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto whileStmt = dynamic_cast<WhileStmt *>(body->statements[0].get());
    ASSERT_NE(whileStmt, nullptr);
    EXPECT_NE(whileStmt->condition, nullptr);
    EXPECT_NE(whileStmt->body, nullptr);
}

TEST_F(ParserTest, BinaryOpIsLeftAssociative)
{
    parseSource("let r = a - b - c;");
    auto gv = dynamic_cast<GlobalVarDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(gv, nullptr);
    auto outer = dynamic_cast<BinaryOp *>(gv->initValue.get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, "-");
    // Left-assoc: (a - b) - c, so the LEFT child is the inner subtraction.
    auto inner = dynamic_cast<BinaryOp *>(outer->left.get());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->op, "-");
}

TEST_F(ParserTest, StructInitHasMembers)
{
    parseSource("struct P { x: i32, y: i32 } fn main() -> i32 { let p = P { x: 1, y: 2 }; ret 0; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[1].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto decl = dynamic_cast<DeclStmt *>(body->statements[0].get());
    auto init = decl->initValue.has_value() ? decl->initValue.value().get() : nullptr;
    auto initStruct = dynamic_cast<StructInitExpr *>(init);
    ASSERT_NE(initStruct, nullptr);
    EXPECT_EQ(initStruct->memberInits.size(), 2);
}

// ── D2: more valid structures ──────────────────────────────────────────────────

TEST_F(ParserTest, ValidStructWithReferenceField)
{
    parseSource("struct S { p: &i32 } fn main() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidArrayOfArraysType)
{
    parseSource("fn main() -> i32 { let a = [[1, 2], [3, 4]]; ret a[0][0]; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidReturnNoValue)
{
    parseSource("fn f() { ret; } fn main() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidBareReturnInVoid)
{
    parseSource("fn main() { ret; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidGenericTraitImpl)
{
    parseSource("trait T<T> { fn get(self: &Self) -> T; }"
                " struct S { v: i32 } impl T<i32> for S { fn get(self: &S) -> i32 { ret self.v; } }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidCallGenericSyntax)
{
    parseSource("fn id<T>(x: T) -> T { ret x; } fn main() -> i32 { ret id(5); }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidDeepMemberChain)
{
    parseSource("struct A { x: i32 } struct B { a: A } fn main() -> i32 {"
                " let b = B { a: A { x: 1 } }; ret b.a.x; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidConditionalBorrowExpression)
{
    parseSource("struct S { v: i32 } fn main() -> i32 { let s = S { v: 1 };"
                " let r = &s; ret r.v; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMutBorrowExpression)
{
    parseSource("struct S { v: i32 } fn main() -> i32 { let mut s = S { v: 1 };"
                " let r = &mut s; r.v = 2; ret r.v; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMultipleGlobalsAndFunctions)
{
    parseSource("let a = 1; let b = 2; fn f() -> i32 { ret a + b; }"
                " fn main() -> i32 { ret f(); }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidDerefViaIndex)
{
    parseSource("fn main() -> i32 { let a = [1, 2]; let r = &a; ret r[1]; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

// ── D2: more malformed structures ──────────────────────────────────────────────

TEST_F(ParserTest, UnclosedEnumToleratedAtEof)
{
    // An unclosed enum auto-closes at EOF (like an unclosed struct) — no parse
    // error; the sema is responsible for rejecting a half-defined enum.
    parseSource("enum E { A ");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingStructName)
{
    parseSource("struct { a: i32 }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingEnumVariantName)
{
    parseSource("enum E { , B }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingFunctionReturnArrow)
{
    parseSource("fn f() i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingIfCondition
)
{
    parseSource("fn main() -> i32 { if { ret 1; } ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingLoopVariable
)
{
    parseSource("fn main() -> i32 { for in range(1, 5) { ret 0; } }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingInKeyword
)
{
    parseSource("fn main() -> i32 { for x range(1, 5) { ret 0; } }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, StructSelfReferenceNoParseError
)
{
    // A recursive struct field parses fine; the sema rejects it later.
    parseSource("struct S { next: S } fn main() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, EnumIncompletePayload
)
{
    parseSource("enum E { A(i32, ) }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingGlobalInit
)
{
    parseSource("let g; fn main() -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, TwoTypesForParam
)
{
    parseSource("fn f(x: i32 i32) -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, EmptyParensParam
)
{
    parseSource("fn f(()) -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, TrailingCommaInArgs
)
{
    parseSource("fn main() -> i32 { ret f(1, 2,); }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

// ── D2: more AST structure ─────────────────────────────────────────────────────

TEST_F(ParserTest, EnumVariantPayloadCount)
{
    parseSource("enum E { A, B(i32), C(i32, i32, i32) }");
    auto def = dynamic_cast<EnumDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->variants[0]->payloadTypes.size(), 0);
    EXPECT_EQ(def->variants[1]->payloadTypes.size(), 1);
    EXPECT_EQ(def->variants[2]->payloadTypes.size(), 3);
}

TEST_F(ParserTest, StructImplHasMethods
)
{
    parseSource("struct S { v: i32 } impl S { fn a(self: &S) -> i32 { ret 0; }"
                " fn b(self: &S) -> i32 { ret 0; } }");
    auto impl = dynamic_cast<StructImpl *>(context->program.globalStatements[1].get());
    ASSERT_NE(impl, nullptr);
    EXPECT_EQ(impl->methods.size(), 2);
}

TEST_F(ParserTest, FunctionGenericParamsCount
)
{
    parseSource("fn f<T, U>(a: T, b: U) -> T { ret a; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->genericParams.size(), 2);
}

TEST_F(ParserTest, MatchArmBindingCount
)
{
    parseSource("enum E { P(i32, i32) } fn main() -> i32 { let e = E::P(1, 2);"
                " let y = match e { P(a, b) => a + b }; ret y; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[1].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto decl = dynamic_cast<DeclStmt *>(body->statements[1].get());
    auto init = decl->initValue.value().get();
    auto matchExpr = dynamic_cast<MatchExpr *>(init);
    ASSERT_NE(matchExpr, nullptr);
    EXPECT_EQ(matchExpr->arms[0]->pattern->bindings.size(), 2);
}

TEST_F(ParserTest, ArrayLiteralElementCount
)
{
    parseSource("fn main() -> i32 { let a = [1, 2, 3, 4]; ret 0; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto decl = dynamic_cast<DeclStmt *>(body->statements[0].get());
    auto init = decl->initValue.value().get();
    auto arr = dynamic_cast<ArrayLiteral *>(init);
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->elements.size(), 4);
}

TEST_F(ParserTest, ReturnStatementHasValue
)
{
    parseSource("fn main() -> i32 { ret 42; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto ret = dynamic_cast<ReturnStmt *>(body->statements[0].get());
    ASSERT_NE(ret, nullptr);
    EXPECT_TRUE(ret->returnValue.has_value());
}

TEST_F(ParserTest, ReturnStatementNoValue
)
{
    parseSource("fn main() { ret; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto ret = dynamic_cast<ReturnStmt *>(body->statements[0].get());
    ASSERT_NE(ret, nullptr);
    EXPECT_FALSE(ret->returnValue.has_value());
}

TEST_F(ParserTest, AssignStatementTarget
)
{
    parseSource("fn main() -> i32 { x = 5; ret 0; }");
    auto func = dynamic_cast<FunctionDef *>(context->program.globalStatements[0].get());
    auto body = dynamic_cast<CompoundStmt *>(func->body.get());
    auto assign = dynamic_cast<AssignStmt *>(body->statements[0].get());
    ASSERT_NE(assign, nullptr);
    EXPECT_NE(assign->target, nullptr);
    EXPECT_NE(assign->value, nullptr);
}

TEST_F(ParserTest, BreakContinueInLoopsParse
)
{
    parseSource("fn main() -> i32 { while true { break; continue; } ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, NestedStructLiteralInInit
)
{
    parseSource("struct A { x: i32 } fn main() -> i32 { let a = A { x: A { x: 1 }.x }; ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

// ── D3: final parser breadth ───────────────────────────────────────────────────

TEST_F(ParserTest, ValidMutBorrowInStructFieldType)
{
    parseSource("struct S { p: &mut i32 } fn main() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidEmptyImpl)
{
    parseSource("struct S { v: i32 } impl S { } fn main() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidEmptyTrait)
{
    parseSource("trait Marker { } fn main() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidEnumWithTrailingComma
)
{
    parseSource("enum E { A, B, } fn main() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidCallOnMemberAccess
)
{
    parseSource("struct S { v: i32 } impl S { fn get(self: &S) -> i32 { ret self.v; } }"
                " fn main() -> i32 { let s = S { v: 1 }; ret s.get(); }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidFunctionWithManyParams
)
{
    parseSource("fn f(a: i32, b: i32, c: i32, d: i32) -> i32 { ret a + b + c + d; }"
                " fn main() -> i32 { ret f(1, 2, 3, 4); }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidChainedCalls
)
{
    parseSource("fn f(x: i32) -> i32 { ret x; } fn main() -> i32 { ret f(f(1)); }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidArithmeticWithAllOperators
)
{
    parseSource("fn main() -> i32 { ret 1 + 2 - 3 * 4 / 5 % 6; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingDotInMemberAccess
)
{
    parseSource("struct S { v: i32 } fn main() -> i32 { let s = S { v: 1 }; ret sv; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingMatchScrutinee
)
{
    parseSource("enum E { A } fn main() -> i32 { match { A => 1 } ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, UnexpectedKeywordAsStatement
)
{
    parseSource("fn main() -> i32 { struct x; ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, MissingParamType
)
{
    parseSource("fn f(x:) -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, EmptyFunctionParamList
)
{
    parseSource("fn f() -> i32 { ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidMultipleMethodsInImpl
)
{
    parseSource("struct S { v: i32 } impl S { fn a(self: &S) -> i32 { ret self.v; }"
                " fn b(self: &mut S, x: i32) { self.v = x; } }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidNestedMatch
)
{
    parseSource("enum A { X(i32) } enum B { M(i32) } fn main() -> i32 {"
                " let a = A::X(1); match a { X(v) => { match B::M(v) { M(w) => { ret w; } } } } }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

// NOTE: `impl S { fn a(self: &S) -> i32 ` (method with no body at EOF) has
// ambiguous error-recovery behavior (the ParserTest harness reports 0 errors
// while the full pipeline's run() gate treats it as failing) — not pinned here.

TEST_F(ParserTest, StraySemicolonAfterFunction
)
{
    // A `;` between top-level definitions is an illegal global statement.
    parseSource("fn f() -> i32 { ret 0; }; fn main() -> i32 { ret 0; }");
    EXPECT_GT(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidBooleanLiteralsInExpr
)
{
    parseSource("fn main() -> i32 { if true && false { ret 1; } ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidIndexExpressionWithVariable
)
{
    parseSource("fn main() -> i32 { let a = [1, 2]; let i = 0; ret a[i]; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}

TEST_F(ParserTest, ValidBorrowOfCallResult
)
{
    parseSource("fn make() -> i32 { ret 5; } fn main() -> i32 { let r = &make(); ret 0; }");
    EXPECT_EQ(Logger::GetErrorCount(), 0);
}