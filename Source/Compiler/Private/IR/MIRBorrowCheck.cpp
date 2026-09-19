/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * MIRBorrowCheck — see the header for why the checks live here.
 *
 * Layout:
 *   - state:       per-local {maybeInit, moved paths}
 *   - fixpoint:    forward dataflow over the CFG, NO diagnostics
 *   - reporting:   one walk per block using the converged IN state, so a
 *                  diagnostic is emitted exactly once even though the transfer
 *                  function runs many times
 */

#include "IR/MIRBorrowCheck.hpp"

#include "Core/Context.hpp"
#include "IR/MIRPrinter.hpp"
#include "Logger/ErrorID.hpp"
#include "Logger/Logger.hpp"

#include <iostream>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// A move path: the projection chain below a local's root. Field names are used
/// verbatim, an index is "*" (unknown indices cannot be told apart), and an
/// EMPTY path means the whole value.
using MovePath = std::vector<std::string>;

std::string joinPath(const MovePath &path)
{
    std::string out;
    for (const auto &seg : path)
    {
        if (!out.empty())
            out += ".";
        out += seg;
    }
    return out;
}

/// One is a prefix of the other (including the empty path = the whole value).
bool pathsOverlap(const MovePath &a, const MovePath &b)
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i])
            return false;
    return true;
}

bool hasPath(const std::vector<MovePath> &paths, const MovePath &p)
{
    return std::find(paths.begin(), paths.end(), p) != paths.end();
}

bool samePaths(const std::vector<MovePath> &a, const std::vector<MovePath> &b)
{
    if (a.size() != b.size())
        return false;
    for (const MovePath &p : a)
        if (!hasPath(b, p))
            return false;
    return true;
}

struct LocalState
{
    /// Definitely assigned on EVERY path that reaches this point (the AND rule
    /// at a join) — the complement of "may be uninitialized".
    bool maybeInit = false;
    /// Paths moved out on SOME path (the UNION rule at a join): a may-analysis,
    /// which is what a use-after-move diagnostic needs.
    std::vector<MovePath> moved;
};

struct State
{
    std::vector<LocalState> locals;
};

bool sameState(const State &a, const State &b)
{
    if (a.locals.size() != b.locals.size())
        return false;
    for (size_t i = 0; i < a.locals.size(); ++i)
    {
        if (a.locals[i].maybeInit != b.locals[i].maybeInit)
            return false;
        if (!samePaths(a.locals[i].moved, b.locals[i].moved))
            return false;
    }
    return true;
}

/// The place, decomposed for move bookkeeping.
struct PlaceInfo
{
    bool isLocal = false;   // a global / return slot is never moved out of
    size_t root = 0;        // local index
    MovePath path;          // projections up to the first Deref
    bool throughDeref = false;
    bool derefIsLast = false;
};

PlaceInfo describePlace(const MIRPlace &place)
{
    PlaceInfo info;
    info.isLocal = (place.base == PlaceBase::Local);
    info.root = place.index;
    for (size_t i = 0; i < place.projections.size(); ++i)
    {
        const Projection &p = place.projections[i];
        if (p.kind == ProjectionKind::Deref)
        {
            if (!info.throughDeref)
            {
                info.throughDeref = true;
                info.derefIsLast = (i + 1 == place.projections.size());
            }
            continue;
        }
        if (!info.throughDeref)
            info.path.push_back(p.kind == ProjectionKind::Field ? p.field : std::string("*"));
    }
    return info;
}

/// The type a projection chain denotes, starting from the base local's type.
/// Returns null as soon as the chain leaves the type information MIR carries.
std::shared_ptr<Type> typeAfter(const std::shared_ptr<Type> &base, const MIRPlace &place, size_t upTo)
{
    std::shared_ptr<Type> cur = base;
    for (size_t i = 0; i < upTo && i < place.projections.size(); ++i)
    {
        if (!cur)
            return nullptr;
        const Projection &p = place.projections[i];
        while (p.kind != ProjectionKind::Deref && cur->getKind() == Type::Kind::Reference)
            cur = std::static_pointer_cast<ReferenceType>(cur)->getBaseType();
        if (p.kind == ProjectionKind::Deref)
        {
            if (cur->getKind() == Type::Kind::Reference)
                cur = std::static_pointer_cast<ReferenceType>(cur)->getBaseType();
            else if (cur->getKind() == Type::Kind::Pointer)
                cur = std::static_pointer_cast<PointerType>(cur)->getBaseType();
            continue;
        }
        if (cur->getKind() != Type::Kind::Custom)
            return nullptr;
        auto custom = std::static_pointer_cast<CustomType>(cur);
        if (p.kind == ProjectionKind::Field)
        {
            const auto &fields = custom->getFields();
            auto field = std::find(fields.begin(), fields.end(), p.field);
            if (field == fields.end())
                return nullptr;
            cur = field->type;
        }
        else
        {
            // An index: only arrays carry the element type at this level.
            return nullptr;
        }
    }
    return cur;
}

