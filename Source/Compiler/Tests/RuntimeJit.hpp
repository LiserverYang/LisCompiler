/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * RuntimeJit.hpp — execute a compiled Lis module INSIDE the test process.
 *
 * Why: the runtime tests used to spawn one linked executable per case
 * (~330 ms of g++/ld per test, 500+ tests). JIT-ing the LLVM module the
 * compiler just built removes the link AND the process spawn, leaving only the
 * codegen the test is actually about.
 *
 * The JIT'd program runs in this process, so:
 *   - anything that calls abort() (panic, an out-of-bounds array write) would
 *     kill the test runner; those cases must stay on the subprocess path;
 *   - stdout/stdin are the process's own, captured/redirected here.
 */

#pragma once

#include <memory>
#include <string>

namespace llvm
{
class LLVMContext;
class Module;
} // namespace llvm

/**
 * JIT the module's main function and return its exit code.
 *
 * Takes OWNERSHIP of both the module and its context (the JIT destroys them,
 * and the code generated from them, before returning) — hence the unique_ptrs.
 *
 * @param Out if non-null, receives everything the program wrote to stdout.
 * @param In  if non-null, is fed to the program's stdin.
 * @return the program's exit status (0 for a void main), or -1000-N for the
 *         N-th internal JIT failure (never a legitimate program result).
 */
int RunModuleInJit(std::unique_ptr<llvm::Module> Mod,
    std::unique_ptr<llvm::LLVMContext> Ctx,
    std::string *Out = nullptr,
    const std::string *In = nullptr);
