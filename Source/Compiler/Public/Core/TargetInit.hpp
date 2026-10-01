/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 * Register the native LLVM target, once per process.
 */

#pragma once

#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <memory>
#include <string>

/// Both the early target-resolution pass (which needs a TargetMachine to learn
/// the data layout) and the Emitter need the native target registered. The pass
/// runs first, so the registration has to live outside the Emitter, and the
/// static flag keeps repeated calls cheap.
inline void initLLVMTargetsOnce()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
}

/// The host data layout, for the paths that never ran the early target-resolution
/// pass (the test fixture builds its own pass list, so Context::dataLayout is
/// empty there). The ABI rules need type sizes, so this must not silently be blank.
inline std::string hostDataLayout()
{
    initLLVMTargetsOnce();
    const std::string triple = llvm::sys::getDefaultTargetTriple();
    std::string err;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target)
        return {};
    llvm::TargetOptions to;
    std::unique_ptr<llvm::TargetMachine> tm(
        target->createTargetMachine(triple, "generic", "", to, llvm::Reloc::PIC_));
    return tm ? tm->createDataLayout().getStringRepresentation() : std::string();
}