class FunctionChecker
{
public:
    FunctionChecker(Context *ctx, const MIRFunction &fn) : context_(ctx), fn_(fn), body_(fn.body) {}

    void run();

private:
    Context *context_;
    const MIRFunction &fn_;
    const MIRBody &body_;

    std::vector<std::vector<size_t>> preds_;
    std::vector<State> in_;
    /// The state AFTER a block's statements. A successor joins its
    /// predecessors' OUT states — joining their IN states would lose every
    /// definition the predecessor block itself performed.
    std::vector<State> out_;
    bool reporting_ = false;

    void logAt(const MIRPlace &place, const std::string &msg, size_t errorId);

    State entryState() const;
    /// The TOP of the state lattice: every local assigned, nothing moved. Used
    /// to initialise every block's IN state, so that a predecessor the fixpoint
    /// has not visited yet contributes nothing instead of 'uninitialized'.
    State topState() const;
    State joinPredecessors(size_t block) const;
    State transfer(const MIRBasicBlock &block, const State &in, bool report);

    void transferStmt(const MIRStatement &stmt, State &st, bool report);
    void transferTerm(const MIRTerminator &term, State &st, bool report);
    void useRValue(const MIRRValue &rv, State &st, bool report);
    void useOperand(const MIROperand &op, State &st, bool report);

    /// A place READ (copy, borrow, addr-of, drop, return): it must be assigned
    /// and not moved out of. Reports E3011 / the whole-value E3005 form.
    void checkReadable(const MIRPlace &place, const State &st, bool report);
    /// The TARGET of an assignment. A whole-binding write initializes the
    /// binding (so it is not an E3011), but it is still a read of the place:
    /// assigning over a MOVED binding is rejected (single owner, no revival),
    /// and writing a FIELD requires the binding to be initialized already.
    void checkAssignTarget(const MIRPlace &place, const State &st, bool report);
    /// A MOVE source: the full ownership rule set (E3005/E3016/E3017).
    void checkMove(const MIRPlace &place, State &st, bool report);
    /// Mark the destination of an assignment as owned again.
    void definePlace(const MIRPlace &place, State &st);

    std::string displayName(const MIRPlace &place) const;
};

void FunctionChecker::logAt(const MIRPlace &place, const std::string &msg, size_t errorId)
{
    if (!reporting_)
        return;
    Logger::LogInfo info{};
    info.codePath = context_->filePath;
    info.code = &context_->fileValue;
    info.col = place.pos.col;
    info.line = place.pos.line;
    info.length = place.length;
    info.beginPosition = place.pos.lineStart;
    info.msg = msg;
    info.errorId = errorId;
    info.exit = false;
    Logger::Log(Logger::LogLevel::ERROR, info);
}

std::string FunctionChecker::displayName(const MIRPlace &place) const
{
    std::string name = (place.base == PlaceBase::Local && place.index < body_.locals.size())
                           ? body_.locals[place.index].name
                           : place.name;
    const PlaceInfo info = describePlace(place);
    if (!info.path.empty())
        name += "." + joinPath(info.path);
    return name;
}

State FunctionChecker::entryState() const
{
    State st;
    st.locals.resize(body_.locals.size());
    for (size_t i = 0; i < body_.locals.size(); ++i)
    {
        const MIRLocal &loc = body_.locals[i];
        // Parameters hold a value from the caller and index 0 is the return
        // slot; a 'let x;' binding starts UNASSIGNED (that is the whole point of
        // the definite-assignment analysis).
        st.locals[i].maybeInit = (loc.isArg || i == 0);
    }
    return st;
}

