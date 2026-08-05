/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "IR/Emitter.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <system_error>

// LLVM headers
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/SubtargetFeature.h>

static void initLLVMTargets()
{
    static bool done = false;
    if (done) return;
    done = true;

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
}

void Emitter::initTargetMachine()
{
    // Resolve the triple.
    std::string triple = opts_.targetTriple.empty()
                             ? llvm::sys::getDefaultTargetTriple()
                             : opts_.targetTriple;

    std::string errStr;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, errStr);
    if (!target)
        throw std::runtime_error("LLVM target lookup failed for '" + triple + "': " + errStr);

    // Resolve CPU and features.
    std::string cpu = opts_.cpu.empty() ? llvm::sys::getHostCPUName().str()
                                        : opts_.cpu;
    std::string features = opts_.features;

    if (features.empty())
    {
        // Auto-detect host CPU features (e.g. "+avx2,+bmi2").
        llvm::StringMap<bool> fm = llvm::sys::getHostCPUFeatures();

        llvm::SubtargetFeatures sf;

        for (auto &[name, enabled] : fm)
            sf.AddFeature(name, enabled);

        features = sf.getString();
    }

    // Map our opt level to LLVM's CodeGenOptLevel.
    llvm::CodeGenOptLevel cgOpt;
    switch (opts_.optLevel)
    {
    case 0: cgOpt = llvm::CodeGenOptLevel::None; break;
    case 1: cgOpt = llvm::CodeGenOptLevel::Less; break;
    case 3: cgOpt = llvm::CodeGenOptLevel::Aggressive; break;
    default: cgOpt = llvm::CodeGenOptLevel::Default; break;
    }

    llvm::TargetOptions to;
    tm_.reset(target->createTargetMachine(triple, cpu, features, to, llvm::Reloc::PIC_, llvm::CodeModel::Small, cgOpt));
    if (!tm_)
        throw std::runtime_error("Failed to create TargetMachine for: " + triple);
}

// ─────────────────────────────────────────────────────────────────────────────
// Optimisation pipeline
// ─────────────────────────────────────────────────────────────────────────────

void Emitter::runOptPipeline(llvm::Module &module)
{
    // Set the target data layout so middle-end passes have correct sizes.
    module.setDataLayout(tm_->createDataLayout());
    module.setTargetTriple(tm_->getTargetTriple().str());

    llvm::PassBuilder pb(tm_.get());

    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::OptimizationLevel lvl;
    switch (opts_.optLevel)
    {
    case 0: lvl = llvm::OptimizationLevel::O0; break;
    case 1: lvl = llvm::OptimizationLevel::O1; break;
    case 3: lvl = llvm::OptimizationLevel::O3; break;
    default: lvl = llvm::OptimizationLevel::O2; break;
    }

    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(lvl);
    mpm.run(module, mam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Emit — object file
// ─────────────────────────────────────────────────────────────────────────────

void Emitter::emitObjectFile(llvm::Module &module, const std::string &outPath)
{
    // Verify before doing anything (catches codegen bugs early).
    std::string verifyErr;
    llvm::raw_string_ostream ve(verifyErr);
    if (llvm::verifyModule(module, &ve))
        throw std::runtime_error("Module verification failed:\n" + ve.str());

    // Run the optimiser.
    if (opts_.runOptimiser)
        runOptPipeline(module);

    // Open the output file.
    std::error_code ec;
    llvm::raw_fd_ostream dest(outPath, ec, llvm::sys::fs::OF_None);
    if (ec)
        throw std::runtime_error("Cannot open output file '" + outPath + "': " + ec.message());

    // Emit machine code via the legacy pass manager.
    // (The new pass manager doesn't yet have a codegen emission API.)
    llvm::legacy::PassManager codegenPM;
    if (tm_->addPassesToEmitFile(codegenPM, dest, nullptr, llvm::CodeGenFileType::ObjectFile))
        throw std::runtime_error("TargetMachine cannot emit object files");

    codegenPM.run(module);
    dest.flush();
}

// Convenience overload: emit + link in one call.
void Emitter::emitObjectFile(llvm::Module &module,
    const std::string &objPath,
    const std::string &exePath,
    const std::vector<std::string> &extraFlags)
{
    emitObjectFile(module, objPath);
    linkExecutable({objPath}, exePath, extraFlags);
}

// ─────────────────────────────────────────────────────────────────────────────
// Emit — LLVM IR text (for debugging)
// ─────────────────────────────────────────────────────────────────────────────

void Emitter::emitLLVMIR(llvm::Module &module, const std::string &outPath)
{
    std::error_code ec;
    llvm::raw_fd_ostream dest(outPath, ec, llvm::sys::fs::OF_Text);
    if (ec)
        throw std::runtime_error("Cannot open '" + outPath + "': " + ec.message());
    module.print(dest, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Emit — LLVM bitcode
// ─────────────────────────────────────────────────────────────────────────────

void Emitter::emitBitcode(llvm::Module &module, const std::string &outPath)
{
    std::error_code ec;
    llvm::raw_fd_ostream dest(outPath, ec, llvm::sys::fs::OF_None);
    if (ec)
        throw std::runtime_error("Cannot open '" + outPath + "': " + ec.message());
    llvm::WriteBitcodeToFile(module, dest);
}

// ─────────────────────────────────────────────────────────────────────────────
// Link — invoke the system linker via the clang driver
// ─────────────────────────────────────────────────────────────────────────────
//
// Why clang and not ld directly?
//   Clang knows how to find:
//     • crt0.o / crt1.o / crti.o / crtn.o  (C runtime startup)
//     • libc, libgcc/libunwind, libstdc++ / libc++
//     • Platform-specific linker flags (macOS -lSystem, Windows /DEFAULTLIB)
//   Calling ld directly means replicating all of that knowledge yourself.
//   Using clang -o out a.o is the standard approach used by rustc, zig, etc.

void Emitter::linkExecutable(const std::vector<std::string> &objectFiles,
    const std::string &outPath,
    const std::vector<std::string> &extraFlags)
{
    if (objectFiles.empty())
        throw std::runtime_error("linkExecutable: no object files provided");

    // Build the command string.
    std::ostringstream cmd;

#if defined(_WIN32)
    // On Windows, prefer clang-cl or lld-link.\
    cmd << "clang -o ";
    cmd << '"' << outPath << '"';
    for (const auto &obj : objectFiles)
        cmd << " \"" << obj << '"';
    for (const auto &flag : extraFlags)
        cmd << " " << flag;
#else
    // Linux / macOS
    cmd << "clang -o ";
    cmd << '"' << outPath << '"';
    for (const auto &obj : objectFiles)
        cmd << " \"" << obj << '"';
    for (const auto &flag : extraFlags)
        cmd << " " << flag;
#endif

    int ret = std::system(cmd.str().c_str());
    if (ret != 0)
        throw std::runtime_error(
            "Linker exited with code " + std::to_string(ret) + "\n  Command was: " + cmd.str());
}

void Emitter::run()
{
    initLLVMTargets();
    initTargetMachine();

    runOptPipeline(*context->module.get());

    emitObjectFile(*context->module.get(), opts_.outPath);
}