// MIRPrinter.cpp
#include "IR/MIRPrinter.hpp"
#include <iomanip>
#include <sstream>
#include <string>

// ─── Forward declarations ────────────────────────────────────────────────────

static std::string fmtType(const std::shared_ptr<Type> &t);
static std::string fmtPlace(const MIRPlace &p);
static std::string fmtOperand(const MIROperand &op);
static std::string fmtRValue(const MIRRValue &rv);
static std::string fmtStatement(const MIRStatement &stmt);
static std::string fmtTerminator(const MIRTerminator &term);
static void printBlock(const MIRBasicBlock &bb, std::ostream &out);

// ─── ANSI colour helpers (disable by setting NO_COLOR=1) ────────────────────

static bool useColor()
{
    const char *nc = std::getenv("NO_COLOR");
    return !(nc && nc[0] != '\0');
}

namespace C
{
static const char *RST = "\033[0m";
static const char *BOLD = "\033[1m";
static const char *DIM = "\033[2m";
static const char *KW = "\033[38;5;33m";   // blue    – keywords
static const char *TY = "\033[38;5;36m";   // cyan    – types
static const char *NUM = "\033[38;5;208m"; // orange  – numeric literals
static const char *STR = "\033[38;5;70m";  // green   – string literals
static const char *LBL = "\033[38;5;245m"; // grey    – bb labels
static const char *TMP = "\033[38;5;245m"; // grey    – temps (_0, _1…)
static const char *VAR = "\033[38;5;255m"; // white   – named locals
static const char *OP = "\033[38;5;220m";  // yellow  – operators
} // namespace C

// Returns the escape code if colour is on, else "".
static const char *col(const char *code)
{
    return useColor() ? code : "";
}

// ─── Type formatting ─────────────────────────────────────────────────────────
// Delegates to whatever Type::toString() your type system exposes.
// If your Type class doesn't have toString(), swap this out.

static std::string fmtType(const std::shared_ptr<Type> &t)
{
    if (!t) return "?";
    // Assumes Type has a virtual toString().  Adjust to your API.
    return col(C::TY) + t->toString() + col(C::RST);
}

// ─── Place ───────────────────────────────────────────────────────────────────

static std::string fmtPlace(const MIRPlace &p)
{
    std::string base;
    if (p.base == PlaceBase::Return)
    {
        base = std::string(col(C::TMP)) + "_0" + col(C::RST);
    }
    else
    {
        const char *nameCol = p.name.empty() || p.name[0] == '_' ? C::TMP : C::VAR;
        base = std::string(col(nameCol))
               + (p.name.empty() ? ("_" + std::to_string(p.index)) : p.name)
               + col(C::RST);
    }

    std::string proj;
    for (const auto &pr : p.projections)
    {
        switch (pr.kind)
        {
        case ProjectionKind::Field:
            proj += "." + pr.field;
            break;
        case ProjectionKind::Deref:
            // wrap everything so far in a deref
            base = std::string(col(C::OP)) + "(*" + col(C::RST) + base
                   + std::string(col(C::OP)) + ")" + col(C::RST);
            break;
        case ProjectionKind::Index:
            proj += std::string(col(C::OP)) + "[" + col(C::RST)
                    + "_" + std::to_string(pr.localIndex)
                    + std::string(col(C::OP)) + "]" + col(C::RST);
            break;
        }
    }
    return base + proj;
}

// ─── Operand ─────────────────────────────────────────────────────────────────

static std::string fmtConst(const MIRConst &c)
{
    std::ostringstream s;
    s << col(C::KW) << "const" << col(C::RST) << " ";
    switch (c.kind)
    {
    case MIRConst::Kind::Int:
        s << col(C::NUM) << std::get<int64_t>(c.value) << col(C::RST);
        break;
    case MIRConst::Kind::Float:
        s << col(C::NUM) << std::get<double>(c.value) << col(C::RST);
        break;
    case MIRConst::Kind::Bool:
        s << col(C::KW) << (std::get<bool>(c.value) ? "true" : "false") << col(C::RST);
        break;
    case MIRConst::Kind::Char:
        s << col(C::STR) << "'" << std::get<char>(c.value) << "'" << col(C::RST);
        break;
    case MIRConst::Kind::String:
        s << col(C::STR) << '"' << std::get<std::string>(c.value) << '"' << col(C::RST);
        break;
    }
    s << col(C::TY) << ": " << fmtType(c.type) << col(C::RST);
    return s.str();
}

