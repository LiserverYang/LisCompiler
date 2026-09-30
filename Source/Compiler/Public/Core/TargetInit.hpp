/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 * Register the native LLVM target, once per process.
 */

#pragma once

#include <llvm/Support/TargetSelect.h>

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