State FunctionChecker::topState() const
{
    State st;
    st.locals.resize(body_.locals.size());
    for (auto &l : st.locals)
        l.maybeInit = true;
    return st;
}

State FunctionChecker::joinPredecessors(size_t block) const
{
    const std::vector<size_t> &preds = preds_[block];

    // Unreachable: the TOP state reports nothing, exactly like the HIR pass
    // suppresses diagnostics in unreachable code.
    if (preds.empty())
        return topState();

    State st = topState();

    for (size_t i = 0; i < body_.locals.size(); ++i)
    {
        st.locals[i].maybeInit = true;
        for (size_t p : preds)
            st.locals[i].maybeInit = st.locals[i].maybeInit && out_[p].locals[i].maybeInit;
    }
    for (size_t p : preds)
        for (size_t i = 0; i < body_.locals.size(); ++i)
            for (const auto &movedPath : out_[p].locals[i].moved)
                if (!hasPath(st.locals[i].moved, movedPath))
                    st.locals[i].moved.push_back(movedPath);
    return st;
}

void FunctionChecker::run()
{
    const size_t blockCount = body_.blocks.size();
    if (blockCount == 0)
        return;

    preds_.assign(blockCount, {});
    for (size_t b = 0; b < blockCount; ++b)
    {
        std::vector<size_t> succs;
        const MIRTerminator &term = body_.blocks[b].terminator;
        if (auto *go = std::get_if<MIRTermGoto>(&term))
            succs.push_back(go->target);
        else if (auto *br = std::get_if<MIRTermBranch>(&term))
        {
            succs.push_back(br->thenBlock);
            succs.push_back(br->elseBlock);
        }
        else if (auto *call = std::get_if<MIRTermCall>(&term))
        {
            succs.push_back(call->normalDest);
            if (call->unwindDest.has_value())
                succs.push_back(*call->unwindDest);
        }
        for (size_t s : succs)
            if (s < blockCount)
                preds_[s].push_back(b);
    }

    in_.assign(blockCount, topState());
    out_.assign(blockCount, topState());
    const State entry = entryState();

    // ── fixpoint (no diagnostics) ────────────────────────────────────────────
    std::vector<bool> seen(blockCount, false);
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t b = 0; b < blockCount; ++b)
        {
            State next = (b == 0) ? entry : joinPredecessors(b);
            State nextOut = transfer(body_.blocks[b], next, /*report=*/false);
            if (!seen[b] || !sameState(in_[b], next) || !sameState(out_[b], nextOut))
            {
                in_[b] = std::move(next);
                out_[b] = std::move(nextOut);
                seen[b] = true;
                changed = true;
            }
        }
    }

    // ── reporting walk ───────────────────────────────────────────────────────
    reporting_ = true;
    for (size_t b = 0; b < blockCount; ++b)
    {
        State st = in_[b];
        transfer(body_.blocks[b], st, /*report=*/true);
    }
    reporting_ = false;
}

State FunctionChecker::transfer(const MIRBasicBlock &block, const State &in, bool report)
{
    State st = in;
    for (const MIRStatement &stmt : block.stmts)
        transferStmt(stmt, st, report);
    transferTerm(block.terminator, st, report);
    return st;
}

void FunctionChecker::transferStmt(const MIRStatement &stmt, State &st, bool report)
{
    if (auto *as = std::get_if<MIRStmtAssign>(&stmt))
    {
        checkAssignTarget(as->lhs, st, report);
        useRValue(as->rhs, st, report);
        definePlace(as->lhs, st);
        return;
    }
    if (auto *call = std::get_if<MIRStmtCall>(&stmt))
    {
        useOperand(call->callee, st, report);
        for (const MIROperand &arg : call->args)
            useOperand(arg, st, report);
        if (call->dest.has_value())
            definePlace(*call->dest, st);
        return;
    }
    if (auto *drop = std::get_if<MIRStmtDrop>(&stmt))
    {
        checkReadable(drop->place, st, report);
        return;
    }
    // MIRStmtNop: nothing to track.
}

void FunctionChecker::transferTerm(const MIRTerminator &term, State &st, bool report)
{
    if (auto *br = std::get_if<MIRTermBranch>(&term))
    {
        useOperand(br->cond, st, report);
        return;
    }
    if (auto *ret = std::get_if<MIRTermReturn>(&term))
    {
        if (ret->value.has_value())
            useOperand(*ret->value, st, report);
        return;
    }
    if (auto *call = std::get_if<MIRTermCall>(&term))
    {
        useOperand(call->call.callee, st, report);
        for (const MIROperand &arg : call->call.args)
            useOperand(arg, st, report);
        if (call->call.dest.has_value())
            definePlace(*call->call.dest, st);
        return;
    }
}