static std::string fmtOperand(const MIROperand &op)
{
    return std::visit([](auto &&v) -> std::string
        {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, MIRConst>)
            return fmtConst(v);
        else if constexpr (std::is_same_v<T, MIRCopy>)
            return std::string(col(C::KW)) + "copy" + col(C::RST)
                 + " " + fmtPlace(v.place);
        else  // MIRMove
            return std::string(col(C::KW)) + "move" + col(C::RST)
                 + " " + fmtPlace(v.place); },
        op);
}

// ─── RValue ──────────────────────────────────────────────────────────────────

static const char *opStr(MIRRValueBinaryOp::Op op)
{
    switch (op)
    {
    case MIRRValueBinaryOp::Op::Add: return "+";
    case MIRRValueBinaryOp::Op::Sub: return "-";
    case MIRRValueBinaryOp::Op::Mul: return "*";
    case MIRRValueBinaryOp::Op::Div: return "/";
    case MIRRValueBinaryOp::Op::Mod: return "%";
    case MIRRValueBinaryOp::Op::Eq: return "==";
    case MIRRValueBinaryOp::Op::Ne: return "!=";
    case MIRRValueBinaryOp::Op::Lt: return "<";
    case MIRRValueBinaryOp::Op::Gt: return ">";
    case MIRRValueBinaryOp::Op::Le: return "<=";
    case MIRRValueBinaryOp::Op::Ge: return ">=";
    case MIRRValueBinaryOp::Op::And: return "&&";
    case MIRRValueBinaryOp::Op::Or: return "||";
    case MIRRValueBinaryOp::Op::BitAnd: return "&";
    case MIRRValueBinaryOp::Op::BitOr: return "|";
    case MIRRValueBinaryOp::Op::BitXor: return "^";
    case MIRRValueBinaryOp::Op::Shl: return "<<";
    case MIRRValueBinaryOp::Op::Shr: return ">>";
    }
    return "?";
}

static const char *unaryOpStr(MIRRValueUnaryOp::Op op)
{
    switch (op)
    {
    case MIRRValueUnaryOp::Op::Neg: return "-";
    case MIRRValueUnaryOp::Op::Not: return "!";
    case MIRRValueUnaryOp::Op::BitNot: return "~";
    }
    return "?";
}

static std::string fmtRValue(const MIRRValue &rv)
{
    return std::visit([](auto &&v) -> std::string
        {
        using T = std::decay_t<decltype(v)>;
        std::ostringstream s;

        if constexpr (std::is_same_v<T, MIRRValueUse>)
        {
            s << fmtOperand(v.operand);
        }
        else if constexpr (std::is_same_v<T, MIRRValueBinaryOp>)
        {
            s << col(C::KW) << "BinaryOp" << col(C::RST)
              << col(C::OP) << "(" << col(C::RST)
              << fmtOperand(v.left)
              << " " << col(C::OP) << opStr(v.op) << col(C::RST) << " "
              << fmtOperand(v.right)
              << col(C::OP) << ")" << col(C::RST);
        }
        else if constexpr (std::is_same_v<T, MIRRValueUnaryOp>)
        {
            s << col(C::KW) << "UnaryOp" << col(C::RST)
              << col(C::OP) << "(" << col(C::RST)
              << col(C::OP) << unaryOpStr(v.op) << col(C::RST)
              << fmtOperand(v.operand)
              << col(C::OP) << ")" << col(C::RST);
        }
        else if constexpr (std::is_same_v<T, MIRRValueCast>)
        {
            s << fmtOperand(v.operand)
              << " " << col(C::KW) << "as" << col(C::RST)
              << " " << fmtType(v.targetType);
        }
        else if constexpr (std::is_same_v<T, MIRRValueRef>)
        {
            s << col(C::OP) << "&" << col(C::RST);
            if (v.isMut) s << col(C::KW) << "mut " << col(C::RST);
            s << fmtPlace(v.place);
        }
        else if constexpr (std::is_same_v<T, MIRRValueAddrOf>)
        {
            s << col(C::KW) << "addr_of" << col(C::RST)
              << col(C::OP) << "(" << col(C::RST)
              << fmtPlace(v.place)
              << col(C::OP) << ")" << col(C::RST);
        }
        else if constexpr (std::is_same_v<T, MIRRValueStructInit>)
        {
            s << fmtType(v.type)
              << col(C::OP) << " { " << col(C::RST);
            for (size_t i = 0; i < v.fields.size(); ++i)
            {
                if (i) s << col(C::OP) << ", " << col(C::RST);
                s << v.fields[i].first
                  << col(C::OP) << ": " << col(C::RST)
                  << fmtOperand(v.fields[i].second);
            }
            s << col(C::OP) << " }" << col(C::RST);
        }
        return s.str(); },
        rv);
}

