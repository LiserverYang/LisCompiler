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

#include "Analysiser/Symbol.hpp"
#include "Core/Context.hpp"
#include "IR/MIRPrinter.hpp"
#include "Logger/ErrorID.hpp"
#include "Logger/Logger.hpp"

#include <iostream>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

/// Two places are rooted at the same thing: the same LOCAL, or the same GLOBAL
/// (module-level `let`). The two index spaces are disjoint, so the kind has to
/// be compared too.
bool sameRoot(const struct PlaceInfo &a, const struct PlaceInfo &b);

/// Do two path SEGMENTS denote overlapping storage? Mirrors the HIR checker:
///  - a field never overlaps an index (`a.f` vs `a[0]`);
///  - two constant indices are disjoint unless they are equal (`a[0]` vs `a[1]`);
///  - the unknown-index wildcard `[*]` overlaps every index;
///  - the deref marker `*` overlaps only itself.
bool pathSegmentOverlaps(const std::string &a, const std::string &b)
{
    if (a == b)
        return true;
    if (a == "*" || b == "*")
        return false;
    const bool aIndex = a.size() >= 2 && a.front() == '[';
    const bool bIndex = b.size() >= 2 && b.front() == '[';
    if (aIndex != bIndex || !aIndex)
        return false; // an index never overlaps a field, nor two different fields
    return a == "[*]" || b == "[*]";
}

/// One path is a prefix of the other (including the empty path = the whole
/// value), comparing every shared segment.
bool pathsOverlap(const MovePath &a, const MovePath &b)
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (!pathSegmentOverlaps(a[i], b[i]))
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
    /// Paths CONSUMED by a drop (`d;` discards a value, a scope-end drop releases
    /// one). A read or a move over either set is use-after-move, but only `moved`
    /// blocks an ASSIGNMENT: dropping the old value and immediately writing a new
    /// one is how every re-assignment is lowered (`x = S { .. };`), while a plain
    /// assignment over a MOVED binding stays E3005 (single owner, no revival).
    std::vector<MovePath> dropped;
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
        if (!samePaths(a.locals[i].dropped, b.locals[i].dropped))
            return false;
    }
    return true;
}

/// The place, decomposed for move bookkeeping.
struct PlaceInfo
{
    bool isLocal = false;   // a global / return slot is never moved out of
    bool isGlobal = false;  // a module-level `let` — borrowed like a local
    size_t root = 0;        // local index
    /// A global's identity is its NAME: MIRBuilder leaves MIRPlace::index
    /// unset for a global place (codegen looks the global up by name), so the
    /// index is not comparable across places.
    std::string globalName;
    MovePath path;          // projections up to the first Deref
    MovePath tail;          // projections AFTER the first Deref
    bool throughDeref = false;
    bool derefIsLast = false;
    size_t derefAt = 0;     // index of the first Deref projection
};

PlaceInfo describePlace(const MIRPlace &place)
{
    PlaceInfo info;
    info.isLocal = (place.base == PlaceBase::Local);
    info.isGlobal = (place.base == PlaceBase::Global);
    info.root = place.index;
    info.globalName = info.isGlobal ? place.name : std::string();
    for (size_t i = 0; i < place.projections.size(); ++i)
    {
        const Projection &p = place.projections[i];
        if (p.kind == ProjectionKind::Deref)
        {
            if (!info.throughDeref)
            {
                info.throughDeref = true;
                info.derefAt = i;
                info.derefIsLast = (i + 1 == place.projections.size());
            }
            continue;
        }
        MovePath &into = info.throughDeref ? info.tail : info.path;
        if (p.kind == ProjectionKind::Field)
            into.push_back(p.field);
        else if (p.hasConstIndex)
            into.push_back("[" + std::to_string(p.constIndex) + "]");
        else
            into.push_back("[*]"); // an unknown index may alias ANY element
    }
    return info;
}