void FunctionChecker::useRValue(const MIRRValue &rv, State &st, bool report)
{
    if (auto *use = std::get_if<MIRRValueUse>(&rv))
    {
        useOperand(use->operand, st, report);
        return;
    }
    if (auto *bin = std::get_if<MIRRValueBinaryOp>(&rv))
    {
        useOperand(bin->left, st, report);
        useOperand(bin->right, st, report);
        return;
    }
    if (auto *un = std::get_if<MIRRValueUnaryOp>(&rv))
    {
        useOperand(un->operand, st, report);
        return;
    }
    if (auto *cast = std::get_if<MIRRValueCast>(&rv))
    {
        useOperand(cast->operand, st, report);
        return;
    }
    if (auto *ref = std::get_if<MIRRValueRef>(&rv))
    {
        // Taking a reference reads the place: borrowing an unassigned binding is
        // E3011, and the borrow itself is checked by the borrow machinery.
        checkReadable(ref->place, st, report);
        return;
    }
    if (auto *addr = std::get_if<MIRRValueAddrOf>(&rv))
    {
        checkReadable(addr->place, st, report);
        return;
    }
    if (auto *si = std::get_if<MIRRValueStructInit>(&rv))
    {
        for (const auto &field : si->fields)
            useOperand(field.second, st, report);
        return;
    }
    if (auto *ai = std::get_if<MIRRValueArrayInit>(&rv))
    {
        for (const MIROperand &el : ai->elements)
            useOperand(el, st, report);
        return;
    }
}

void FunctionChecker::useOperand(const MIROperand &op, State &st, bool report)
{
    if (auto *cp = std::get_if<MIRCopy>(&op))
    {
        checkReadable(cp->place, st, report);
        return;
    }
    if (auto *mv = std::get_if<MIRMove>(&op))
    {
        checkMove(mv->place, st, report);
        return;
    }
    // MIRConst: nothing.
}

void FunctionChecker::checkReadable(const MIRPlace &place, const State &st, bool report)
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.root >= st.locals.size())
        return;
    const LocalState &ls = st.locals[info.root];

    if (!ls.maybeInit)
    {
        logAt(place, "use of uninitialized value: '" + body_.locals[info.root].name + "'",
            E_UseOfUninitializedValue);
        return;
    }

    // Touching ANY part of a value that was moved out AS A WHOLE re-reads the
    // moved binding (the HIR checker reports this at the name reference, whatever
    // the surrounding projection is). A merely PARTIALLY moved value is not a
    // read error — the sibling fields are still owned.
    if (hasPath(ls.moved, MovePath{}))
        logAt(place, "use of moved value: '" + body_.locals[info.root].name + "'", E_UseOfMovedValue);
}

void FunctionChecker::checkAssignTarget(const MIRPlace &place, const State &st, bool report)
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.throughDeref || info.root >= st.locals.size())
        return;
    const LocalState &ls = st.locals[info.root];
    const std::string rootName = body_.locals[info.root].name;

    // Writing a FIELD/ELEMENT requires the binding to be initialized already
    // (the language tracks whole bindings, not individual fields).
    if (!info.path.empty() && !ls.maybeInit)
    {
        logAt(place, "use of uninitialized value: '" + rootName + "'", E_UseOfUninitializedValue);
        return;
    }

    if (hasPath(ls.moved, MovePath{}))
        logAt(place, "use of moved value: '" + rootName + "'", E_UseOfMovedValue);
}

