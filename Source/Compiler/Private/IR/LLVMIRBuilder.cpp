/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * LLVMIRBuilder.cpp — full implementation of the MIR → LLVM IR lowering pass.
 */

#include "IR/LLVMIRBuilder.hpp"
#include "IR/BuiltinNames.hpp"
#include "IR/MIRMonomorphization.hpp"

#include <cassert>
#include <sstream>
#include <stdexcept>

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

LLVMIRBuilder::LLVMIRBuilder(std::shared_ptr<Context> cnt, llvm::LLVMContext &ctx, const std::string &name)
    : Pass(cnt),
      ctx_(ctx),
      builder_(std::make_unique<llvm::IRBuilder<>>(ctx))
{
    context->module = std::make_unique<llvm::Module>(name, ctx);
}

// ─────────────────────────────────────────────────────────────────────────────
// Top-level entry point
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerProgram(const MIRProgram &prog)
{
    // Pass 1 — struct types (needed for any field GEP to work).
    declareStructTypes(prog);

    // Pass 2 — function signatures (allows forward calls / recursion).
    declareFunctions(prog);

    // Pass 3 — globals.
    lowerGlobals(prog);

    // Pass 4 — generate drop glue bodies. Must run BEFORE function bodies are
    // lowered: lowerDrop() skips a glue function that is still a pure
    // declaration, so every __drop_<T> that a MIRStmtDrop will reference must
    // already have a body by then. All struct field layouts (structFields_)
    // and function signatures (for Drop::drop methods) are ready by now.
    generateDropGlue();

    // Pass 5 — function bodies.
    for (const auto &mirFn : prog.functions)
    {
        lowerFunctionBody(*mirFn.get());
    }

    // Optional: verify the whole module in debug builds.
#ifndef NDEBUG
    std::string err;
    llvm::raw_string_ostream es(err);
    if (llvm::verifyModule(*context->module, &es))
    {
        context->module->print(llvm::outs(), nullptr);
        throw std::runtime_error("LLVM module verification failed:\n" + es.str());
    }

#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 1 — struct type declarations
// ─────────────────────────────────────────────────────────────────────────────
// We create opaque StructTypes first, then set their bodies, to handle
// mutually recursive structs.

void LLVMIRBuilder::declareStructTypes(const MIRProgram &prog)
{
    // Collect every CustomType we need to lower: non-generic definitions
    // from the symbol table + every instantiation from the type context.
    std::vector<std::shared_ptr<CustomType>> toDeclare;

    auto scope = SymbolTable::getInstance().getCurrentScope();
    while (scope && scope->getParent() != nullptr)
        scope = scope->getParent();

    for (const auto &[name, sym] : scope->getSymbols())
    {
        if (sym->kind != SymbolKind::Struct) continue;
        auto ct = std::dynamic_pointer_cast<CustomType>(sym->type);
        if (!ct) continue;
        if (ct->isGeneric()) continue; // skip definitions like Box<T>
        toDeclare.push_back(ct);
    }

    for (const auto &[name, type] : context->typeContext->getInstantiatedCustoms())
    {
        toDeclare.push_back(type);
    }

    // Pass 1: create opaque struct types so self-referential fields work.
    for (const auto &ct : toDeclare)
    {
        std::string name = ct->getName();
        if (structTypes_.count(name)) continue;
        structTypes_[name] = llvm::StructType::create(ctx_, name);
    }

    // Pass 2: set bodies.
    for (const auto &ct : toDeclare)
    {
        std::string name = ct->getName();
        llvm::StructType *structTy = structTypes_[name];
        if (!structTy->isOpaque()) continue;

        const auto &fields = ct->getFields();
        std::vector<llvm::Type *> llvmFields;
        llvmFields.reserve(fields.size());
        std::vector<std::pair<std::string, std::shared_ptr<Type>>> _fields;

        for (const auto &field : fields)
        {
            llvmFields.push_back(semanticTypeToLLVM(field.type, ctx_));
            _fields.emplace_back(field.name, field.type);
            fieldIndex_[name][field.name] = llvmFields.size() - 1;
        }

        structFields_[name] = std::move(_fields);
        structTy->setBody(llvmFields, false);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 2 — function declarations
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::declareFunctions(const MIRProgram &prog)
{
    for (const auto &mirFn : prog.functions)
    {
        if (!mirFn->genericParams.empty())
            continue;

        // Skip if already declared (e.g. declared as external).
        if (context->module->getFunction(mirFn->name))
            continue;

        const MIRBody &body = mirFn->body;

        // Build LLVM parameter types from locals[1..argCount].
        std::vector<llvm::Type *> paramTypes;
        paramTypes.reserve(body.argCount);
        for (size_t i = 1; i <= body.argCount; ++i)
            paramTypes.push_back(toLLVMType(body.locals[i].type));

        llvm::Type *retTy = toLLVMType(body.returnType);
        llvm::FunctionType *fty = llvm::FunctionType::get(retTy, paramTypes, /*isVarArg=*/false);

        llvm::Function::Create(fty,
            llvm::GlobalValue::ExternalLinkage,
            mirFn->name,
            context->module.get());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 3 — globals
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerGlobals(const MIRProgram &prog)
{
    for (const auto &g : prog.globals)
    {
        llvm::Type *ty = toLLVMType(g.type);

        llvm::Constant *init = nullptr;
        if (g.init.has_value())
        {
            // Constant-folded RValue — must be a MIRRValueUse wrapping a MIRConst.
            // Other RValue kinds aren't valid as global initialisers.
            if (auto *use = std::get_if<MIRRValueUse>(&g.init.value()))
            {
                if (auto *c = std::get_if<MIRConst>(&use->operand))
                    init = llvm::cast<llvm::Constant>(lowerConst(*c));
            }
            if (!init)
                throw std::runtime_error(
                    "Global '" + g.name + "' initialiser is not a constant");
        }
        else
        {
            init = llvm::Constant::getNullValue(ty);
        }

        if (context->module->getNamedGlobal(g.name)) continue;

        auto gvar = new llvm::GlobalVariable(*context->module,
            ty,
            /*isConstant=*/false,
            llvm::GlobalValue::ExternalLinkage,
            init,
            g.name);

        gvar->setVisibility(llvm::GlobalValue::DefaultVisibility);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 4 — function body lowering
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerFunctionBody(const MIRFunction &mirFn)
{
    if (!mirFn.genericParams.empty())
        return;

    llvm::Function *fn = context->module->getFunction(mirFn.name);
    assert(fn && "Function must be declared before its body is lowered");

    const MIRBody &body = mirFn.body;

    FunctionState fs;
    fs.fn = fn;
    fs.body = &body;
    fs.blocks.clear();
    fs.allocas.clear();

    // ── Create all llvm::BasicBlocks up front so terminators can forward-ref.
    for (const auto &bb : body.blocks)
    {
        std::string label = bb.label.empty()
                                ? ("bb" + std::to_string(bb.id))
                                : bb.label;
        fs.blocks[bb.id] = llvm::BasicBlock::Create(ctx_, label, fn);
    }

    // ── Entry block: emit allocas for every local.
    builder_->SetInsertPoint(fs.blocks.at(0));
    createAllocas(fs);

    // ── Copy function arguments into their allocas.
    // locals[0] = return slot (handled by ret), locals[1..argCount] = params.
    size_t argIdx = 0;
    for (llvm::Argument &arg : fn->args())
    {
        size_t localIdx = argIdx + 1; // skip the return slot at index 0
        llvm::AllocaInst *alloca = fs.allocas.at(localIdx);
        builder_->CreateStore(&arg, alloca);
        arg.setName(body.locals[localIdx].name);
        ++argIdx;
    }

    // ── Lower each basic block.
    for (const auto &bb : body.blocks)
        lowerBlock(fs, bb);
}

// ─────────────────────────────────────────────────────────────────────────────
// Alloca creation — one per local, all at function entry
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::createAllocas(FunctionState &fs)
{
    llvm::Function *fn = fs.fn;
    const MIRBody &body = *fs.body;

    // Position at the very start of the entry block so all allocas are
    // grouped together — this lets mem2reg promote them to SSA registers.
    llvm::BasicBlock &entry = fn->getEntryBlock();
    builder_->SetInsertPoint(&entry, entry.begin());

    for (const auto &local : body.locals)
    {
        llvm::Type *ty = toLLVMType(local.type);
        if (ty->isVoidTy())
        {
            fs.allocas[local.index] = nullptr;
            continue;
        }
        llvm::AllocaInst *alloca =
            builder_->CreateAlloca(ty, nullptr, local.name);
        assert(local.index >= 0 && "local index cannot be negative");
        fs.allocas[local.index] = alloca;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Basic block lowering
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerBlock(FunctionState &fs, const MIRBasicBlock &bb)
{
    builder_->SetInsertPoint(fs.blocks.at(bb.id));

    for (const auto &stmt : bb.stmts)
        lowerStatement(fs, stmt);

    lowerTerminator(fs, bb.terminator);
}

// ─────────────────────────────────────────────────────────────────────────────
// Statement lowering (dispatch)
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerStatement(FunctionState &fs, const MIRStatement &stmt)
{
    std::visit([&](const auto &s)
        {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, MIRStmtAssign>)
                lowerAssign(fs, s);
            else if constexpr (std::is_same_v<T, MIRStmtCall>)
                lowerCall(fs, s);
            else if constexpr (std::is_same_v<T, MIRStmtDrop>)
                lowerDrop(fs, s);
            else if constexpr (std::is_same_v<T, MIRStmtNop>)
            {
            } // nothing to emit
        },
        stmt);
}

// ─────────────────────────────────────────────────────────────────────────────
// MIRStmtAssign
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerAssign(FunctionState &fs, const MIRStmtAssign &s)
{
    llvm::Value *rhs = lowerRValue(fs, s.rhs);
    storePlace(fs, s.lhs, rhs);
}

// ─────────────────────────────────────────────────────────────────────────────
// MIRStmtCall
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerCall(FunctionState &fs,
    const MIRStmtCall &s,
    std::optional<llvm::BasicBlock *> normalDest,
    std::optional<llvm::BasicBlock *> unwindDest)
{
    // Lower arguments first (shared by the direct and indirect paths).
    std::vector<llvm::Value *> args;
    args.reserve(s.args.size());
    for (const auto &arg : s.args)
        args.push_back(lowerOperand(fs, arg));

    // Builtin print calls lower to libc printf; intercept before the normal
    // callee resolution (there is no `print_int` symbol).
    if (isPrintBuiltin(s.funcName))
    {
        emitPrintCall(fs, s, args);
        return;
    }

    // Builtin input calls lower to libc fgets + parse (`read_line`/`read_int`/
    // `read_f64`); intercept for the same reason.
    if (isInputBuiltin(s.funcName))
    {
        emitInputCall(fs, s, args);
        return;
    }

    // Builtin heap calls lower to libc malloc/free/memcpy/strlen.
    if (isHeapBuiltin(s.funcName))
    {
        emitHeapCall(fs, s, args);
        return;
    }

    // Builtin to_string calls lower to malloc + sprintf + strlen → String.
    if (isToStringBuiltin(s.funcName))
    {
        emitToStringCall(fs, s, args);
        return;
    }

    // Resolve the callee.
    llvm::Function *callee = nullptr;
    llvm::Value *indirectPtr = nullptr;
    std::shared_ptr<FunctionType> indirectTy;

    if (auto *move = std::get_if<MIRMove>(&s.callee))
    {
        // Direct call by name (most common path): the callee temp holds the
        // function name and s.funcName is the real symbol.
        callee = context->module->getFunction(s.funcName);
        if (!callee)
        {
            // Not a module function. If the callee place is FUNCTION-typed it is
            // a function-pointer VALUE (a local/param) — call through it rather
            // than minting a bogus external declaration.
            auto funcTy = std::dynamic_pointer_cast<FunctionType>(move->place.type);
            if (funcTy)
            {
                indirectPtr = loadPlace(fs, move->place);
                indirectTy = funcTy;
            }
            else
            {
                // The function is external — declare it with an opaque signature
                // (stdlib/intrinsic calls).
                callee = getOrDeclareFn(s.funcName);
            }
        }
    }
    else if (auto *copy = std::get_if<MIRCopy>(&s.callee))
    {
        callee = context->module->getFunction(s.funcName);
        if (!callee)
        {
            auto funcTy = std::dynamic_pointer_cast<FunctionType>(copy->place.type);
            if (funcTy)
            {
                indirectPtr = loadPlace(fs, copy->place);
                indirectTy = funcTy;
            }
            else
            {
                callee = getOrDeclareFn(s.funcName);
            }
        }
    }

    // Indirect call: rebuild the llvm::FunctionType from the MIR FunctionType
    // (opaque pointers erase it), then call through the loaded pointer.
    if (indirectPtr)
    {
        std::vector<llvm::Type *> paramTys;
        for (const auto &p : indirectTy->getParams())
            paramTys.push_back(toLLVMType(p));
        llvm::FunctionType *fty = llvm::FunctionType::get(
            toLLVMType(indirectTy->getReturnType()), paramTys, /*isVarArg=*/false);

        llvm::Type *retTy = fty->getReturnType();
        bool isVoid = retTy->isVoidTy();
        llvm::CallInst *call =
            builder_->CreateCall(fty, indirectPtr, args, isVoid ? "" : s.funcName + ".ret");

        if (s.dest.has_value() && !isVoid)
            storePlace(fs, *s.dest, call);
        return;
    }

    if (unwindDest.has_value())
    {
        // Emit an invoke (for future exception / panic support).
        assert(normalDest.has_value());
        llvm::InvokeInst *inv =
            builder_->CreateInvoke(callee->getFunctionType(), callee, *normalDest, *unwindDest, args, s.funcName + ".ret");
        if (s.dest.has_value())
            storePlace(fs, *s.dest, inv);
    }
    else
    {
        // Ordinary call.
        llvm::Type *retTy = callee->getFunctionType()->getReturnType();
        bool isVoid = retTy->isVoidTy();

        llvm::CallInst *call =
            builder_->CreateCall(callee->getFunctionType(), callee, args,
                isVoid ? "" : s.funcName + ".ret"); // void calls must have no name

        if (s.dest.has_value() && !isVoid) // don't store a void result
            storePlace(fs, *s.dest, call);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MIRStmtDrop — call the struct's drop glue function
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerDrop(FunctionState &fs, const MIRStmtDrop &s)
{
    std::string sName = getStructName(s.place.type);
    if (sName.empty())
        return; // primitives have no drop glue

    llvm::Function *dropFn = getOrDeclareDropGlue(sName);

    // Drop glue for a struct may not have been generated yet. Calling a pure
    // declaration (no body) would only produce an undefined symbol at link
    // time, so skip the call until the glue exists.
    if (dropFn->isDeclaration())
        return;

    // Drop glue receives a pointer to the value.
    llvm::Value *ptr = lowerPlaceAsPtr(fs, s.place);
    builder_->CreateCall(dropFn->getFunctionType(), dropFn, {ptr});
}

// ─────────────────────────────────────────────────────────────────────────────
// Terminator lowering (dispatch)
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::lowerTerminator(FunctionState &fs, const MIRTerminator &term)
{
    std::visit([&](const auto &t)
        {
        using T = std::decay_t<decltype(t)>;

        if constexpr (std::is_same_v<T, MIRTermGoto>)
        {
            builder_->CreateBr(fs.blocks.at(t.target));
        }
        else if constexpr (std::is_same_v<T, MIRTermBranch>)
        {
            llvm::Value* cond = lowerOperand(fs, t.cond);
            // Ensure i1.
            if (!cond->getType()->isIntegerTy(1))
                cond = builder_->CreateICmpNE(
                    cond, llvm::ConstantInt::get(cond->getType(), 0), "bool");
            builder_->CreateCondBr(cond,
                                   fs.blocks.at(t.thenBlock),
                                   fs.blocks.at(t.elseBlock));
        }
        else if constexpr (std::is_same_v<T, MIRTermReturn>)
        {
            if (t.value.has_value())
            {
                llvm::Value* retVal = lowerOperand(fs, *t.value);
                builder_->CreateRet(retVal);
            }
            else if (fs.body->returnType && !fs.body->returnType->equals(context->typeContext->getPrimitive(PrimitiveType::PrimKind::VOID)))
            {
                // Return slot (locals[0]) holds the return value — load it.
                MIRPlace retSlot;
                retSlot.index = 0;
                retSlot.type  = fs.body->returnType;
                retSlot.base = PlaceBase::Return;
                llvm::Value* retVal = loadPlace(fs, retSlot);
                builder_->CreateRet(retVal);
            }
            else
            {
                builder_->CreateRetVoid();
            }
        }
        else if constexpr (std::is_same_v<T, MIRTermCall>)
        {
            llvm::BasicBlock* normal = fs.blocks.at(t.normalDest);
            std::optional<llvm::BasicBlock*> unwind;
            if (t.unwindDest.has_value())
                unwind = fs.blocks.at(*t.unwindDest);
            lowerCall(fs, t.call, normal, unwind);
            // The invoke itself acts as the terminator; don't emit another branch.
        }
        else if constexpr (std::is_same_v<T, MIRTermUnreachable>)
        {
            builder_->CreateUnreachable();
        } },
        term);
}

// ─────────────────────────────────────────────────────────────────────────────
// RValue lowering
// ─────────────────────────────────────────────────────────────────────────────

llvm::Value *LLVMIRBuilder::lowerRValue(FunctionState &fs, const MIRRValue &rv)
{
    return std::visit([&](const auto &r) -> llvm::Value *
        {
        using T = std::decay_t<decltype(r)>;

        // ── Use (identity / copy / move) ────────────────────────────────────
        if constexpr (std::is_same_v<T, MIRRValueUse>)
        {
            return lowerOperand(fs, r.operand);
        }

        // ── Binary operation ────────────────────────────────────────────────
        else if constexpr (std::is_same_v<T, MIRRValueBinaryOp>)
        {
            llvm::Value* lhs = lowerOperand(fs, r.left);
            llvm::Value* rhs = lowerOperand(fs, r.right);
            bool isFP = lhs->getType()->isFloatingPointTy();

            using Op = MIRRValueBinaryOp::Op;
            switch (r.op)
            {
            // Arithmetic
            case Op::Add:
            {
                if (isFP)
                    return builder_->CreateFAdd(lhs, rhs);

                llvm::Type* lhsTy = lhs->getType();
                llvm::Type* rhsTy = rhs->getType();
                llvm::Type* i8    = llvm::Type::getInt8Ty(builder_->getContext());

                if (lhsTy->isPointerTy() && rhsTy->isIntegerTy())
                    return builder_->CreateGEP(i8, lhs, rhs);           // ptr + int
                if (rhsTy->isPointerTy() && lhsTy->isIntegerTy())
                    return builder_->CreateGEP(i8, rhs, lhs);           // int + ptr

                // ptr + ptr is semantically invalid — surface it loudly rather than
                // silently emitting broken IR.
                assert(!lhsTy->isPointerTy() && !rhsTy->isPointerTy() &&
                    "BUG: pointer + pointer reached binary Add lowering");

                return builder_->CreateAdd(lhs, rhs);
            }

            case Op::Sub:
            {
                if (isFP)
                    return builder_->CreateFSub(lhs, rhs);

                llvm::Type* lhsTy = lhs->getType();
                llvm::Type* rhsTy = rhs->getType();
                llvm::Type* i8    = llvm::Type::getInt8Ty(builder_->getContext());

                if (lhsTy->isPointerTy() && rhsTy->isIntegerTy()) {
                    // ptr - int  →  GEP with negated offset
                    llvm::Value* neg = builder_->CreateNeg(rhs);
                    return builder_->CreateGEP(i8, lhs, neg);
                }
                if (lhsTy->isPointerTy() && rhsTy->isPointerTy()) {
                    // ptr - ptr  →  byte difference as i64
                    llvm::Type* i64 = llvm::Type::getInt64Ty(builder_->getContext());
                    llvm::Value* lhsInt = builder_->CreatePtrToInt(lhs, i64);
                    llvm::Value* rhsInt = builder_->CreatePtrToInt(rhs, i64);
                    return builder_->CreateSub(lhsInt, rhsInt);
                }

                return builder_->CreateSub(lhs, rhs);
            }
            case Op::Mul:  return isFP ? builder_->CreateFMul(lhs, rhs)
                                       : builder_->CreateMul(lhs, rhs);
            case Op::Div:  return isFP ? builder_->CreateFDiv(lhs, rhs)
                                       : builder_->CreateSDiv(lhs, rhs);
            case Op::Mod:  return isFP ? builder_->CreateFRem(lhs, rhs)
                                       : builder_->CreateSRem(lhs, rhs);
            // Comparison
            case Op::Eq:   return isFP ? builder_->CreateFCmpOEQ(lhs, rhs)
                                       : builder_->CreateICmpEQ(lhs, rhs);
            case Op::Ne:   return isFP ? builder_->CreateFCmpONE(lhs, rhs)
                                       : builder_->CreateICmpNE(lhs, rhs);
            case Op::Lt:   return isFP ? builder_->CreateFCmpOLT(lhs, rhs)
                                       : builder_->CreateICmpSLT(lhs, rhs);
            case Op::Gt:   return isFP ? builder_->CreateFCmpOGT(lhs, rhs)
                                       : builder_->CreateICmpSGT(lhs, rhs);
            case Op::Le:   return isFP ? builder_->CreateFCmpOLE(lhs, rhs)
                                       : builder_->CreateICmpSLE(lhs, rhs);
            case Op::Ge:   return isFP ? builder_->CreateFCmpOGE(lhs, rhs)
                                       : builder_->CreateICmpSGE(lhs, rhs);
            // Logical (short-circuit should have been lowered to branches already)
            case Op::And:  return builder_->CreateAnd(lhs, rhs);
            case Op::Or:   return builder_->CreateOr(lhs, rhs);
            // Bitwise
            case Op::BitAnd: return builder_->CreateAnd(lhs, rhs);
            case Op::BitOr:  return builder_->CreateOr(lhs, rhs);
            case Op::BitXor: return builder_->CreateXor(lhs, rhs);
            case Op::Shl:    return builder_->CreateShl(lhs, rhs);
            case Op::Shr:    return builder_->CreateAShr(lhs, rhs); // arithmetic shift
            }
            llvm_unreachable("unknown binary op");
        }

        // ── Unary operation ─────────────────────────────────────────────────
        else if constexpr (std::is_same_v<T, MIRRValueUnaryOp>)
        {
            llvm::Value* val = lowerOperand(fs, r.operand);
            using Op = MIRRValueUnaryOp::Op;
            switch (r.op)
            {
            case Op::Neg:    return val->getType()->isFloatingPointTy()
                                        ? builder_->CreateFNeg(val)
                                        : builder_->CreateNeg(val);
            case Op::Not:    return builder_->CreateNot(val);
            case Op::BitNot: return builder_->CreateNot(val);
            }
            llvm_unreachable("unknown unary op");
        }

        // ── Cast ────────────────────────────────────────────────────────────
        else if constexpr (std::is_same_v<T, MIRRValueCast>)
        {
            llvm::Value* val  = lowerOperand(fs, r.operand);
            llvm::Type*  dest = toLLVMType(r.targetType);
            llvm::Type*  src  = val->getType();

            // Integer ↔ integer resize
            if (src->isIntegerTy() && dest->isIntegerTy())
            {
                unsigned srcBits  = src->getIntegerBitWidth();
                unsigned destBits = dest->getIntegerBitWidth();
                if (destBits > srcBits)
                {
                    // A 1-bit source is a bool — zero-extend: bool is unsigned,
                    // and sign-extending an i1 `true` yields -1 (all bits set),
                    // not 1. Other integer widths are signed and sign-extend.
                    // (`print_bool`/`to_string_bool` already ZExt for the same
                    // reason; the language has no other 1-bit integer type, so
                    // srcBits == 1 ⟺ bool.)
                    return srcBits == 1 ? builder_->CreateZExt(val, dest)
                                        : builder_->CreateSExt(val, dest);
                }
                if (destBits < srcBits)
                    return builder_->CreateTrunc(val, dest);
                return val; // same width, no-op
            }
            // Int → float
            if (src->isIntegerTy() && dest->isFloatingPointTy())
                return builder_->CreateSIToFP(val, dest);
            // Float → int
            if (src->isFloatingPointTy() && dest->isIntegerTy())
                return builder_->CreateFPToSI(val, dest);
            // Float ↔ float resize
            if (src->isFloatingPointTy() && dest->isFloatingPointTy())
                return builder_->CreateFPCast(val, dest);
            // Pointer → pointer (bitcast in opaque-ptr LLVM is a no-op)
            if (src->isPointerTy() && dest->isPointerTy())
                return builder_->CreatePointerCast(val, dest);
            // Int ↔ pointer
            if (src->isIntegerTy() && dest->isPointerTy())
                return builder_->CreateIntToPtr(val, dest);
            if (src->isPointerTy() && dest->isIntegerTy())
                return builder_->CreatePtrToInt(val, dest);

            // Fallback: bitcast (undefined behaviour if sizes differ!)
            return builder_->CreateBitCast(val, dest);
        }

        // ── Ref / AddrOf — return a pointer to the place ────────────────────
        else if constexpr (std::is_same_v<T, MIRRValueRef> ||
                           std::is_same_v<T, MIRRValueAddrOf>)
        {
            if constexpr (std::is_same_v<T, MIRRValueRef>)
                return lowerPlaceAsPtr(fs, r.place);
            else
                return lowerPlaceAsPtr(fs, r.place);
        }

        // ── Struct initialisation ────────────────────────────────────────────
        else if constexpr (std::is_same_v<T, MIRRValueStructInit>)
        {
            llvm::StructType* st = structTypes_.at(r.structName);
            llvm::Value* agg = llvm::UndefValue::get(st);
            for (const auto& [name, operand] : r.fields)
            {
                unsigned idx = fieldIndexOf(r.structName, name);
                llvm::Value* fval = lowerOperand(fs, operand);
                agg = builder_->CreateInsertValue(agg, fval, {idx});
            }
            return agg;
        }
        else if constexpr (std::is_same_v<T, MIRRValueArrayInit>)
        {
            auto *arrTy = llvm::cast<llvm::ArrayType>(toLLVMType(r.type));
            llvm::Value *agg = llvm::UndefValue::get(arrTy);
            for (size_t i = 0; i < r.elements.size(); ++i)
            {
                llvm::Value *fval = lowerOperand(fs, r.elements[i]);
                agg = builder_->CreateInsertValue(agg, fval, {i});
            }
            return agg;
        }

        llvm_unreachable("unhandled MIRRValue variant"); },
        rv);
}

// ─────────────────────────────────────────────────────────────────────────────
// Operand lowering
// ─────────────────────────────────────────────────────────────────────────────

llvm::Value *LLVMIRBuilder::lowerOperand(FunctionState &fs, const MIROperand &op)
{
    return std::visit([&](const auto &o) -> llvm::Value *
        {
        using T = std::decay_t<decltype(o)>;

        if constexpr (std::is_same_v<T, MIRConst>)
        {
            return lowerConst(o);
        }
        else if constexpr (std::is_same_v<T, MIRCopy>)
        {
            // Load the value — the original remains readable.
            return loadPlace(fs, o.place);
        }
        else if constexpr (std::is_same_v<T, MIRMove>)
        {
            return loadPlace(fs, o.place);
        }
        llvm_unreachable("unhandled MIROperand variant"); },
        op);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constant lowering
// ─────────────────────────────────────────────────────────────────────────────

llvm::Value *LLVMIRBuilder::lowerConst(const MIRConst &c)
{
    using Kind = MIRConst::Kind;
    switch (c.kind)
    {
    case Kind::Int:
        return llvm::ConstantInt::getSigned(
            toLLVMType(c.type), std::get<int64_t>(c.value));

    case Kind::Float:
        return llvm::ConstantFP::get(
            toLLVMType(c.type), std::get<double>(c.value));

    case Kind::Bool:
        return llvm::ConstantInt::get(
            llvm::Type::getInt1Ty(ctx_),
            std::get<bool>(c.value) ? 1 : 0);

    case Kind::Char:
        return llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(ctx_),
            static_cast<uint32_t>(std::get<char>(c.value)));

    case Kind::String:
    {
        // A function name used as a VALUE (`let fp = foo;`, passing `foo`)
        // resolves to the named function's address, not a string literal.
        if (c.type && c.type->getKind() == Type::Kind::Function)
        {
            const std::string &fnName = std::get<std::string>(c.value);
            if (llvm::Function *fn = context->module->getFunction(fnName))
                return fn;
            return getOrDeclareFn(fnName); // external — opaque declaration
        }
        // Emit a null-terminated global string constant and return a pointer.
        const std::string &s = std::get<std::string>(c.value);
        return builder_->CreateGlobalStringPtr(s, ".str");
    }
    }
    llvm_unreachable("unhandled MIRConst kind");
}

// ─────────────────────────────────────────────────────────────────────────────
// Place — compute a pointer to the place
// ─────────────────────────────────────────────────────────────────────────────

llvm::Value *LLVMIRBuilder::lowerPlaceAsPtr(
    FunctionState &fs,
    const MIRPlace &p,
    std::shared_ptr<Type> *outFinalTy)
{
    llvm::Value *ptr = nullptr;
    std::shared_ptr<Type> currentTy = nullptr;

    switch (p.base)
    {
    case PlaceBase::Local:
    case PlaceBase::Return:
        ptr = fs.allocas.at(p.index);
        currentTy = fs.body->locals[p.index].type;
        break;
    case PlaceBase::Global:
        ptr = context->module->getNamedGlobal(p.name);
        currentTy = p.type;
        break;
    }

    for (const auto &proj : p.projections)
    {
        switch (proj.kind)
        {
        case ProjectionKind::Deref:
        {
            assert(currentTy->getKind() == Type::Kind::Reference);
            auto refTy = std::static_pointer_cast<ReferenceType>(currentTy);
            auto innerTy = refTy->getBaseType();

            // A reference is a pointer value stored in an addressable slot
            // (alloca / global). Dereferencing must LOAD that pointer to get
            // the referent's address — a GEP with index 0 would be a no-op.
            ptr = builder_->CreateLoad(
                toLLVMType(currentTy), ptr, "deref");
            currentTy = innerTy;
            break;
        }
        case ProjectionKind::Field:
        {
            assert(currentTy->getKind() == Type::Kind::Custom && "Field access requires a struct type");

            std::string sName = getStructName(currentTy);
            unsigned idx = fieldIndexOf(sName, proj.field);
            llvm::StructType *st = structTypes_.at(sName);

            ptr = builder_->CreateStructGEP(st, ptr, idx, proj.field.c_str());
            currentTy = fieldTypeOf(sName, proj.field);
            break;
        }
        case ProjectionKind::Index:
        {
            auto elemTy = getElementType(currentTy);
            MIRPlace idxPlace{PlaceBase::Local, proj.localIndex, "_idx", {}, fs.body->locals[proj.localIndex].type};
            llvm::Value *idx = loadPlace(fs, idxPlace);

            // Runtime bounds check for ARRAY indexing only. An array's length
            // is known here, so `i < 0 || i >= N` traps via libc abort(). A
            // C-style pointer index (`s.data[i]` — currentTy is a primitive,
            // not an ArrayType) has no length to check and is left unchecked
            // (documented in string.lis). sema already rejected empty/oversized
            // arrays, so N >= 1 here.
            if (auto arrTy = std::dynamic_pointer_cast<ArrayType>(currentTy))
            {
                llvm::Value *neg = builder_->CreateICmpSLT(idx, builder_->getInt32(0));
                llvm::Value *oob = builder_->CreateICmpSGE(
                    idx, builder_->getInt32((uint32_t)arrTy->getSize()));
                llvm::Value *bad = builder_->CreateOr(neg, oob);

                llvm::Function *abortFn = getOrDeclareAbort();
                llvm::BasicBlock *trapBB = llvm::BasicBlock::Create(ctx_, "idx_oob", fs.fn);
                llvm::BasicBlock *contBB = llvm::BasicBlock::Create(ctx_, "idx_ok", fs.fn);
                builder_->CreateCondBr(bad, trapBB, contBB);

                builder_->SetInsertPoint(trapBB);
                builder_->CreateCall(abortFn->getFunctionType(), abortFn, {});
                builder_->CreateUnreachable();

                builder_->SetInsertPoint(contBB);
            }

            ptr = builder_->CreateGEP(
                toLLVMType(elemTy),
                ptr,
                {idx},
                "idx");
            currentTy = elemTy;
            break;
        }
        }
    }

    if (outFinalTy)
        *outFinalTy = currentTy;

    return ptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Place — load
// ─────────────────────────────────────────────────────────────────────────────

llvm::Value *LLVMIRBuilder::loadPlace(FunctionState &fs, const MIRPlace &p)
{
    std::shared_ptr<Type> finalTy;
    llvm::Value *ptr = lowerPlaceAsPtr(fs, p, &finalTy);
    return builder_->CreateLoad(toLLVMType(finalTy), ptr, "load");
}

// ─────────────────────────────────────────────────────────────────────────────
// Place — store
// ─────────────────────────────────────────────────────────────────────────────

void LLVMIRBuilder::storePlace(FunctionState &fs, const MIRPlace &p, llvm::Value *val)
{
    llvm::Value *ptr = lowerPlaceAsPtr(fs, p);
    // Skip a LOCAL whose alloca is absent (the void return slot). GLOBALs are
    // not allocas — they are module-level variables, so never gate on them.
    if (p.base != PlaceBase::Global && fs.allocas[p.index] == nullptr)
        return;
    builder_->CreateStore(val, ptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

llvm::Type *LLVMIRBuilder::toLLVMType(const std::shared_ptr<Type> &ty)
{
    return semanticTypeToLLVM(ty, ctx_);
}

llvm::Function *LLVMIRBuilder::getOrDeclareDropGlue(const std::string &structName)
{
    std::string name = "__drop_" + structName;
    if (auto *fn = context->module->getFunction(name))
        return fn;

    // Drop glue signature: void(ptr)
    llvm::FunctionType *fty = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx_),
        {llvm::PointerType::getUnqual(ctx_)},
        /*isVarArg=*/false);

    llvm::Function *func = llvm::Function::Create(fty,
        llvm::GlobalValue::ExternalLinkage,
        name,
        context->module.get());

    func->setVisibility(llvm::GlobalValue::DefaultVisibility);

    return func;
}

llvm::Function *LLVMIRBuilder::getOrDeclareFn(const std::string &name)
{
    if (auto *fn = context->module->getFunction(name))
        return fn;

    // Create an opaque variadic declaration — the real signature is unknown.
    // The caller is responsible for ensuring argument types match at the call site.
    llvm::FunctionType *fty = llvm::FunctionType::get(
        llvm::Type::getInt8Ty(ctx_), /*isVarArg=*/true);
    return llvm::Function::Create(fty,
        llvm::GlobalValue::ExternalLinkage,
        name,
        context->module.get());
}

bool LLVMIRBuilder::isPrintBuiltin(const std::string &name)
{
    return classifyBuiltin(name) == BuiltinCategory::Print;
}

llvm::Function *LLVMIRBuilder::getOrDeclarePrintf()
{
    if (auto *fn = context->module->getFunction("printf"))
        return fn;
    llvm::FunctionType *fty = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(ctx_),
        {llvm::PointerType::getUnqual(ctx_)}, // const char* fmt
        /*isVarArg=*/true);
    return llvm::Function::Create(fty,
        llvm::GlobalValue::ExternalLinkage,
        "printf",
        context->module.get());
}

void LLVMIRBuilder::emitPrintCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args)
{
    llvm::Function *printf = getOrDeclarePrintf();
    auto format = [&](const char *fmt)
    {
        return builder_->CreateGlobalStringPtr(fmt, ".fmt");
    };

    std::vector<llvm::Value *> callArgs;
    llvm::Value *fmtPtr = nullptr;

    if (s.funcName == "print_str")
    {
        fmtPtr = format("%s");
        callArgs = args; // the string arg is already an i8*
    }
    else if (s.funcName == "print_int")
    {
        fmtPtr = format("%d");
        callArgs = args;
    }
    else if (s.funcName == "print_float")
    {
        fmtPtr = format("%f");
        callArgs = args;
    }
    else if (s.funcName == "print_bool")
    {
        fmtPtr = format("%d");
        // Bool is i1/i8 — widen to the int that variadic printf expects.
        if (!args.empty())
            callArgs.push_back(builder_->CreateZExt(args[0], builder_->getInt32Ty()));
    }
    else if (s.funcName == "print_char")
    {
        fmtPtr = format("%c");
        callArgs = args; // char already lowers to i32
    }
    else if (s.funcName == "println")
    {
        fmtPtr = format("\n");
    }

    // printf(fmt, arg...)
    std::vector<llvm::Value *> printfArgs{fmtPtr};
    printfArgs.insert(printfArgs.end(), callArgs.begin(), callArgs.end());
    builder_->CreateCall(printf->getFunctionType(), printf, printfArgs);
}

// ── Builtin input: read_line / read_int / read_f64 ───────────────────────────

bool LLVMIRBuilder::isInputBuiltin(const std::string &name)
{
    return classifyBuiltin(name) == BuiltinCategory::Input;
}

llvm::Function *LLVMIRBuilder::getOrDeclareFgets()
{
    // char* fgets(char* str, int count, FILE* stream)
    return getOrDeclareLibcFunction("fgets",
        llvm::FunctionType::get(
            llvm::PointerType::getUnqual(ctx_), // returns char*
            {llvm::PointerType::getUnqual(ctx_), llvm::Type::getInt32Ty(ctx_), llvm::PointerType::getUnqual(ctx_)},
            /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareStrCspn()
{
    // size_t strcspn(const char* str, const char* reject)
    return getOrDeclareLibcFunction("strcspn",
        llvm::FunctionType::get(
            llvm::Type::getInt64Ty(ctx_),
            {llvm::PointerType::getUnqual(ctx_), llvm::PointerType::getUnqual(ctx_)},
            /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareAtoi()
{
    // int atoi(const char* str)
    return getOrDeclareLibcFunction("atoi",
        llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx_),
            {llvm::PointerType::getUnqual(ctx_)},
            /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareStrtod()
{
    // double strtod(const char* str, char** endptr)
    return getOrDeclareLibcFunction("strtod",
        llvm::FunctionType::get(
            llvm::Type::getDoubleTy(ctx_),
            {llvm::PointerType::getUnqual(ctx_), llvm::PointerType::getUnqual(ctx_)},
            /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareAcrtIobFunc()
{
    if (auto *fn = context->module->getFunction("__acrt_iob_func"))
        return fn;
    // FILE* __acrt_iob_func(unsigned int fd)  [MinGW UCRT's stdin macro]
    llvm::FunctionType *fty = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(ctx_),
        {llvm::Type::getInt32Ty(ctx_)},
        /*isVarArg=*/false);
    return llvm::Function::Create(fty,
        llvm::GlobalValue::ExternalLinkage,
        "__acrt_iob_func",
        context->module.get());
}

llvm::GlobalVariable *LLVMIRBuilder::getOrDeclareStdin()
{
    if (auto *g = context->module->getNamedGlobal("stdin"))
        return g;
    // FILE* stdin — an external global holding a FILE* (glibc & friends; NOT
    // MinGW, where stdin is a macro → see getOrDeclareAcrtIobFunc).
    return new llvm::GlobalVariable(*context->module,
        llvm::PointerType::getUnqual(ctx_),
        /*isConstant=*/false,
        llvm::GlobalValue::ExternalLinkage,
        nullptr,
        "stdin");
}

llvm::GlobalVariable *LLVMIRBuilder::getOrCreateInputBuf()
{
    if (auto *g = context->module->getNamedGlobal("__lis_input_buf"))
        return g;
    auto *arrTy = llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_), 256);
    return new llvm::GlobalVariable(*context->module, arrTy, /*isConstant=*/false, llvm::GlobalValue::PrivateLinkage, llvm::Constant::getNullValue(arrTy), "__lis_input_buf");
}

void LLVMIRBuilder::emitInputCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args)
{
    llvm::GlobalVariable *buf = getOrCreateInputBuf();
    llvm::Value *bufPtr = buf; // globals are pointer values

    llvm::Function *fgets = getOrDeclareFgets();
    // stdin FILE*: MinGW/UCRT exposes it via `__acrt_iob_func(0)` (a macro in
    // <stdio.h> → no `stdin` data symbol); other libcs export a `stdin` global.
    llvm::Value *stdinVal;
#ifdef _WIN32
    stdinVal = builder_->CreateCall(getOrDeclareAcrtIobFunc()->getFunctionType(),
        getOrDeclareAcrtIobFunc(),
        {llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0)});
#else
    llvm::GlobalVariable *stdinGlob = getOrDeclareStdin();
    stdinVal = builder_->CreateLoad(llvm::PointerType::getUnqual(ctx_), stdinGlob);
#endif

    // fgets(buf, 256, stdin)
    builder_->CreateCall(fgets->getFunctionType(), fgets, {bufPtr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 256), stdinVal});

    if (s.funcName == "read_line")
    {
        // Strip trailing \r\n: idx = strcspn(buf, "\r\n"); buf[idx] = 0.
        llvm::Function *strcspn = getOrDeclareStrCspn();
        llvm::Value *reject = builder_->CreateGlobalStringPtr("\r\n", ".rstr");
        llvm::Value *idx = builder_->CreateCall(strcspn->getFunctionType(), strcspn, {bufPtr, reject});
        llvm::Value *end = builder_->CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), bufPtr, idx);
        builder_->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx_), 0), end);
        if (s.dest.has_value())
            storePlace(fs, *s.dest, bufPtr);
    }
    else if (s.funcName == "read_int")
    {
        llvm::Function *atoi = getOrDeclareAtoi();
        llvm::Value *val = builder_->CreateCall(atoi->getFunctionType(), atoi, {bufPtr});
        if (s.dest.has_value())
            storePlace(fs, *s.dest, val);
    }
    else if (s.funcName == "read_f64")
    {
        llvm::Function *strtod = getOrDeclareStrtod();
        llvm::Value *endptr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(ctx_));
        llvm::Value *val = builder_->CreateCall(strtod->getFunctionType(), strtod, {bufPtr, endptr});
        if (s.dest.has_value())
            storePlace(fs, *s.dest, val);
    }
}

// ── Builtin heap: __alloc / __free / __memcpy / __strlen ─────────────────────

bool LLVMIRBuilder::isHeapBuiltin(const std::string &name)
{
    return classifyBuiltin(name) == BuiltinCategory::Heap;
}

llvm::Function *LLVMIRBuilder::getOrDeclareMalloc()
{
    // void* malloc(size_t n)  (n passed as i32, widened at the call site)
    return getOrDeclareLibcFunction("malloc",
        llvm::FunctionType::get(
            llvm::PointerType::getUnqual(ctx_), {llvm::Type::getInt64Ty(ctx_)},
            /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareFree()
{
    // void free(void* ptr)
    return getOrDeclareLibcFunction("free",
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(ctx_), {llvm::PointerType::getUnqual(ctx_)},
            /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareMemcpy()
{
    // void* memcpy(void* dst, const void* src, size_t n)
    return getOrDeclareLibcFunction("memcpy",
        llvm::FunctionType::get(
            llvm::PointerType::getUnqual(ctx_),
            {llvm::PointerType::getUnqual(ctx_), llvm::PointerType::getUnqual(ctx_), llvm::Type::getInt64Ty(ctx_)},
            /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareStrlen()
{
    // size_t strlen(const char* s)
    return getOrDeclareLibcFunction("strlen",
        llvm::FunctionType::get(
            llvm::Type::getInt64Ty(ctx_), {llvm::PointerType::getUnqual(ctx_)},
            /*isVarArg=*/false));
}

void LLVMIRBuilder::emitHeapCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args)
{
    if (s.funcName == "__alloc")
    {
        llvm::Function *mallocFn = getOrDeclareMalloc();
        llvm::Value *size = builder_->CreateSExt(args[0], llvm::Type::getInt64Ty(ctx_));
        llvm::Value *ptr = builder_->CreateCall(mallocFn->getFunctionType(), mallocFn, {size});
        if (s.dest.has_value())
            storePlace(fs, *s.dest, ptr);
    }
    else if (s.funcName == "__free")
    {
        llvm::Function *freeFn = getOrDeclareFree();
        builder_->CreateCall(freeFn->getFunctionType(), freeFn, {args[0]});
    }
    else if (s.funcName == "__memcpy")
    {
        llvm::Function *memcpyFn = getOrDeclareMemcpy();
        llvm::Value *n = builder_->CreateSExt(args[2], llvm::Type::getInt64Ty(ctx_));
        llvm::Value *res = builder_->CreateCall(memcpyFn->getFunctionType(), memcpyFn, {args[0], args[1], n});
        if (s.dest.has_value())
            storePlace(fs, *s.dest, res);
    }
    else if (s.funcName == "__strlen")
    {
        llvm::Function *strlenFn = getOrDeclareStrlen();
        llvm::Value *len = builder_->CreateCall(strlenFn->getFunctionType(), strlenFn, {args[0]});
        llvm::Value *len32 = builder_->CreateTrunc(len, llvm::Type::getInt32Ty(ctx_));
        if (s.dest.has_value())
            storePlace(fs, *s.dest, len32);
    }
}

// ── Builtin to_string: malloc + sprintf + strlen → String ────────────────────

bool LLVMIRBuilder::isToStringBuiltin(const std::string &name)
{
    return classifyBuiltin(name) == BuiltinCategory::ToString;
}

llvm::Function *LLVMIRBuilder::getOrDeclareSprintf()
{
    // int sprintf(char* str, const char* format, ...)
    return getOrDeclareLibcFunction("sprintf",
        llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx_),
            {llvm::PointerType::getUnqual(ctx_), llvm::PointerType::getUnqual(ctx_)},
            /*isVarArg=*/true));
}

llvm::Function *LLVMIRBuilder::getOrDeclareAbort()
{
    return getOrDeclareLibcFunction("abort",
        llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_), {}, /*isVarArg=*/false));
}

llvm::Function *LLVMIRBuilder::getOrDeclareLibcFunction(
    const std::string &name,
    llvm::FunctionType *fty)
{
    if (auto *fn = context->module->getFunction(name))
    {
        // sema rejects user functions with libc names (see isReservedFunctionName),
        // so an existing symbol is normally a previous declaration. A type
        // mismatch would crash the verifier later — report it instead of
        // silently returning a wrong-typed function.
        if (fn->getFunctionType() != fty)
            llvm::errs() << "internal error: '" << name
                         << "' already exists with an incompatible signature\n";
        return fn;
    }
    return llvm::Function::Create(fty,
        llvm::GlobalValue::ExternalLinkage,
        name,
        context->module.get());
}

void LLVMIRBuilder::emitToStringCall(FunctionState &fs, const MIRStmtCall &s, const std::vector<llvm::Value *> &args)
{
    // Allocate a fixed TO_STRING_BUF_CAP-byte scratch buffer for the formatted
    // digits (see the constant in LLVMIRBuilder.hpp: a large double like 1e100
    // overflows a 64-byte buffer).
    llvm::Function *mallocFn = getOrDeclareMalloc();
    llvm::Value *buf = builder_->CreateCall(mallocFn->getFunctionType(), mallocFn, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), TO_STRING_BUF_CAP)});

    llvm::Function *sprintfFn = getOrDeclareSprintf();
    llvm::Function *strlenFn = getOrDeclareStrlen();

    const char *fmt = "%d";
    std::vector<llvm::Value *> callArgs;
    if (s.funcName == "to_string_i32")
    {
        fmt = "%d";
        callArgs = args;
    }
    else if (s.funcName == "to_string_i64")
    {
        fmt = "%lld";
        callArgs = args;
    }
    else if (s.funcName == "to_string_f64")
    {
        fmt = "%f";
        callArgs = args;
    }
    else if (s.funcName == "to_string_bool")
    {
        fmt = "%d";
        if (!args.empty())
            callArgs.push_back(builder_->CreateZExt(args[0], builder_->getInt32Ty()));
    }
    else if (s.funcName == "to_string_char")
    {
        fmt = "%c";
        callArgs = args;
    }

    llvm::Value *fmtPtr = builder_->CreateGlobalStringPtr(fmt, ".fmt");
    std::vector<llvm::Value *> sprintfArgs{buf, fmtPtr};
    sprintfArgs.insert(sprintfArgs.end(), callArgs.begin(), callArgs.end());
    builder_->CreateCall(sprintfFn->getFunctionType(), sprintfFn, sprintfArgs);

    llvm::Value *len = builder_->CreateCall(strlenFn->getFunctionType(), strlenFn, {buf});
    llvm::Value *len32 = builder_->CreateTrunc(len, llvm::Type::getInt32Ty(ctx_));

    // Build the String struct { data, len, cap } and store it into the dest.
    if (s.dest.has_value())
    {
        const std::string structName = "string$String";
        // Defensive: a stale stdlib or a renamed field must not crash codegen
        // with an uncaught out_of_range. sema verifies the type exists, so this
        // is only reachable through an internal inconsistency — report it
        // cleanly instead of throwing.
        auto sit = structTypes_.find(structName);
        if (sit == structTypes_.end())
        {
            llvm::errs() << "internal error: to_string_* requires a struct named 'String'\n";
            return;
        }
        auto fit = fieldIndex_.find(structName);
        if (fit == fieldIndex_.end() || !fit->second.count("data")
            || !fit->second.count("len") || !fit->second.count("cap"))
        {
            llvm::errs() << "internal error: 'String' struct lacks data/len/cap fields\n";
            return;
        }
        llvm::StructType *st = sit->second;
        llvm::Value *agg = llvm::UndefValue::get(st);
        agg = builder_->CreateInsertValue(agg, buf, {fit->second["data"]});
        agg = builder_->CreateInsertValue(agg, len32, {fit->second["len"]});
        agg = builder_->CreateInsertValue(agg,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), TO_STRING_BUF_CAP),
            {fit->second["cap"]});
        storePlace(fs, *s.dest, agg);
    }
}

llvm::AllocaInst *LLVMIRBuilder::emitEntryAlloca(llvm::Function *fn,
    llvm::Type *ty,
    const std::string &name)
{
    llvm::BasicBlock &entry = fn->getEntryBlock();
    llvm::IRBuilder<> eb(&entry, entry.begin());
    return eb.CreateAlloca(ty, nullptr, name);
}

unsigned LLVMIRBuilder::fieldIndexOf(const std::string &structName,
    const std::string &fieldName) const
{
    auto sit = fieldIndex_.find(structName);
    if (sit == fieldIndex_.end())
        throw std::runtime_error("Unknown struct: " + structName);
    auto fit = sit->second.find(fieldName);
    if (fit == sit->second.end())
        throw std::runtime_error("Unknown field: " + structName + "::" + fieldName);
    return fit->second;
}

std::shared_ptr<Type>
LLVMIRBuilder::fieldTypeOf(const std::string &structName,
    const std::string &fieldName)
{
    auto it = structFields_.find(structName);
    assert(it != structFields_.end()
           && "fieldTypeOf: unknown struct");

    for (const auto &[name, ty] : it->second)
    {
        if (name == fieldName)
            return ty;
    }
    llvm_unreachable(("fieldTypeOf: field '" + fieldName + "' not found in '" + structName + "'").c_str());
}

std::shared_ptr<Type>
LLVMIRBuilder::getElementType(const std::shared_ptr<Type> &ty)
{
    switch (ty->getKind())
    {
    case Type::Kind::Reference:
        return std::static_pointer_cast<ReferenceType>(ty)->getBaseType();

    case Type::Kind::Array:
        return std::static_pointer_cast<ArrayType>(ty)->getElementType();

    // A deref'd pointer to a primitive (e.g. `s.data: &mut i8` → i8 after the
    // Deref projection) is indexed as `T*` — the element type is the pointee.
    case Type::Kind::Primitive:
        return ty;

    default:
        llvm_unreachable("getElementType: type has no element type");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Drop-glue generation
// ─────────────────────────────────────────────────────────────────────────────
// Synthesize a __drop_<StructName> body for every concrete struct type. Must
// run before function bodies are lowered (lowerDrop() skips glue that is still
// a declaration).
//
// Two cases:
//   * The struct implements the Drop trait (`impl Drop for X { fn drop(self) }`)
//     → the glue calls X::drop on the value, by value. Because drop(self) takes
//       self BY VALUE, the user's method owns the whole teardown — the glue does
//       NOT also recurse into fields (that would double-drop anything the drop
//       method moved, and there is no field-drop syntax to coordinate with).
//   * Otherwise → recursively call the glue of every non-Copy, non-reference
//     field. References (&T / &mut T) are borrowed data, not owned, so they are
//     skipped; primitives are Copy and need no drop.

void LLVMIRBuilder::generateDropGlue()
{
    for (const auto &[structName, fields] : structFields_)
    {
        llvm::Function *dropFn = getOrDeclareDropGlue(structName);
        if (!dropFn->isDeclaration())
            continue; // body already exists (e.g. hand-written drop glue)

        auto stIt = structTypes_.find(structName);
        if (stIt == structTypes_.end())
            continue;

        llvm::StructType *stTy = stIt->second;

        // ── Create the entry block ─────────────────────────────────────────
        llvm::BasicBlock *entry =
            llvm::BasicBlock::Create(ctx_, "entry", dropFn);
        builder_->SetInsertPoint(entry);

        // The single parameter is an opaque pointer to the struct value.
        llvm::Value *ptr = dropFn->getArg(0);

        // ── Case 1: the struct implements the Drop trait ───────────────────
        // If so, delegate the whole teardown to the user's drop(self) method.
        bool implementsDrop = false;
        if (auto custom = context->typeContext->getCustom(structName))
            implementsDrop = (*custom)->implementsTrait("Drop");

        if (implementsDrop)
        {
            // The drop method is a by-value `fn drop(self)` → LLVM void(X).
            // Glue holds a pointer, so load the whole value and pass it by
            // value. The method is a regular MIR function, already declared.
            //
            // Derive its name from the struct's ORIGIN + generic args so a
            // generic instantiation resolves to the monomorphized drop method
            // (`impl<T> Drop for Box<T>` → Box::drop_Mono_int32), not the
            // never-existing `Box$int32::drop`. For a non-generic struct the
            // args are empty and the name is just "Box::drop".
            std::string dropName;
            if (auto custom = context->typeContext->getCustom(structName))
            {
                dropName = (*custom)->getOriginName() + "::drop";
                const auto &args = (*custom)->getGenericArgs();
                if (!args.empty())
                    dropName = MIRMonomorphization::makeMonoFuncName(dropName, args);
            }
            if (!dropName.empty())
            {
                if (llvm::Function *userDrop =
                        context->module->getFunction(dropName))
                {
                    llvm::Value *val = builder_->CreateLoad(stTy, ptr);
                    builder_->CreateCall(
                        userDrop->getFunctionType(), userDrop, {val});
                    builder_->CreateRetVoid();
                    continue;
                }
            }
            // Defensive: the Drop impl exists but the method is missing.
            // Fall through to field recursion rather than dropping nothing.
        }

        // ── Case 2: no Drop impl — recursively drop owned fields ───────────
        // For an ENUM the drop is tag-aware: only the ACTIVE variant's payload
        // slots are valid (the others are uninitialized garbage). Branch on the
        // discriminant and drop each variant's non-Copy payloads conditionally.
        bool isEnum = false;
        std::vector<CustomType::EnumVariantInfo> enumVariants;
        if (auto custom = context->typeContext->getCustom(structName))
        {
            isEnum = (*custom)->isEnum();
            enumVariants = (*custom)->getVariants();
        }

        if (isEnum)
        {
            llvm::Value *tagPtr = builder_->CreateStructGEP(
                stTy, ptr, fieldIndexOf(structName, "__tag"), "__tag");
            llvm::Value *tagVal = builder_->CreateLoad(
                builder_->getInt32Ty(), tagPtr);

            llvm::BasicBlock *done =
                llvm::BasicBlock::Create(ctx_, "drop_done", dropFn);

            for (size_t vi = 0; vi < enumVariants.size(); ++vi)
            {
                // Collect this variant's non-Copy payload slots.
                std::vector<std::pair<std::string, std::shared_ptr<Type>>> slots;
                for (size_t j = 0; j < enumVariants[vi].payloadTypes.size(); ++j)
                {
                    auto pt = enumVariants[vi].payloadTypes[j];
                    if (!pt->isCopyable())
                        slots.push_back({enumVariants[vi].name + "_" + std::to_string(j), pt});
                }
                if (slots.empty()) continue;

                llvm::BasicBlock *thenBlk =
                    llvm::BasicBlock::Create(ctx_, "drop_v" + std::to_string(vi), dropFn);
                llvm::BasicBlock *nextBlk =
                    llvm::BasicBlock::Create(ctx_, "drop_next" + std::to_string(vi), dropFn);

                llvm::Value *cmp = builder_->CreateICmpEQ(
                    tagVal, builder_->getInt32((uint32_t)vi));
                builder_->CreateCondBr(cmp, thenBlk, nextBlk);

                builder_->SetInsertPoint(thenBlk);
                for (auto &[slotName, slotType] : slots)
                {
                    std::string slotStruct = getStructName(slotType);
                    if (slotStruct.empty()) continue;
                    llvm::Function *slotDrop = getOrDeclareDropGlue(slotStruct);
                    llvm::Value *slotPtr = builder_->CreateStructGEP(
                        stTy, ptr, fieldIndexOf(structName, slotName), slotName);
                    builder_->CreateCall(slotDrop->getFunctionType(), slotDrop, {slotPtr});
                }
                builder_->CreateBr(nextBlk);

                builder_->SetInsertPoint(nextBlk);
            }

            builder_->CreateBr(done);
            builder_->SetInsertPoint(done);
            builder_->CreateRetVoid();
            continue;
        }

        // ── Case 2 (struct): recurse into every non-Copy field ─────────────
        for (const auto &[fieldName, fieldType] : fields)
        {
            // Primitives and references are Copy / borrowed — no drop needed.
            if (fieldType->isCopyable())
                continue;
            // Arrays of Copy elements (v1) own nothing — skip their glue.
            if (fieldType->getKind() == Type::Kind::Array)
                continue;

            // Custom / struct fields: recursively call the field type's glue.
            std::string fieldStructName = getStructName(fieldType);
            if (fieldStructName.empty())
                continue; // Trait objects etc. — skip for now

            llvm::Function *fieldDropFn =
                getOrDeclareDropGlue(fieldStructName);

            // GEP to the field and call its drop glue.
            unsigned idx = fieldIndexOf(structName, fieldName);
            llvm::Value *fieldPtr =
                builder_->CreateStructGEP(stTy, ptr, idx, fieldName);

            builder_->CreateCall(
                fieldDropFn->getFunctionType(), fieldDropFn, {fieldPtr});
        }

        builder_->CreateRetVoid();
    }
}