// ─── Statements ──────────────────────────────────────────────────────────────

static std::string fmtStatement(const MIRStatement &stmt)
{
    return std::visit([](auto &&v) -> std::string
        {
        using T = std::decay_t<decltype(v)>;
        std::ostringstream s;

        if constexpr (std::is_same_v<T, MIRStmtAssign>)
        {
            s << fmtPlace(v.lhs)
              << " " << col(C::OP) << "=" << col(C::RST) << " "
              << fmtRValue(v.rhs);
        }
        else if constexpr (std::is_same_v<T, MIRStmtCall>)
        {
            if (v.dest)
                s << fmtPlace(*v.dest)
                  << " " << col(C::OP) << "=" << col(C::RST) << " ";

            s << col(C::KW) << "call" << col(C::RST) << " ";
            // Prefer the name for direct calls, else format the callee operand.
            if (!v.funcName.empty())
                s << col(C::VAR) << v.funcName << col(C::RST);
            else
                s << fmtOperand(v.callee);

            s << col(C::OP) << "(" << col(C::RST);
            for (size_t i = 0; i < v.args.size(); ++i)
            {
                if (i) s << col(C::OP) << ", " << col(C::RST);
                s << fmtOperand(v.args[i]);
            }
            s << col(C::OP) << ")" << col(C::RST);
        }
        else if constexpr (std::is_same_v<T, MIRStmtDrop>)
        {
            s << col(C::KW) << "drop" << col(C::RST)
              << col(C::OP) << "(" << col(C::RST)
              << fmtPlace(v.place)
              << col(C::OP) << ")" << col(C::RST);
        }
        else if constexpr (std::is_same_v<T, MIRStmtNop>)
        {
            s << col(C::DIM) << "nop" << col(C::RST);
        }

        return s.str(); },
        stmt);
}

// ─── Terminator ──────────────────────────────────────────────────────────────

static std::string fmtBBId(BasicBlockId id)
{
    return std::string(col(C::LBL)) + "bb" + std::to_string(id) + col(C::RST);
}

static std::string fmtTerminator(const MIRTerminator &term)
{
    return std::visit([](auto &&v) -> std::string
        {
        using T = std::decay_t<decltype(v)>;
        std::ostringstream s;

        if constexpr (std::is_same_v<T, MIRTermGoto>)
        {
            s << col(C::KW) << "goto" << col(C::RST)
              << " -> " << fmtBBId(v.target);
        }
        else if constexpr (std::is_same_v<T, MIRTermBranch>)
        {
            s << col(C::KW) << "switchInt" << col(C::RST)
              << col(C::OP) << "(" << col(C::RST)
              << fmtOperand(v.cond)
              << col(C::OP) << ")" << col(C::RST)
              << " -> ["
              << col(C::KW) << "true" << col(C::RST) << ": " << fmtBBId(v.thenBlock)
              << ", "
              << col(C::KW) << "false" << col(C::RST) << ": " << fmtBBId(v.elseBlock)
              << "]";
        }
        else if constexpr (std::is_same_v<T, MIRTermReturn>)
        {
            s << col(C::KW) << "return" << col(C::RST);
            if (v.value)
                s << " " << fmtOperand(*v.value);
        }
        else if constexpr (std::is_same_v<T, MIRTermCall>)
        {
            // Call-as-terminator (may unwind)
            s << fmtStatement(MIRStatement{ v.call });
            s << " -> [" << col(C::KW) << "ok" << col(C::RST)
              << ": " << fmtBBId(v.normalDest);
            if (v.unwindDest)
                s << ", " << col(C::KW) << "unwind" << col(C::RST)
                  << ": " << fmtBBId(*v.unwindDest);
            s << "]";
        }
        else if constexpr (std::is_same_v<T, MIRTermUnreachable>)
        {
            s << col(C::KW) << "unreachable" << col(C::RST);
        }

        return s.str(); },
        term);
}

