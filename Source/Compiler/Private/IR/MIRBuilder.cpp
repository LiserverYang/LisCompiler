/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/MIRBuilder.hpp"
#include "Core/Debugging.hpp"

#include <cassert>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: copy-semantics predicate
// ─────────────────────────────────────────────────────────────────────────────

bool MIRBuilder::isCopyType(const std::shared_ptr<Type> &type)
{
    if (!type) return true;
    // Primitives are Copy; structs / trait objects / references are Move.
    // Adjust this predicate as your Type system grows.

    return type->getKind() == Type::Kind::Primitive;
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
    return MIRPlace{
        .base = PlaceBase::Local,
        .index = idx,
        .name = name,
        .projections = {},
        .type = std::move(type),
    };
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
    emit(MIRStmtAssign{.lhs = std::move(lhs), .rhs = std::move(rhs)});
}

void MIRBuilder::emitDrop(MIRPlace place)
{
    if (!isCopyType(place.type))
        emit(MIRStmtDrop{.place = std::move(place)});
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: operand helpers
// ─────────────────────────────────────────────────────────────────────────────

MIROperand MIRBuilder::placeToOperand(MIRPlace place)
{
    if (isCopyType(place.type))
        return MIRCopy{.place = std::move(place)};
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
            out.functions.push_back(buildFunction(fn));
        }
        else if (auto *impl = dynamic_cast<HIRImpl *>(raw))
        {
            // Each impl method becomes its own MIRFunction.
            for (auto &method : impl->methods)
                out.functions.push_back(buildFunction(method.get()));
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

    // ── entry basic block ────────────────────────────────────────────────────
    BasicBlockId entry = newBlock("entry");
    switchTo(entry);

    // ── lower body ───────────────────────────────────────────────────────────
    buildBlock(fn->body.get());

    // ── ensure the last block has a terminator ────────────────────────────────
    // If control falls off the end of a void function, add an implicit return.
    MIRBasicBlock &last = body_->blocks[curBB_];
    if (std::holds_alternative<MIRTermUnreachable>(last.terminator))
        sealBlock(curBB_, MIRTermReturn{.value = std::nullopt});

    MIRFunction out;
    out.name = fn->name;
    out.body = std::move(freshBody);
    out.isMethod = fn->isMethod;
    out.isStatic = fn->isStatic;
    out.associatedStruct = fn->associatedStruct;
    out.associatedTrait = fn->associatedTrait;

    body_ = nullptr;
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Block & statement lowering
// ─────────────────────────────────────────────────────────────────────────────

void MIRBuilder::buildBlock(HIRBlock *block)
{
    // Collect the places of non-Copy locals declared in this block
    // so we can drop them when the block ends.
    std::vector<MIRPlace> ownedLocals;

    for (auto &stmt : block->stmts)
        buildStmt(stmt.get());

    // Emit drops in reverse declaration order (LIFO, like real destructors).
    for (auto it = ownedLocals.rbegin(); it != ownedLocals.rend(); ++it)
        emitDrop(*it);
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

    emitAssign(lhs, MIRRValueUse{.operand = std::move(rhs)});
}

// ── if cond { then } [else { else }] ─────────────────────────────────────────

void MIRBuilder::buildIf(HIRIf *ifStmt)
{
    // 1. Evaluate condition into a temp.
    MIROperand cond = exprToOperand(ifStmt->cond.get());

    // 2. Allocate successor blocks.
    BasicBlockId thenId = newBlock("then");
    BasicBlockId elseId = ifStmt->elseBlock.has_value() ? newBlock("else") : 0;
    BasicBlockId joinId = newBlock("if_join");

    // 3. Terminate the current block with a branch.
    sealBlock(curBB_, MIRTermBranch{
                          .cond = std::move(cond),
                          .thenBlock = thenId,
                          .elseBlock = ifStmt->elseBlock.has_value() ? elseId : joinId,
                      });

    // 4. Lower then-branch.
    switchTo(thenId);
    buildBlock(ifStmt->thenBlock.get());
    if (std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator))
        sealBlock(curBB_, MIRTermGoto{.target = joinId});

    // 5. Lower else-branch (if present).
    if (ifStmt->elseBlock.has_value())
    {
        switchTo(elseId);
        buildBlock(ifStmt->elseBlock->get());
        if (std::holds_alternative<MIRTermUnreachable>(currentBlock().terminator))
            sealBlock(curBB_, MIRTermGoto{.target = joinId});
    }

    // 6. Continue in the join block.
    switchTo(joinId);
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

    // Lower the body.
    switchTo(bodyId);
    buildBlock(loop->body.get());

    // Back-edge: body jumps back to header (unless body already terminated,
    // e.g. via an inner return or a future break statement).
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
        // Write into the return slot (_0) and then return.
        MIROperand val = exprToOperand(ret->value->get());
        MIRPlace ret0{.base = PlaceBase::Local, .index = 0, .name = "_0", .projections = {}, .type = body_->returnType};
        emitAssign(ret0, MIRRValueUse{.operand = std::move(val)});
        sealBlock(curBB_, MIRTermReturn{.value = std::nullopt});
    }
    else
    {
        sealBlock(curBB_, MIRTermReturn{.value = std::nullopt});
    }
}

// ── expr; (expression used as statement) ─────────────────────────────────────

void MIRBuilder::buildExprStmt(HIRExprStmt *es)
{
    MIRPlace result = buildExpr(es->expr.get());
    // The value is discarded. If it's a non-Copy type, drop it immediately.
    emitDrop(result);
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
        .isMethod = call->isMethod,
        .isStatic = call->isStatic,
    });

    return dest;
}

// ── member access: expr.field ─────────────────────────────────────────────────

MIRPlace MIRBuilder::buildMemberAccess(HIRMemberAccess *ma)
{
    // Lower the object, then project into the field.
    MIRPlace obj = buildExpr(ma->object.get());
    obj.projections.push_back(Projection{
        .kind = ProjectionKind::Field,
        .field = ma->memberName,
    });
    // Update the type to the member's type (already resolved in sema).
    if (ma->memberSymbol)
        obj.type = ma->memberSymbol->type; // assumes Symbol has a `type` field
    return obj;
}

// ── struct initialiser: Point { x: 1, y: 2 } ─────────────────────────────────

MIRPlace MIRBuilder::buildStructInit(HIRStructInit *si)
{
    std::vector<std::pair<std::string, MIROperand>> fields;
    fields.reserve(si->members.size());

    for (auto &[fname, fexpr] : si->members)
        fields.push_back({fname, exprToOperand(fexpr.get())});

    MIRPlace tmp = makeTempPlace(si->type);
    emitAssign(tmp, MIRRValueStructInit{
                        .structName = si->structSymbol ? si->structSymbol->name : "",
                        .fields = std::move(fields),
                        .type = si->type,
                    });
    return tmp;
}
