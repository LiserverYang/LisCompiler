/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once
#include "Analysiser/Type.hpp"
#include "Core/SourcePosition.hpp"
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

    /// Source span of the HIR expression that produced this place. Borrow /
    /// move / definite-assignment checking runs on MIR (2026-09-19), and those
    /// diagnostics point at the PLACE a program uses (E3005 use-after-move,
    /// E3011 uninitialized use, E4001-E4004 borrow conflicts), so the position
    /// has to ride along with the place instead of being looked up in the HIR
    /// tree afterwards. MIRBuilder::buildExpr stamps it.
    ///
    /// Empty for compiler-generated places that have no source form (a temp
    /// holding a call result, the return slot): the checkers fall back to a
    /// statement/function-level location for those.
    SourcePosition pos;
    size_t length = 0;
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

struct MIRRValueArrayInit
{
    std::vector<MIROperand> elements;
    std::shared_ptr<Type> type; // the ArrayType

    /// `[v; N]` (the HIR repeat form). 0 means "one operand per element" (the
    /// normal literal). When > 0, `elements` holds the SINGLE element operand
    /// and the backend replicates it `repeatCount` times — a store loop rather
    /// than N insertvalue nodes, because N may be up to MAX_ARRAY_ELEMENTS
    /// (1 << 20). The element is evaluated once in MIRBuilder, so any side
    /// effect in it happens exactly once.
    size_t repeatCount = 0;
};

using MIRRValue = std::variant<
    MIRRValueUse,
    MIRRValueBinaryOp,
    MIRRValueUnaryOp,
    MIRRValueCast,
    MIRRValueRef,
    MIRRValueAddrOf,
    MIRRValueStructInit,
    MIRRValueArrayInit>;

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

/// Control flow DIVERGES here: the preceding statement was a call to a
/// `never`-returning function (`panic("...")`), which never returns to its
/// caller. Everything after it in the block is unreachable.
///
/// This is a SEPARATE terminator from MIRTermUnreachable even though both lower
/// to LLVM `unreachable`: MIRTermUnreachable is the "not yet sealed" sentinel
/// every fresh block starts with (MIRBuilder::newBlock), and the builders test
/// for it to decide whether a block still needs a terminator. Reusing it for
/// divergence would make a diverged block look unsealed, so the fall-through
/// paths would append a `goto`/`ret` after it and the dead statements would
/// execute.
struct MIRTermDiverge
{
};

using MIRTerminator = std::variant<
    MIRTermGoto,
    MIRTermBranch,
    MIRTermReturn,
    MIRTermCall,
    MIRTermUnreachable,
    MIRTermDiverge>;

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