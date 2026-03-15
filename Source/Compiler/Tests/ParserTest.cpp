/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include <gtest/gtest.h>

#include "Core/Context.hpp"
#include "Lexer/Lexer.hpp"
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
        parser.run();
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