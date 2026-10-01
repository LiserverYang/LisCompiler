/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 * Register the LLVM targets, and resolve a requested (triple, cpu, features) into
 * the triple + data layout the whole compiler agrees on.
 */

#pragma once

#include "Argparser/Argparser.hpp"

#include <llvm/ADT/StringMap.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/SubtargetFeature.h>
#include <llvm/TargetParser/Triple.h>

#include <memory>
#include <string>

// llvm/Support/TargetSelect.h declares <Arch>TargetInfo/Target/TargetMC for every
// target in this install's Targets.def (at global scope, which is exactly how
// InitializeAllTargetInfos() calls them). The AsmPrinter/AsmParser entry points are
// generated from AsmPrinters.def/AsmParsers.def instead, so the two architectures
// lisc can build for are declared here. The names are the symbols in the archives
// (checked with llvm-nm: LLVMInitializeARMAsmPrinter in LLVMARMCodeGen.lib,
// LLVMInitializeARMAsmParser in LLVMARMAsmParser.lib, same for AArch64).
extern "C" void LLVMInitializeARMAsmPrinter();
extern "C" void LLVMInitializeARMAsmParser();
extern "C" void LLVMInitializeAArch64AsmPrinter();
extern "C" void LLVMInitializeAArch64AsmParser();

/// Register the targets `--target` can name, not just the native one.
///
/// NOT llvm::InitializeAllTargets(): that references EVERY backend an LLVM was
/// CONFIGURED with, and an install that ships a subset of the target archives then
/// fails to LINK (measured here: F:/LLVM has AArch64/ARM/BPF/NVPTX/RISCV/
/// WebAssembly/X86 while its Targets.def also lists AMDGPU, AVR, Hexagon, Mips,
/// MSP430, PowerPC, Sparc, SPIRV, SystemZ, VE, XCore -- all absent). Registering the
/// architectures this compiler targets keeps the link line honest; any OTHER triple
/// still fails cleanly, with an ArgParseError out of lookupTarget().
///
/// The registration cannot live in the Emitter: the early target-resolution pass
/// runs first, and the static flag keeps repeated calls cheap.
inline void initLLVMTargetsOnce()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    // The target this process was built for: what every existing test, example and
    // the judge build for.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // The embedded ones, for --target thumbv7m-none-eabi / aarch64-*-*.
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmPrinter();
    LLVMInitializeARMAsmParser();

    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
}

/// What the pipeline has to agree on: the triple the module claims, the data layout
/// sizes/alignments are computed with, and the codegen knobs the Emitter hands to
/// createTargetMachine. Resolved ONCE, early, because the FFI ABI rules need type
/// sizes and the ANALYZER enforces them -- long before the Emitter would build a
/// machine of its own.
struct ResolvedTarget
{
    std::string triple;
    std::string dataLayout;
    std::string cpu;
    std::string features;
    llvm::Reloc::Model reloc = llvm::Reloc::PIC_;

    /// The requested triple was the host's (or absent): the host CPU and feature
    /// detection were used, and the module could be linked by the host toolchain.
    bool host = false;
};

/// Resolve a requested target. An EMPTY triple means the host; so does a triple that
/// EQUALS the host's -- both keep the host cpu and feature detection, so
/// `--target <host triple>` and no flag at all produce the same object (a test pins
/// that byte for byte).
///
/// A real cross target gets the requested cpu (default "generic") and features
/// (default none) verbatim: asking for the HOST cpu on another architecture hands
/// LLVM a CPU name that does not exist there. Bare-metal targets default to the
/// STATIC relocation model -- position independence on a target with no dynamic
/// loader only buys indirection.
///
/// Throws ArgParseError for a triple LLVM does not know: a typo on the command line
/// is a usage error (exit 2), not an internal compiler error (exit 70).
inline ResolvedTarget resolveTarget(
    const std::string &requestedTriple, const std::string &requestedCpu, const std::string &requestedFeatures)
{
    initLLVMTargetsOnce();

    const std::string hostTriple = llvm::sys::getDefaultTargetTriple();
    const bool isHost = requestedTriple.empty() || llvm::Triple(requestedTriple) == llvm::Triple(hostTriple);

    ResolvedTarget out;
    out.host = isHost;
    out.triple = requestedTriple.empty() ? hostTriple : requestedTriple;

    std::string errStr;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(out.triple, errStr);
    if (!target)
        throw ArgParseError("unknown target '" + out.triple + "': " + errStr);

    if (isHost)
    {
        // Exactly what the compiler did before --target existed.
        out.cpu = llvm::sys::getHostCPUName().str();
        llvm::StringMap<bool> featureMap = llvm::sys::getHostCPUFeatures();
        llvm::SubtargetFeatures sf;
        for (auto &[name, enabled] : featureMap)
            sf.AddFeature(name, enabled);
        out.features = sf.getString();
        out.reloc = llvm::Reloc::PIC_;
    }
    else
    {
        out.cpu = requestedCpu.empty() ? "generic" : requestedCpu;
        out.features = requestedFeatures;
        const llvm::Triple triple(out.triple);
        out.reloc = triple.getOS() == llvm::Triple::UnknownOS ? llvm::Reloc::Static : llvm::Reloc::PIC_;
    }

    llvm::TargetOptions to;
    std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
        out.triple, out.cpu, out.features, to, out.reloc, llvm::CodeModel::Small, llvm::CodeGenOptLevel::Default));
    if (!tm)
        throw ArgParseError("cannot create a target machine for '" + out.triple + "'");
    out.dataLayout = tm->createDataLayout().getStringRepresentation();
    return out;
}

/// The host data layout, for the paths that never ran the early target-resolution
/// pass (the test fixture builds its own pass list, so Context::dataLayout is empty
/// there). The ABI rules need type sizes, so this must not silently be blank.
inline std::string hostDataLayout()
{
    return resolveTarget("", "", "").dataLayout;
}
