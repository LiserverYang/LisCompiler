/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Context.hpp"
#include "Pass.hpp"

#include <llvm/Demangle/Demangle.h>
#include <llvm/Support/TimeProfiler.h>

#include <cctype>
#include <memory>
#include <string>
#include <typeinfo>

/**
 * Pipeline is the order of passes, that is the whole compile process
 * Common Pipeline: CompilePipeline and ParseMoudulePipeline
 */
namespace
{
/// A readable name for a pass, for the time trace. `typeid` gives the C++ name of the
/// pass, which on the Itanium ABI is the bare type encoding ("19HIRSemanticAnalyzer"),
/// and llvm::demangle only rewrites the _Z… symbol form -- so the length prefix is
/// stripped here. Every pass in this project is a flat class name, so that is exact.
inline std::string passDisplayName(const std::type_info &info)
{
    std::string name = llvm::demangle(info.name());
    size_t digits = 0;
    while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits])))
        digits++;
    if (digits > 0 && digits < name.size() &&
        (std::isalpha(static_cast<unsigned char>(name[digits])) || name[digits] == '_'))
        name = name.substr(digits);
    return name;
}
} // namespace

class Pipeline
{
protected:
    std::shared_ptr<Context> context;
    std::vector<std::unique_ptr<Pass>> passes;

public:
    Pipeline() = default;
    virtual ~Pipeline() = default;

    void run()
    {
        for (auto &pass : passes)
        {
            // One scope per pass in the time trace (`lisc --time-trace file.json`).
            // The name comes from RTTI: adding a pass needs no bookkeeping here, and
            // the trace still says Lexer / Parser / HIRSemanticAnalyzer / MIRBorrowCheck
            // / LLVMIRBuilder / Emitter instead of "pass 4".
            //
            // A TimeTraceScope costs nothing when the profiler was never started, so
            // this stays on the hot path for every compile.
            const std::string passName = passDisplayName(typeid(*pass));
            llvm::TimeTraceScope passScope(passName);
            pass->run();
        }
    }
};