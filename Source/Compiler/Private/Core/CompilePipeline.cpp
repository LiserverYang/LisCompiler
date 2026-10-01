/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 * Implement the compile pipeline
 */

#include "Core/CompilePipeline.hpp"
#include "Argparser/Argparser.hpp"
#include "Argparser/Behaviors.hpp"
#include "Core/Debugging.hpp"
#include "Core/LambdaPass.hpp"
#include "IR/Emitter.hpp"
#include "IR/HIRBuilder.hpp"
#include "IR/HIRSemanticAnalyzer.hpp"
#include "IR/LLVMIRBuilder.hpp"
#include "IR/MIRBorrowCheck.hpp"
#include "IR/MIRBuilder.hpp"
#include "IR/MIRMonomorphization.hpp"

// Resolving the target (triple + data layout) early: the FFI ABI rules need it,
// and the analyzer enforces them long before the Emitter builds its own machine.
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/SubtargetFeature.h>

#include "Core/TargetInit.hpp"
#include "Lexer/Lexer.hpp"

// --time-trace: LLVM's profiler (its implementation is compiled into the LLVMSupport
// this project already links). Pipeline::run() wraps every pass in a scope, and
// LLVM's own pass manager contributes one per optimisation pass, so the trace covers
// our phases AND the middle-end.
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TimeProfiler.h>
#include <llvm/Support/raw_ostream.h>
#include "Parser/Parser.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
/// Where `--time-trace` wants the Chrome trace. Global because the write has to
/// happen on the way OUT, including the `exit(1)` the analyzer uses for "this
/// program is illegal" -- a trace of a failed compile is often the interesting one.
std::string gTimeTracePath;

void writeTimeTraceOnExit()
{
    if (gTimeTracePath.empty())
        return;

    std::error_code ec;
    llvm::raw_fd_ostream os(gTimeTracePath, ec, llvm::sys::fs::OF_None);
    if (ec)
    {
        llvm::errs() << "cannot write the time trace to '" << gTimeTracePath << "': " << ec.message() << "\n";
        return;
    }
    llvm::timeTraceProfilerWrite(os);
    llvm::timeTraceProfilerCleanup();
}
} // namespace