bool sameRoot(const PlaceInfo &a, const PlaceInfo &b)
{
    if (a.isLocal != b.isLocal || a.isGlobal != b.isGlobal)
        return false;
    if (a.isGlobal)
        return a.globalName == b.globalName;
    return a.root == b.root;
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

/// How a place is USED — the conflict rules are a matrix over this and whether
/// the existing borrow is exclusive (mirrors the HIR checker's BorrowUseKind).
enum class AccessKind
{
    Read,         // a Copy read
    Write,        // an assignment target
    Move,         // a non-Copy operand consumed by value
    BorrowShared, // taking &
    BorrowMut,    // taking &mut
};

/// One borrow of a place, created by '&place' / '&mut place'.
struct BorrowRecord
{
    MIRPlace place;              // what is borrowed (root local + its path)
    bool isMut = false;
    size_t holder = 0;           // the local that stores the reference VALUE
    size_t parent = SIZE_MAX;    // the pointer this one was reborrowed through
    bool twoPhase = false;       // a call receiver / reference argument
};

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

    // ── borrow checking ─────────────────────────────────────────────────────
    /// Every borrow in the function (created site by site in the pre-pass, so
    /// the fixpoint never invents duplicates).
    std::vector<BorrowRecord> borrows_;
    /// (block, statement) -> the borrows that statement creates.
    std::map<std::pair<size_t, size_t>, std::vector<size_t>> borrowSites_;
    /// The borrow that put a reference value into a local (the alias table the
    /// deref resolution reads). Flow-insensitive, exactly like the HIR one.
    std::unordered_map<size_t, size_t> holderBorrow_;
    /// holder local -> the pointer local it was reborrowed through.
    std::unordered_map<size_t, size_t> parentOfHolder_;
    /// Locals whose value is passed to a call (receiver / reference argument):
    /// a borrow they hold is only RESERVED until the call runs (two-phase).
    std::unordered_set<size_t> callArgLocals_;
    /// Live locals after each statement (per block), for the borrow kill rule.
    std::vector<std::vector<std::vector<char>>> liveAfter_;
    std::vector<std::vector<char>> liveOut_;
    // ── dangling returns (E4007) ────────────────────────────────────────────
    /// Where the reference VALUE held by a local ultimately points (the HIR
    /// checker's per-Symbol RefOrigin, re-derived on MIR). A reference into
    /// this function's own frame (Local) dangles once the function returns.
    std::unordered_map<size_t, RefOrigin> refOriginOf_;
    /// Struct local -> the origins of its reference-typed fields.
    std::unordered_map<size_t, std::map<std::string, RefOrigin>> refFieldOriginOf_;

    /// The active-borrow set the transfer is currently working on: borrow id ->
    /// the local that CURRENTLY carries the reference value. MIR materializes
    /// '&x' into a temp and then copies the reference into the real binding
    /// ('_5 = &x; r = copy _5;'), so the borrow has to follow the copies —
    /// otherwise its live range ends at the copy and a write to x right after
    /// 'let r = &x' looks legal.
    std::map<size_t, size_t> *activeNow_ = nullptr;
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
    State transfer(size_t blockIndex, const MIRBasicBlock &block, const State &in, bool report, std::map<size_t, size_t> *active);

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
    void checkAssignTarget(const MIRPlace &place, const State &st, bool report, bool isDeclaration);
    /// A MOVE source: the full ownership rule set (E3005/E3016/E3017).
    void checkMove(const MIRPlace &place, State &st, bool report);
    /// Moving something out of a REFERENCE is E3017 (the `*p` wording when the
    /// whole referent leaves, "behind the reference" for one of its fields).
    /// Returns true when the caller must stop (reported, or a raw pointer, which
    /// the stdlib uses precisely to move values in and out).
    bool referenceMoveForbidden(const MIRPlace &place, const PlaceInfo &info, bool report);
    /// E0509: the field's owner implements Drop, so its fields are released
    /// together by its destructor. Returns true when it reported.
    bool dropOwnerForbidden(const MIRPlace &place, const PlaceInfo &info, bool report);
    /// Type rules for a DISCARDED value (`p.a;`): dropping it is a move out of
    /// the owner, so the same E3017/E0507/E0509 rules apply.
    void checkDiscardedPlace(const MIRPlace &place, bool report);
    /// Borrowing a binding that was moved out of (E4005).
    void checkBorrowOfMoved(const MIRPlace &place, const State &st);
    /// A whole local holding a `&mut T`. The type is NOT Copy, so an assignment
    /// RHS MOVES it — but MIRBuilder lowers every pointer-like operand as a
    /// pointer copy (it owns nothing to hand over), so the ownership rule has to
    /// be applied here rather than read off the operand kind.
    bool isWholeMutReference(const MIRPlace &place) const;
    /// The whole-local MIRCopy of an assignment RHS, if that is the shape.
    bool moveByAssignment(const MIRStatement &stmt, MIRPlace &source) const;
    /// Mark the destination of an assignment as owned again.
    void definePlace(const MIRPlace &place, State &st);
    /// Record that a place was consumed (a drop releases it) without reporting
    /// anything: the run-time drop flags cover the conditional cases.
    void markMoved(const MIRPlace &place, State &st);

    std::string displayName(const MIRPlace &place) const;

    // ── borrow checking ─────────────────────────────────────────────────────

    /// Number every borrow site once, build the alias/parent maps and mark the
    /// two-phase (call-argument) reservations.
    void collectBorrowSites();
    /// Successor blocks of one block.
    std::vector<size_t> successorsOf(size_t block) const;
    /// Locals READ / WRITTEN by one statement (or terminator), as masks. A
    /// place reads its base local (dereferencing a pointer loads it); only a
    /// whole-local write defines one.
    void collectStmtAccess(const MIRStatement &stmt, std::vector<char> &reads, std::vector<char> &writes) const;
    void collectTermAccess(const MIRTerminator &term, std::vector<char> &reads, std::vector<char> &writes) const;
    /// Backward liveness over the CFG, plus the per-statement live sets the
    /// borrow kill rule needs.
    void computeLiveness();
    /// Add a record; returns its id.
    size_t addBorrow(const MIRPlace &place, size_t holder, size_t parent, bool isMut, bool twoPhase);
    /// A place denoting a whole local (no projections).
    MIRPlace placeOfLocal(size_t index) const;
    /// The pointer local and its parents (the borrows an access THROUGH the
    /// reference is exempt from: they are what grants the access).
    std::vector<size_t> exemptionChain(size_t holder) const;
    /// What a reference local points at, when the alias table knows.
    const MIRPlace *referentOf(size_t holder) const;
    /// True when the place goes through a REFERENCE (`&T` / `&mut T`). Going
    /// through a RAW pointer (`*mut T`, stdlib only) is not a borrow at all: the
    /// heap primitives exist precisely to move values in and out
    /// (`self.data[self.len]` in Vec::pop), and nothing can dangle.
    bool derefIsReference(const PlaceInfo &info, const MIRPlace &place) const;
    /// Check one use of a place against the active borrows.
    void checkAccess(const MIRPlace &place, AccessKind kind);
    void checkAccessRaw(const MIRPlace &target, AccessKind kind, const std::vector<size_t> &exempt, const MIRPlace &diagPlace);
    /// Walk every statement in order and record where the reference values held by
    /// locals point (and the per-field origins of struct locals). The last
    /// assignment wins, exactly like the HIR checker's per-Symbol state — which is
    /// what makes 'let mut r = &G; r = &x; ret r;' rejected.
    void collectReferenceOrigins();
    RefOrigin originOfBinding(size_t local) const;
    /// Where the STORAGE a place denotes lives ('&<place>' points at it).
    RefOrigin storageOrigin(const MIRPlace &place) const;
    /// Origin of the reference VALUE stored in a struct field.
    RefOrigin fieldValueOrigin(const MIRPlace &place) const;
    RefOrigin originOfOperand(const MIROperand &op) const;
    const MIRPlace *operandPlace(const MIROperand &op) const;
    /// The name a dangling-return message should print ('ret r' names r, 'ret &x'
    /// names the place the borrow points at).
    std::string returnedName(const MIROperand &op) const;
    /// Reject 'ret <reference into this frame>' and 'ret <struct with such a
    /// field>' (E4007).
    void checkReturnTerm(const MIRTermReturn &ret);
    static bool isReferenceType(const std::shared_ptr<Type> &ty);
    static bool structHasRefFields(const std::shared_ptr<Type> &ty);

    /// Insert the borrows a statement creates, move them onto the destination of
    /// a reference copy, and drop the ones whose current holder is dead (or
    /// overwritten) — the transfer function of the borrow-set analysis.
    void settleBorrows(size_t blockIndex, size_t stmtIndex, const MIRStatement &stmt, std::map<size_t, size_t> &active);
    /// The whole local a statement copies/moves its value FROM (SIZE_MAX when the
    /// statement is not a plain reference copy).
    size_t copySourceOf(const MIRStatement &stmt) const;
    size_t wholeLocalDefinedBy(const MIRStatement &stmt) const;
    std::string borrowName(const MIRPlace &place) const;
};

void FunctionChecker::logAt(const MIRPlace &place, const std::string &msg, size_t errorId)
{
    if (!reporting_)
        return;
    if (std::getenv("LIS_MIR_DEBUG") != nullptr)
        std::cout << "  [dbg] in " << fn_.name << " / " << body_.funcName << ": " << msg << "\n";
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
        {
            for (const auto &movedPath : out_[p].locals[i].moved)
                if (!hasPath(st.locals[i].moved, movedPath))
                    st.locals[i].moved.push_back(movedPath);
            for (const auto &dropPath : out_[p].locals[i].dropped)
                if (!hasPath(st.locals[i].dropped, dropPath))
                    st.locals[i].dropped.push_back(dropPath);
        }
    return st;
}

void FunctionChecker::run()
{
    const size_t blockCount = body_.blocks.size();
    if (blockCount == 0)
        return;

    preds_.assign(blockCount, {});
    for (size_t b = 0; b < blockCount; ++b)
        for (size_t s : successorsOf(b))
            preds_[s].push_back(b);

    // Borrowing needs the alias/two-phase tables and the live ranges of the
    // locals that hold references; both are computed before any fixpoint runs.
    collectBorrowSites();
    collectReferenceOrigins();
    computeLiveness();

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
            State nextOut = transfer(b, body_.blocks[b], next, /*report=*/false, nullptr);
            if (!seen[b] || !sameState(in_[b], next) || !sameState(out_[b], nextOut))
            {
                in_[b] = std::move(next);
                out_[b] = std::move(nextOut);
                seen[b] = true;
                changed = true;
            }
        }
    }

    // ── borrow-set fixpoint (no diagnostics) ─────────────────────────────────
    // Which borrows are active at a point: created on a path reaching it and
    // still live (the holder's live range). The join takes the UNION — "may be
    // active" is what a conflict check needs.
    std::vector<std::map<size_t, size_t>> borrowIn(blockCount), borrowOut(blockCount);
    bool changedBorrows = true;
    while (changedBorrows)
    {
        changedBorrows = false;
        for (size_t b = 0; b < blockCount; ++b)
        {
            std::map<size_t, size_t> next;
            for (size_t p : preds_[b])
                next.insert(borrowOut[p].begin(), borrowOut[p].end());
            std::map<size_t, size_t> nextOut = next;
            State scratch = in_[b];
            transfer(b, body_.blocks[b], scratch, /*report=*/false, &nextOut);
            if (next != borrowIn[b] || nextOut != borrowOut[b])
            {
                borrowIn[b] = std::move(next);
                borrowOut[b] = std::move(nextOut);
                changedBorrows = true;
            }
        }
    }

    // ── reporting walk ───────────────────────────────────────────────────────
    reporting_ = true;
    for (size_t b = 0; b < blockCount; ++b)
    {
        State st = in_[b];
        std::map<size_t, size_t> active = borrowIn[b];
        transfer(b, body_.blocks[b], st, /*report=*/true, &active);
    }
    reporting_ = false;
}

