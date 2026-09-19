/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/MIRBuilder.hpp"
#include "Analysiser/SymbolTable.hpp"
#include "Core/Debugging.hpp"
#include "Logger/ErrorID.hpp"

#include <cassert>
#include <stdexcept>

/**
 * 这是我以前放在 LLVM IR 阶段的 mangle 函数，你可能会用到
 */
std::string mangleName(const MIRFunction &fn)
{
    // Simple mangling: for methods → "StructName::methodName",
    // for free functions → just "funcName".
    if (fn.associatedStruct.empty())
        return fn.name;

    std::string mangled = fn.associatedStruct + "::";

    mangled += fn.name;
    return mangled;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: copy-semantics predicate
// ─────────────────────────────────────────────────────────────────────────────

bool MIRBuilder::isCopyType(const std::shared_ptr<Type> &type)
{
    // Single source of truth is Type::isCopyable() (primitives, raw pointers
    // and SHARED references — `&mut T` is not Copy).
    return !type || type->isCopyable();
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: local / temp management
// ─────────────────────────────────────────────────────────────────────────────

size_t MIRBuilder::newLocal(const std::string &name,
    std::shared_ptr<Type> type,
    bool isMutable,
    bool isTemp,
    bool isArg)
{
    size_t idx = body_->locals.size();
    body_->locals.push_back(MIRLocal{
        .index = idx,
        .name = name,
        .type = type,
        .isMutable = isMutable,
        .isTemp = isTemp,
        .isArg = isArg,
    });
    return idx;
}

MIRPlace MIRBuilder::makeTempPlace(std::shared_ptr<Type> type)
{
    std::string name = "_" + std::to_string(++tempCtr_);
    size_t idx = newLocal(name, type, /*isMutable=*/true,
        /*isTemp=*/true,
        /*isArg=*/false);
    MIRPlace place{
        .base = PlaceBase::Local,
        .index = idx,
        .name = name,
        .projections = {},
        .type = std::move(type),
    };

    // Non-Copy temporaries are owned by the current scope and get dropped at
    // block end. placeToOperand() marks a temp's root moved when its value is
    // transferred (so a moved-out temp is skipped by emitDrop), and
    // buildExprStmt() explicitly drops discarded expression results — the
    // suppression machinery already exists; this just makes temps visible to it.
    // needsDrop(), not "!Copy": a `&mut T` temp is non-Copy yet owns nothing,
    // so tracking it as an owned local would only queue a no-op drop.
    if (!ownedLocalsStack_.empty() && place.type && place.type->needsDrop())
    {
        ownedLocalsStack_.back().push_back(place);
        // Temps are moved conditionally just like named locals (`let r =
        // if c { s } else { t };` hands one of two owned temps over), so
        // they need the same runtime drop flags.
        registerDropSlots(place);
    }

    return place;
}

MIRPlace MIRBuilder::localPlace(size_t index)
{
    assert(index < body_->locals.size());
    const MIRLocal &loc = body_->locals[index];
    return MIRPlace{
        .base = PlaceBase::Local,
        .index = index,
        .name = loc.name,
        .projections = {},
        .type = loc.type,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: basic-block management
// ─────────────────────────────────────────────────────────────────────────────

BasicBlockId MIRBuilder::newBlock(const std::string &label)
{
    BasicBlockId id = body_->blocks.size();
    std::string lbl = label.empty()
                          ? ("bb" + std::to_string(id))
                          : label;
    body_->blocks.push_back(MIRBasicBlock{
        .id = id,
        .label = lbl,
        .stmts = {},
        .terminator = MIRTermUnreachable{}, // filled in by sealBlock
    });
    return id;
}

void MIRBuilder::sealBlock(BasicBlockId id, MIRTerminator term)
{
    assert(id < body_->blocks.size());
    body_->blocks[id].terminator = std::move(term);
}

void MIRBuilder::switchTo(BasicBlockId id)
{
    assert(id < body_->blocks.size());
    curBB_ = id;
}

MIRBasicBlock &MIRBuilder::currentBlock()
{
    assert(curBB_ < body_->blocks.size());
    return body_->blocks[curBB_];
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: statement emitters
// ─────────────────────────────────────────────────────────────────────────────

bool MIRBuilder::isNeverType(const std::shared_ptr<Type> &type)
{
    if (!type || type->getKind() != Type::Kind::Primitive)
        return false;
    return std::static_pointer_cast<PrimitiveType>(type)->getPrimKind()
           == PrimitiveType::PrimKind::NEVER;
}

void MIRBuilder::emit(MIRStatement stmt)
{
    // Single choke point for the DIVERGENCE invariant: once a block has been
    // sealed with MIRTermDiverge (a `panic(...)` call), nothing may be appended
    // to it — the statements that follow are unreachable.
    //
    // The guard lives here rather than in each buildXxx because a diverging call
    // can be nested ARBITRARILY DEEP inside a larger expression, and the
    // enclosing builder keeps emitting after the seal:
    //     foo(panic("x"), other())   // the `other()` call + the foo call
    //     [panic("x"), 2, 3]         // the remaining array elements
    //     s.m(panic("x"))            // the receiver borrow + the method call
    // Per-statement guards would have to be repeated at every such site (and a
    // new builder would silently miss one). Emitting into a sealed block would
    // append statements AFTER a terminator, which LLVM's verifier rejects (or
    // worse, would type-mismatch a `never` temp against the real slot type).
    //
    // Dropping the statement is correct, not merely safe: it is provably
    // unreachable, since the only way to seal with Diverge is a call that never
    // returns to its caller.
    if (std::holds_alternative<MIRTermDiverge>(currentBlock().terminator))
        return;

    currentBlock().stmts.push_back(std::move(stmt));
}

void MIRBuilder::emitAssign(MIRPlace lhs, MIRRValue rhs)
{
    // A write to a whole root local re-arms ownership: the local now owns a
    // fresh value, so a later drop is valid again. This covers both explicit
    // reassignment (x = ...) and loop-body re-initialization of a let that was
    // dropped at the previous iteration's continue/block-end edge — without it
    // the re-initialized local would be leaked (never dropped again).
    if (lhs.base == PlaceBase::Local && lhs.projections.empty())
    {
        movedLocals_.erase(lhs.index);
        // A fresh whole value re-owns every field — clear any stale partial
        // move record so a later drop decomposes nothing.
        partiallyMovedFields_.erase(lhs.index);
        // ...and re-arm the run-time ownership bits of every slot.
        writeSlotFlags(lhs.index, {}, true);
    }
    else if (lhs.base == PlaceBase::Local)
    {
        // A field write (e.g. `p.a = x`) re-owns that field and everything
        // under it: drop any partial-move path that starts with this field path,
        // or the re-assigned field's value would be skipped (leaked) at drop.
        std::vector<std::string> lhsPath;
        for (const auto &proj : lhs.projections)
            if (proj.kind == ProjectionKind::Field)
                lhsPath.push_back(proj.field);
        if (!lhsPath.empty())
        {
            auto pmIt = partiallyMovedFields_.find(lhs.index);
            if (pmIt != partiallyMovedFields_.end())
            {
                pmIt->second.erase(
                    std::remove_if(pmIt->second.begin(), pmIt->second.end(), [&](const std::vector<std::string> &existing)
                        {
                            if (existing.size() < lhsPath.size()) return false;
                            return std::equal(lhsPath.begin(), lhsPath.end(), existing.begin()); }),
                    pmIt->second.end());
                if (pmIt->second.empty())
                    partiallyMovedFields_.erase(pmIt);
            }

            // The re-assigned field owns a value again: re-arm the bits of
            // every slot at or under it.
            writeSlotFlags(lhs.index, lhsPath, true);
        }
    }

    emit(MIRStmtAssign{.lhs = std::move(lhs), .rhs = std::move(rhs)});
}

void MIRBuilder::emitDrop(MIRPlace place)
{
    // A local that was moved out on ONLY SOME paths has no static answer:
    // whether it still owns each of its slots is a run-time fact, recorded
    // in the drop flags. Handle it before the movedLocals_ short-circuit
    // below, which is exactly the test that cannot be trusted here.
    if (place.base == PlaceBase::Local && place.projections.empty()
        && dynamicSlots_.count(place.index))
    {
        emitDropDynamic(place);
        return;
    }

    // A moved-out local no longer owns its value — dropping it would double-free.
    if (place.base == PlaceBase::Local && movedLocals_.count(place.index))
        return;

    // Partial-move decomposition: if one or more fields (possibly nested) were
    // moved out of this root, dropping the whole struct would double-free the
    // moved value. Decompose recursively via emitDropPartial() so only the
    // still-owned fields are dropped.
    if (place.base == PlaceBase::Local && place.projections.empty())
    {
        auto pmIt = partiallyMovedFields_.find(place.index);
        if (pmIt != partiallyMovedFields_.end())
        {
            emitDropPartial(place, pmIt->second);
            // The root is now fully consumed — no further drop on any path.
            movedLocals_.insert(place.index);
            partiallyMovedFields_.erase(pmIt);
            return;
        }
    }

    // needsDrop() — a reference is non-Copy (for `&mut`) but owns nothing, and
    // lowering a drop of one has no glue to call.
    if (place.type && place.type->needsDrop())
        emit(MIRStmtDrop{.place = std::move(place)});
}

// ─────────────────────────────────────────────────────────────────────────────
// emitDropPartial — drop `place` while skipping moved-out sub-paths
// ─────────────────────────────────────────────────────────────────────────────
// movedPaths is a list of Field-name paths (relative to `place`) whose values
// were moved out. Everything else that is owned is dropped, recursing through
// nested structs. Leaves are emitted as projected MIRStmtDrop, which the LLVM
// backend lowers to GEP + per-field glue.

void MIRBuilder::emitDropPartial(MIRPlace place,
    const std::vector<std::vector<std::string>> &movedPaths)
{
    // No moved sub-paths — drop the whole value.
    if (movedPaths.empty())
    {
        emitDrop(place);
        return;
    }

    auto ct = std::dynamic_pointer_cast<CustomType>(place.type);
    if (!ct)
    {
        // Not a struct with movable fields — safe fallback.
        emitDrop(place);
        return;
    }

    for (const auto &field : ct->getFields())
    {
        if (!field.type || !field.type->needsDrop()) continue;
        // Arrays of Copy elements (v1) own nothing — skip their drop.
        if (field.type->getKind() == Type::Kind::Array) continue;

        bool fieldFullyMoved = false;
        std::vector<std::vector<std::string>> subPaths;
        for (const auto &path : movedPaths)
        {
            if (path.empty() || path.front() != field.name) continue;
            if (path.size() == 1)
                fieldFullyMoved = true; // the whole field moved out
            else
                subPaths.push_back(std::vector<std::string>(path.begin() + 1, path.end()));
        }

        if (fieldFullyMoved) continue; // the receiver owns this field

        MIRPlace fieldPlace = place;
        fieldPlace.projections.push_back(
            Projection{ProjectionKind::Field, field.name, 0});
        fieldPlace.type = field.type;

        if (subPaths.empty())
            emitDrop(fieldPlace);
        else
            emitDropPartial(fieldPlace, subPaths);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Drop slots & drop flags (conditional drops)
// ─────────────────────────────────────────────────────────────────────────────
// A value moved out on ONE branch is still owned on the others, so its drop
// has no single static home. The old lowering compensated by dropping it on
// the edge where it was still owned, right before the join:
//
//     let a = String::from_lit("hello");
//     let p = a.to_cstr();
//     if c == 1 { let b = a; }      // a moved only on the taken edge
//     ...                           // <- the else edge dropped `a` HERE
//
// That is not merely early: it RELEASES the value while the rest of its scope
// may still hold untracked aliases (`p` above -> use-after-free), and it makes
// the destructor run at a point the language says it does not. It was also
// simply wrong for partial moves, where the early per-field drop and the
// scope-end decomposition both ran (double free).
//
// The fix is the one Rust uses: give every drop SLOT a run-time ownership bit.
// The bit is written true where the slot becomes live (the declaration),
// cleared by the move that ends its life, and tested by the drop -- which now
// sits at the scope end, where the language says it belongs. Because the set
// and the clear both happen on the path that performs them, the bit needs no
// phi and no merge write: every path through a branch already carries the
// right value, including a path sealed by an early break or return.
//
// Only slots whose ownership actually differs across two edges take this path
// (markConditionalMoves); every other local keeps the purely static drop, so
// code that never conditionally moves a value is byte-for-byte unchanged.
// ─────────────────────────────────────────────────────────────────────────────

std::string MIRBuilder::slotKey(size_t local, const std::vector<std::string> &path)
{
    std::string key = std::to_string(local);
    for (const auto &field : path)
    {
        key += '.';
        key += field;
    }
    return key;
}

void MIRBuilder::collectDropSlots(const std::shared_ptr<Type> &type,
    std::vector<std::string> &prefix,
    std::vector<DropSlot> &out)
{
    size_t before = out.size();

    // The SAME recursion emitDropPartial() walks: a field is decomposed when
    // it needs a drop (needsDrop(): not Copy, not a pointer/reference) and is
    // not an array (arrays of Copy elements own nothing in v1).
    if (auto ct = std::dynamic_pointer_cast<CustomType>(type))
    {
        for (const auto &field : ct->getFields())
        {
            if (!field.type || !field.type->needsDrop()) continue;
            if (field.type->getKind() == Type::Kind::Array) continue;
            prefix.push_back(field.name);
            collectDropSlots(field.type, prefix, out);
            prefix.pop_back();
        }
    }

    // Nothing to decompose into: the value is its own drop target (a String,
    // an enum with tag-aware glue, or a struct whose fields are all Copy).
    if (out.size() == before)
        out.push_back(DropSlot{.path = prefix, .type = type});
}

void MIRBuilder::registerDropSlots(const MIRPlace &place)
{
    if (place.base != PlaceBase::Local || !place.projections.empty())
        return;
    if (!place.type || !place.type->needsDrop())
        return;
    if (dropSlots_.count(place.index))
        return;

    std::vector<DropSlot> slots;
    std::vector<std::string> prefix;
    collectDropSlots(place.type, prefix, slots);
    dropSlots_[place.index] = slots;

    // Create the ownership bits HERE, at the declaration, rather than lazily
    // when the first conditional move is seen. A lazy flag would be born
    // inside one branch edge and could then be read by a path that never
    // passed a write to it (a sibling edge sealed by break/continue, whose
    // drop runs at the enclosing scope end). Declaring the bit with its value
    // and clearing it on the move keeps it defined on every path by
    // construction.
    for (const auto &slot : slots)
    {
        size_t flag = newLocal("_owns" + std::to_string(++tempCtr_),
            context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL),
            /*isMutable=*/true,
            /*isTemp=*/true,
            /*isArg=*/false);
        dropFlags_[slotKey(place.index, slot.path)] = flag;

        MIRConst bit;
        bit.kind = MIRConst::Kind::Bool;
        bit.value = true;
        bit.type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        emitAssign(localPlace(flag), MIRRValueUse{.operand = std::move(bit)});
    }
}

void MIRBuilder::writeSlotFlags(size_t local, const std::vector<std::string> &path, bool owned)
{
    auto it = dropSlots_.find(local);
    if (it == dropSlots_.end())
        return;

    for (const auto &slot : it->second)
    {
        // A move/re-arm of `path` only touches the slots at or under it:
        // moving `s.a` leaves the slot `s.b` alone.
        if (path.size() > slot.path.size()) continue;
        if (!std::equal(path.begin(), path.end(), slot.path.begin())) continue;

        auto flagIt = dropFlags_.find(slotKey(local, slot.path));
        if (flagIt == dropFlags_.end()) continue;

        MIRConst bit;
        bit.kind = MIRConst::Kind::Bool;
        bit.value = owned;
        bit.type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);
        emitAssign(localPlace(flagIt->second), MIRRValueUse{.operand = std::move(bit)});
    }
}

bool MIRBuilder::slotMoved(
    const std::unordered_set<size_t> &moved,
    const std::unordered_map<size_t, std::vector<std::vector<std::string>>> &partial,
    size_t local,
    const std::vector<std::string> &path)
{
    if (moved.count(local))
        return true;

    auto it = partial.find(local);
    if (it == partial.end())
        return false;

    for (const auto &movedPath : it->second)
    {
        if (movedPath.size() > path.size())
            continue; // moved INside this slot -- the slot still owns the rest
        if (std::equal(movedPath.begin(), movedPath.end(), path.begin()))
            return true;
    }
    return false;
}

void MIRBuilder::emitDropDynamic(const MIRPlace &place)
{
    auto it = dropSlots_.find(place.index);
    if (it == dropSlots_.end())
        return;

    const auto &dynamic = dynamicSlots_[place.index];

    for (const auto &slot : it->second)
    {
        MIRPlace leaf = place;
        for (const auto &field : slot.path)
            leaf.projections.push_back(Projection{ProjectionKind::Field, field, 0});
        leaf.type = slot.type;

        std::string key = slotKey(place.index, slot.path);
        auto flagIt = dropFlags_.find(key);

        // A branch can only be appended to a block that is still open. When
        // one is not (a diverging call above sealed it with MIRTermDiverge)
        // the drops are unreachable and emit() discards the statement.
        if (dynamic.count(key) && flagIt != dropFlags_.end()
            && std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator))
        {
            BasicBlockId ownedBB = newBlock("drop_owned");
            BasicBlockId doneBB = newBlock("drop_done");
            sealBlock(curBB_,
                MIRTermBranch{
                    .cond = MIROperand(MIRCopy{localPlace(flagIt->second)}),
                    .thenBlock = ownedBB,
                    .elseBlock = doneBB,
                });
            switchTo(ownedBB);
            emit(MIRStmtDrop{.place = leaf});
            sealBlock(curBB_, MIRTermGoto{.target = doneBB});
            switchTo(doneBB);
        }
        else if (!slotMoved(movedLocals_, partiallyMovedFields_, place.index, slot.path))
        {
            emit(MIRStmtDrop{.place = leaf});
        }
    }

    // The drop consumed whatever the local still owned, so the bits are the
    // truth from here on: a second drop site reached on the same path (the
    // block-end sweep after an explicit `x;`) releases nothing. The old
    // lowering got this from movedLocals_, which cannot be path-sensitive.
    writeSlotFlags(place.index, {}, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: operand helpers
// ─────────────────────────────────────────────────────────────────────────────

MIROperand MIRBuilder::placeToOperand(MIRPlace place)
{
    // Copy types (primitives, raw pointers, shared references) transfer by
    // value. A POINTER-LIKE type is not a Move source even when it is not
    // Copy: `&mut T` is non-Copy (duplicating it would break exclusivity),
    // but an operand of it owns nothing to transfer, and the common case is
    // an IMPLICIT REBORROW — `s.push_str(&mut x)` passes a temporary borrow
    // of `x`, so recording `x` as moved would be a lie (the borrow checker
    // has already accepted the later uses of `x`). Only aggregates own a
    // value that a move has to hand over.
    if (isCopyType(place.type) || (place.type && place.type->isPointerLike()))
        return MIRCopy{.place = std::move(place)};

    // A non-Copy local read as an operand is a *move* — the source no longer
    // owns its value, so it must not be dropped at block end.
    if (place.base == PlaceBase::Local)
    {
        if (place.projections.empty())
        {
            // Whole-root move: the root owns nothing anymore. Also clears any
            // stale partial-move record so a later re-assign can't re-decompose.
            movedLocals_.insert(place.index);
            partiallyMovedFields_.erase(place.index);
            // Every slot's run-time bit drops with it.
            writeSlotFlags(place.index, {}, false);
        }
        else
        {
            // Partial (field) move, e.g. `let x = opt.value`. The root still
            // owns the remaining fields, so it is NOT fully moved: record which
            // direct field left and let emitDrop() drop the rest individually.
            // Complex chains (derefs / index / multi-level) stay conservative —
            // mark the root moved, which leaks sibling fields but never
            // double-frees.
            bool simpleFieldChain = true;
            for (const auto &proj : place.projections)
            {
                if (proj.kind != ProjectionKind::Field)
                {
                    simpleFieldChain = false;
                    break;
                }
            }
            if (simpleFieldChain)
            {
                // Record the FULL Field projection path (e.g. p.a.b →
                // ["a","b"]). emitDrop() decomposes the root recursively,
                // dropping everything except the moved-out leaves.
                std::vector<std::string> path;
                for (const auto &proj : place.projections)
                    path.push_back(proj.field);
                // Clear the bits of the slots that just left (a slot is dead
                // when it, or an ancestor of it, was moved out).
                writeSlotFlags(place.index, path, false);
                partiallyMovedFields_[place.index].push_back(std::move(path));
            }
            else
            {
                movedLocals_.insert(place.index);
                writeSlotFlags(place.index, {}, false);
            }
        }
    }

    return MIRMove{.place = std::move(place)};
}

MIROperand MIRBuilder::exprToOperand(HIRExpr *expr)
{
    MIRPlace p = buildExpr(expr);
    return placeToOperand(std::move(p));
}

// ─────────────────────────────────────────────────────────────────────────────
// BinaryOp kind conversion
// ─────────────────────────────────────────────────────────────────────────────

MIRRValueBinaryOp::Op MIRBuilder::convertBinOp(HIRBinaryOp::OpKind kind)
{
    using H = HIRBinaryOp::OpKind;
    using M = MIRRValueBinaryOp::Op;
    switch (kind)
    {
    case H::Add: return M::Add;
    case H::Sub: return M::Sub;
    case H::Mul: return M::Mul;
    case H::Div: return M::Div;
    case H::Mod: return M::Mod;
    case H::Eq: return M::Eq;
    case H::Ne: return M::Ne;
    case H::Lt: return M::Lt;
    case H::Gt: return M::Gt;
    case H::Le: return M::Le;
    case H::Ge: return M::Ge;
    case H::And: return M::And;
    case H::Or: return M::Or;
    case H::BitAnd: return M::BitAnd;
    case H::BitOr: return M::BitOr;
    case H::BitXor: return M::BitXor;
    case H::ShiftLeft: return M::Shl;
    case H::ShiftRight: return M::Shr;
    }
    throw std::runtime_error("MIRBuilder: unknown HIRBinaryOp::OpKind");
}

// ═════════════════════════════════════════════════════════════════════════════
// Top-level entry point
// ═════════════════════════════════════════════════════════════════════════════

void MIRBuilder::logAtItem(const SourcePosition &pos, size_t length, const std::string &msg)
{
    Logger::LogInfo info{};
    info.codePath = currentItemFilePath_.empty() ? context->filePath : currentItemFilePath_;
    info.code = &context->fileValue;
    info.col = pos.col;
    info.line = pos.line;
    info.length = length;
    info.beginPosition = pos.lineStart;
    info.msg = msg;
    info.errorId = E_SemanticError;
    info.exit = false; // reported, counted, and gated by run()
    Logger::Log(Logger::LogLevel::ERROR, info);
}

MIRProgram MIRBuilder::buildProgram(HIRProgram *prog)
{
    MIRProgram out;

    for (size_t itemIndex = 0; itemIndex < prog->items.size(); ++itemIndex)
    {
        auto &item = prog->items[itemIndex];
        HIRNode *raw = item.get();

        // Module attribution (parallel to hirProgram->items) so a MIR-level
        // diagnostic against a stdlib/module function names the right file.
        currentItemFilePath_ = itemIndex < context->stmtAttributions.size()
                                   ? context->stmtAttributions[itemIndex].filePath
                                   : std::string();

        if (auto *fn = dynamic_cast<HIRFunction *>(raw))
        {
            out.functions.push_back(std::make_shared<MIRFunction>(buildFunction(fn)));
        }
        else if (auto *impl = dynamic_cast<HIRImpl *>(raw))
        {
            // Each impl method becomes its own MIRFunction.
            for (auto &method : impl->methods)
                out.functions.push_back(std::make_shared<MIRFunction>(buildFunction(method.get())));
        }
        else if (auto *decl = dynamic_cast<HIRVarDecl *>(raw))
        {
            if (decl->isGlobal)
                out.globals.push_back(buildGlobal(decl));
        }
        // HIRStruct / HIRTrait / HIRImport: no MIR items generated,
        // struct layout is already encoded in Type objects.
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global variables
// ─────────────────────────────────────────────────────────────────────────────

MIRGlobal MIRBuilder::buildGlobal(HIRVarDecl *decl)
{
    std::optional<MIRRValue> init;

    if (decl->init.has_value())
    {
        // Only constant-foldable expressions should reach here after sema.
        // We lower the initialiser as if it were in a tiny function body,
        // then fish out the single RValue from the first (and only) assignment.
        //
        // For now we handle the common case of a literal directly; a full
        // constant-folder pass could run here before codegen.
        HIRExpr *initExpr = decl->init->get();
        if (auto *lit = dynamic_cast<HIRLiteral *>(initExpr))
        {
            MIRConst c;
            c.type = lit->type;
            switch (lit->kind)
            {
            case HIRLiteral::Kind::Int:
                c.kind = MIRConst::Kind::Int;
                c.value = std::get<int64_t>(lit->value);
                break;
            case HIRLiteral::Kind::Float:
                c.kind = MIRConst::Kind::Float;
                c.value = std::get<double>(lit->value);
                break;
            case HIRLiteral::Kind::Bool:
                c.kind = MIRConst::Kind::Bool;
                c.value = std::get<bool>(lit->value);
                break;
            case HIRLiteral::Kind::Char:
                c.kind = MIRConst::Kind::Char;
                c.value = std::get<char>(lit->value);
                break;
            case HIRLiteral::Kind::String:
                c.kind = MIRConst::Kind::String;
                c.value = std::get<std::string>(lit->value);
                break;
            }
            init = MIRRValueUse{.operand = std::move(c)};
        }
        // Non-literal global initialisers: leave as nullopt (zero-init),
        // or extend this with a constant-expression evaluator.
    }

    return MIRGlobal{
        .name = decl->name,
        .type = decl->type,
        .init = std::move(init),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Function lowering
// ─────────────────────────────────────────────────────────────────────────────

MIRFunction MIRBuilder::buildFunction(HIRFunction *fn)
{
    // ── reset per-function state ─────────────────────────────────────────────
    MIRBody freshBody;
    freshBody.funcName = fn->name;
    freshBody.returnType = fn->returnType;
    body_ = &freshBody;
    tempCtr_ = 0;
    varMap_.clear();
    // Local indices restart per function, so per-function ownership/loop state
    // must not leak across functions (a moved local in fn A must not suppress
    // the drop of an unrelated same-index local in fn B).
    movedLocals_.clear();
    partiallyMovedFields_.clear();
    ownedLocalsStack_.clear();
    loopTargets_.clear();
    // Drop flags are per-function too: local indices restart, so a slot of
    // fn A must never be matched against a flag of the same index in fn B.
    dropSlots_.clear();
    dropFlags_.clear();
    dynamicSlots_.clear();

    // ── local[0]: return slot ────────────────────────────────────────────────
    newLocal("_0", fn->returnType, /*isMut=*/true, /*isTemp=*/true);

    // ── locals[1..N]: parameters ─────────────────────────────────────────────
    for (auto &[pname, ptype] : fn->params)
    {
        size_t idx = newLocal(pname, ptype,
            /*isMut=*/false,
            /*isTemp=*/false,
            /*isArg=*/true);
        varMap_[pname] = idx;
    }
    freshBody.argCount = fn->params.size();

    // ── function-scope owned frame ────────────────────────────────────────────
    // By-value non-Copy parameters are owned by this function and must be
    // dropped on exit (both the explicit `ret` path — dropOwnedLocalsFrom(0)
    // in buildReturn walks frame 0 — and the fall-through path below). Push a
    // dedicated frame so args are tracked independently of the body's blocks.
    ownedLocalsStack_.emplace_back();
    for (auto &[pname, ptype] : fn->params)
    {
        if (!ptype || !ptype->needsDrop())
            continue; // Copy values and non-owning references release nothing
        // The Drop trait's `drop(self)` takes self BY VALUE; the drop glue
        // calls X::drop itself. Dropping `self` again at function exit would
        // infinite-recurse (X::drop → __drop_X → X::drop → ...).
        // associatedTrait carries the module prefix (`drop$Drop`).
        if (displayName(fn->associatedTrait) == "Drop" && pname == "self")
            continue;
        ownedLocalsStack_.back().push_back(localPlace(varMap_[pname]));
    }

    // ── entry basic block ────────────────────────────────────────────────────
    BasicBlockId entry = newBlock("entry");
    switchTo(entry);

    // Drop flags for the by-value owned parameters. Deferred until here
    // because registering a place EMITS its 'owns its value' initialisation,
    // and there was no block to emit into before the entry block existed.
    // A parameter is movable like any other local:
    //     fn take(s: String) -> i32 { if c { let b = s; } ret 0; }
    // moves `s` on one edge only, and the drop at function exit must test
    // the same runtime bit.
    for (const auto &place : ownedLocalsStack_.back())
        registerDropSlots(place);

    // ── lower body ───────────────────────────────────────────────────────────
    buildBlock(fn->body.get());

    // ── a `never` function must contain a diverging point ────────────────────
    // The declared return type is uninhabited, i.e. the function promises not to
    // return. The check is deliberately CONSERVATIVE: the built MIR must contain
    // at least one diverging terminator (a call whose result type is `never` —
    // today `panic` — seals its block with MIRTermDiverge). Proving "every path
    // diverges" needs a full dataflow analysis and would false-positive on
    // `while true { panic("..."); }`, whose exit edge exists statically; so the
    // compiler catches the common mistake (a body that just falls off its end),
    // and per-path correctness stays the language's documented UB rule.
    if (isNeverType(freshBody.returnType))
    {
        bool diverges = false;
        for (const auto &bb : freshBody.blocks)
            if (std::holds_alternative<MIRTermDiverge>(bb.terminator))
            {
                diverges = true;
                break;
            }
        if (!diverges)
            logAtItem(fn->position, fn->length,
                "function '" + fn->name + "' is declared to return 'never' but never diverges; call panic(...) (directly or through another 'never' function) on every path.");
    }

    // ── ensure the last block has a terminator ────────────────────────────────
    // If control falls off the end of a void function, add an implicit return.
    // On the fall-through edge, drop the function-scope owned frame (params)
    // before sealing — the body's own block already dropped its locals when it
    // fell through (buildBlock), but frame 0 belongs to this function only.
    MIRBasicBlock &last = body_->blocks[curBB_];
    if (std::holds_alternative<MIRTermUnreachable>(last.terminator))
    {
        // Skip the drop when the body ended with an explicit `ret`: buildBlock
        // switched curBB_ to its "dead" router block, and buildReturn already
        // dropped frame 0 on the live path. Re-dropping here would only add
        // unreachable duplicate drops.
        if (last.label != "dead")
            dropOwnedLocalsFrom(0);
        sealBlock(curBB_, MIRTermReturn{.value = std::nullopt});
    }

    MIRFunction out;
    out.name = fn->name;
    out.body = std::move(freshBody);
    out.isMethod = fn->isMethod;
    out.isStatic = fn->isStatic;
    out.associatedStruct = fn->associatedStruct;

    out.name = mangleName(out);

    for (auto &gParam : fn->gParams)
    {
        out.genericParams.push_back(gParam->getParamName());
    }

    // Methods of a GENERIC struct (`impl<T> Foo<T> { fn bar(self) }`) are
    // written against `self: Foo<T>`, so their bodies reference the struct's
    // generic params. Without them in the mono signature, monomorphization has
    // nothing to substitute (and the generic `Foo<T>` param would crash the
    // LLVM type lowering). Prepend them in declaration order (the dedup keeps
    // `impl Box { fn new<T> }` — own T == struct T — at a single [T]).
    if (!out.associatedStruct.empty())
    {
        if (auto ct = context->typeContext->getCustom(out.associatedStruct))
        {
            std::vector<std::string> structParams;
            for (auto &gp : (*ct)->getGenericParams())
            {
                auto gpTy = std::static_pointer_cast<GenericParamType>(gp);
                if (std::find(out.genericParams.begin(), out.genericParams.end(), gpTy->getParamName()) == out.genericParams.end())
                    structParams.push_back(gpTy->getParamName());
            }
            out.genericParams.insert(out.genericParams.begin(),
                structParams.begin(),
                structParams.end());
        }
    }

    if (!fn->associatedTrait.empty())
    {
        out.associatedTrait = fn->associatedTrait;
    }

    body_ = nullptr;
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Block & statement lowering
// ─────────────────────────────────────────────────────────────────────────────

void MIRBuilder::buildBlock(HIRBlock *block)
{
    // Open a fresh owned-local list for this block. Nested blocks push their
    // own list, so each scope only drops what it declared.
    ownedLocalsStack_.emplace_back();

    bool terminatedEarly = false;

    for (auto &stmt : block->stmts)
    {
        buildStmt(stmt.get());

        // A break/continue/return sealed the current block with a terminator.
        // Any following statements are dead code — route them into a fresh
        // block so they aren't appended after a terminator (which would make
        // them execute at runtime).
        if (!std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator))
        {
            curBB_ = newBlock("dead");
            switchTo(curBB_);
            terminatedEarly = true;
        }
    }

    // Fetch the list fresh: building nested blocks may have reallocated
    // ownedLocalsStack_, so a reference cached earlier would be dangling.
    auto &ownedLocals = ownedLocalsStack_.back();

    // Emit drops in reverse declaration order (LIFO, like real destructors)
    // — but only when this block ended by falling through. If a jump/return
    // sealed it, the jump handler already dropped this block's locals on that
    // edge; re-dropping here would just land in unreachable code (a "dead"
    // block) and double the drop on sibling runtime paths.
    if (!terminatedEarly)
    {
        for (auto it = ownedLocals.rbegin(); it != ownedLocals.rend(); ++it)
            emitDrop(*it);
    }

    ownedLocalsStack_.pop_back();
}

void MIRBuilder::buildStmt(HIRStmt *stmt)
{
    if (auto *decl = dynamic_cast<HIRVarDecl *>(stmt))
        return buildVarDecl(decl);

    if (auto *assign = dynamic_cast<HIRAssign *>(stmt))
        return buildAssign(assign);

    if (auto *ifStmt = dynamic_cast<HIRIf *>(stmt))
        return buildIf(ifStmt);

    if (auto *loop = dynamic_cast<HIRLoop *>(stmt))
        return buildLoop(loop);

    if (auto *ret = dynamic_cast<HIRReturn *>(stmt))
        return buildReturn(ret);

    if (auto *brk = dynamic_cast<HIRBreak *>(stmt))
        return buildBreak(brk);

    if (auto *cont = dynamic_cast<HIRContinue *>(stmt))
        return buildContinue(cont);

    if (auto *es = dynamic_cast<HIRExprStmt *>(stmt))
        return buildExprStmt(es);

    if (auto *blk = dynamic_cast<HIRBlock *>(stmt))
        return buildBlock(blk);

    throw std::runtime_error("MIRBuilder::buildStmt: unhandled HIRStmt subtype");
}

// ── let x [: T] [= init]; ────────────────────────────────────────────────────

void MIRBuilder::buildVarDecl(HIRVarDecl *decl)
{
    size_t idx = newLocal(decl->name, decl->type, decl->isMutable, /*isTemp=*/false);
    varMap_[decl->name] = idx;

    // Non-Copy locals declared in this block are owned by this scope and get
    // dropped when the block ends.
    // Registering the place also creates its drop flags, initialised to
    // 'owns its value' (see registerDropSlots).
    if (!ownedLocalsStack_.empty() && decl->type && decl->type->needsDrop())
    {
        ownedLocalsStack_.back().push_back(localPlace(idx));
        registerDropSlots(localPlace(idx));
    }

    if (decl->init.has_value())
    {
        MIRPlace dest = localPlace(idx);
        MIRRValue rhs = MIRRValueUse{.operand = exprToOperand(decl->init->get())};
        emitAssign(dest, std::move(rhs));
    }
    // No initialiser → zero-init is codegen's responsibility.
}

// ── target = value; ──────────────────────────────────────────────────────────

void MIRBuilder::buildAssign(HIRAssign *assign)
{
    // Lower the RHS first (important: avoids wrong temp ordering on self-assign)
    MIROperand rhs = exprToOperand(assign->value.get());

    // `v[i] = x` on a USER TYPE is a call to `IndexMut::set(obj, i, x)`, not a
    // store: the container hands out its elements through a method, so there is
    // no place to write. The RHS was evaluated first, exactly as for the array
    // and field forms above.
    if (auto *idx = dynamic_cast<HIRIndexAccess *>(assign->target.get()))
    {
        if (!idx->setMethodName.empty())
        {
            emitIndexMethodCall(idx, idx->setMethodName, idx->setMethodType,
                std::move(rhs));
            return;
        }
    }

    // Lower the LHS to a place.
    // The LHS must be a valid l-value: name-ref, member access, or deref.
    MIRPlace lhs = buildExpr(assign->target.get());

    // Drop the OLD value before overwriting a whole non-Copy local that owns
    // one on this path — otherwise the previous owner leaks (its drop glue
    // never runs). Not in emitAssign: buildVarDecl's initializer (and loop
    // re-inits of a let) must not drop the local that is being (re)initialized.
    // emitDrop respects the partial-move decomposition, so `x = x.field` drops
    // only the other fields.
    // The test is only 'does this place hold a value that needs a drop' —
    // emitDrop() decides whether THIS path still owns one: statically from the
    // moved-set for a local whose ownership is path-independent, and through
    // the run-time drop flags for a local that was moved on only some paths.
    // (The old `&& !movedLocals_.count(...)` skipped the drop of a
    // conditionally-moved local, whose old value IS still owned on the paths
    // that did not move it — overwriting it there would leak it.)
    if (lhs.base == PlaceBase::Local && lhs.projections.empty()
        && lhs.type && lhs.type->needsDrop())
        emitDrop(localPlace(lhs.index));

    // `*out = v` overwrites a value that lives in someone else's frame, so it is
    // NOT covered by the drop-flag machinery above. Release the old value first
    // (Rust does the same). Only the whole-pointee form is handled here: `p.f = v`
    // / `(*p).f = v` keep their existing behaviour (documented in limitations.md).
    if (lhs.projections.size() == 1 && lhs.projections[0].kind == ProjectionKind::Deref
        && lhs.type && lhs.type->needsDrop())
        emitDrop(lhs);

    // emitAssign re-arms ownership on a whole-root-local write.
    emitAssign(lhs, MIRRValueUse{.operand = std::move(rhs)});
}

// ── if cond { then } [else { else }] ─────────────────────────────────────────

void MIRBuilder::buildIf(HIRIf *ifStmt)
{
    // 1. Evaluate condition into a temp.
    MIROperand cond = exprToOperand(ifStmt->cond.get());

    // 2. Allocate successor blocks. The else block is ALWAYS created (even
    //    without an else) so the implicit-else edge can hold path-specific
    //    drops before jumping to the join.
    BasicBlockId thenId = newBlock("then");
    BasicBlockId elseId = newBlock("else");
    BasicBlockId joinId = newBlock("if_join");

    // 3. Terminate the current block with a branch.
    sealBlock(curBB_, MIRTermBranch{
                          .cond = std::move(cond),
                          .thenBlock = thenId,
                          .elseBlock = elseId,
                      });

    // 4. Snapshot the entry ownership state — each branch starts from it, so a
    //    move in one branch does not leak into the other (flow-sensitivity).
    OwnershipState entry{movedLocals_, partiallyMovedFields_};

    // A branch "fell through" when its final live block ends in Unreachable and
    // is NOT the dead-code router block buildBlock switches to after a jump.
    auto fellThrough = [&]()
    {
        return std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator)
               && currentBlock().label != "dead";
    };

    // 5. Lower then-branch. Capture the block where the branch ACTUALLY ends
    //    (thenEnd), not just its entry block thenId: when the branch body
    //    contains NESTED control flow (an inner if/while/for), that nested flow
    //    seals thenId with ITS OWN terminator and switches curBB_ to its join,
    //    so the branch "ends" at a different block than it entered.
    switchTo(thenId);
    buildBlock(ifStmt->thenBlock.get());
    BasicBlockId thenEnd = curBB_;
    bool thenFell = fellThrough();
    OwnershipState thenSt{movedLocals_, partiallyMovedFields_};

    // 6. Lower else-branch from the ENTRY state (restore the snapshot).
    movedLocals_ = entry.moved;
    partiallyMovedFields_ = entry.partial;
    switchTo(elseId);
    if (ifStmt->elseBlock.has_value())
        buildBlock(ifStmt->elseBlock->get());
    BasicBlockId elseEnd = curBB_;
    bool elseFell = fellThrough();
    OwnershipState elseSt{movedLocals_, partiallyMovedFields_};

    // 7. Record the conditional moves, then seal each real edge with a Goto to
    //    the join. An outer local owned on THIS edge but dead on the sibling
    //    was moved on only some paths: its drop is NOT emitted here (that
    //    would release the value while the rest of its scope can still reach
    //    it — a use-after-free for every untracked alias, and a destructor
    //    running at the wrong point), it is emitted at the scope end under
    //    the slot's run-time ownership bit. See the drop-flag section above.
    //
    //    Seal the branch's END block (thenEnd/elseEnd), never its entry block.
    //    The pre-fix code sealed thenId/elseId: for a branch with nested control
    //    flow that OVERWROTE the inner terminator the nested flow had sealed onto
    //    the entry block, making the inner if/while/for body unreachable
    //    (`if x>3 { if x>4 { r=1; } }` never ran r=1; `else if` segfaulted on the
    //    orphaned inner blocks). buildMatch already seals curBB_ the same way —
    //    buildIf was the only branch handler sealing the entry block.
    if (thenFell)
    {
        switchTo(thenEnd);
        markConditionalMoves(thenSt, elseSt);
        sealBlock(curBB_, MIRTermGoto{.target = joinId});
    }
    else if (std::holds_alternative<MIRTermUnreachable>(body_->blocks[thenEnd].terminator))
        sealBlock(thenEnd, MIRTermGoto{.target = joinId}); // dead router block

    if (elseFell)
    {
        switchTo(elseEnd);
        markConditionalMoves(elseSt, thenSt);
        sealBlock(curBB_, MIRTermGoto{.target = joinId});
    }
    else if (std::holds_alternative<MIRTermUnreachable>(body_->blocks[elseEnd].terminator))
        sealBlock(elseEnd, MIRTermGoto{.target = joinId}); // dead router block

    // 8. Merge at the join: a local is dead iff dead on ANY path (owned needs
    //    all paths to own it — the AND rule). Partial-move paths merge by union.
    movedLocals_ = thenSt.moved;
    movedLocals_.insert(elseSt.moved.begin(), elseSt.moved.end());
    partiallyMovedFields_ = std::move(thenSt.partial);
    for (const auto &[idx, paths] : elseSt.partial)
    {
        auto &dst = partiallyMovedFields_[idx];
        for (const auto &path : paths)
            if (std::find(dst.begin(), dst.end(), path) == dst.end())
                dst.push_back(path);
    }
    for (size_t idx : movedLocals_)
        partiallyMovedFields_.erase(idx);

    switchTo(joinId);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMatch — lower `match scrutinee { pattern => body, ... }` to a chain of
// discriminant checks. The scrutinee is moved into an owned temp; each arm
// binds its Copy payloads by projecting into the temp; a wildcard arm over a
// non-Copy enum drops the whole temp via the tag-aware glue. After the match
// the temp is marked fully moved (its payloads are now owned by the bindings or
// were dropped), so the scope-end drop is skipped.
// ─────────────────────────────────────────────────────────────────────────────

MIRPlace MIRBuilder::buildMatch(HIRMatch *match)
{
    // 1. Evaluate the scrutinee into an owned temp (an owned enum is moved).
    MIRPlace mval = makeTempPlace(match->scrutinee->type);
    emitAssign(mval, MIRRValueUse{exprToOperand(match->scrutinee.get())});

    auto enumTy = std::dynamic_pointer_cast<CustomType>(match->scrutinee->type);
    const auto &variants = enumTy->getVariants();

    // A value match (`let y = match ...`) writes each arm's tail value into a
    // shared result slot (the no-phi write-then-read pattern); a statement match
    // has a VOID slot that no arm writes.
    MIRPlace resultSlot = makeTempPlace(match->type);

    auto fellThrough = [&]()
    {
        return std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator)
               && currentBlock().label != "dead";
    };

    // Does any variant carry a non-Copy payload? (→ wildcard arms must release
    // the active payload via the whole-enum tag-aware drop glue.)
    bool enumHasNonCopy = false;
    for (const auto &v : variants)
        for (const auto &pt : v.payloadTypes)
            if (pt->needsDrop())
            {
                enumHasNonCopy = true;
                break;
            }

    BasicBlockId doneId = newBlock("match_done");

    for (size_t armIdx = 0; armIdx < match->arms.size(); ++armIdx)
    {
        auto &arm = match->arms[armIdx];
        bool isLast = (armIdx + 1 == match->arms.size());
        BasicBlockId nextId = doneId;

        if (!arm.isWildcard)
        {
            // Discriminant check: `if __m.__tag == variantIndex`.
            int64_t vi = -1;
            for (size_t i = 0; i < variants.size(); ++i)
                if (variants[i].name == arm.variantName)
                {
                    vi = (int64_t)i;
                    break;
                }
            if (vi < 0)
            {
                switchTo(doneId);
                continue;
            } // sema already errored

            BasicBlockId bodyId = newBlock("match_arm" + std::to_string(armIdx));
            nextId = isLast ? doneId : newBlock("match_check" + std::to_string(armIdx + 1));

            MIRPlace tagPlace = mval;
            tagPlace.projections.push_back(Projection{ProjectionKind::Field, "__tag", 0});
            tagPlace.type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
            MIRConst tagConst;
            tagConst.kind = MIRConst::Kind::Int;
            tagConst.value = vi;
            tagConst.type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
            MIRPlace cond = makeTempPlace(context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL));
            emitAssign(cond, MIRRValueBinaryOp{
                                 .op = MIRRValueBinaryOp::Op::Eq,
                                 .left = MIROperand(MIRCopy{tagPlace}),
                                 .right = MIROperand(tagConst),
                                 .type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL),
                             });

            sealBlock(curBB_, MIRTermBranch{
                                  .cond = MIROperand(MIRCopy{cond}),
                                  .thenBlock = bodyId,
                                  .elseBlock = nextId,
                              });
            switchTo(bodyId);
        }
        // A wildcard arm has no check — the current block flows into its body.

        // Bind payloads. Copy payloads are copied; non-Copy payloads are MOVED
        // out of the scrutinee temp and owned by a per-arm frame so each binding
        // is dropped exactly on its own arm's path (the match consumes the whole
        // scrutinee, so its drop is skipped).
        size_t armFrameBase = ownedLocalsStack_.size();
        ownedLocalsStack_.emplace_back();
        for (size_t i = 0; i < arm.bindings.size(); ++i)
        {
            const std::string &name = arm.bindings[i].first;
            auto ty = arm.bindings[i].second;
            if (!ty) continue;
            size_t idx = newLocal(name, ty, /*isMutable=*/false, /*isTemp=*/false, /*isArg=*/false);
            varMap_[name] = idx;
            MIRPlace bindPlace = localPlace(idx);
            MIRPlace payload = mval;
            payload.projections.push_back(Projection{ProjectionKind::Field, arm.variantName + "_" + std::to_string(i), 0});
            payload.type = ty;
            if (!ty->needsDrop())
                emitAssign(bindPlace, MIRRValueUse{MIROperand(MIRCopy{payload})});
            else
            {
                ownedLocalsStack_.back().push_back(localPlace(idx));
                // Transfer the payload through placeToOperand() rather than
                // building MIRMove by hand: it is the single place that decides
                // Copy vs Move AND records the transfer, so the ownership
                // bookkeeping cannot drift from the emitted operand.
                emitAssign(bindPlace, MIRRValueUse{placeToOperand(std::move(payload))});
            }
        }

        // The scrutinee temp is CONSUMED from here on: this arm took the active
        // payload (moved into the binding, or copied out for a Copy payload), and
        // every OTHER variant's slot is uninitialized on this path. Mark the temp
        // fully moved BEFORE the body so that an early exit from it — ret / break /
        // continue, all of which run dropOwnedLocalsFrom — cannot drop the temp.
        // Without this, emitDrop sees the partial-move record of the payload and
        // decomposes the drop over the remaining fields, emitting a drop for a
        // SIBLING VARIANT slot that was never written, i.e. freeing uninitialized
        // memory: `enum P { A(String), B(String) }` matched by an arm that returns
        // early drops B_0 on the A path. (Observable with a Drop counter: the bogus
        // drop runs exactly once per early exit.)
        // The wildcard arm below still drops the whole enum EXPLICITLY through
        // emit(MIRStmtDrop), which this bookkeeping deliberately does not gate.
        movedLocals_.insert(mval.index);
        partiallyMovedFields_.erase(mval.index);

        // A wildcard arm over a possibly non-Copy enum releases the active
        // payload via the whole-enum tag-aware drop glue.
        if (arm.isWildcard && enumHasNonCopy)
        {
            emit(MIRStmtDrop{.place = mval});
            movedLocals_.insert(mval.index);
            partiallyMovedFields_.erase(mval.index);
            // This raw drop bypasses emitDrop(), so clear the scrutinee's
            // drop flags by hand: a later scope-end drop (emitDropDynamic)
            // must not release the same payload a second time.
            writeSlotFlags(mval.index, {}, false);
        }

        if (arm.body)
        {
            buildBlock(arm.body.get());
        }
        else if (arm.tailValue)
        {
            // A DIVERGING value arm (`None => panic("...")`) produces no value:
            // lower the call for its effect, but write nothing to the result
            // slot — the arm's `never` temp has no compatible type for the slot,
            // and control never reaches the read anyway. emit()'s guard would
            // already drop the store; skipping it here keeps the MIR dump clean
            // (no dead store after `diverge`).
            if (isNeverType(arm.tailValue->type))
                (void)buildExpr(arm.tailValue.get());
            else
                emitAssign(resultSlot, MIRRValueUse{exprToOperand(arm.tailValue.get())});
        }

        // Drop this arm's non-Copy bindings on the fall-through path (a return
        // already dropped them via buildReturn's dropOwnedLocalsFrom). The
        // per-arm frame keeps each arm's bindings scoped to its own path, so a
        // binding that another arm (or a wildcard) didn't assign is never dropped.
        if (fellThrough())
        {
            dropOwnedLocalsFrom(armFrameBase);
            sealBlock(curBB_, MIRTermGoto{.target = doneId});
        }
        ownedLocalsStack_.pop_back();

        if (isLast || arm.isWildcard)
            switchTo(doneId);
        else
            switchTo(nextId); // the next arm's check block
    }

    // The match consumed the scrutinee: mark the temp fully moved so its
    // scope-end drop is skipped (payloads are owned by the bindings or were
    // dropped by the wildcard arm's glue).
    switchTo(doneId);
    movedLocals_.insert(mval.index);
    partiallyMovedFields_.erase(mval.index);

    return resultSlot;
}

// ─────────────────────────────────────────────────────────────────────────────
// markConditionalMoves — record per-slot ownership that differs across the
// two edges of a branch
// ─────────────────────────────────────────────────────────────────────────────
//
// This is the whole replacement for the old emitPathDrops(), which dropped a
// local on the edge where it was still owned. That drop was placed before the
// join and therefore ran while the value's scope was still open:
//
//     let a = String::from_lit("hello");
//     let p = a.to_cstr();                 // an alias the borrow checker does
//     if c == 1 { let b = a; }             //   not track (a raw pointer)
//     print_str(p);                        // <- read AFTER the early drop
//
// printed freed memory, and with a Drop counter the destructor was observable
// before the statements that follow the if. For a PARTIAL move it was worse:
// the early per-field drop and the scope-end decomposition both ran, which is
// a double free.
//
// Nothing is dropped here any more. A slot that the two edges disagree about
// is marked DYNAMIC, and emitDropDynamic() emits its drop at the scope end
// under the slot's run-time ownership bit — which each edge has already set
// correctly, because the bit is written where the slot becomes live and where
// it is moved out.

void MIRBuilder::markConditionalMoves(const OwnershipState &self, const OwnershipState &sibling)
{
    // The branch's own frame was already popped by buildBlock; these are the
    // enclosing scopes' locals — the ones that outlive the if.
    for (const auto &frame : ownedLocalsStack_)
    {
        for (const auto &place : frame)
        {
            auto slotIt = dropSlots_.find(place.index);
            if (slotIt == dropSlots_.end())
                continue;

            for (const auto &slot : slotIt->second)
            {
                bool ownedHere = !slotMoved(self.moved, self.partial, place.index, slot.path);
                bool ownedThere = !slotMoved(sibling.moved, sibling.partial, place.index, slot.path);
                if (ownedHere == ownedThere)
                    continue; // both edges agree — the static drop is exact

                dynamicSlots_[place.index].insert(slotKey(place.index, slot.path));
            }
        }
    }
}

// ── while cond { body } / for iter { body } ──────────────────────────────────

void MIRBuilder::buildLoop(HIRLoop *loop)
{
    // ┌──────────────────────────────────────────────────┐
    // │  CFG shape                                       │
    // │                                                  │
    // │  cur ──goto──> header                            │
    // │  header (while): eval cond                       │
    // │         branch(cond) → body | exit               │
    // │  body:  statements                               │
    // │         goto → header                            │
    // │  exit:  (join point, continue here after loop)   │
    // └──────────────────────────────────────────────────┘

    BasicBlockId headerId = newBlock("loop_header");
    BasicBlockId bodyId = newBlock("loop_body");
    BasicBlockId exitId = newBlock("loop_exit");

    // Fall into the loop header.
    sealBlock(curBB_, MIRTermGoto{.target = headerId});
    switchTo(headerId);

    // There is no `for` kind here: HIRBuilder desugars `for x in it` into
    // `while true { let __opt = __it.next(); match __opt { ... } }`, so the
    // header branch is always driven by loop->cond.
    if (loop->cond.has_value())
    {
        MIROperand cond = exprToOperand(loop->cond->get());
        sealBlock(curBB_, MIRTermBranch{
                              .cond = std::move(cond),
                              .thenBlock = bodyId,
                              .elseBlock = exitId,
                          });
    }
    else
    {
        // No condition (defensive: the desugared form always has one):
        // unconditional entry into the body.
        sealBlock(curBB_, MIRTermGoto{.target = bodyId});
    }

    // Make break/continue inside this loop resolve to its exit/header.
    // The frame base is captured BEFORE the body block pushes its frame, so a
    // break/continue can drop exactly the loop body's scopes and nothing above
    // them.
    size_t bodyFrameBase = ownedLocalsStack_.size();
    loopTargets_.emplace_back(LoopTarget{exitId, headerId, bodyFrameBase});

    // Lower the body.
    switchTo(bodyId);
    buildBlock(loop->body.get());

    loopTargets_.pop_back();

    // Back-edge: body jumps back to header (unless body already terminated,
    // e.g. via an inner return or a break/continue statement).
    if (std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator))
        sealBlock(curBB_, MIRTermGoto{.target = headerId});

    // Continue after the loop.
    switchTo(exitId);
}

// ── return [expr]; ────────────────────────────────────────────────────────────

void MIRBuilder::buildReturn(HIRReturn *ret)
{
    if (ret->value.has_value())
    {
        // Write into the return slot (_0) and then return. Evaluating the
        // return expression first is essential: a non-Copy `ret x;` moves `x`
        // into the slot (marking it moved), so the drop sweep below must NOT
        // drop `x` itself — otherwise the returned value would be dropped
        // before the caller ever receives it.
        //
        // A `never` value has no value to store, so only its side effects are
        // lowered. That is not just an optimisation:
        // `ret match o { Some(v) => panic("s"), None => panic("n") };` — every
        // arm diverges, so the match is `never` — hands back buildMatch's
        // result-slot block, which MIRTermDiverge does NOT seal. Routing the
        // value there would store a never operand into the (differently typed)
        // return slot and reach codegen as an ill-typed store.
        if (isNeverType((*ret->value)->type))
            (void)buildExpr(ret->value->get());
        else
        {
            MIROperand val = exprToOperand(ret->value->get());
            MIRPlace ret0{.base = PlaceBase::Local, .index = 0, .name = "_0", .projections = {}, .type = body_->returnType};
            emitAssign(ret0, MIRRValueUse{.operand = std::move(val)});
        }
    }

    // Drop all owned locals (except any moved into the return slot) before
    // the return takes effect. Without this, a function whose body ends in
    // `ret` would leak its drops into a dead block.
    dropOwnedLocalsFrom(0);

    sealBlock(curBB_, MIRTermReturn{.value = std::nullopt});
}

// ── break; / continue; ────────────────────────────────────────────────────────

void MIRBuilder::buildBreak(HIRBreak *)
{
    buildJump("break", /*toExit=*/true);
}

void MIRBuilder::buildContinue(HIRContinue *)
{
    buildJump("continue", /*toExit=*/false);
}

void MIRBuilder::buildJump(const char *keyword, bool toExit)
{
    if (loopTargets_.empty())
        throw std::runtime_error(std::string("MIRBuilder::buildJump: ") + keyword + " outside of a loop");

    const LoopTarget &target = loopTargets_.back();

    // Drop the locals owned by the loop body and any nested scopes before
    // jumping out. Enclosing (loop-outer) scopes stay alive, so only the
    // frames from the loop body's frame down are dropped — never above it.
    dropOwnedLocalsFrom(target.ownedFrameBase);

    // Seals the current block with a jump to the innermost loop's exit/header.
    sealBlock(curBB_, MIRTermGoto{.target = toExit ? target.breakTarget : target.continueTarget});
}

// ── expr; (expression used as statement) ─────────────────────────────────────

void MIRBuilder::buildExprStmt(HIRExprStmt *es)
{
    MIRPlace result = buildExpr(es->expr.get());
    // The value is discarded. If it's a non-Copy type, drop it immediately.
    emitDrop(result);
    if (result.base == PlaceBase::Local)
    {
        if (result.projections.empty())
        {
            // The whole value was dropped — block-end must not drop it again.
            movedLocals_.insert(result.index);
        }
        else
        {
            // Only a FIELD was discarded (`p.a;`): the root still owns the
            // other fields, so record the dropped field as partially moved.
            // The block-end sweep then drops the remaining fields and skips
            // the discarded one — otherwise marking the whole root moved
            // would leak the sibling fields, and dropping the whole root
            // would double-free the discarded field.
            std::vector<std::string> path;
            for (const auto &proj : result.projections)
                if (proj.kind == ProjectionKind::Field)
                    path.push_back(proj.field);
            if (!path.empty())
                partiallyMovedFields_[result.index].push_back(std::move(path));
        }
    }
}

// ── Drop owned locals in scopes down to `frameBase` (for early-exit paths) ─────

void MIRBuilder::dropOwnedLocalsFrom(size_t frameBase)
{
    // Walk scopes from innermost (last pushed) down to frameBase, emitting
    // drops in reverse declaration order within each scope. emitDrop() skips
    // locals whose value was moved out (movedLocals_), which persists across
    // all edges — a moved-out local must never be dropped on any path.
    for (size_t f = ownedLocalsStack_.size(); f > frameBase; --f)
    {
        const auto &frame = ownedLocalsStack_[f - 1];
        for (auto it = frame.rbegin(); it != frame.rend(); ++it)
            emitDrop(*it);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Expression lowering
// Each builder evaluates its sub-expressions, emits the necessary MIR
// statements, and returns the MIRPlace containing the final result.
// ═════════════════════════════════════════════════════════════════════════════

MIRPlace MIRBuilder::buildExpr(HIRExpr *expr)
{
    if (auto *lit = dynamic_cast<HIRLiteral *>(expr))
        return buildLiteral(lit);

    if (auto *ref = dynamic_cast<HIRNameRef *>(expr))
        return buildNameRef(ref);

    if (auto *bin = dynamic_cast<HIRBinaryOp *>(expr))
        return buildBinaryOp(bin);

    if (auto *cast = dynamic_cast<HIRCast *>(expr))
        return buildCast(cast);

    if (auto *call = dynamic_cast<HIRCall *>(expr))
        return buildCall(call);

    if (auto *ma = dynamic_cast<HIRMemberAccess *>(expr))
        return buildMemberAccess(ma);

    if (auto *ia = dynamic_cast<HIRIndexAccess *>(expr))
        return buildIndexAccess(ia);

    if (auto *al = dynamic_cast<HIRArrayLiteral *>(expr))
        return buildArrayLiteral(al);

    if (auto *si = dynamic_cast<HIRStructInit *>(expr))
        return buildStructInit(si);

    if (auto *vi = dynamic_cast<HIRVariantInit *>(expr))
        return buildVariantInit(vi);

    if (auto *match = dynamic_cast<HIRMatch *>(expr))
        return buildMatch(match);

    if (auto *ref = dynamic_cast<HIRRef *>(expr))
        return buildRef(ref);

    if (auto *deref = dynamic_cast<HIRDeref *>(expr))
        return buildDeref(deref);

    if (auto *tryExpr = dynamic_cast<HIRTry *>(expr))
        return buildTry(tryExpr);

    throw std::runtime_error("MIRBuilder::buildExpr: unhandled HIRExpr subtype");
}

// ── literals ──────────────────────────────────────────────────────────────────

MIRPlace MIRBuilder::buildLiteral(HIRLiteral *lit)
{
    MIRConst c;
    c.type = lit->type;
    switch (lit->kind)
    {
    case HIRLiteral::Kind::Int:
        c.kind = MIRConst::Kind::Int;
        c.value = std::get<int64_t>(lit->value);
        break;
    case HIRLiteral::Kind::Float:
        c.kind = MIRConst::Kind::Float;
        c.value = std::get<double>(lit->value);
        break;
    case HIRLiteral::Kind::Bool:
        c.kind = MIRConst::Kind::Bool;
        c.value = std::get<bool>(lit->value);
        break;
    case HIRLiteral::Kind::Char:
        c.kind = MIRConst::Kind::Char;
        c.value = std::get<char>(lit->value);
        break;
    case HIRLiteral::Kind::String:
        c.kind = MIRConst::Kind::String;
        c.value = std::get<std::string>(lit->value);
        break;
    }

    MIRPlace tmp = makeTempPlace(lit->type);
    emitAssign(tmp, MIRRValueUse{.operand = std::move(c)});
    return tmp;
}

// ── name references ───────────────────────────────────────────────────────────

MIRPlace MIRBuilder::buildNameRef(HIRNameRef *ref)
{
    auto it = varMap_.find(ref->name);
    if (it != varMap_.end())
        return localPlace(it->second);

    // A GLOBAL variable resolves to a `PlaceBase::Global` (codegen looks it up
    // by name) so reads/writes hit the llvm::GlobalVariable. Function/type
    // names fall through to the temp+String-const path below (function pointers).
    if (auto *sym = SymbolTable::getInstance().lookupSymbol(ref->name);
        sym && sym->kind == SymbolKind::GlobalVar)
    {
        MIRPlace g;
        g.base = PlaceBase::Global;
        g.name = ref->name;
        g.type = ref->type;
        return g;
    }

    // Fall back: could be a function, or a type name.
    // For function references we create a placeholder local.
    MIRPlace tmp = makeTempPlace(ref->type);
    // The name-ref itself becomes an unresolved operand – codegen resolves it.
    // We model it as a use of the name directly in a temp assignment.
    MIRConst nameConst{
        .kind = MIRConst::Kind::String,
        .value = ref->name,
        .type = ref->type,
    };
    emitAssign(tmp, MIRRValueUse{.operand = std::move(nameConst)});
    return tmp;
}

// ── binary operations ─────────────────────────────────────────────────────────

MIRPlace MIRBuilder::buildBinaryOp(HIRBinaryOp *bin)
{
    // Evaluate operands before making the temp (important for aliased places).
    MIROperand lhs = exprToOperand(bin->left.get());
    MIROperand rhs = exprToOperand(bin->right.get());

    // Operator overloading: sema resolved `a + b` to `a.add(b)` on a struct
    // implementing the operator trait. Lower to a call of the trait method.
    if (!bin->operatorMethodName.empty())
    {
        // The result temp carries the CALLEE's return type: for an operator
        // method that is bin->type, but a string comparison ('&'i8 == &i8''
        // lowered to str_cmp) calls an i32-returning helper and then produces a
        // bool from it.
        std::shared_ptr<Type> calleeRet = bin->operatorMethodType
                                              ? bin->operatorMethodType->getReturnType()
                                              : bin->type;
        MIRPlace dest = makeTempPlace(calleeRet);

        // Callee name operand (mirrors buildCall's direct-call path: the place
        // holds the fully-qualified name as a string const; lowerCall resolves
        // funcName against the module, falling back to a declared external).
        MIRPlace calleePlace = makeTempPlace(bin->operatorMethodType);
        MIRConst nameConst{
            .kind = MIRConst::Kind::String,
            .value = bin->operatorMethodName,
            .type = bin->operatorMethodType,
        };
        emitAssign(calleePlace, MIRRValueUse{.operand = std::move(nameConst)});

        emit(MIRStmtCall{
            .dest = dest,
            .callee = placeToOperand(calleePlace),
            .funcName = bin->operatorMethodName,
            .args = {std::move(lhs), std::move(rhs)},
            .genericParams = bin->operatorStructArgs,
            // A generic-param operator (`<T>::add`) needs the original op so
            // monomorphization can fall back to a direct binary op for primitive
            // instantiations (which have no `add` method).
            .genericOpFallback = convertBinOp(bin->opKind),
        });

        // `&i8 == &i8` / `!=`: the helper returns strcmp's ordering, so the bool
        // the OPERATOR yields is `str_cmp(a, b) == 0` (or `!= 0`).
        if (bin->isStrCompare)
        {
            auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
            MIRPlace boolTmp = makeTempPlace(bin->type);
            emitAssign(boolTmp, MIRRValueBinaryOp{
                                    .op = convertBinOp(bin->opKind),
                                    .left = placeToOperand(dest),
                                    .right = MIROperand{MIRConst{.kind = MIRConst::Kind::Int,
                                                                    .value = (int64_t)0,
                                                                    .type = i32Ty}},
                                    .type = bin->type,
                                });
            return boolTmp;
        }

        return dest;
    }

    MIRPlace tmp = makeTempPlace(bin->type);
    emitAssign(tmp, MIRRValueBinaryOp{
                        .op = convertBinOp(bin->opKind),
                        .left = std::move(lhs),
                        .right = std::move(rhs),
                        .type = bin->type,
                    });
    return tmp;
}

// ── cast expressions ──────────────────────────────────────────────────────────

MIRPlace MIRBuilder::buildCast(HIRCast *cast)
{
    MIROperand operand = exprToOperand(cast->expr.get());
    MIRPlace tmp = makeTempPlace(cast->targetType);
    emitAssign(tmp, MIRRValueCast{
                        .operand = std::move(operand),
                        .targetType = cast->targetType,
                    });
    return tmp;
}

// ── borrow expressions ──────────────────────────────────────────────────────────

MIRPlace MIRBuilder::buildRef(HIRRef *ref)
{
    MIRPlace place = buildExpr(ref->expr.get());

    MIRPlace tmp = makeTempPlace(ref->type);

    emitAssign(tmp, MIRRValueRef{.place = std::move(place), .isMut = ref->isMutable});

    return tmp;
}

// ── error propagation: `expr?` ────────────────────────────────────────────────
// The operand (a Result value, checked by sema) is moved into an owned temp, then:
//   tag == Ok  → the Ok payload is moved/copied into the result temp, and control
//                joins the following code;
//   otherwise  → `Err(payload)` is built into the RETURN SLOT and the function
//                returns (the enclosing function returns a Result with the same
//                layout, so the slot type matches the struct init).
// Both paths CONSUME the scrutinee temp: its active payload belongs to the
// binding/return value afterwards, and the other variant slot was never written,
// so the temp must never be dropped (the same rule buildMatch follows).
MIRPlace MIRBuilder::buildTry(HIRTry *node)
{
    auto i32Ty = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    auto boolTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL);

    MIRPlace scrutinee = makeTempPlace(node->expr->type);
    emitAssign(scrutinee, MIRRValueUse{exprToOperand(node->expr.get())});
    movedLocals_.insert(scrutinee.index);
    partiallyMovedFields_.erase(scrutinee.index);

    MIRPlace result = makeTempPlace(node->type);
    // The result slot is written only on the Ok path, so mark it consumed up
    // front: the Err path runs dropOwnedLocalsFrom(0) BEFORE any write, and a
    // non-Copy payload type would otherwise be dropped uninitialized. The
    // emitAssign on the Ok path re-arms ownership (it erases this entry).
    movedLocals_.insert(result.index);

    auto resultTy = std::dynamic_pointer_cast<CustomType>(body_->returnType);
    auto operandCt = std::dynamic_pointer_cast<CustomType>(node->expr->type);
    if (!resultTy || !operandCt || !operandCt->isEnum())
        return result; // sema already reported; keep the MIR well-formed

    int64_t okIndex = -1;
    int64_t errIndex = -1;
    std::string okSlot;
    std::string errSlot;
    std::shared_ptr<Type> errPayloadTy;
    for (size_t i = 0; i < operandCt->getVariants().size(); ++i)
    {
        const auto &v = operandCt->getVariants()[i];
        if (v.name == "Ok" && !v.payloadTypes.empty())
        {
            okIndex = (int64_t)i;
            okSlot = v.name + "_0";
        }
        else if (v.name == "Err" && !v.payloadTypes.empty())
        {
            errIndex = (int64_t)i;
            errSlot = v.name + "_0";
            errPayloadTy = v.payloadTypes[0];
        }
    }
    if (okIndex < 0 || errIndex < 0)
        return result; // sema already reported

    BasicBlockId okId = newBlock("try_ok");
    BasicBlockId errId = newBlock("try_err");
    BasicBlockId joinId = newBlock("try_join");

    // Discriminant test: `if __r.__tag == OkIndex`.
    MIRPlace tagPlace = scrutinee;
    tagPlace.projections.push_back(Projection{ProjectionKind::Field, "__tag", 0});
    tagPlace.type = i32Ty;
    MIRConst tagConst;
    tagConst.kind = MIRConst::Kind::Int;
    tagConst.value = okIndex;
    tagConst.type = i32Ty;
    MIRPlace cond = makeTempPlace(boolTy);
    emitAssign(cond, MIRRValueBinaryOp{
                         .op = MIRRValueBinaryOp::Op::Eq,
                         .left = MIROperand(MIRCopy{tagPlace}),
                         .right = MIROperand(tagConst),
                         .type = boolTy,
                     });
    sealBlock(curBB_, MIRTermBranch{
                          .cond = MIROperand(MIRCopy{cond}),
                          .thenBlock = okId,
                          .elseBlock = errId,
                      });

    // ── Ok: carry the payload on ─────────────────────────────────────────────
    switchTo(okId);
    {
        MIRPlace payload = scrutinee;
        payload.projections.push_back(Projection{ProjectionKind::Field, okSlot, 0});
        payload.type = node->type;
        emitAssign(result, MIRRValueUse{placeToOperand(std::move(payload))});
    }
    sealBlock(curBB_, MIRTermGoto{.target = joinId});

    // ── Err: return Err(payload) ─────────────────────────────────────────────
    switchTo(errId);
    {
        MIRPlace payload = scrutinee;
        payload.projections.push_back(Projection{ProjectionKind::Field, errSlot, 0});
        payload.type = errPayloadTy;

        std::vector<std::pair<std::string, MIROperand>> fields;
        MIRConst errTag;
        errTag.kind = MIRConst::Kind::Int;
        errTag.value = errIndex;
        errTag.type = i32Ty;
        fields.emplace_back("__tag", MIROperand(errTag));
        fields.emplace_back(errSlot, placeToOperand(std::move(payload)));

        MIRPlace ret0{.base = PlaceBase::Local, .index = 0, .name = "_0", .projections = {}, .type = body_->returnType};
        emitAssign(ret0, MIRRValueStructInit{
                            .structName = resultTy->getName(),
                            .fields = std::move(fields),
                            .type = body_->returnType,
                        });
        dropOwnedLocalsFrom(0);
        sealBlock(curBB_, MIRTermReturn{.value = std::nullopt});
    }

    switchTo(joinId);
    return result;
}

// ── function / method calls ───────────────────────────────────────────────────

MIRPlace MIRBuilder::buildCall(HIRCall *call)
{
    // 1. Lower each argument expression into an operand.
    //
    // `__sizeof(x)` is a TYPE query: codegen only reads the loaded value type,
    // so its argument must not be consumed. Lower it as a COPY even when the
    // type is not Copy (MIRCopy = load without a move) — otherwise the local
    // would be recorded as moved and never dropped.
    const bool sizeofBuiltin = [&]
    {
        if (auto *nr = dynamic_cast<HIRNameRef *>(call->callee.get()))
            return nr->name == "__sizeof";
        return false;
    }();
    std::vector<MIROperand> args;
    args.reserve(call->args.size());
    for (auto &arg : call->args)
    {
        if (sizeofBuiltin)
            args.push_back(MIRCopy{.place = buildExpr(arg.get())});
        else
            args.push_back(exprToOperand(arg.get()));
    }

    // 2. Lower the callee expression to a place / name.
    std::string funcName;
    MIROperand calleeOp = [&]() -> MIROperand
    {
        if (auto *nameRef = dynamic_cast<HIRNameRef *>(call->callee.get()))
        {
            funcName = nameRef->name;
            MIRPlace p = buildNameRef(nameRef);
            return placeToOperand(p);
        }
        // Generic callee (function pointer, closure, etc.)
        MIRPlace p = buildExpr(call->callee.get());
        return placeToOperand(p);
    }();

    // ── Builtin `assert` ────────────────────────────────────────────────────
    // Not a call: a conditional divergence. The condition branches to a fail
    // block that writes `file:line: assertion failed: <msg>` to stderr and
    // aborts, while the fall-through continues in a fresh block. `assert_fail`
    // is a synthesized backend entry (LLVMIRBuilder intercepts it by name,
    // exactly like panic).
    if (funcName == "assert")
    {
        auto voidTy = context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID);
        auto i8PtrTy = context->typeContext->getReference(
            context->typeContext->getPrimitive(PrimitiveType::PrimKind::I8), false);

        // The condition itself may have diverged (`assert(panic("x"))`), which
        // sealed this block: nothing to assert, everything after is dead. This
        // is the same invariant emit() enforces.
        if (std::holds_alternative<MIRTermDiverge>(currentBlock().terminator))
            return makeTempPlace(voidTy);

        MIROperand cond = args.empty()
                              ? MIROperand{MIRConst{.kind = MIRConst::Kind::Bool, .value = true, .type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::BOOL)}}
                              : std::move(args[0]);
        MIROperand msg = args.size() > 1
                             ? std::move(args[1])
                             : MIROperand{MIRConst{.kind = MIRConst::Kind::String, .value = "", .type = i8PtrTy}};

        // "<file>:<line>" — a compile-time constant the backend prints verbatim.
        std::string loc = currentItemFilePath_.empty() ? context->filePath : currentItemFilePath_;
        if (call->position.line > 0)
            loc += ":" + std::to_string(call->position.line);

        BasicBlockId okId = newBlock("assert_ok");
        BasicBlockId failId = newBlock("assert_fail");
        sealBlock(curBB_, MIRTermBranch{.cond = std::move(cond), .thenBlock = okId, .elseBlock = failId});

        switchTo(failId);
        emit(MIRStmtCall{
            .dest = std::nullopt,
            .callee = MIROperand{MIRConst{.kind = MIRConst::Kind::String, .value = "assert_fail", .type = i8PtrTy}},
            .funcName = "assert_fail",
            .args = {MIROperand{MIRConst{.kind = MIRConst::Kind::String, .value = loc, .type = i8PtrTy}},
                std::move(msg)}});
        sealBlock(curBB_, MIRTermDiverge{});

        switchTo(okId);
        return makeTempPlace(voidTy);
    }

    // 3. Create the result temp (void calls also create a unit-type temp so
    //    the code above can always return a MIRPlace).
    MIRPlace dest = makeTempPlace(call->type);

    // 4. Emit the call statement.
    emit(MIRStmtCall{
        .dest = dest,
        .callee = std::move(calleeOp),
        .funcName = funcName,
        .args = std::move(args),
        .genericParams = std::move(call->typedGenericParams)});

    // 5. A call whose return type is `never` (today: the `panic` builtin) does
    //    not return to its caller — seal the block so everything after it is
    //    treated as dead code. buildBlock sees the sealed terminator and routes
    //    the following statements into a "dead" block (skipping the block-end
    //    drop sweep, exactly like an early `ret`), and emit()'s guard drops
    //    anything the enclosing expression would still have appended here.
    //
    //    Must come AFTER the emit above, or the guard would drop the panic call
    //    itself.
    if (isNeverType(call->type))
        sealBlock(curBB_, MIRTermDiverge{});

    return dest;
}

// ── member access: expr.field ─────────────────────────────────────────────────

MIRPlace MIRBuilder::buildMemberAccess(HIRMemberAccess *ma)
{
    // Lower the object, then project into the field.
    MIRPlace obj = buildExpr(ma->object.get());

    while (obj.type->getKind() == Type::Kind::Reference)
    {
        obj.projections.insert(obj.projections.begin(), Projection{.kind = ProjectionKind::Deref});
        auto refTy = std::static_pointer_cast<ReferenceType>(obj.type);
        obj.type = refTy->getBaseType();
    }

    obj.projections.push_back(Projection{
        .kind = ProjectionKind::Field,
        .field = ma->memberName,
    });
    // Update the type to the member's type (already resolved in sema).
    if (ma->type)
        obj.type = ma->type;
    return obj;
}

// ── dereference: *p ──────────────────────────────────────────────────────────
// A HIRDeref is a PLACE, not a value (exactly like a member access): the
// operand's place gets a Deref projection appended, which loads the pointer out
// of its slot and yields the referent's address. Appending (not prepending) is
// what buildIndexAccess does and is the right order here — the operand's own
// projections run FIRST.
MIRPlace MIRBuilder::buildDeref(HIRDeref *d)
{
    MIRPlace base = buildExpr(d->operand.get());

    while (base.type && base.type->isPointerLike())
    {
        base.projections.push_back(Projection{.kind = ProjectionKind::Deref});
        base.type = base.type->getKind() == Type::Kind::Reference
                        ? std::static_pointer_cast<ReferenceType>(base.type)->getBaseType()
                        : std::static_pointer_cast<PointerType>(base.type)->getBaseType();
    }

    // The resolved type wins so the place matches what sema checked.
    if (d->type) base.type = d->type;
    return base;
}

// ── array / pointer indexing: a[i], p[i] ─────────────────────────────────────
// The object's place is built first; if it is an indirection (a reference, or
// the heap buffer `p: *mut i8`) a Deref projection is APPENDED to load the
// base pointer, then an Index projection GEPs from it (with a bounds check
// for real arrays only). lowerPlaceAsPtr already lowers Index via GEP.
MIRPlace MIRBuilder::emitIndexMethodCall(HIRIndexAccess *ia,
    const std::string &methodName,
    const std::shared_ptr<FunctionType> &methodType,
    std::optional<MIROperand> value)
{
    auto selfRefTy = methodType && !methodType->getParams().empty()
                         ? std::dynamic_pointer_cast<ReferenceType>(methodType->getParams()[0])
                         : nullptr;

    // The receiver: `&Self` / `&mut Self`, so an object that IS a reference is
    // passed by value (the pointer to the container) and anything else has its
    // address taken — exactly the rule the method-call path applies.
    MIROperand recv = [&]() -> MIROperand
    {
        auto objRef = ia->object->type ? std::dynamic_pointer_cast<ReferenceType>(ia->object->type)
                                       : nullptr;
        if (objRef && selfRefTy && (objRef->isMutableRef() || !selfRefTy->isMutableRef()))
            return exprToOperand(ia->object.get());

        MIRPlace objPlace = buildExpr(ia->object.get());
        MIRPlace refTmp = makeTempPlace(selfRefTy ? std::static_pointer_cast<Type>(selfRefTy)
                                                  : ia->object->type);
        emitAssign(refTmp, MIRRValueRef{.place = std::move(objPlace),
                          .isMut = selfRefTy ? selfRefTy->isMutableRef() : false});
        return placeToOperand(refTmp);
    }();

    MIROperand idx = exprToOperand(ia->index.get());

    // The callee name operand, exactly as buildBinaryOp builds it for an
    // overloaded operator (lowerCall resolves funcName against the module).
    MIRPlace calleePlace = makeTempPlace(methodType);
    MIRConst nameConst{
        .kind = MIRConst::Kind::String, .value = methodName, .type = methodType};
    emitAssign(calleePlace, MIRRValueUse{.operand = std::move(nameConst)});

    std::vector<MIROperand> args;
    args.push_back(std::move(recv));
    args.push_back(std::move(idx));
    if (value.has_value())
        args.push_back(std::move(*value));

    MIRPlace dest = makeTempPlace(methodType->getReturnType());
    emit(MIRStmtCall{
        .dest = dest,
        .callee = placeToOperand(calleePlace),
        .funcName = methodName,
        .args = std::move(args),
        // The struct's generic args MUST ride along: a call with a non-empty
        // genericParams is what monomorphization renames to `at_Mono_i32`.
        .genericParams = value.has_value() ? ia->setStructArgs : ia->indexStructArgs});
    return dest;
}

MIRPlace MIRBuilder::buildIndexAccess(HIRIndexAccess *ia)
{
    // A user type (`v[i]`) is indexed by CALLING the operator trait method — it
    // is a value-returning access, not a place. Arrays and raw pointers never
    // set these fields and keep the projection path below.
    if (!ia->indexMethodName.empty())
        return emitIndexMethodCall(ia, ia->indexMethodName, ia->indexMethodType);

    MIRPlace base = buildExpr(ia->object.get());

    // Deref a reference-typed base (append — the reference is the VALUE of the
    // object place, e.g. the `data` field; `&a` / `&mut a` objects get the same
    // treatment via their empty projection prefix).
    // A raw pointer base (the heap-buffer case, `data: *mut i8`) is lowered
    // exactly like a reference: load the pointer VALUE out of the slot, then GEP
    // from it. No bounds check is generated — that is the documented contract of
    // a raw pointer, and sema only accepts it inside the standard library.
    while (base.type->isPointerLike())
    {
        base.projections.push_back(Projection{.kind = ProjectionKind::Deref});
        auto baseKind = base.type->getKind();
        base.type = baseKind == Type::Kind::Reference
                        ? std::static_pointer_cast<ReferenceType>(base.type)->getBaseType()
                        : std::static_pointer_cast<PointerType>(base.type)->getBaseType();
    }

    // Materialize the index into a fresh temp so the projection can reference a
    // plain local (handles arbitrary index expressions and aliasing: a[b.c],
    // a[a[0]] — buildExpr already evaluated any side effects).
    MIRPlace idx = buildExpr(ia->index.get());
    MIRPlace idxTemp = makeTempPlace(idx.type);
    emitAssign(idxTemp, MIRRValueUse{placeToOperand(idx)});
    base.projections.push_back(Projection{
        .kind = ProjectionKind::Index,
        .localIndex = idxTemp.index,
    });
    // Update the type to the element type (already resolved in sema).
    if (ia->type)
        base.type = ia->type;
    return base;
}

// ── array literal: [a, b, c] ─────────────────────────────────────────────────

MIRPlace MIRBuilder::buildArrayLiteral(HIRArrayLiteral *al)
{
    std::vector<MIROperand> elements;
    size_t repeatCount = 0;

    if (al->isRepeat)
    {
        // `[v; N]`: evaluate the element ONCE (its side effects must not be
        // repeated N times) and let the backend replicate the operand. N can be
        // as large as MAX_ARRAY_ELEMENTS, so materializing N operands here is
        // not an option.
        repeatCount = (size_t)al->repeatCount;
        if (!al->elements.empty())
            elements.push_back(exprToOperand(al->elements[0].get()));
    }
    else
    {
        elements.reserve(al->elements.size());
        for (auto &e : al->elements)
            elements.push_back(exprToOperand(e.get()));
    }

    MIRPlace tmp = makeTempPlace(al->type);
    emitAssign(tmp, MIRRValueArrayInit{
                        .elements = std::move(elements),
                        .type = al->type,
                        .repeatCount = repeatCount,
                    });
    return tmp;
}

// ── struct initialiser: Point { x: 1, y: 2 } ─────────────────────────────────

MIRPlace MIRBuilder::buildStructInit(HIRStructInit *si)
{
    std::vector<std::pair<std::string, MIROperand>> fields;
    fields.reserve(si->members.size());

    for (auto &[fname, fexpr] : si->members)
        fields.push_back({fname, exprToOperand(fexpr.get())});

    // Use the *resolved* (possibly instantiated) type's name: for a generic
    // struct like Option<T>, si->type is the mangled instantiation (Option$i32),
    // which is what LLVM declares. structSymbol->name would be the un-mangled
    // "Option", which has no LLVM struct body → crash.
    std::string structName;
    if (auto ct = std::dynamic_pointer_cast<CustomType>(si->type))
        structName = ct->getName();

    MIRPlace tmp = makeTempPlace(si->type);
    emitAssign(tmp, MIRRValueStructInit{
                        .structName = structName,
                        .fields = std::move(fields),
                        .type = si->type,
                    });
    return tmp;
}

MIRPlace MIRBuilder::buildVariantInit(HIRVariantInit *vi)
{
    // The fat tagged-union layout: write `__tag` (the variant's discriminant =
    // its index in the enum's variants) plus the variant's payload slots.
    std::string structName;
    int64_t tag = 0;
    if (auto ct = std::dynamic_pointer_cast<CustomType>(vi->type))
    {
        structName = ct->getName();
        const auto &variants = ct->getVariants();
        for (size_t i = 0; i < variants.size(); ++i)
        {
            if (variants[i].name == vi->variantName)
            {
                tag = (int64_t)i;
                break;
            }
        }
    }

    std::vector<std::pair<std::string, MIROperand>> fields;
    MIRConst tagConst;
    tagConst.kind = MIRConst::Kind::Int;
    tagConst.value = tag;
    tagConst.type = context->typeContext->getPrimitive(PrimitiveType::PrimKind::I32);
    fields.push_back({"__tag", MIROperand(tagConst)});

    for (size_t i = 0; i < vi->args.size(); ++i)
        fields.push_back({vi->variantName + "_" + std::to_string(i), exprToOperand(vi->args[i].get())});

    MIRPlace tmp = makeTempPlace(vi->type);
    emitAssign(tmp, MIRRValueStructInit{
                        .structName = structName,
                        .fields = std::move(fields),
                        .type = vi->type,
                    });
    return tmp;
}
