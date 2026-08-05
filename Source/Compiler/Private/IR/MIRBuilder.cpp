/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/MIRBuilder.hpp"
#include "Core/Debugging.hpp"

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
    // Single source of truth is Type::isCopyable() (primitives + references).
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
    if (!ownedLocalsStack_.empty() && !isCopyType(place.type))
        ownedLocalsStack_.back().push_back(place);

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

void MIRBuilder::emit(MIRStatement stmt)
{
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
                    std::remove_if(pmIt->second.begin(), pmIt->second.end(),
                        [&](const std::vector<std::string> &existing)
                        {
                            if (existing.size() < lhsPath.size()) return false;
                            return std::equal(lhsPath.begin(), lhsPath.end(), existing.begin());
                        }),
                    pmIt->second.end());
                if (pmIt->second.empty())
                    partiallyMovedFields_.erase(pmIt);
            }
        }
    }

    emit(MIRStmtAssign{.lhs = std::move(lhs), .rhs = std::move(rhs)});
}

void MIRBuilder::emitDrop(MIRPlace place)
{
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

    if (!isCopyType(place.type))
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
        if (isCopyType(field.type)) continue;

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
// Helpers: operand helpers
// ─────────────────────────────────────────────────────────────────────────────

MIROperand MIRBuilder::placeToOperand(MIRPlace place)
{
    if (isCopyType(place.type))
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
                partiallyMovedFields_[place.index].push_back(std::move(path));
            }
            else
            {
                movedLocals_.insert(place.index);
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

MIRProgram MIRBuilder::buildProgram(HIRProgram *prog)
{
    MIRProgram out;

    for (auto &item : prog->items)
    {
        HIRNode *raw = item.get();

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
        if (isCopyType(ptype))
            continue;
        // The Drop trait's `drop(self)` takes self BY VALUE; the drop glue
        // calls X::drop itself. Dropping `self` again at function exit would
        // infinite-recurse (X::drop → __drop_X → X::drop → ...).
        if (fn->associatedTrait == "Drop" && pname == "self")
            continue;
        ownedLocalsStack_.back().push_back(localPlace(varMap_[pname]));
    }

    // ── entry basic block ────────────────────────────────────────────────────
    BasicBlockId entry = newBlock("entry");
    switchTo(entry);

    // ── lower body ───────────────────────────────────────────────────────────
    buildBlock(fn->body.get());

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
                if (std::find(out.genericParams.begin(), out.genericParams.end(),
                              gpTy->getParamName()) == out.genericParams.end())
                    structParams.push_back(gpTy->getParamName());
            }
            out.genericParams.insert(out.genericParams.begin(),
                                     structParams.begin(), structParams.end());
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
    if (!ownedLocalsStack_.empty() && !isCopyType(decl->type))
        ownedLocalsStack_.back().push_back(localPlace(idx));

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

    // Lower the LHS to a place.
    // The LHS must be a valid l-value: name-ref, member access, or deref.
    MIRPlace lhs = buildExpr(assign->target.get());

    // Drop the OLD value before overwriting a whole non-Copy local that owns
    // one on this path — otherwise the previous owner leaks (its drop glue
    // never runs). Not in emitAssign: buildVarDecl's initializer (and loop
    // re-inits of a let) must not drop the local that is being (re)initialized.
    // emitDrop respects the partial-move decomposition, so `x = x.field` drops
    // only the other fields.
    if (lhs.base == PlaceBase::Local && lhs.projections.empty()
        && !isCopyType(lhs.type) && !movedLocals_.count(lhs.index))
        emitDrop(localPlace(lhs.index));

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

    // 5. Lower then-branch.
    switchTo(thenId);
    buildBlock(ifStmt->thenBlock.get());
    bool thenFell = fellThrough();
    OwnershipState thenSt{movedLocals_, partiallyMovedFields_};

    // 6. Lower else-branch from the ENTRY state (restore the snapshot).
    movedLocals_ = entry.moved;
    partiallyMovedFields_ = entry.partial;
    switchTo(elseId);
    if (ifStmt->elseBlock.has_value())
        buildBlock(ifStmt->elseBlock->get());
    bool elseFell = fellThrough();
    OwnershipState elseSt{movedLocals_, partiallyMovedFields_};

    // 7. Path-specific drops: an outer local owned on THIS edge but dead on the
    //    sibling edge (moved there) must be dropped here before the join, or it
    //    leaks on this path. Then seal each real edge with a Goto to the join.
    if (thenFell)
    {
        switchTo(thenId);
        emitPathDrops(thenSt, elseSt);
        sealBlock(curBB_, MIRTermGoto{.target = joinId});
    }
    else if (std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator))
        sealBlock(curBB_, MIRTermGoto{.target = joinId}); // dead router block

    if (elseFell)
    {
        switchTo(elseId);
        emitPathDrops(elseSt, thenSt);
        sealBlock(curBB_, MIRTermGoto{.target = joinId});
    }
    else if (std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator))
        sealBlock(curBB_, MIRTermGoto{.target = joinId}); // dead router block

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
            if (!pt->isCopyable()) { enumHasNonCopy = true; break; }

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
                if (variants[i].name == arm.variantName) { vi = (int64_t)i; break; }
            if (vi < 0) { switchTo(doneId); continue; } // sema already errored

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
            if (isCopyType(ty))
                emitAssign(bindPlace, MIRRValueUse{MIROperand(MIRCopy{payload})});
            else
            {
                ownedLocalsStack_.back().push_back(localPlace(idx));
                emitAssign(bindPlace, MIRRValueUse{MIROperand(MIRMove{payload})});
            }
        }

        // A wildcard arm over a possibly non-Copy enum releases the active
        // payload via the whole-enum tag-aware drop glue.
        if (arm.isWildcard && enumHasNonCopy)
        {
            emit(MIRStmtDrop{.place = mval});
            movedLocals_.insert(mval.index);
            partiallyMovedFields_.erase(mval.index);
        }

        if (arm.body)
            buildBlock(arm.body.get());
        else if (arm.tailValue)
            emitAssign(resultSlot, MIRRValueUse{exprToOperand(arm.tailValue.get())});

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
// emitPathDrops — drop outer locals owned on this path but dead on the sibling
// ─────────────────────────────────────────────────────────────────────────────