State FunctionChecker::transfer(size_t blockIndex, const MIRBasicBlock &block, const State &in, bool report,
    std::map<size_t, size_t> *active)
{
    State st = in;
    std::map<size_t, size_t> *saved = activeNow_;
    activeNow_ = active;

    for (size_t i = 0; i < block.stmts.size(); ++i)
    {
        transferStmt(block.stmts[i], st, report);
        if (activeNow_)
            settleBorrows(blockIndex, i, block.stmts[i], *activeNow_);
    }
    transferTerm(block.terminator, st, report);
    if (activeNow_)
    {
        // The terminator creates no borrows, but a holder can reach its last
        // use here — which ends every borrow it held.
        const std::vector<char> &liveOut = liveOut_[blockIndex];
        for (auto it = activeNow_->begin(); it != activeNow_->end();)
        {
            const size_t holder = it->second;
            if (holder >= liveOut.size() || !liveOut[holder])
                it = activeNow_->erase(it);
            else
                ++it;
        }
    }

    activeNow_ = saved;
    return st;
}

void FunctionChecker::transferStmt(const MIRStatement &stmt, State &st, bool report)
{
    if (auto *as = std::get_if<MIRStmtAssign>(&stmt))
    {
        checkAssignTarget(as->lhs, st, report, as->isDeclaration);
        checkAccess(as->lhs, AccessKind::Write);

        // 'let b = a;' with a: &mut T MOVES the exclusive reference (the type is
        // not Copy) even though the MIR operand is a pointer copy.
        MIRPlace movedSource;
        if (moveByAssignment(stmt, movedSource))
        {
            checkAccess(movedSource, AccessKind::Move);
            checkMove(movedSource, st, report);
        }
        else
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
        checkAccess(drop->place, AccessKind::Read);
        // A drop RELEASES the value, so the place is moved out afterwards: that
        // is what makes a DISCARDED expression statement (`d;`) a move —
        // `d; let e = d;` must report use-after-move.
        //
        // It never REPORTS use-after-move itself: MIRBuilder guards a drop of a
        // conditionally moved value with run-time drop flags
        // (`if c == 1 { let b = a; } ... drop(a)` is legal and drops nothing when
        // the branch did not run). It also never reports E3011 — the same flags
        // decide whether there is anything to release.
        checkDiscardedPlace(drop->place, report);
        markMoved(drop->place, st);
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
        checkReturnTerm(*ret);
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
        // Taking a reference is an ACCESS (a second &mut, or a & while a &mut is
        // live, is E4001/E4002), the place must not be moved out of (E4005), and
        // borrowing an unassigned binding is E3011.
        checkAccess(ref->place, ref->isMut ? AccessKind::BorrowMut : AccessKind::BorrowShared);
        checkBorrowOfMoved(ref->place, st);
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
        checkAccess(cp->place, AccessKind::Read);
        checkReadable(cp->place, st, report);
        return;
    }
    if (auto *mv = std::get_if<MIRMove>(&op))
    {
        // The borrow rules come first (HIR does the same: moving a borrowed
        // place is E4004, before any ownership bookkeeping).
        checkAccess(mv->place, AccessKind::Move);
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
    if (hasPath(ls.moved, MovePath{}) || hasPath(ls.dropped, MovePath{}))
        logAt(place, "use of moved value: '" + body_.locals[info.root].name + "'", E_UseOfMovedValue);
}

void FunctionChecker::checkAssignTarget(const MIRPlace &place, const State &st, bool report, bool isDeclaration)
{
    // A DECLARATION RE-INITIALISES the binding: 'let r = &mut x;' inside a loop
    // body is legal even though the previous iteration moved r out (the HIR
    // checker resets the binding on a var-decl for the same reason). A plain
    // assignment over a moved binding stays E3005 — single owner, no revival.
    //
    // Neither rule is about COMPILER TEMPS: the single-owner story is about user
    // bindings, and MIRBuilder re-initialises a temp at every use site (the callee
    // name temp in a loop body is assigned, dropped at the block end and assigned
    // again on the next iteration).
    if (isDeclaration)
        return;

    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.throughDeref || info.root >= st.locals.size())
        return;
    if (body_.locals[info.root].isTemp)
        return; // a compiler temp is not a user binding (see above)
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

    // A FUNCTION value is a code pointer: it owns nothing and is never a move
    // (mirrors MIRBuilder::isCopyType).
    const bool nonCopy = place.type && !place.type->isCopyable()
                         && place.type->getKind() != Type::Kind::Function;
    if (!nonCopy)
        return; // a Copy source is a read, not a move

    if (info.throughDeref)
    {
        referenceMoveForbidden(place, info, report);
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
        for (const MovePath &p : ls.dropped)
            if (!p.empty())
                anyFieldMoved = true;
        if (anyFieldMoved)
        {
            logAt(place, "use of moved value: '" + rootName + "' (partially moved)", E_UseOfMovedValue);
            return;
        }
        if (hasPath(ls.moved, MovePath{}) || hasPath(ls.dropped, MovePath{}))
        {
            logAt(place, "use of moved value: '" + rootName + "'", E_UseOfMovedValue);
            return;
        }
        ls.moved.push_back(MovePath{}); // the whole value is gone
        return;
    }

    if (hasPath(ls.moved, MovePath{}) || hasPath(ls.dropped, MovePath{}))
    {
        logAt(place, "use of moved value: '" + rootName + "." + joinPath(info.path) + "'", E_UseOfMovedValue);
        return;
    }

    if (dropOwnerForbidden(place, info, report))
        return;

    for (const MovePath &existing : ls.moved)
    {
        if (pathsOverlap(existing, info.path))
        {
            logAt(place, "use of moved value: '" + rootName + "." + joinPath(info.path) + "'", E_UseOfMovedValue);
            return;
        }
    }
    for (const MovePath &existing : ls.dropped)
    {
        if (pathsOverlap(existing, info.path))
        {
            logAt(place, "use of moved value: '" + rootName + "." + joinPath(info.path) + "'", E_UseOfMovedValue);
            return;
        }
    }
    ls.moved.push_back(info.path);
}

bool FunctionChecker::isWholeMutReference(const MIRPlace &place) const
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.throughDeref || !info.path.empty())
        return false;
    // Only a USER binding carries the ownership rule. MIRBuilder materializes
    // every '&mut place' into a temp and copies it into the real binding
    // ('_5 = &mut x; r = copy _5;'); the temp-to-binding copy is the compiler's
    // own plumbing, not the user's 'let t = r' — reading it as a move would mark
    // the borrow temp moved and reject the very borrow it just created.
    if (body_.locals[info.root].isTemp)
        return false;
    auto ref = std::dynamic_pointer_cast<ReferenceType>(place.type);
    return ref != nullptr && ref->isMutableRef();
}

bool FunctionChecker::moveByAssignment(const MIRStatement &stmt, MIRPlace &source) const
{
    auto *as = std::get_if<MIRStmtAssign>(&stmt);
    if (!as)
        return false;
    auto *use = std::get_if<MIRRValueUse>(&as->rhs);
    if (!use)
        return false;
    auto *cp = std::get_if<MIRCopy>(&use->operand);
    if (!cp || !isWholeMutReference(cp->place))
        return false;
    source = cp->place;
    return true;
}

void FunctionChecker::checkBorrowOfMoved(const MIRPlace &place, const State &st)
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.root >= st.locals.size())
        return;
    const LocalState &ls = st.locals[info.root];
    std::vector<MovePath> consumed = ls.moved;
    consumed.insert(consumed.end(), ls.dropped.begin(), ls.dropped.end());
    for (const MovePath &moved : consumed)
    {
        if (pathsOverlap(moved, info.path))
        {
            logAt(place, "cannot borrow moved value '" + body_.locals[info.root].name + "'",
                E_CannotBorrowMovedValue);
            return;
        }
    }
}

