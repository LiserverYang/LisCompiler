/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * LLVMIRBuilder.cpp — full implementation of the MIR → LLVM IR lowering pass.
 */

#include "IR/LLVMIRBuilder.hpp"

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

    // Pass 4 — function bodies.

    for (const auto &mirFn : prog.functions)
    {
        lowerFunctionBody(mirFn);
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
    auto scope = SymbolTable::getInstance().getCurrentScope();

    while (scope && scope->getParent() != nullptr)
    {
        scope = scope->getParent();
    }

    for (const auto &[name, sym] : scope->getSymbols())
    {
        if (sym->kind == SymbolKind::Struct)
        {
            if (structTypes_.count(name)) continue;

            structTypes_[name] = llvm::StructType::create(ctx_, name);
        }
    }

    for (const auto &[name, sym] : scope->getSymbols())
    {
        if (sym->kind != SymbolKind::Struct) continue;

        llvm::StructType *structTy = structTypes_[name];
        if (!structTy->isOpaque()) continue;

        auto customType = std::static_pointer_cast<CustomType>(sym->type);
        const auto &fields = customType->getFields();

        std::vector<llvm::Type *> llvmFields;
        llvmFields.reserve(fields.size());
        std::vector<std::pair<std::string, std::shared_ptr<Type>>> _fields;
        for (const auto &field : fields)
        {
            llvm::Type *fieldTy = semanticTypeToLLVM(field.type, ctx_);
            llvmFields.push_back(fieldTy);
            _fields.emplace_back(field.name, field.type);
            fieldIndex_[name][field.name] = llvmFields.size() - 1;
        }

        structFields_[sym->name] = std::move(_fields);

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
        std::string mangledName = mangleName(mirFn);

        // Skip if already declared (e.g. declared as external).
        if (context->module->getFunction(mangledName))
            continue;

        const MIRBody &body = mirFn.body;

        // Build LLVM parameter types from locals[1..argCount].
        std::vector<llvm::Type *> paramTypes;
        paramTypes.reserve(body.argCount);
        for (size_t i = 1; i <= body.argCount; ++i)
            paramTypes.push_back(toLLVMType(body.locals[i].type));

        llvm::Type *retTy = toLLVMType(body.returnType);
        llvm::FunctionType *fty = llvm::FunctionType::get(retTy, paramTypes, /*isVarArg=*/false);

        llvm::Function::Create(fty,
            llvm::GlobalValue::ExternalLinkage,
            mangledName,
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
    llvm::Function *fn = context->module->getFunction(mangleName(mirFn));
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
    // Resolve the callee.
    llvm::Function *callee = nullptr;

    if (auto *move = std::get_if<MIRMove>(&s.callee))
    {
        // Function pointer in a local — load it.
        llvm::Value *fnPtr = loadPlace(fs, move->place);
        // For now treat as a direct call if we can resolve the name.
        // A full implementation would emit an indirect call via fnPtr.
        (void)fnPtr;
    }

    // Direct call by name (most common path).
    callee = context->module->getFunction(s.funcName);
    if (!callee)
    {
        // The function is external — declare it with an opaque signature.
        // This happens for stdlib/intrinsic calls.
        callee = getOrDeclareFn(s.funcName);
    }

    // Lower arguments.
    std::vector<llvm::Value *> args;
    args.reserve(s.args.size());
    for (const auto &arg : s.args)
        args.push_back(lowerOperand(fs, arg));

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
                    return builder_->CreateSExt(val, dest);   // sign-extend
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
            // Load the value.  In a full borrow-checked compiler you'd also
            // poison the source slot here; for now just load it.
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

            ptr = builder_->CreateGEP(
                toLLVMType(innerTy),
                ptr,
                {builder_->getInt32(0)},
                "deref");
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
    if (fs.allocas[p.index] != nullptr)
        builder_->CreateStore(val, ptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

llvm::Type *LLVMIRBuilder::toLLVMType(const std::shared_ptr<Type> &ty)
{
    // Delegate to the bridge function you implement in TypeHelper.cpp.
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

std::string LLVMIRBuilder::mangleName(const MIRFunction &fn) const
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

    default:
        llvm_unreachable("getElementType: type has no element type");
    }
}