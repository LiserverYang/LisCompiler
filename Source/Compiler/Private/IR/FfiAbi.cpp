// The platform ABI of an extern C signature (by-value aggregates).
//
// Ground truth for the SysV rules below: clang -S -emit-llvm for
// x86_64-unknown-linux-gnu over the same shapes -- {f32}->float,
// {f32,f32}-><2 x float>, {f64}->double, {i32,f32}->i64, {i32,i32}->i64,
// {i32,i32,i32}->(i64, i32) as TWO arguments, {f64,i32}->(double, i32),
// {f64,f64}->(double, double), {i64,i64}->(i64, i64), and a returned 12/16-byte
// one as { i64, i32 } / { i64, i64 }.

#include "IR/FfiAbi.hpp"

// semanticTypeToLLVM lives here: the same conversion codegen uses, so the size we
// classify is the size the object file will have.
#include "IR/LLVMIRBuilder.hpp"

#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
#include <functional>

namespace
{
using namespace FfiAbi;

bool isByValueAggregate(const std::shared_ptr<Type> &ty)
{
    if (!ty || ty->getKind() != Type::Kind::Custom)
        return false;
    return !std::static_pointer_cast<CustomType>(ty)->isEnum();
}

bool isFloatType(const std::shared_ptr<Type> &ty)
{
    if (!ty || ty->getKind() != Type::Kind::Primitive)
        return false;
    const auto k = std::static_pointer_cast<PrimitiveType>(ty)->getPrimKind();
    return k == PrimitiveType::PrimKind::F32 || k == PrimitiveType::PrimKind::F64;
}

llvm::Type *intTypeOf(uint64_t size, llvm::LLVMContext &ctx)
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

size_t sizeOf(const std::shared_ptr<Type> &ty, const llvm::DataLayout &dl, llvm::LLVMContext &ctx)
{
    return dl.getTypeAllocSize(semanticTypeToLLVM(ty, ctx));
}

void eachScalar(const std::shared_ptr<Type> &ty, uint64_t base, const llvm::DataLayout &dl, llvm::LLVMContext &ctx, const std::function<void(uint64_t, const std::shared_ptr<Type> &)> &visit)
{
    if (!ty)
        return;
    if (ty->getKind() == Type::Kind::Custom)
    {
        const llvm::StructLayout *sl = dl.getStructLayout(
            llvm::cast<llvm::StructType>(semanticTypeToLLVM(ty, ctx)));
        const auto &fields = std::static_pointer_cast<CustomType>(ty)->getFields();
        for (size_t i = 0; i < fields.size(); ++i)
            eachScalar(fields[i].type, base + sl->getElementOffset(i), dl, ctx, visit);
        return;
    }
    if (ty->getKind() == Type::Kind::Array)
    {
        auto at = std::static_pointer_cast<ArrayType>(ty);
        const uint64_t elem = sizeOf(at->getElementType(), dl, ctx);
        for (size_t i = 0; i < at->getSize(); ++i)
            eachScalar(at->getElementType(), base + i * elem, dl, ctx, visit);
        return;
    }
    visit(base, ty);
}
} // namespace

bool FfiAbi::needsPlan(const FunctionType &sig)
{
    for (const auto &p : sig.getParams())
        if (isByValueAggregate(p))
            return true;
    return isByValueAggregate(sig.getReturnType());
}