bool FunctionChecker::referenceMoveForbidden(const MIRPlace &place, const PlaceInfo &info, bool report)
{
    // A RAW pointer is not a borrow: moving a value out through `*mut T` is
    // exactly what the stdlib heap containers do (`self.data[self.len]`).
    if (!derefIsReference(info, place))
        return true;
    // A projection AFTER the deref means a FIELD of the referent is being moved
    // out (`r.s`), which HIR reports as "behind the reference"; moving the whole
    // referent (`*p`) is the other wording.
    if (info.tail.empty())
    {
        logAt(place,
            "cannot move out of a reference: '*p' only borrows the value, so the referent still owns it.",
            E_MoveOutOfReference);
        return true;
    }
    const std::shared_ptr<Type> base = typeAfter(body_.locals[info.root].type, place, info.derefAt);
    const std::string refName = base ? base->toString() : std::string("&T");
    logAt(place,
        "cannot move out of '" + body_.locals[info.root].name + "." + joinPath(info.tail)
            + "': it is behind the reference '" + refName
            + "', so the value is only borrowed here and the referent still owns it.",
        E_MoveOutOfReference);
    return true;
}

bool FunctionChecker::dropOwnerForbidden(const MIRPlace &place, const PlaceInfo &info, bool report)
{
    // E0509: a field cannot leave a value whose type implements Drop — the
    // destructor releases that type's fields as a whole. The owner is the type
    // the LAST field projection reads out of.
    const size_t ownerUpTo = place.projections.empty()
                                 ? 0
                                 : (place.projections.back().kind == ProjectionKind::Field
                                       ? place.projections.size() - 1
                                       : place.projections.size());
    std::shared_ptr<Type> owner = typeAfter(body_.locals[info.root].type, place, ownerUpTo);
    while (owner && owner->getKind() == Type::Kind::Reference)
        owner = std::static_pointer_cast<ReferenceType>(owner)->getBaseType();
    if (!owner || owner->getKind() != Type::Kind::Custom)
        return false;
    auto custom = std::static_pointer_cast<CustomType>(owner);
    if (!custom->implementsTrait("Drop"))
        return false;
    logAt(place,
        "cannot move out of '" + custom->getName()
            + "': the type implements Drop, so its fields are released together by its own destructor.",
        E_MoveOutOfDropType);
    return true;
}

void FunctionChecker::checkDiscardedPlace(const MIRPlace &place, bool report)
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.root >= body_.locals.size())
        return;
    const bool nonCopy = place.type && !place.type->isCopyable()
                         && place.type->getKind() != Type::Kind::Function;
    if (!nonCopy)
        return;
    // A write through a reference drops the OVERWRITTEN value first (`*out = v`),
    // which is not a discarded expression and must not be read as a move out of
    // the referent — so the reference rules stay with real moves (checkMove).
    if (info.throughDeref)
        return;
    if (!info.path.empty())
        (void)dropOwnerForbidden(place, info, report);
}

void FunctionChecker::markMoved(const MIRPlace &place, State &st)
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal || info.throughDeref || info.root >= st.locals.size())
        return;
    LocalState &ls = st.locals[info.root];
    if (!hasPath(ls.moved, info.path) && !hasPath(ls.dropped, info.path))
        ls.dropped.push_back(info.path);
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
    std::vector<MovePath> keptDropped;
    for (const MovePath &p : ls.dropped)
        if (!pathsOverlap(p, info.path))
            keptDropped.push_back(p);
    ls.dropped = std::move(keptDropped);
}

// ─── borrow checking ────────────────────────────────────────────────────────
//
// A borrow is a record {place, isMut, holder}. Its LIVE RANGE is the range of the
// local holding the reference value — exactly what the liveness pass computes.
// That is the whole reason this checker runs on a CFG instead of walking a tree
// with statement ordinals: "is this borrow still live HERE" is a dataflow fact.
// An access conflicts when a live, overlapping borrow blocks it (the matrix below
// is the HIR checker's), minus the borrows it is EXEMPT from: using a reference
// is granted by the very borrows that produced it.

size_t FunctionChecker::addBorrow(const MIRPlace &place, size_t holder, size_t parent, bool isMut, bool twoPhase)
{
    BorrowRecord rec;
    rec.place = place;
    rec.isMut = isMut;
    rec.holder = holder;
    rec.parent = parent;
    rec.twoPhase = twoPhase;
    borrows_.push_back(std::move(rec));
    return borrows_.size() - 1;
}

MIRPlace FunctionChecker::placeOfLocal(size_t index) const
{
    MIRPlace place;
    place.base = PlaceBase::Local;
    place.index = index;
    place.name = body_.locals[index].name;
    place.type = body_.locals[index].type;
    return place;
}

std::vector<size_t> FunctionChecker::exemptionChain(size_t holder) const
{
    std::vector<size_t> chain;
    size_t cur = holder;
    while (true)
    {
        chain.push_back(cur);
        auto it = parentOfHolder_.find(cur);
        if (it == parentOfHolder_.end() || it->second == SIZE_MAX || it->second == cur)
            break;
        cur = it->second;
    }
    return chain;
}

const MIRPlace *FunctionChecker::referentOf(size_t holder) const
{
    auto it = holderBorrow_.find(holder);
    if (it == holderBorrow_.end())
        return nullptr;
    return &borrows_[it->second].place;
}

bool FunctionChecker::derefIsReference(const PlaceInfo &info, const MIRPlace &place) const
{
    if (!info.throughDeref || !info.isLocal || info.root >= body_.locals.size())
        return true;
    // The deref the access actually goes through is the LAST one: a chain like
    // `self.data[i]` derefs `&mut Vec` first (a reference) and then the `*mut T`
    // field (a raw pointer) — the element access is the raw one, and that is the
    // one the borrow checker does not track (HIR treats `p[i]` the same way).
    size_t lastDeref = info.derefAt;
    for (size_t i = 0; i < place.projections.size(); ++i)
        if (place.projections[i].kind == ProjectionKind::Deref)
            lastDeref = i;
    const std::shared_ptr<Type> pointee = typeAfter(body_.locals[info.root].type, place, lastDeref);
    return pointee && pointee->getKind() == Type::Kind::Reference;
}

std::string FunctionChecker::borrowName(const MIRPlace &place) const
{
    return displayName(place);
}

size_t FunctionChecker::wholeLocalDefinedBy(const MIRStatement &stmt) const
{
    const MIRPlace *lhs = nullptr;
    if (auto *as = std::get_if<MIRStmtAssign>(&stmt))
        lhs = &as->lhs;
    else if (auto *call = std::get_if<MIRStmtCall>(&stmt))
    {
        if (call->dest.has_value())
            lhs = &*call->dest;
    }
    if (!lhs)
        return SIZE_MAX;
    const PlaceInfo info = describePlace(*lhs);
    if (!info.isLocal || info.throughDeref || !info.path.empty())
        return SIZE_MAX;
    return info.root;
}

