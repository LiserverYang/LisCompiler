/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Analysiser/Scope.hpp"
#include "Analysiser/Symbol.hpp"
#include "Analysiser/Type.hpp"
#include "Core/SourcePosition.hpp"

class HIRNode
{
public:
    SourcePosition position;
    size_t length;
    virtual ~HIRNode() = default;
};

class HIRExpr : public HIRNode
{
public:
    std::shared_ptr<Type> type;
};

class HIRStmt : public HIRNode
{
};

class HIRProgram : public HIRNode
{
public:
    std::vector<std::unique_ptr<HIRNode>> items;
};

class HIRNameRef : public HIRExpr
{
public:
    std::string name;
    Symbol *symbol;
    std::shared_ptr<Scope> scope;
};

class HIRLiteral : public HIRExpr
{
public:
    enum class Kind
    {
        Int,
        Float,
        String,
        Bool,
        Char
    };
    Kind kind;
    std::variant<int64_t, double, std::string, bool, char> value;
};

class HIRBinaryOp : public HIRExpr
{
public:
    enum class OpKind
    {
        // 算术运算
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        // 比较运算
        Eq,
        Ne,
        Lt,
        Gt,
        Le,
        Ge,
        // 逻辑运算
        And,
        Or,
        // 位运算
        BitAnd,
        BitOr,
        BitXor,
        ShiftLeft,
        ShiftRight
    };

    std::unique_ptr<HIRExpr> left;
    std::unique_ptr<HIRExpr> right;
    OpKind opKind;
};

class HIRCast : public HIRExpr
{
public:
    std::unique_ptr<HIRExpr> expr;
    std::shared_ptr<Type> targetType;
};

class HIRCall : public HIRExpr
{
public:
    std::unique_ptr<HIRExpr> callee;            // 被调用者
    std::vector<std::unique_ptr<HIRExpr>> args; // 实参
};

class HIRMemberAccess : public HIRExpr
{
public:
    std::unique_ptr<HIRExpr> object; // 被访问的对象
    std::string memberName;          // 成员名称
    Symbol *memberSymbol;            // 成员符号（来自符号表）
};

class HIRStructInit : public HIRExpr
{
public:
    Symbol *structSymbol;                                                  // 结构体符号
    std::vector<std::pair<std::string, std::unique_ptr<HIRExpr>>> members; // 成员初始化
};

class HIRRef : public HIRExpr
{
public:
    std::unique_ptr<HIRExpr> expr;
    bool isMutable;
};

class HIRBlock : public HIRStmt
{
public:
    std::vector<std::unique_ptr<HIRStmt>> stmts;
    std::shared_ptr<Scope> scope; // 块所属作用域
};

class HIRVarDecl : public HIRStmt
{
public:
    std::string name;                             // 变量名
    std::shared_ptr<Type> type;                   // 变量类型（语义类型）
    std::optional<std::unique_ptr<HIRExpr>> init; // 初始化值
    bool isMutable;                               // 是否可变
    bool isGlobal;                                // 是否是全局变量
    Symbol *varSymbol;                            // 变量符号
};

class HIRAssign : public HIRStmt
{
public:
    std::unique_ptr<HIRExpr> target; // 赋值目标（必须是左值）
    std::unique_ptr<HIRExpr> value;  // 赋值值
};

class HIRIf : public HIRStmt
{
public:
    std::unique_ptr<HIRExpr> cond;                      // 条件表达式
    std::unique_ptr<HIRBlock> thenBlock;                // then 分支
    std::optional<std::unique_ptr<HIRBlock>> elseBlock; // else 分支
};

class HIRLoop : public HIRStmt
{
public:
    enum class Kind
    {
        While,
        For
    };
    Kind kind;

    // While 循环：条件表达式
    // For 循环：迭代器表达式 + 循环变量
    std::optional<std::unique_ptr<HIRExpr>> cond; // While 条件 / For 迭代器
    // std::optional<std::pair<std::string, Type>> loopVar; // For 循环变量（名称+类型）
    std::unique_ptr<HIRBlock> body; // 循环体
};

class HIRReturn : public HIRStmt
{
public:
    std::optional<std::unique_ptr<HIRExpr>> value; // 返回值
};

class HIRExprStmt : public HIRStmt
{
public:
    std::unique_ptr<HIRExpr> expr;
};

class HIRFunction : public HIRNode
{
public:
    std::string name;                                                  // 函数名
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> params; // 参数（名称/类型）
    std::shared_ptr<Type> returnType;                                  // 返回类型
    std::shared_ptr<Type> type;                                        // 函数类型
    std::unique_ptr<HIRBlock> body;                                    // 函数体
    Symbol *funcSymbol;                                                // 函数符号
    bool isMethod;                                                     // 是否是方法
    bool isStatic;                                                     // 是否是静态方法
    bool isTraitMethod;                                                // 是否是 trait 方法
    std::string associatedStruct;                                      // 关联的结构体名
    std::string associatedTrait;                                       // 关联的 trait 名
};

class HIRStruct : public HIRNode
{
public:
    struct Method
    {
        std::string name;
        std::shared_ptr<Type> type;
        bool isPublic;
    };

public:
    std::string name;                        // 结构体名
    std::vector<Method> members;             // 成员（名称+类型）
    std::vector<Symbol *> implementedTraits; // 实现的 trait 列表
    Symbol *structSymbol;                    // 结构体符号
};

class HIRTrait : public HIRNode
{
public:
    std::string name;                                  // Trait 名
    std::vector<std::unique_ptr<HIRFunction>> methods; // 方法声明
    Symbol *traitSymbol;                               // Trait 符号
};

// TODO: 暂时不考虑模块管理

class HIRImpl : public HIRNode
{
public:
    std::string structName;                            // 关联的结构体名
    std::optional<std::string> traitName;              // 关联的 trait 名（空则为普通 impl）
    std::vector<std::unique_ptr<HIRFunction>> methods; // 实现的方法
};

class HIRImport : public HIRNode
{
public:
    std::vector<std::string> path;                   // 模块路径
    std::optional<std::vector<std::string>> symbols; // 导入的符号
    std::optional<std::string> alias;                // 别名
};

void printHIR(HIRNode *node);