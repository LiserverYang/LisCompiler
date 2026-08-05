/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * The implementation of Lexer
 */

#pragma once

#include "Core/Pass.hpp"

#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"

#include <cctype>
#include <memory>
#include <stdexcept>

class Emitter : public Pass
{
public:
    struct Options
    {
        // Target triple, e.g. "x86_64-pc-linux-gnu" or "aarch64-apple-macosx13.0".
        // Leave empty to use the host triple automatically.
        std::string targetTriple;

        // CPU name, e.g. "znver3", "apple-m1", "generic".
        // Leave empty to use the host CPU.
        std::string cpu;

        // Feature string, e.g. "+avx2,+bmi". Leave empty = host features.
        std::string features;

        // Optimisation level: 0 = none, 1 = less, 2 = default, 3 = aggressive.
        unsigned optLevel = 2;

        // Run mem2reg + a basic optimisation pipeline before emitting.
        bool runOptimiser = true;

        // Where to write the object file. Defaults to the current directory.
        std::string outPath = "./a.o";
    };

    /// Emit an object file from the given module.
    /// @param module  The LLVM module (not consumed — you can emit multiple times).
    /// @param outPath Path to write, e.g. "build/main.o".
    void emitObjectFile(llvm::Module &module, const std::string &outPath);

    /// Emit raw LLVM IR text (useful for debugging).
    void emitLLVMIR(llvm::Module &module, const std::string &outPath);

    /// Emit LLVM bitcode (.bc).
    void emitBitcode(llvm::Module &module, const std::string &outPath);

    /// Link one or more object files into an executable.
    /// Calls the system linker (clang driver — handles platform differences).
    ///
    /// @param objectFiles  e.g. {"build/main.o", "build/utils.o"}
    /// @param outPath      e.g. "build/myapp" or "build/myapp.exe"
    /// @param extraFlags   Any extra linker flags, e.g. {"-lm", "-lpthread"}
    void linkExecutable(const std::vector<std::string> &objectFiles,
        const std::string &outPath,
        const std::vector<std::string> &extraFlags = {});

    // Convenience: single object → executable in one call.
    void emitObjectFile(llvm::Module &module, const std::string &objPath, const std::string &exePath, const std::vector<std::string> &extraFlags = {});

    const llvm::TargetMachine *targetMachine() const
    {
        return tm_.get();
    }

public:
    Emitter(std::shared_ptr<Context> cnt, Options opts) : opts_(opts)
    {
        context = cnt;
    }

    ~Emitter() {}

    virtual void run() override;

private:
    Options opts_;
    std::unique_ptr<llvm::TargetMachine> tm_;

    void runOptPipeline(llvm::Module &module);
    void initTargetMachine();
};