void FunctionChecker::collectBorrowSites()
{
    // A reference handed to a call is only RESERVED until the callee runs, so a
    // sibling argument may still read the place (two-phase borrow). MIR has
    // already linearized the arguments into temps, so "this holder is passed to a
    // call" is a property of the local — and the reference may take a few COPIES
    // on the way to the call ('_5 = &mut s; _6 = copy _5; call(_6)'), so the
    // marking has to travel back along the copy chain.
    auto noteCallArg = [&](const MIROperand &op)
    {
        const MIRPlace *place = nullptr;
        if (auto *cp = std::get_if<MIRCopy>(&op))
            place = &cp->place;
        else if (auto *mv = std::get_if<MIRMove>(&op))
            place = &mv->place;
        if (!place)
            return;
        const PlaceInfo info = describePlace(*place);
        if (info.isLocal && !info.throughDeref && info.path.empty())
            callArgLocals_.insert(info.root);
    };

    // Pass 1: the call arguments and the reference-copy edges between locals.
    std::vector<std::pair<size_t, size_t>> copyEdges;
    for (size_t b = 0; b < body_.blocks.size(); ++b)
    {
        const MIRBasicBlock &block = body_.blocks[b];
        for (const MIRStatement &stmt : block.stmts)
        {
            if (auto *call = std::get_if<MIRStmtCall>(&stmt))
            {
                noteCallArg(call->callee);
                for (const MIROperand &arg : call->args)
                    noteCallArg(arg);
            }
            const size_t copySource = copySourceOf(stmt);
            if (copySource != SIZE_MAX)
            {
                auto *as = std::get_if<MIRStmtAssign>(&stmt);
                const PlaceInfo dest = describePlace(as->lhs);
                if (dest.isLocal && !dest.throughDeref && dest.path.empty())
                    copyEdges.push_back({dest.root, copySource});
            }
        }
        if (auto *termCall = std::get_if<MIRTermCall>(&block.terminator))
        {
            noteCallArg(termCall->call.callee);
            for (const MIROperand &arg : termCall->call.args)
                noteCallArg(arg);
        }
    }
    bool grew = true;
    while (grew)
    {
        grew = false;
        for (const auto &edge : copyEdges)
            if (callArgLocals_.count(edge.first) && !callArgLocals_.count(edge.second))
            {
                callArgLocals_.insert(edge.second);
                grew = true;
            }
    }

    // ... but a reference that is copied into a USER BINDING is a PROMOTED
    // borrow, not a reservation: `let r = &mut v;` must keep blocking reads of v
    // even though the temp it flowed through is also passed to a call later
    // (`let r = &mut v; let x = v[0]; r.push(2);` is E4002). Propagate that back
    // along the copy chain: whoever feeds a user binding is promoted too.
    std::unordered_set<size_t> promotedTemps;
    for (const auto &edge : copyEdges)
        if (!body_.locals[edge.first].isTemp)
            promotedTemps.insert(edge.second);
    grew = true;
    while (grew)
    {
        grew = false;
        for (const auto &edge : copyEdges)
            if (promotedTemps.count(edge.first) && !promotedTemps.count(edge.second))
            {
                promotedTemps.insert(edge.second);
                grew = true;
            }
    }

    // Pass 2: the borrow sites themselves, in source order (the alias table is
    // built incrementally, exactly like the HIR checker's).
    for (size_t b = 0; b < body_.blocks.size(); ++b)
    {
        const MIRBasicBlock &block = body_.blocks[b];
        for (size_t i = 0; i < block.stmts.size(); ++i)
        {
            const MIRStatement &stmt = block.stmts[i];
            auto *as = std::get_if<MIRStmtAssign>(&stmt);
            if (!as)
                continue;

            // A reference copied into another binding keeps pointing at the same
            // place, so '*r' must keep resolving through it (and the derivation
            // chain must follow, or an access through the copy would be blocked by
            // the very borrow that grants it).
            const size_t copySource = copySourceOf(stmt);
            const PlaceInfo copyDest = describePlace(as->lhs);
            if (copySource != SIZE_MAX && copyDest.isLocal && !copyDest.throughDeref && copyDest.path.empty())
            {
                auto alias = holderBorrow_.find(copySource);
                if (alias != holderBorrow_.end())
                {
                    holderBorrow_[copyDest.root] = alias->second;
                    auto parent = parentOfHolder_.find(copySource);
                    if (parent != parentOfHolder_.end())
                        parentOfHolder_[copyDest.root] = parent->second;
                }
            }

            auto *ref = std::get_if<MIRRValueRef>(&as->rhs);
            if (!ref)
                continue;

            // Only a whole local can hold a tracked reference value.
            const PlaceInfo dest = describePlace(as->lhs);
            if (!dest.isLocal || dest.throughDeref || !dest.path.empty())
                continue;

            // A RESERVATION is a temporary taken for a call (a receiver or a
            // reference argument) — never a `let r = &mut x;` BINDING, which is a
            // promoted borrow that lasts as long as r does. Only a temp can be a
            // reservation; marking a binding relaxed reads through its &mut
            // borrow (`let r = &mut v; let x = v[0]; r.push(2);` must be rejected).
            const bool twoPhase = body_.locals[dest.root].isTemp
                                  && callArgLocals_.count(dest.root) > 0
                                  && promotedTemps.count(dest.root) == 0;
            std::vector<size_t> ids;
            const PlaceInfo rinfo = describePlace(ref->place);
            if (rinfo.throughDeref)
            {
                // '&mut *r' has two halves: the POINTER is frozen (a second
                // reborrow while this one lives is a conflict) and the REFERENT is
                // what the new reference borrows.
                ids.push_back(addBorrow(placeOfLocal(rinfo.root), dest.root, SIZE_MAX, ref->isMut, twoPhase));
                if (const MIRPlace *target = referentOf(rinfo.root))
                {
                    MIRPlace resolved = *target;
                    bool seenDeref = false;
                    for (const Projection &p : ref->place.projections)
                    {
                        if (p.kind == ProjectionKind::Deref)
                        {
                            seenDeref = true;
                            continue;
                        }
                        if (seenDeref)
                            resolved.projections.push_back(p);
                    }
                    ids.push_back(addBorrow(resolved, dest.root, rinfo.root, ref->isMut, twoPhase));
                    parentOfHolder_[dest.root] = rinfo.root;
                }
            }
            else
                ids.push_back(addBorrow(ref->place, dest.root, SIZE_MAX, ref->isMut, twoPhase));

            borrowSites_[{b, i}] = ids;
            // The alias table remembers the REFERENT (the last record), so a
            // reborrow of a reborrow still resolves to the original place.
            holderBorrow_[dest.root] = ids.back();
        }
    }
}

std::vector<size_t> FunctionChecker::successorsOf(size_t block) const
{
    std::vector<size_t> succs;
    const MIRTerminator &term = body_.blocks[block].terminator;
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
    std::vector<size_t> valid;
    for (size_t s : succs)
        if (s < body_.blocks.size())
            valid.push_back(s);
    return valid;
}

void FunctionChecker::collectStmtAccess(const MIRStatement &stmt, std::vector<char> &reads, std::vector<char> &writes) const
{
    const size_t localCount = body_.locals.size();
    reads.assign(localCount, 0);
    writes.assign(localCount, 0);

    auto readPlace = [&](const MIRPlace &place)
    {
        const PlaceInfo info = describePlace(place);
        if (info.isLocal && info.root < localCount)
            reads[info.root] = 1;
    };
    auto readOperand = [&](const MIROperand &op)
    {
        if (auto *cp = std::get_if<MIRCopy>(&op))
            readPlace(cp->place);
        else if (auto *mv = std::get_if<MIRMove>(&op))
            readPlace(mv->place);
    };
    auto readRValue = [&](const MIRRValue &rv)
    {
        if (auto *use = std::get_if<MIRRValueUse>(&rv))
            readOperand(use->operand);
        else if (auto *bin = std::get_if<MIRRValueBinaryOp>(&rv))
        {
            readOperand(bin->left);
            readOperand(bin->right);
        }
        else if (auto *un = std::get_if<MIRRValueUnaryOp>(&rv))
            readOperand(un->operand);
        else if (auto *cast = std::get_if<MIRRValueCast>(&rv))
            readOperand(cast->operand);
        else if (auto *ref = std::get_if<MIRRValueRef>(&rv))
            readPlace(ref->place);
        else if (auto *addr = std::get_if<MIRRValueAddrOf>(&rv))
            readPlace(addr->place);
        else if (auto *si = std::get_if<MIRRValueStructInit>(&rv))
        {
            for (const auto &field : si->fields)
                readOperand(field.second);
        }
        else if (auto *ai = std::get_if<MIRRValueArrayInit>(&rv))
        {
            for (const MIROperand &el : ai->elements)
                readOperand(el);
        }
    };
    auto writePlace = [&](const MIRPlace &place)
    {
        const PlaceInfo info = describePlace(place);
        if (!info.isLocal || info.root >= localCount || info.throughDeref || !info.path.empty())
            return;
        writes[info.root] = 1;
    };

    if (auto *as = std::get_if<MIRStmtAssign>(&stmt))
    {
        // A field write still READS its root binding.
        const PlaceInfo linfo = describePlace(as->lhs);
        if (linfo.isLocal && (linfo.throughDeref || !linfo.path.empty()))
            readPlace(as->lhs);
        writePlace(as->lhs);
        readRValue(as->rhs);
        return;
    }
    if (auto *call = std::get_if<MIRStmtCall>(&stmt))
    {
        readOperand(call->callee);
        for (const MIROperand &arg : call->args)
            readOperand(arg);
        if (call->dest.has_value())
            writePlace(*call->dest);
        return;
    }
    if (auto *drop = std::get_if<MIRStmtDrop>(&stmt))
        readPlace(drop->place);
}

