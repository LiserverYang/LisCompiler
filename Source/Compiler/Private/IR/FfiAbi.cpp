/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 * The platform ABI of an extern "C" signature (by-value aggregates).
 */

#include "IR/FfiAbi.hpp"

// semanticTypeToLLVM lives here (the same conversion codegen uses, so the size
// we classify is the size the object file will have).
#include "IR/LLVMIRBuilder.hpp"

#include <llvm/TargetParser/Triple.h>

namespace
{
using namespace FfiAbi;

/// A struct passed BY VALUE. Enums never reach here (the type whitelist refuses
/// them first), and arrays are not parameters in this language.
bool isByValueAggregate(const std::shared_ptr<Type> &ty)
{
    if (!ty || ty->getKind() != Type::Kind::Custom)
        return false;
    return !std::static_pointer_cast<CustomType>(ty)->isEnum();
}

/// Does the aggregate contain a float anywhere? On SysV that decides whether the
/// eightbytes go to SSE registers, which this compiler does not model yet.
bool hasFloat(const std::shared_ptr<Type> &ty)
{
    if (!ty)
        return false;
    switch (ty->getKind())
    {
    case Type::Kind::Primitive:
    {
        const auto kind = std::static_pointer_cast<PrimitiveType>(ty)->getPrimKind();
        return kind == PrimitiveType::PrimKind::F32 || kind == PrimitiveType::PrimKind::F64;
    }
    case Type::Kind::Array:
        return hasFloat(std::static_pointer_cast<ArrayType>(ty)->getElementType());
    case Type::Kind::Custom:
    {
        for (const auto &f : std::static_pointer_cast<CustomType>(ty)->getFields())
            if (hasFloat(f.type))
                return true;
        return false;
    }
    default:
        return false;
    }
}

/// The integer type an aggregate of \p size bytes is coerced to when the platform
/// passes it in a register -- null when that size is not 1/2/4/8.
llvm::Type *intTypeOf(size_t size, llvm::LLVMContext &ctx)
{
    switch (size)
    {
    case 1: return llvm::Type::getInt8Ty(ctx);
    case 2: return llvm::Type::getInt16Ty(ctx);
    case 4: return llvm::Type::getInt32Ty(ctx);
    case 8: return llvm::Type::getInt64Ty(ctx);
    default: return nullptr;
    }
}
} // namespace

bool FfiAbi::needsPlan(const FunctionType &sig)
{
    for (const auto &p : sig.getParams())
        if (isByValueAggregate(p))
            return true;
    return isByValueAggregate(sig.getReturnType());
}

FfiAbi::Plan FfiAbi::classify(const FunctionType &sig, const std::string &triple,
    const llvm::DataLayout &dl, llvm::LLVMContext &ctx)
{
    Plan plan;
    llvm::Triple t(triple);
    const bool win64 = t.getArch() == llvm::Triple::x86_64 && t.isOSWindows();

    auto classifyOne = [&](const std::shared_ptr<Type> &ty, bool isReturn) -> bool
    {
        if (!isByValueAggregate(ty))
        {
            plan.args.push_back(ArgKind::Direct);
            plan.argTys.push_back(nullptr);
            return true;
        }
        if (t.getArch() != llvm::Triple::x86_64)
        {
            plan.valid = false;
            plan.why = "only x86-64 is implemented for by-value aggregates; pass a pointer instead.";
            return false;
        }

        llvm::Type *structTy = semanticTypeToLLVM(ty, ctx);
        const size_t size = dl.getTypeAllocSize(structTy);
        if (size == 0)
        {
            plan.valid = false;
            plan.why = "an empty struct has no C layout.";
            return false;
        }

        if (win64)
        {
            // Microsoft x64: 1/2/4/8-byte aggregates travel as an integer of the
            // same size; every other size is passed by REFERENCE (a copy), and a
            // returned one comes back through a hidden first pointer.
            if (llvm::Type *asInt = intTypeOf(size, ctx))
            {
                if (isReturn) { plan.ret = RetKind::AsInt; plan.retTy = asInt; }
                else { plan.args.push_back(ArgKind::AsInt); plan.argTys.push_back(asInt); }
            }
            else if (isReturn)
            {
                plan.ret = RetKind::Sret;
                plan.retTy = llvm::PointerType::getUnqual(ctx);
            }
            else
            {
                plan.args.push_back(ArgKind::ByAddr);
                plan.argTys.push_back(llvm::PointerType::getUnqual(ctx));
            }
            return true;
        }

        // SysV x86-64.
        if (size > 16)
        {
            if (isReturn) { plan.ret = RetKind::Sret; plan.retTy = llvm::PointerType::getUnqual(ctx); }
            else { plan.args.push_back(ArgKind::ByAddr); plan.argTys.push_back(llvm::PointerType::getUnqual(ctx)); }
            return true;
        }
        if (hasFloat(ty))
        {
            plan.valid = false;
            plan.why = "on this target a small aggregate containing floats is passed in SSE registers, which is not implemented yet; pass a pointer instead.";
            return false;
        }
        if (llvm::Type *asInt = intTypeOf(size, ctx))
        {
            if (isReturn) { plan.ret = RetKind::AsInt; plan.retTy = asInt; }
            else { plan.args.push_back(ArgKind::AsInt); plan.argTys.push_back(asInt); }
            return true;
        }
        plan.valid = false;
        plan.why = "on this target an aggregate of 9..16 bytes is split across two eightbytes, which is not implemented yet; pass a pointer instead.";
        return false;
    };

    for (const auto &p : sig.getParams())
        if (!classifyOne(p, /*isReturn=*/false))
            return plan;
    classifyOne(sig.getReturnType(), /*isReturn=*/true);
    return plan;
}
