/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <unordered_map>

#include "Analysiser/Scope.hpp"
#include "Analysiser/Symbol.hpp"
#include "Analysiser/Type.hpp"
#include "Core/SourcePosition.hpp"
#include "IR/HIRVisitor.hpp"

// ---------------------------------------------------------------------------
// Raw (unresolved) type reference — filled by HIRBuilder from the AST TypeNode,
// resolved into std::shared_ptr<Type> by HIRSemanticAnalyzer.
// ---------------------------------------------------------------------------
struct HIRRawType
{
    std::string name;
    bool isRef = false;
    bool isMutRef = false;
    bool isPresent = false;  // false = "not explicitly written"
    bool isPrimitive = true; // false = custom / struct type

    std::vector<HIRRawType> genericArgs;
};

/** A trait bound on a generic param, with optional concrete args. */
struct HIRGenericConstraint
{
    std::string traitName;
    std::vector<HIRRawType> args;
};

// ---------------------------------------------------------------------------
// Base nodes
// ---------------------------------------------------------------------------
class HIRNode
{
public:
    SourcePosition position;
    size_t length;
    virtual ~HIRNode() = default;
    virtual void accept(HIRVisitor *visitor) = 0;
};

class HIRExpr : public HIRNode
{
public:
    std::shared_ptr<Type> type; // filled by HIRSemanticAnalyzer
};

class HIRStmt : public HIRNode
{
};