void FunctionChecker::collectTermAccess(const MIRTerminator &term, std::vector<char> &reads, std::vector<char> &writes) const
{
    const size_t localCount = body_.locals.size();
    reads.assign(localCount, 0);
    writes.assign(localCount, 0);
    auto readOperand = [&](const MIROperand &op)
    {
        const MIRPlace *place = nullptr;
        if (auto *cp = std::get_if<MIRCopy>(&op))
            place = &cp->place;
        else if (auto *mv = std::get_if<MIRMove>(&op))
            place = &mv->place;
        if (!place)
            return;
        const PlaceInfo info = describePlace(*place);
        if (info.isLocal && info.root < localCount)
            reads[info.root] = 1;
    };

    if (auto *br = std::get_if<MIRTermBranch>(&term))
        readOperand(br->cond);
    else if (auto *ret = std::get_if<MIRTermReturn>(&term))
    {
        if (ret->value.has_value())
            readOperand(*ret->value);
    }
    else if (auto *call = std::get_if<MIRTermCall>(&term))
    {
        readOperand(call->call.callee);
        for (const MIROperand &arg : call->call.args)
            readOperand(arg);
        if (call->call.dest.has_value())
        {
            const PlaceInfo info = describePlace(*call->call.dest);
            if (info.isLocal && !info.throughDeref && info.path.empty())
                writes[info.root] = 1;
        }
    }
}

void FunctionChecker::computeLiveness()
{
    const size_t blockCount = body_.blocks.size();
    const size_t localCount = body_.locals.size();
    liveOut_.assign(blockCount, std::vector<char>(localCount, 0));
    liveAfter_.assign(blockCount, {});

    std::vector<std::vector<char>> use(blockCount, std::vector<char>(localCount, 0));
    std::vector<std::vector<char>> def(blockCount, std::vector<char>(localCount, 0));

    // Upward-exposed uses per block: walk the block BACKWARDS and record a read
    // only when no later statement in the block already defined the local.
    for (size_t b = 0; b < blockCount; ++b)
    {
        const MIRBasicBlock &block = body_.blocks[b];
        std::vector<char> r(localCount, 0), w(localCount, 0);

        collectTermAccess(block.terminator, r, w);
        for (size_t l = 0; l < localCount; ++l)
        {
            if (r[l] && !def[b][l])
                use[b][l] = 1;
            if (w[l])
            {
                def[b][l] = 1;
                use[b][l] = 0;
            }
        }

        for (size_t i = block.stmts.size(); i-- > 0;)
        {
            collectStmtAccess(block.stmts[i], r, w);
            for (size_t l = 0; l < localCount; ++l)
            {
                if (r[l] && !def[b][l])
                    use[b][l] = 1;
                if (w[l])
                {
                    def[b][l] = 1;
                    use[b][l] = 0;
                }
            }
        }
    }

    // Backward fixpoint: liveOut = union of the successors' liveIn; a definition
    // kills, an upward-exposed use generates.
    std::vector<std::vector<char>> liveIn(blockCount, std::vector<char>(localCount, 0));
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t b = blockCount; b-- > 0;)
        {
            std::vector<char> newOut(localCount, 0);
            for (size_t s : successorsOf(b))
                for (size_t l = 0; l < localCount; ++l)
                    if (liveIn[s][l])
                        newOut[l] = 1;
            std::vector<char> newIn = use[b];
            for (size_t l = 0; l < localCount; ++l)
                if (newOut[l] && !def[b][l])
                    newIn[l] = 1;
            if (newIn != liveIn[b] || newOut != liveOut_[b])
            {
                liveIn[b] = std::move(newIn);
                liveOut_[b] = std::move(newOut);
                changed = true;
            }
        }
    }

    // Per-statement live sets: the borrow kill rule reads liveAfter_[b][i].
    for (size_t b = 0; b < blockCount; ++b)
    {
        const MIRBasicBlock &block = body_.blocks[b];
        liveAfter_[b].assign(block.stmts.size(), std::vector<char>(localCount, 0));
        std::vector<char> cur = liveOut_[b];
        for (size_t i = block.stmts.size(); i-- > 0;)
        {
            liveAfter_[b][i] = cur;
            std::vector<char> r(localCount, 0), w(localCount, 0);
            collectStmtAccess(block.stmts[i], r, w);
            for (size_t l = 0; l < localCount; ++l)
                if (w[l])
                    cur[l] = 0;
            for (size_t l = 0; l < localCount; ++l)
                if (r[l])
                    cur[l] = 1;
        }
    }
}

void FunctionChecker::checkAccessRaw(const MIRPlace &target, AccessKind kind,
    const std::vector<size_t> &exempt, const MIRPlace &diagPlace)
{
    if (!activeNow_)
        return;
    const PlaceInfo tinfo = describePlace(target);
    for (const auto &entry : *activeNow_)
    {
        const BorrowRecord &b = borrows_[entry.first];
        const PlaceInfo binfo = describePlace(b.place);
        if (!sameRoot(binfo, tinfo) || !pathsOverlap(binfo.path, tinfo.path))
            continue;
        // Using a reference is GRANTED by the borrows that produced it: they are
        // what makes the access legal in the first place.
        if (std::find(exempt.begin(), exempt.end(), entry.second) != exempt.end())
            continue;

        bool conflict = false;
        switch (kind)
        {
        case AccessKind::BorrowMut: conflict = true; break;
        case AccessKind::BorrowShared: conflict = b.isMut; break;
        case AccessKind::Write: conflict = true; break;
        case AccessKind::Move: conflict = true; break;
        case AccessKind::Read: conflict = b.isMut; break;
        }
        // A reservation only relaxes what the callee has NOT touched yet: reads
        // and shared borrows, never writes or moves.
        if (conflict && b.twoPhase && b.isMut
            && (kind == AccessKind::Read || kind == AccessKind::BorrowShared))
            conflict = false;
        if (!conflict)
            continue;

        const std::string name = displayName(diagPlace);
        std::string msg;
        size_t errorId = E_SemanticError;
        switch (kind)
        {
        case AccessKind::BorrowMut:
            msg = "cannot borrow '" + name + "' as mutable because it is already borrowed";
            errorId = E_CannotBorrowMutWhileBorrowed;
            break;
        case AccessKind::BorrowShared:
            msg = "cannot borrow '" + name + "' because it is already borrowed as mutable";
            errorId = E_CannotBorrowWhileMutBorrowed;
            break;
        case AccessKind::Write:
            msg = "cannot assign to '" + name + "' because it is borrowed";
            errorId = E_CannotMutateWhileBorrowed;
            break;
        case AccessKind::Move:
            msg = "cannot move out of '" + name + "' because it is borrowed";
            errorId = E_CannotMoveWhileBorrowed;
            break;
        case AccessKind::Read:
            msg = "cannot read '" + name + "' because it is borrowed as mutable";
            errorId = E_CannotBorrowWhileMutBorrowed;
            break;
        }
        logAt(diagPlace, msg, errorId);
        return;
    }
}