FfiAbi::Plan FfiAbi::classify(const FunctionType &sig, const std::string &triple, const llvm::DataLayout &dl, llvm::LLVMContext &ctx)
{
    Plan plan;
    llvm::Triple t(triple.empty() ? llvm::sys::getDefaultTargetTriple() : triple);
    const bool win64 = t.getArch() == llvm::Triple::x86_64 && t.isOSWindows();

    auto partsOf = [&](const std::shared_ptr<Type> &ty, std::string &why) -> std::vector<llvm::Type *>
    {
        const uint64_t size = sizeOf(ty, dl, ctx);
        if (size == 0)
        {
            why = "an empty struct has no C layout.";
            return {};
        }
        if (win64)
        {
            if (llvm::Type *asInt = intTypeOf(size, ctx))
                return {asInt};
            return {}; // by reference
        }
        if (size > 16)
            return {}; // MEMORY

        const unsigned eightbytes = (unsigned)((size + 7) / 8);
        std::vector<bool> sse(eightbytes, true);
        std::vector<uint64_t> lastByte(eightbytes, 0);
        eachScalar(ty, 0, dl, ctx, [&](uint64_t off, const std::shared_ptr<Type> &leaf)
            {
                const unsigned idx = (unsigned)(off / 8);
                if (idx >= eightbytes)
                    return;
                lastByte[idx] = std::max(lastByte[idx], off + sizeOf(leaf, dl, ctx));
                if (!isFloatType(leaf))
                    sse[idx] = false; });

        std::vector<llvm::Type *> parts;
        for (unsigned i = 0; i < eightbytes; ++i)
        {
            const uint64_t begin = i * 8;
            const uint64_t bytes = std::min<uint64_t>(8, size - begin);
            if (sse[i])
            {
                if (bytes == 8 && lastByte[i] == begin + 8)
                {
                    uint64_t floats = 0;
                    eachScalar(ty, 0, dl, ctx, [&](uint64_t off, const std::shared_ptr<Type> &leaf)
                        {
                            if (off >= begin && off < begin + 8
                                && std::static_pointer_cast<PrimitiveType>(leaf)->getPrimKind()
                                       == PrimitiveType::PrimKind::F32)
                                ++floats; });
                    if (floats == 2)
                    {
                        parts.push_back(llvm::FixedVectorType::get(llvm::Type::getFloatTy(ctx), 2));
                        continue;
                    }
                    parts.push_back(llvm::Type::getDoubleTy(ctx));
                    continue;
                }
                parts.push_back(bytes >= 8 ? llvm::Type::getDoubleTy(ctx)
                                           : llvm::Type::getFloatTy(ctx));
                continue;
            }
            // The integer is sized by the bytes the chunk actually USES, not by
            // the eightbyte: { f64, i32 } (16 bytes) is (double, i32) in clang,
            // because the second chunk only holds four bytes of value.
            const uint64_t used = lastByte[i] > begin ? lastByte[i] - begin : bytes;
            llvm::Type *asInt = intTypeOf(used, ctx);
            if (!asInt)
            {
                why = "this aggregate does not split into register-sized parts; pass a pointer instead.";
                return {};
            }
            parts.push_back(asInt);
        }
        return parts;
    };

    for (const auto &p : sig.getParams())
    {
        if (!isByValueAggregate(p))
        {
            plan.args.push_back(ArgKind::Direct);
            plan.argTys.push_back({});
            continue;
        }
        if (t.getArch() != llvm::Triple::x86_64)
        {
            plan.valid = false;
            plan.why = "only x86-64 is implemented for by-value aggregates; pass a pointer instead.";
            return plan;
        }
        std::string why;
        std::vector<llvm::Type *> parts = partsOf(p, why);
        if (parts.empty())
        {
            if (!why.empty())
            {
                plan.valid = false;
                plan.why = why;
                return plan;
            }
            plan.args.push_back(ArgKind::ByAddr);
            plan.argTys.push_back({llvm::PointerType::getUnqual(ctx)});
            continue;
        }
        plan.args.push_back(ArgKind::Coerce);
        plan.argTys.push_back(std::move(parts));
    }

    if (!isByValueAggregate(sig.getReturnType()))
        return plan;
    if (t.getArch() != llvm::Triple::x86_64)
    {
        plan.valid = false;
        plan.why = "only x86-64 is implemented for by-value aggregates; pass a pointer instead.";
        return plan;
    }
    std::string why;
    std::vector<llvm::Type *> parts = partsOf(sig.getReturnType(), why);
    if (parts.empty())
    {
        if (!why.empty())
        {
            plan.valid = false;
            plan.why = why;
            return plan;
        }
        plan.ret = RetKind::Sret;
        plan.retTy = llvm::PointerType::getUnqual(ctx);
        return plan;
    }
    plan.ret = RetKind::Coerce;
    plan.retParts = std::move(parts);
    plan.retTy = plan.retParts.size() == 1
                     ? plan.retParts[0]
                     : llvm::StructType::get(ctx, plan.retParts);
    return plan;
}