void FunctionChecker::checkMove(const MIRPlace &place, State &st, bool report)
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.root >= st.locals.size())
        return;

    const bool nonCopy = place.type && !place.type->isCopyable();
    if (!nonCopy)
        return; // a Copy source is a read, not a move

    if (info.throughDeref)
    {
        if (info.derefIsLast || info.path.empty())
        {
            logAt(place,
                "cannot move out of a reference: '*p' only borrows the value, so the referent still owns it.",
                E_MoveOutOfReference);
            return;
        }
        const std::shared_ptr<Type> base = typeAfter(body_.locals[info.root].type, place, info.path.size());
        const std::string refName = base ? base->toString() : std::string("&T");
        logAt(place,
            "cannot move out of '" + displayName(place) + "': it is behind the reference '" + refName
                + "', so the value is only borrowed here and the referent still owns it.",
            E_MoveOutOfReference);
        return;
    }

    LocalState &ls = st.locals[info.root];
    const std::string rootName = body_.locals[info.root].name;

    if (info.path.empty())
    {
        // Any field already moved out makes the whole-value move a double free
        // ("partially moved"); an already-moved whole value is plain E3005. The
        // order mirrors the HIR checker, which tests the field list first.
        bool anyFieldMoved = false;
        for (const MovePath &p : ls.moved)
            if (!p.empty())
                anyFieldMoved = true;
        if (anyFieldMoved)
        {
            logAt(place, "use of moved value: '" + rootName + "' (partially moved)", E_UseOfMovedValue);
            return;
        }
        if (hasPath(ls.moved, MovePath{}))
        {
            logAt(place, "use of moved value: '" + rootName + "'", E_UseOfMovedValue);
            return;
        }
        ls.moved.push_back(MovePath{}); // the whole value is gone
        return;
    }

    if (hasPath(ls.moved, MovePath{}))
    {
        logAt(place, "use of moved value: '" + rootName + "." + joinPath(info.path) + "'", E_UseOfMovedValue);
        return;
    }

    // E0509: a field cannot leave a value whose type implements Drop — the
    // destructor releases that type's fields as a whole. The owner is the type
    // the LAST projection reads out of.
    const size_t ownerUpTo = place.projections.empty()
                                 ? 0
                                 : (place.projections.back().kind == ProjectionKind::Field
                                           && place.projections.size() >= 1
                                       ? place.projections.size() - 1
                                       : place.projections.size());
    std::shared_ptr<Type> owner = typeAfter(body_.locals[info.root].type, place, ownerUpTo);
    while (owner && owner->getKind() == Type::Kind::Reference)
        owner = std::static_pointer_cast<ReferenceType>(owner)->getBaseType();
    if (owner && owner->getKind() == Type::Kind::Custom)
    {
        auto custom = std::static_pointer_cast<CustomType>(owner);
        if (custom->implementsTrait("Drop"))
        {
            logAt(place,
                "cannot move out of '" + custom->getName()
                    + "': the type implements Drop, so its fields are released together by its own destructor.",
                E_MoveOutOfDropType);
            return;
        }
    }

    for (const MovePath &existing : ls.moved)
    {
        if (pathsOverlap(existing, info.path))
        {
            logAt(place, "use of moved value: '" + rootName + "." + joinPath(info.path) + "'", E_UseOfMovedValue);
            return;
        }
    }
    ls.moved.push_back(info.path);
}

void FunctionChecker::definePlace(const MIRPlace &place, State &st)
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.throughDeref || info.root >= st.locals.size())
        return; // a write through a pointer initializes the referent, not the local
    LocalState &ls = st.locals[info.root];
    ls.maybeInit = true;
    // The assigned path owns a fresh value: any moved marker that overlaps it is
    // cleared ('x = v' revives everything, 'x.f = v' revives x.f).
    std::vector<MovePath> kept;
    for (const MovePath &p : ls.moved)
        if (!pathsOverlap(p, info.path))
            kept.push_back(p);
    ls.moved = std::move(kept);
}

} // namespace

bool MIRBorrowCheck::enabled()
{
    const char *checker = std::getenv("LIS_BORROW_CHECK");
    return checker != nullptr && std::string(checker) == "mir";
}

void MIRBorrowCheck::run()
{
    if (!context || !context->mirProgram)
        return;

    // LIS_MIR_DEBUG=1 dumps every function body. This pass runs right after
    // MIRBuilder, so the dump is exactly the input the checks see (the
    // --print_mir flag prints the same bodies one stage later, after
    // monomorphization).
    if (std::getenv("LIS_MIR_DEBUG") != nullptr)
        printMIRProgram(*context->mirProgram, std::cout);

    if (!enabled())
        return;
    for (const auto &fn : context->mirProgram->functions)
    {
        if (!fn)
            continue;
        FunctionChecker checker(context.get(), *fn);
        checker.run();
    }
}