void FunctionChecker::checkAccess(const MIRPlace &place, AccessKind kind)
{
    if (!activeNow_)
        return;
    const PlaceInfo info = describePlace(place);
    // A global place is checked like a local one (same table, separate index
    // space); the return slot and other compiler-made bases are not tracked.
    if (!info.isLocal && !info.isGlobal)
        return;

    if (info.throughDeref && info.isLocal)
    {
        // A raw pointer is not tracked by the borrow checker at all (HIR does the
        // same: `p[i]` is not a borrow of anything), so nothing to conflict with.
        if (!derefIsReference(info, place))
            return;
        // Taking a reference THROUGH a reference READS the pointer first — and
        // HIR reports that read before the borrow itself, which is where
        // "cannot read 'r' because it is borrowed as mutable" comes from.
        if (kind == AccessKind::BorrowMut || kind == AccessKind::BorrowShared)
        {
            checkAccessRaw(placeOfLocal(info.root), AccessKind::Read, {}, place);
            if (const MIRPlace *readTarget = referentOf(info.root))
            {
                MIRPlace resolvedRead = *readTarget;
                bool seenReadDeref = false;
                for (const Projection &p : place.projections)
                {
                    if (p.kind == ProjectionKind::Deref)
                    {
                        seenReadDeref = true;
                        continue;
                    }
                    if (seenReadDeref)
                        resolvedRead.projections.push_back(p);
                }
                checkAccessRaw(resolvedRead, AccessKind::Read, exemptionChain(info.root), place);
            }
        }
        // Going through '*r' USES r: writing through it, moving out of it or
        // reborrowing it must find the pointer free ...
        if (kind != AccessKind::Read)
            checkAccessRaw(placeOfLocal(info.root), kind, {}, place);
        // ... and the access lands on the REFERENT, exempt from this reference's
        // own derivation chain.
        if (const MIRPlace *target = referentOf(info.root))
        {
            MIRPlace resolved = *target;
            bool seenDeref = false;
            for (const Projection &p : place.projections)
            {
                if (p.kind == ProjectionKind::Deref)
                {
                    seenDeref = true;
                    continue;
                }
                if (seenDeref)
                    resolved.projections.push_back(p);
            }
            checkAccessRaw(resolved, kind, exemptionChain(info.root), place);
        }
        return;
    }

    // `&<place>` ANALYSES the place first, and a COPY-typed place is read by that
    // analysis even though the enclosing expression is a borrow (HIR does the
    // same in visit(HIRMemberAccess)/visit(HIRIndexAccess)) — which is where
    // "cannot read 'a[0]' because it is borrowed as mutable" comes from.
    if ((kind == AccessKind::BorrowMut || kind == AccessKind::BorrowShared)
        && place.type && place.type->isCopyable())
        checkAccessRaw(place, AccessKind::Read, {}, place);

    checkAccessRaw(place, kind, {}, place);
}

size_t FunctionChecker::copySourceOf(const MIRStatement &stmt) const
{
    auto *as = std::get_if<MIRStmtAssign>(&stmt);
    if (!as)
        return SIZE_MAX;
    auto *use = std::get_if<MIRRValueUse>(&as->rhs);
    if (!use)
        return SIZE_MAX;
    const MIRPlace *src = nullptr;
    if (auto *cp = std::get_if<MIRCopy>(&use->operand))
        src = &cp->place;
    else if (auto *mv = std::get_if<MIRMove>(&use->operand))
        src = &mv->place;
    if (!src)
        return SIZE_MAX;
    const PlaceInfo info = describePlace(*src);
    if (!info.isLocal || info.throughDeref || !info.path.empty())
        return SIZE_MAX;
    return info.root;
}

void FunctionChecker::settleBorrows(size_t blockIndex, size_t stmtIndex, const MIRStatement &stmt, std::map<size_t, size_t> &active)
{
    auto site = borrowSites_.find({blockIndex, stmtIndex});
    const bool creates = (site != borrowSites_.end());
    const std::vector<char> &live = liveAfter_[blockIndex][stmtIndex];
    const size_t redefined = wholeLocalDefinedBy(stmt);
    const size_t copySource = copySourceOf(stmt);

    // 1. Overwriting a local drops the borrow THAT local was carrying.
    if (redefined != SIZE_MAX && redefined != copySource)
        for (auto it = active.begin(); it != active.end();)
        {
            if (it->second == redefined)
                it = active.erase(it);
            else
                ++it;
        }

    // 2. A reference COPY carries the borrow along: '_5 = &x; r = copy _5;'
    //    means r holds the borrow now, so its live range follows r.
    if (copySource != SIZE_MAX && redefined != SIZE_MAX)
        for (auto &entry : active)
            if (entry.second == copySource)
                entry.second = redefined;

    // 3. A borrow whose current holder is dead afterwards is over.
    for (auto it = active.begin(); it != active.end();)
    {
        const size_t holder = it->second;
        if (holder >= live.size() || !live[holder])
            it = active.erase(it);
        else
            ++it;
    }

    // 4. A reference value created here becomes active AFTER this statement.
    if (creates)
        for (size_t id : site->second)
        {
            active[id] = borrows_[id].holder;
        }
}

// ─── dangling returns (E4007) ───────────────────────────────────────────────
//
// A reference is dangling after this function returns iff it (transitively)
// points into this function's own stack frame. Reference-typed params and
// globals outlive the call; local slots, by-value param slots and local struct
// fields do not. Unknown stays conservative (never falsely reject), exactly like
// the HIR checker's RefOrigin rules — re-derived here from the MIR places.

bool FunctionChecker::isReferenceType(const std::shared_ptr<Type> &ty)
{
    if (!ty)
        return false;
    if (ty->getKind() == Type::Kind::Reference)
        return true;
    // Defensive: trait-method self params are represented as SelfType.
    if (auto st = std::dynamic_pointer_cast<SelfType>(ty))
        return st->isReference();
    return false;
}

bool FunctionChecker::structHasRefFields(const std::shared_ptr<Type> &ty)
{
    auto ct = std::dynamic_pointer_cast<CustomType>(ty);
    if (!ct)
        return false;
    for (const auto &f : ct->getFields())
        if (isReferenceType(f.type))
            return true;
    return false;
}

RefOrigin FunctionChecker::originOfBinding(size_t local) const
{
    if (local >= body_.locals.size())
        return RefOrigin::Unknown;
    // A reference-typed parameter points into caller-owned storage.
    if (body_.locals[local].isArg)
        return RefOrigin::Param;
    auto it = refOriginOf_.find(local);
    if (it != refOriginOf_.end())
        return it->second;
    return RefOrigin::Unknown;
}

RefOrigin FunctionChecker::storageOrigin(const MIRPlace &place) const
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal)
        return RefOrigin::Global;
    // '&*p' / '&r.v': the storage is where the pointer points.
    if (info.throughDeref)
        return originOfBinding(info.root);
    // '&x' / '&param': the bare slot itself lives in this function's frame.
    if (info.path.empty())
        return RefOrigin::Local;
    // '&r.v' with r reference-typed — the field lives where r points.
    if (isReferenceType(body_.locals[info.root].type))
        return originOfBinding(info.root);
    // '&s.v' with s a by-value struct — a local frame copy (also for a by-value
    // PARAM: the callee owns that copy).
    return RefOrigin::Local;
}