// ─── Block ───────────────────────────────────────────────────────────────────

static void printBlock(const MIRBasicBlock &bb, std::ostream &out)
{
    // Header: bb3: {  or  bb3 (loop_header): {
    out << "    " << col(C::LBL) << col(C::BOLD)
        << "bb" << bb.id;
    if (!bb.label.empty())
        out << " (" << bb.label << ")";
    out << col(C::RST) << ": {\n";

    // Statements
    for (const auto &stmt : bb.stmts)
    {
        // Skip pure nops unless you want to see them
        if (std::holds_alternative<MIRStmtNop>(stmt)) continue;
        out << "        " << fmtStatement(stmt) << ";\n";
    }

    // Terminator on its own line, slightly dimmed separator
    out << col(C::DIM) << "        // term\n"
        << col(C::RST);
    out << "        " << fmtTerminator(bb.terminator) << ";\n";
    out << "    }\n";
}

// ─── Body ────────────────────────────────────────────────────────────────────

void printMIRBody(const MIRBody &body, std::ostream &out)
{
    // Local variable table
    out << col(C::DIM) << "    // locals\n"
        << col(C::DIM) << "    // total " + std::to_string(body.locals.size()) + " locals\n"
        << col(C::RST);
    for (const auto &loc : body.locals)
    {
        out << "    ";

        if (loc.index == 0)
            out << col(C::DIM) << "// return slot\n"
                << col(C::RST);

        const char *tag = loc.isArg    ? "arg"
                          : loc.isTemp ? "let" // temps use 'let' too; isTemp is metadata
                                       : "let";

        out << "    " << col(C::KW) << tag << col(C::RST) << " ";

        if (loc.isMutable)
            out << col(C::KW) << "mut " << col(C::RST);

        // temp names are _N, user names are plain
        const char *nameCol = loc.isTemp ? C::TMP : C::VAR;
        out << col(nameCol)
            << (loc.name.empty() ? "_" + std::to_string(loc.index) : loc.name)
            << col(C::RST)
            << ": " << fmtType(loc.type) << ";\n";
    }

    out << "\n";

    // Basic blocks
    for (const auto &bb : body.blocks)
        printBlock(bb, out);
}

// ─── Function ────────────────────────────────────────────────────────────────

static std::string mangleName(const MIRFunction &fn)
{
    // Simple mangling: for methods → "StructName::methodName",
    // for trait impls → "StructName::TraitName::methodName",
    // for free functions → just "funcName".
    if (fn.associatedStruct.empty())
        return fn.name;

    std::string mangled = fn.associatedStruct + "::";
    if (fn.associatedTrait.has_value())
        mangled += *fn.associatedTrait + "::";
    mangled += fn.name;
    return mangled;
}

void printMIRFunction(const MIRFunction &fn, std::ostream &out)
{
    // Signature header
    out << col(C::KW) << "fn " << col(C::RST)
        << col(C::BOLD) << mangleName(fn) << col(C::RST);

    if (!fn.associatedStruct.empty())
    {
        out << col(C::DIM) << "  // impl " << fn.associatedStruct;
        if (fn.associatedTrait)
            out << " for " << *fn.associatedTrait;
        out << col(C::RST);
    }
    out << " {\n";

    printMIRBody(fn.body, out);

    out << "}\n\n";
}

// ─── Program ─────────────────────────────────────────────────────────────────

void printMIRProgram(const MIRProgram &prog, std::ostream &out)
{
    // Globals section
    if (!prog.globals.empty())
    {
        out << col(C::DIM) << "// --- globals ---\n\n"
            << col(C::RST);
        for (const auto &g : prog.globals)
        {
            out << col(C::KW) << "static " << col(C::RST)
                << col(C::VAR) << g.name << col(C::RST)
                << ": " << fmtType(g.type);
            if (g.init)
                out << " = " << fmtRValue(*g.init);
            else
                out << col(C::DIM) << "  // zeroinit" << col(C::RST);
            out << ";\n";
        }
        out << "\n";
    }

    // Functions
    out << col(C::DIM) << "// --- functions ---\n\n"
        << col(C::RST);
    for (const auto &fn : prog.functions)
        printMIRFunction(fn, out);
}