// ---------------------------------------------------------------------------
class HIRProgram : public HIRNode
{
public:
    std::vector<std::unique_ptr<HIRNode>> items;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRNameRef : public HIRExpr
{
public:
    std::string name;
    Symbol *symbol = nullptr; // filled by HIRSemanticAnalyzer
    std::shared_ptr<Scope> scope;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
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
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRBinaryOp : public HIRExpr
{
public:
    enum class OpKind
    {
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        Eq,
        Ne,
        Lt,
        Gt,
        Le,
        Ge,
        And,
        Or,
        BitAnd,
        BitOr,
        BitXor,
        ShiftLeft,
        ShiftRight
    };
    std::unique_ptr<HIRExpr> left;
    std::unique_ptr<HIRExpr> right;
    OpKind opKind;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRCast : public HIRExpr
{
public:
    std::unique_ptr<HIRExpr> expr;
    HIRRawType rawTargetType;         // set by HIRBuilder
    std::shared_ptr<Type> targetType; // filled by HIRSemanticAnalyzer
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
// A single HIRCall covers regular calls, instance-method calls, and static
// calls.  HIRBuilder sets callKind + the appropriate raw fields; the semantic
// analyser resolves callee, symbol, and type.
class HIRCall : public HIRExpr
{
public:
    enum class CallKind
    {
        Regular,
        Method,
        Static
    };
    CallKind callKind = CallKind::Regular;

    // Regular call: function expression
    std::unique_ptr<HIRExpr> callee;

    // Method call: receiver object + method name
    std::unique_ptr<HIRExpr> object;
    std::string methodName;

    // Static call: type name + method name (object is null, callee is null)
    std::string staticTypeName;

    std::vector<HIRRawType> genericParams;
    std::vector<std::shared_ptr<Type>> typedGenericParams;

    std::vector<std::unique_ptr<HIRExpr>> args;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRMemberAccess : public HIRExpr
{
public:
    std::unique_ptr<HIRExpr> object;
    std::string memberName;
    Symbol *memberSymbol = nullptr;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRStructInit : public HIRExpr
{
public:
    std::string structName;         // raw name — set by HIRBuilder
    Symbol *structSymbol = nullptr; // filled by HIRSemanticAnalyzer
    std::vector<std::pair<std::string, std::unique_ptr<HIRExpr>>> members;
    std::vector<HIRRawType> genericArgs;
    std::vector<std::shared_ptr<Type>> typedGenericParams;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRRef : public HIRExpr
{
public:
    std::unique_ptr<HIRExpr> expr;
    bool isMutable = false;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRBlock : public HIRStmt
{
public:
    std::vector<std::unique_ptr<HIRStmt>> stmts;
    std::shared_ptr<Scope> scope;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRVarDecl : public HIRStmt
{
public:
    std::string name;
    HIRRawType rawType; // set by HIRBuilder (if explicit)
    bool hasExplicitType = false;
    std::shared_ptr<Type> type; // filled by HIRSemanticAnalyzer
    std::optional<std::unique_ptr<HIRExpr>> init;
    bool isMutable = false;
    bool isGlobal = false;
    Symbol *varSymbol = nullptr;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRAssign : public HIRStmt
{
public:
    std::unique_ptr<HIRExpr> target;
    std::unique_ptr<HIRExpr> value;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRIf : public HIRStmt
{
public:
    std::unique_ptr<HIRExpr> cond;
    std::unique_ptr<HIRBlock> thenBlock;
    std::optional<std::unique_ptr<HIRBlock>> elseBlock;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRLoop : public HIRStmt
{
public:
    enum class Kind
    {
        While,
        For
    };
    Kind kind;
    std::optional<std::unique_ptr<HIRExpr>> cond;
    std::unique_ptr<HIRBlock> body;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRReturn : public HIRStmt
{
public:
    std::optional<std::unique_ptr<HIRExpr>> value;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRExprStmt : public HIRStmt
{
public:
    std::unique_ptr<HIRExpr> expr;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRBreak : public HIRStmt
{
public:
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRContinue : public HIRStmt
{
public:
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRFunction : public HIRNode
{
public:
    std::string name;

    // Raw info — set by HIRBuilder
    std::vector<std::pair<std::string, HIRRawType>> rawParams;
    HIRRawType rawReturnType;
    bool hasReturnType = false;

    // Self param info (for methods)
    bool hasSelf = false;
    bool selfIsRef = false;
    bool selfIsMut = false;
    std::shared_ptr<Type> selfType; // filled by HIRSemanticAnalyzer

    // Resolved — filled by HIRSemanticAnalyzer
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> params;
    std::vector<std::shared_ptr<GenericParamType>> gParams;
    std::unordered_map<std::string, std::vector<HIRGenericConstraint>> unsolveConstraints;
    std::shared_ptr<Type> returnType;
    std::shared_ptr<Type> type;

    std::unique_ptr<HIRBlock> body;
    Symbol *funcSymbol = nullptr;

    bool isMethod = false;
    bool isStatic = false;
    bool isTraitMethod = false;
    bool isGeneric = false;
    std::string associatedStruct;
    std::string associatedTrait;

    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRStruct : public HIRNode
{
public:
    struct Member
    {
        std::string name;
        HIRRawType rawType;         // set by HIRBuilder
        std::shared_ptr<Type> type; // filled by HIRSemanticAnalyzer
        bool isPublic = false;
    };

    std::string name;
    std::vector<Member> members;
    std::vector<Symbol *> implementedTraits;
    bool isGeneric;

    std::vector<std::shared_ptr<GenericParamType>> gParams;
    std::unordered_map<std::string, std::vector<HIRGenericConstraint>> unsolveConstraints;
    Symbol *structSymbol = nullptr;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRTrait : public HIRNode
{
public:
    std::string name;
    bool isGeneric = false;
    std::vector<std::shared_ptr<GenericParamType>> gParams;
    std::unordered_map<std::string, std::vector<HIRGenericConstraint>> unsolveConstraints;
    std::vector<std::unique_ptr<HIRFunction>> methods;
    Symbol *traitSymbol = nullptr;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRImpl : public HIRNode
{
public:
    std::string structName;
    std::optional<std::string> traitName;
    std::vector<std::unique_ptr<HIRFunction>> methods;
    std::vector<std::shared_ptr<GenericParamType>> gParams;
    std::unordered_map<std::string, std::vector<HIRGenericConstraint>> unsolveConstraints;
    std::vector<HIRRawType> traitGenericArgs;
    std::vector<HIRRawType> structGenericArgs;
    void accept(HIRVisitor *visitor) override
    {
        visitor->visit(this);
    }
};

// ---------------------------------------------------------------------------
class HIRImport : public HIRNode
{
public:
    std::vector<std::string> path;
    std::optional<std::vector<std::string>> symbols;
    std::optional<std::string> alias;
    void accept(HIRVisitor *visitor) override {} // no-op for now
};

void printHIR(HIRNode *node);