RefOrigin FunctionChecker::fieldValueOrigin(const MIRPlace &place) const
{
    const PlaceInfo info = describePlace(place);
    if (!info.isLocal)
        return RefOrigin::Global;
    if (info.path.size() != 1)
        return RefOrigin::Unknown; // nested fields are not tracked (conservative)
    const MIRLocal &root = body_.locals[info.root];
    // The reference value lives in the struct the root binding points at.
    if (isReferenceType(root.type))
        return originOfBinding(info.root);
    if (root.isArg)
        return RefOrigin::Param; // a caller-provided struct copy
    auto it = refFieldOriginOf_.find(info.root);
    if (it != refFieldOriginOf_.end())
    {
        auto field = it->second.find(info.path[0]);
        if (field != it->second.end())
            return field->second;
    }
    return RefOrigin::Unknown;
}

const MIRPlace *FunctionChecker::operandPlace(const MIROperand &op) const
{
    if (auto *cp = std::get_if<MIRCopy>(&op))
        return &cp->place;
    if (auto *mv = std::get_if<MIRMove>(&op))
        return &mv->place;
    return nullptr;
}

RefOrigin FunctionChecker::originOfOperand(const MIROperand &op) const
{
    if (auto *c = std::get_if<MIRConst>(&op))
        return c->kind == MIRConst::Kind::String ? RefOrigin::Global : RefOrigin::Unknown;
    const MIRPlace *place = operandPlace(op);
    if (!place)
        return RefOrigin::Unknown;
    const PlaceInfo info = describePlace(*place);
    if (!info.isLocal)
        return RefOrigin::Global;
    if (info.throughDeref)
        return originOfBinding(info.root);
    if (!info.path.empty())
        return fieldValueOrigin(*place);
    return originOfBinding(info.root);
}

void FunctionChecker::collectReferenceOrigins()
{
    // Where the reference value produced by a right-hand side points.
    auto rvalueOrigin = [&](const MIRRValue &rv) -> RefOrigin
    {
        if (auto *use = std::get_if<MIRRValueUse>(&rv))
            return originOfOperand(use->operand);
        if (auto *ref = std::get_if<MIRRValueRef>(&rv))
            return storageOrigin(ref->place);
        return RefOrigin::Unknown;
    };

    for (size_t b = 0; b < body_.blocks.size(); ++b)
    {
        for (const MIRStatement &stmt : body_.blocks[b].stmts)
        {
            auto *as = std::get_if<MIRStmtAssign>(&stmt);
            if (!as)
                continue;
            const PlaceInfo dest = describePlace(as->lhs);
            if (!dest.isLocal || dest.throughDeref)
                continue;

            // A whole binding: a reference value, or a struct carrying fields.
            if (dest.path.empty())
            {
                if (isReferenceType(as->lhs.type))
                {
                    refOriginOf_[dest.root] = rvalueOrigin(as->rhs);
                    continue;
                }
                // A struct literal: read the origins off its members.
                if (const MIRRValueStructInit *init = std::get_if<MIRRValueStructInit>(&as->rhs))
                {
                    for (const auto &field : init->fields)
                        refFieldOriginOf_[dest.root][field.first] = originOfOperand(field.second);
                    continue;
                }
                // A struct COPY/MOVE: carry the source's per-field origins.
                if (auto *use = std::get_if<MIRRValueUse>(&as->rhs))
                {
                    const MIRPlace *src = nullptr;
                    if (auto *cp = std::get_if<MIRCopy>(&use->operand))
                        src = &cp->place;
                    else if (auto *mv = std::get_if<MIRMove>(&use->operand))
                        src = &mv->place;
                    if (src)
                    {
                        const PlaceInfo sinfo = describePlace(*src);
                        if (sinfo.isLocal && !sinfo.throughDeref && sinfo.path.empty())
                        {
                            auto it = refFieldOriginOf_.find(sinfo.root);
                            if (it != refFieldOriginOf_.end())
                                refFieldOriginOf_[dest.root] = it->second;
                        }
                    }
                }
                continue;
            }

            // A FIELD store of a reference value: 'h.r = &x' (or 'h.r = r2').
            if (dest.path.size() == 1 && isReferenceType(as->lhs.type))
                refFieldOriginOf_[dest.root][dest.path[0]] = rvalueOrigin(as->rhs);
        }
    }
}

std::string FunctionChecker::returnedName(const MIROperand &op) const
{
    const MIRPlace *place = operandPlace(op);
    if (!place)
        return "this value";
    const PlaceInfo info = describePlace(*place);
    // 'ret r' names the binding the user wrote.
    if (info.isLocal && !body_.locals[info.root].isTemp)
        return displayName(*place);
    // 'ret &x' returns a TEMP holding the borrow — name what it points at.
    if (info.isLocal)
        if (const MIRPlace *target = referentOf(info.root))
            return displayName(*target);
    return "this value";
}

void FunctionChecker::checkReturnTerm(const MIRTermReturn &ret)
{
    const std::shared_ptr<Type> declared = body_.returnType;
    if (!declared)
        return;

    // The returned value normally travels through the RETURN SLOT
    // ('_0 = copy _2; return;'), so a terminator without an operand is checked
    // against local 0 — whose origin the pre-pass tracked like any other local.
    const MIROperand operand = ret.value.has_value()
                                   ? *ret.value
                                   : MIROperand(MIRCopy{.place = placeOfLocal(0)});

    if (isReferenceType(declared))
    {
        if (originOfOperand(operand) != RefOrigin::Local)
            return;
        const MIRPlace *place = operandPlace(operand);
        const std::string name = returnedName(operand);
        if (place)
            logAt(*place,
                "cannot return reference to '" + name
                    + "': it does not live long enough (it points into this function's stack frame)",
                E_BorrowDoesNotLiveLongEnough);
        return;
    }

    auto ct = std::dynamic_pointer_cast<CustomType>(declared);
    if (!ct || !structHasRefFields(ct))
        return;

    // 'ret h' / 'ret H { r: &x }': MIRBuilder materializes a struct literal into a
    // temp and returns the temp, so the tracked per-field origins of the returned
    // PLACE are what the check reads (collectReferenceOrigins filled them).
    const MIRPlace *place = operandPlace(operand);
    if (!place)
        return;
    const PlaceInfo info = describePlace(*place);
    if (!info.isLocal || info.throughDeref || !info.path.empty() || body_.locals[info.root].isArg)
        return;
    auto it = refFieldOriginOf_.find(info.root);
    if (it == refFieldOriginOf_.end())
        return;
    for (const auto &entry : it->second)
        if (entry.second == RefOrigin::Local)
            logAt(*place,
                "cannot return struct: reference field '" + entry.first + "' does not live long enough",
                E_BorrowDoesNotLiveLongEnough);
}

} // namespace

bool MIRBorrowCheck::enabled()
{
    // The MIR implementation is the DEFAULT (2026-09-19): the port reached
    // parity — all 246 source-level borrow cases and the whole 1289-case suite
    // run green with it, in both modes. LIS_BORROW_CHECK=hir restores the HIR
    // implementations in HIRSemanticAnalyzer while they are still present (an
    // escape hatch for bisecting a regression); they are deleted once the MIR
    // checker has soaked.
    const char *checker = std::getenv("LIS_BORROW_CHECK");
    return !(checker != nullptr && std::string(checker) == "hir");
}

void MIRBorrowCheck::run()
{
    check();

    // Diagnostics are reported non-fatally so that all of them surface in one
    // run; if any were logged the program is INVALID, so stop here instead of
    // letting the backend emit code for it. Without this gate a rejected program
    // still produced an object file (the borrow checker's own errors used to be
    // ignored: `for e in v { v.push(2); }` compiled and then crashed at run
    // time). HIRSemanticAnalyzer::run gates the same way.
    if (Logger::GetErrorCount() > 0)
        exit(1);
}

void MIRBorrowCheck::check()
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
