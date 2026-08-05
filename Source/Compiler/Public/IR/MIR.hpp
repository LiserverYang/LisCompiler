/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once
#include "Analysiser/Type.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// ─── Places ──────────────────────────────────────────────────────────────────
// A "place" is an addressable location: a local variable, a temp, a field, or
// a deref. Modelled as a base + a list of projections.

enum class PlaceBase
{
    Local,  // named local / parameter
    Global, // global variable
    Return, // the implicit return slot
};

enum class ProjectionKind
{
    Field, // .field_name
    Deref, // *ptr
    Index, // [i]
};

struct Projection
{
    ProjectionKind kind;
    std::string field; // for Field
    size_t localIndex; // for Index (the temp holding the index)
};

struct MIRPlace
{
    PlaceBase base;
    size_t index;                        // index into the local table
    std::string name;                    // debug name
    std::vector<Projection> projections; // in application order
    std::shared_ptr<Type> type;
};

// ─── Operands ────────────────────────────────────────────────────────────────
// Every RHS value is either a constant, a copy, or a move.

struct MIRConst
{
    enum class Kind
    {
        Int,
        Float,
        Bool,
        Char,
        String
    } kind;
    std::variant<int64_t, double, bool, char, std::string> value;
    std::shared_ptr<Type> type;
};

struct MIRCopy
{
    MIRPlace place;
}; // shallow copy (Copy types)
struct MIRMove
{
    MIRPlace place;
}; // move semantics

using MIROperand = std::variant<MIRConst, MIRCopy, MIRMove>;

// ─── RValues ─────────────────────────────────────────────────────────────────
// The right-hand side of an assignment. Each variant is one operation.

struct MIRRValueUse
{
    MIROperand operand;
}; // identity / copy-move

struct MIRRValueBinaryOp
{
    enum class Op
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
        Shl,
        Shr
    };
    Op op;
    MIROperand left;
    MIROperand right;
    std::shared_ptr<Type> type;
};

struct MIRRValueUnaryOp
{
    enum class Op
    {
        Neg,
        Not,
        BitNot
    } op;
    MIROperand operand;
    std::shared_ptr<Type> type;
};

struct MIRRValueCast
{
    MIROperand operand;
    std::shared_ptr<Type> targetType;
};

struct MIRRValueRef
{
    MIRPlace place;
    bool isMut;
}; // &x / &mut x

struct MIRRValueAddrOf
{
    MIRPlace place;
}; // raw addr-of

struct MIRRValueStructInit
{
    std::string structName;
    std::vector<std::pair<std::string, MIROperand>> fields;
    std::shared_ptr<Type> type;
};

using MIRRValue = std::variant<
    MIRRValueUse,
    MIRRValueBinaryOp,
    MIRRValueUnaryOp,
    MIRRValueCast,
    MIRRValueRef,
    MIRRValueAddrOf,
    MIRRValueStructInit>;

// ─── Statements ──────────────────────────────────────────────────────────────
// All statements inside a basic block are non-branching.

struct MIRStmtAssign
{
    MIRPlace lhs;  // where to write
    MIRRValue rhs; // what to compute
};

struct MIRStmtCall
{
    std::optional<MIRPlace> dest; // where to put return value (none = void)
    MIROperand callee;            // func pointer / name operand
    std::string funcName;         // for debug / direct calls
    std::vector<MIROperand> args;
    std::vector<std::shared_ptr<Type>> genericParams;

    // Operator overloading on a generic param (`fn f<T: Add> { a + b }`): the
    // callee is the placeholder `<T>::method`. Monomorphization retargets it to
    // the concrete struct method; if the concrete type is a PRIMITIVE (no
    // method), it converts this call back into a direct binary op using `op`.
    std::optional<MIRRValueBinaryOp::Op> genericOpFallback;
};

struct MIRStmtDrop
{
    MIRPlace place;
}; // explicit drop (destructor hook)
struct MIRStmtNop
{
}; // placeholder / removed statement

using MIRStatement = std::variant<
    MIRStmtAssign,
    MIRStmtCall,
    MIRStmtDrop,
    MIRStmtNop>;

// ─── Terminators ─────────────────────────────────────────────────────────────
// Every basic block ends with exactly one terminator.

using BasicBlockId = size_t;

struct MIRTermGoto
{
    BasicBlockId target;
};

struct MIRTermBranch
{
    MIROperand cond;
    BasicBlockId thenBlock;
    BasicBlockId elseBlock;
};

struct MIRTermReturn
{
    std::optional<MIROperand> value;
};

struct MIRTermCall // call that may unwind (for future exception / panic support)
{
    MIRStmtCall call;
    BasicBlockId normalDest;
    std::optional<BasicBlockId> unwindDest; // landing pad (future)
};

struct MIRTermUnreachable
{
};

using MIRTerminator = std::variant<
    MIRTermGoto,
    MIRTermBranch,
    MIRTermReturn,
    MIRTermCall,
    MIRTermUnreachable>;

// ─── Basic Block ─────────────────────────────────────────────────────────────

struct MIRBasicBlock
{
    BasicBlockId id;
    std::string label; // debug: "bb0", "then", "loop_header"…
    std::vector<MIRStatement> stmts;
    MIRTerminator terminator;
};

// ─── Locals ──────────────────────────────────────────────────────────────────

struct MIRLocal
{
    size_t index;
    std::string name; // "_0" for temps, real name for user vars
    std::shared_ptr<Type> type;
    bool isMutable;
    bool isTemp; // compiler-generated temporary
    bool isArg;  // function parameter
};

// ─── Function body ───────────────────────────────────────────────────────────

struct MIRBody
{
    std::string funcName;
    std::vector<MIRLocal> locals;      // index 0 = return slot
    size_t argCount;                   // locals[1..argCount] are params
    std::vector<MIRBasicBlock> blocks; // blocks[0] = entry
    std::shared_ptr<Type> returnType;
};

// ─── Top-level MIR items ──────────────────────────────────────────────────────

struct MIRFunction
{
    std::string name;
    MIRBody body;
    bool isMethod;
    bool isStatic;
    std::string associatedStruct;
    std::optional<std::string> associatedTrait;
    std::vector<std::string> genericParams;
};

struct MIRGlobal
{
    std::string name;
    std::shared_ptr<Type> type;
    std::optional<MIRRValue> init; // constant-folded init, or nullopt = zeroinit
};

struct MIRProgram
{
    std::vector<std::shared_ptr<MIRFunction>> functions;
    std::vector<MIRGlobal> globals;
};