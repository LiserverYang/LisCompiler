/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * TypeHelper.cpp — implement the four bridge functions declared in MIRToLLVM.hpp.
 */

#include "Analysiser/Type.hpp"
#include "IR/LLVMIRBuilder.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// semanticTypeToLLVM
//
// Adapt this switch to your actual Type::Kind enum.
// The example below assumes a Type with a `.kind` field and optional `.inner`
// (for pointers/references) and `.name` (for structs).
// ─────────────────────────────────────────────────────────────────────────────

llvm::Type *semanticTypeToLLVM(const std::shared_ptr<Type> &ty,
    llvm::LLVMContext &ctx)
{
    if (!ty)
        return llvm::Type::getVoidTy(ctx);

    // ── Replace `Type::Kind::*` with your actual enum values ────────────────
    switch (ty->getKind())
    {
    case Type::Kind::Primitive:
    {
        auto newTy = std::static_pointer_cast<PrimitiveType>(ty);

        switch (newTy->getPrimKind())
        {
        case PrimitiveType::PrimKind::VOID:
            return llvm::Type::getVoidTy(ctx);
        case PrimitiveType::PrimKind::BOOL:
            return llvm::Type::getInt1Ty(ctx);
        case PrimitiveType::PrimKind::CHAR:
            return llvm::Type::getInt32Ty(ctx);
        case PrimitiveType::PrimKind::I8:
            return llvm::Type::getInt8Ty(ctx);
        case PrimitiveType::PrimKind::I16:
            return llvm::Type::getInt16Ty(ctx);
        case PrimitiveType::PrimKind::I32:
            return llvm::Type::getInt32Ty(ctx);
        case PrimitiveType::PrimKind::I64:
            return llvm::Type::getInt64Ty(ctx);
        case PrimitiveType::PrimKind::F32:
            return llvm::Type::getFloatTy(ctx);
        case PrimitiveType::PrimKind::F64:
            return llvm::Type::getDoubleTy(ctx);
        }
    }

    case Type::Kind::Reference:
        return llvm::PointerType::getUnqual(ctx); // opaque ptr

        // Struct — looked up by name; the body is set in the struct-decl pass.
    case Type::Kind::Custom:
    {
        auto ct = std::static_pointer_cast<CustomType>(ty);

        // Fast path: already declared.
        if (auto *existing = llvm::StructType::getTypeByName(ctx, ct->getName()))
            return existing;

        // Lazily create and set the body. Create opaque first to handle
        // self-referential types (e.g. via references) without infinite recursion.
        auto *st = llvm::StructType::create(ctx, ct->getName());
        std::vector<llvm::Type *> fieldTys;
        for (const auto &f : ct->getFields())
            fieldTys.push_back(semanticTypeToLLVM(f.type, ctx));
        st->setBody(fieldTys, /*isPacked=*/false);
        return st;
    }

    // Function pointer
    case Type::Kind::Function:
    {
        auto newTy = std::static_pointer_cast<FunctionType>(ty);

        std::vector<llvm::Type *> params;
        for (const auto &p : newTy->getParams())
            params.push_back(semanticTypeToLLVM(p, ctx));
        llvm::Type *ret = semanticTypeToLLVM(newTy->getReturnType(), ctx);
        return llvm::FunctionType::get(ret, params, /*isVarArg=*/false)
            ->getPointerTo();
    }

    // Trait objects — fat pointer: (data ptr, vtable ptr).
    case Type::Kind::Trait:
    {
        // Represent as { ptr, ptr } — a data pointer and a vtable pointer.
        llvm::Type *ptrTy = llvm::PointerType::getUnqual(ctx);
        return llvm::StructType::get(ctx, {ptrTy, ptrTy});
    }

    // Fixed-size array [T; N] → llvm ArrayType.
    case Type::Kind::Array:
    {
        auto at = std::static_pointer_cast<ArrayType>(ty);
        return llvm::ArrayType::get(
            semanticTypeToLLVM(at->getElementType(), ctx), at->getSize());
    }

    default:
        throw std::runtime_error("semanticTypeToLLVM: unhandled Type::Kind "
                                 + ty->toString());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// isPointerLike
// ─────────────────────────────────────────────────────────────────────────────

bool isPointerLike(const std::shared_ptr<Type> &ty)
{
    if (!ty) return false;
    return ty->getKind() == Type::Kind::Reference;
}

// ─────────────────────────────────────────────────────────────────────────────
// getStructName
// ─────────────────────────────────────────────────────────────────────────────

std::string getStructName(const std::shared_ptr<Type> &ty)
{
    if (!ty) return "";
    if (ty->getKind() == Type::Kind::Custom)
    {
        auto newTy = std::static_pointer_cast<CustomType>(ty);
        return newTy->getName();
    }
    return "";
}