void MIRBuilder::emitPathDrops(const OwnershipState &self, const OwnershipState &sibling)
{
    // Make the global state reflect THIS path so emitDrop's movedLocals_ check
    // matches ownership on this edge.
    movedLocals_ = self.moved;
    partiallyMovedFields_ = self.partial;

    // The branch's own frame was already popped by buildBlock; these are the
    // enclosing scopes' locals — the ones that outlive the if.
    for (const auto &frame : ownedLocalsStack_)
    {
        for (const auto &place : frame)
        {
            size_t i = place.index;
            if (self.moved.count(i)) continue; // not owned on this path

            if (sibling.moved.count(i))
            {
                // Fully moved on the sibling path → drop the whole value here.
                emitDrop(place);
                continue;
            }
            auto sit = sibling.partial.find(i);
            if (sit != sibling.partial.end() && !sit->second.empty())
            {
                // Fields moved on the sibling path → drop those fields here.
                emitDropPartial(place, sit->second);
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

    if (loop->kind == HIRLoop::Kind::While)
    {
        assert(loop->cond.has_value() && "while loop must have a condition");

        MIROperand cond = exprToOperand(loop->cond->get());
        sealBlock(curBB_, MIRTermBranch{
                              .cond = std::move(cond),
                              .thenBlock = bodyId,
                              .elseBlock = exitId,
                          });
    }
    else // For – iterator-based; lowered similarly for now.
    {
        // TODO: proper iterator protocol (next() call + option check).
        // For now we treat the cond expression as a boolean hasNext().
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
            // Infinite for-loop (no condition): unconditional entry into body.
            sealBlock(curBB_, MIRTermGoto{.target = bodyId});
        }
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
        MIROperand val = exprToOperand(ret->value->get());
        MIRPlace ret0{.base = PlaceBase::Local, .index = 0, .name = "_0", .projections = {}, .type = body_->returnType};
        emitAssign(ret0, MIRRValueUse{.operand = std::move(val)});
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

    if (auto *si = dynamic_cast<HIRStructInit *>(expr))
        return buildStructInit(si);

    if (auto *vi = dynamic_cast<HIRVariantInit *>(expr))
        return buildVariantInit(vi);

    if (auto *match = dynamic_cast<HIRMatch *>(expr))
        return buildMatch(match);

    if (auto *ref = dynamic_cast<HIRRef *>(expr))
        return buildRef(ref);

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

    // Fall back: could be a global, a function, or a type name.
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
        MIRPlace dest = makeTempPlace(bin->type);

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

// ── function / method calls ───────────────────────────────────────────────────

MIRPlace MIRBuilder::buildCall(HIRCall *call)
{
    // 1. Lower each argument expression into an operand.
    std::vector<MIROperand> args;
    args.reserve(call->args.size());
    for (auto &arg : call->args)
        args.push_back(exprToOperand(arg.get()));

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

    // 3. Create the result temp (void calls also create a unit-type temp so
    //    the code above can always return a MIRPlace).
    MIRPlace dest = makeTempPlace(call->type);

    // 4. Emit the call statement.
    emit(MIRStmtCall{
        .dest = dest,
        .callee = std::move(calleeOp),
        .funcName = funcName,
        .args = std::move(args),
        .genericParams = std::move(call->typedGenericParams)}
    );

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