CompilePipeline::CompilePipeline(std::shared_ptr<Context> cnt, int argc, const char **argv)
{
    context = cnt;

    // create the argparser and regist the arguments
    ArgparserCreateInfo createInfo{argc, argv, true, context->args, "Usage: lisc [options] file"};
    auto argParser = std::make_unique<Argparser>(context, createInfo);

    argParser->registRule(ArgParseRule{{"filePath"}, nullptr, "./a.lis", "test"});
    argParser->registRule(ArgParseRule{{"--print-ast"}, setAsTrue, "false", "Print the parsed ast."});
    argParser->registRule(ArgParseRule{{"--print-tokenstream"}, setAsTrue, "false", "Print the tokenstream."});
    argParser->registRule(ArgParseRule{{"--print-typetable"}, setAsTrue, "false", "Print the types of TypeContext."});
    argParser->registRule(ArgParseRule{{"--print-hir"}, setAsTrue, "false", "Print the parsed HIR."});
    argParser->registRule(ArgParseRule{{"--print-mir"}, setAsTrue, "false", "Print the parsed MIR."});
    argParser->registRule(ArgParseRule{{"--print-llvmir"}, setAsTrue, "false", "Print the parsed LLVM IR."});
    argParser->registRule(ArgParseRule{{"-o"}, setAsValue, "2", "The optimise level(0-3), default is 2."});
    argParser->registRule(ArgParseRule{{"-I"}, setAsValue, "", "Add module search path(s), ';'-separated (lisbuild packs include_dirs here)."});
    argParser->registRule(ArgParseRule{{"--out"}, setAsValue, "", "Where to write the object file (default ./a.o)."});
    argParser->registRule(ArgParseRule{{"--shared"}, setAsTrue, "false", "Also produce a shared library next to the object (lib<stem>.dll / <stem>.so), linked with g++."});
    argParser->registRule(ArgParseRule{{"--allow-ffi"}, setAsTrue, "false", "Allow extern C declarations (FFI) in user code. OFF by default: FFI can break every guarantee the language makes, so the standard library has it and the judge does not hand it to submissions."});
    argParser->registRule(ArgParseRule{{"--target"}, setAsValue, "", "Build for another target triple (e.g. thumbv7m-none-eabi) instead of the host. Only the object file is produced: --shared stays a host-only operation."});
    argParser->registRule(ArgParseRule{{"--cpu"}, setAsValue, "", "Target CPU (e.g. cortex-m3). Empty = the host CPU for a host target, \"generic\" for a cross target."});
    argParser->registRule(ArgParseRule{{"--features"}, setAsValue, "", "Target feature string (e.g. \"+thumb2\"). Empty = the host features for a host target, none for a cross target."});
    argParser->registRule(ArgParseRule{{"--time-trace"}, setAsValue, "", "Write a Chrome trace of the compile to this file (`chrome://tracing` or ui.perfetto.dev shows it as a flame graph). Scopes cover every pass, and LLVM adds one per optimisation pass. Pair with --time-trace-granularity to drop the noise."});
    argParser->registRule(ArgParseRule{{"--time-trace-granularity"}, setAsValue, "500", "Minimum duration in MICROSECONDS for a scope to appear in the --time-trace output (default 500)."});
    argParser->registRule(ArgParseRule{{"--max-depth"},
        setAsValue,
        "256",
        "Maximum nesting depth of expressions/statements/types. Deep nesting is bounded by the compiler's own recursion, so past this limit it reports E2018 instead of exhausting the stack (default 256)."});

    // ── Parse the command line NOW ────────────────────────────────────────────
    //
    // Every pass object this pipeline builds -- and the Emitter's own options
    // below -- is CONSTRUCTED before any pass RUNS, so a `getArg` in a
    // constructor used to read the default that registRule() had just stored,
    // never what the user typed. That trap is not hypothetical: it silently made
    // `-o` inert (the emitter always saw "2", so -o0/-o1/-o3 emitted
    // byte-identical -O2 objects) and dropped every `-I` directory, and it is
    // the same one that once made `--allow-ffi` look dead.
    //
    // The Argparser is the first pass and depends on nothing, so run it here,
    // before anything is built from argv. From this point on argv is readable
    // everywhere: at construction time and at pass time alike.
    argParser->run();

    // The standard library is NOT auto-preloaded anymore — user code imports
    // the modules it needs (`impt math;`). The stdlib directory is added to
    // the search paths so `impt math;` finds <lstdlib>/math.lis.
    namespace fs = std::filesystem;

    fs::path stdLibDir = fs::path(argv[0]).parent_path() / "lstdlib";
    if (!fs::exists(stdLibDir))
    {
        std::runtime_error("could not find the standard library, please check the binary_path/lstdlib!");
    }
    context->searchPaths.push_back(stdLibDir.string());
    // The same directory is the boundary of the unsafe core: heap primitives and
    // raw-pointer operations are only accepted from files under it (see Context).
    context->stdLibDirs.push_back(stdLibDir.string());
    // `-I` may carry several dirs separated by ';' (the argparser keeps only the
    // last value of a repeated flag, so lisbuild packs its include_dirs into one
    // `-I<dir>;<dir>` argument).
    std::string includeArg = context->args->getArg("I");
    size_t start = 0;
    while (start <= includeArg.size())
    {
        size_t semi = includeArg.find(';', start);
        std::string dir = includeArg.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        if (!dir.empty())
            context->searchPaths.push_back(dir);
        if (semi == std::string::npos)
            break;
        start = semi + 1;
    }

    // `--time-trace`: start LLVM's profiler as soon as the command line is known.
    // It has to happen BEFORE any scope is created (Pipeline::run() and LLVM's own
    // pass manager both create them) and the file has to be written on the way OUT,
    // including the analyzer's exit(1) -- a trace of a FAILED compile is often the
    // interesting one.
    {
        const std::string tracePath = context->args->getArg("time_trace");
        if (!tracePath.empty())
        {
            unsigned granularity = 500;
            try
            {
                granularity = static_cast<unsigned>(std::stoul(context->args->getArg("time_trace_granularity")));
            }
            catch (const std::exception &)
            {
                // A non-numeric granularity keeps the default; the trace is a
                // diagnostic, it should not be the reason a compile fails.
            }
            llvm::timeTraceProfilerInitialize(granularity, "lisc");
            gTimeTracePath = tracePath;
            std::atexit(writeTimeTraceOnExit);
        }
    }

    // Resolve the TARGET here, once -- like `-I` and the stdlib directory above, it
    // is command-line state, and the file the lambda pass below reads is not an
    // input to it. The FFI ABI classification (how a struct is passed by value) is a
    // function of type sizes, and the ANALYZER enforces it -- which runs long before
    // the Emitter creates its TargetMachine. Same resolver as the Emitter's
    // (Core/TargetInit.hpp), so the module's data layout agrees with codegen's.
    {
        ResolvedTarget target = resolveTarget(context->args->getArg("target"),
            context->args->getArg("cpu"),
            context->args->getArg("features"));
        context->targetTriple = target.triple;
        context->dataLayout = target.dataLayout;
        context->targetCPU = target.cpu;
        context->targetFeatures = target.features;

        // --shared is linked by the HOST toolchain (the Emitter drives g++), which
        // cannot produce a foreign target's shared object: fail as a usage error
        // instead of writing a host-linked library next to a cross-target object.
        if (!target.host && context->args->getArg("shared") == "true")
            throw ArgParseError("--shared links with the host toolchain and cannot be combined with "
                                 "--target " + target.triple + "; emit the object and link it with your target linker");
    }

    // NOTE: the Argparser is deliberately NOT pushed into `passes` any more --
    // it has already run above, and a second run would re-walk an exhausted
    // argv (`pos` is left at argc).
    passes.emplace_back(std::make_unique<LambdaPass>(context, [](std::shared_ptr<Context> ctx)
        {
            ctx->ffiAllowed = ctx->args->getArg("allow_ffi") == "true";

            // this pass is to read file
            ctx->filePath = ctx->args->getArg("filePath");
            std::ifstream input{ctx->filePath, std::ios::binary};
            if (!input)
            {
                // A stream that failed to open leaves rdbuf() EMPTY, so a missing
                // (or unreadable) input used to compile as an empty program, exit
                // 0 and even write an a.o. That is a usage error: name the file and
                // stop. ArgParseError makes the driver report it as one, not as an
                // internal compiler error.
                throw ArgParseError("cannot open input file '" + ctx->filePath + "'");
            }
            ctx->fileValue = (std::stringstream{} << input.rdbuf()).str();
            // The main file's directory is appended AFTER the stdlib dir: the
            // stdlib modules always win over same-named user files (e.g. an
            // Examples/iterator.lis must not shadow the stdlib's iterator).
            fs::path mainDir = fs::path(ctx->filePath).parent_path();
            if (mainDir.empty()) mainDir = ".";
            ctx->searchPaths.push_back(mainDir.string());
        }));
    passes.emplace_back(std::make_unique<Lexer>(context));
    passes.emplace_back(std::make_unique<Parser>(context));
    passes.emplace_back(std::make_unique<HIRBuilder>(context));
    passes.emplace_back(std::make_unique<HIRSemanticAnalyzer>(context));
    passes.emplace_back(std::make_unique<MIRBuilder>(context));
    // Borrow / move / definite-assignment / dangling-return checking on the MIR
    // CFG — the only implementation of those rules (the HIR analyzer keeps type
    // checking, mutability and E3012).
    passes.emplace_back(std::make_unique<MIRBorrowCheck>(context));
    passes.emplace_back(std::make_unique<MIRMonomorphization>(context));
    passes.emplace_back(std::make_unique<LLVMIRBuilder>(context, *context->llvmContext, context->args->getArg("filePath")));

    Emitter::Options emitOpts;
    // Readable here because this constructor parsed argv at the top.
    int optLevel = std::stoi(context->args->getArg("o"));
    emitOpts.optLevel = optLevel;
    emitOpts.runOptimiser = (optLevel > 0);
    // The same target the analyzer already classified the ABI against.
    emitOpts.targetTriple = context->targetTriple;
    emitOpts.cpu = context->targetCPU;
    emitOpts.features = context->targetFeatures;
    passes.emplace_back(std::make_unique<Emitter>(context, emitOpts));
}