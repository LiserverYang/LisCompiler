/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Analysiser/TypeContext.hpp"
#include "Argparser/Args.hpp"
#include "Core/ModuleUtils.hpp"
#include "IR/HIR.hpp"
#include "IR/MIR.hpp"
#include "Lexer/Token.hpp"
#include "Parser/AST.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <unordered_map>
#include <unordered_set>

/**
 * Context stored all informations of compiler, these will be shared in different passes
 */
struct Context
{
    std::string filePath;
    std::string fileValue;

    std::shared_ptr<Args> args = std::make_shared<Args>();

    /// Enum type names seen so far, shared across ALL parser instances in this
    /// compilation unit (the stdlib files and the main file are each parsed by
    /// a fresh Parser, so `EnumName::Variant(...)` must know enums from earlier
    /// files). Stores INTERNAL names (module-prefixed).
    std::unordered_set<std::string> knownEnums;

    // ── module system registry (filled by the Parser, consumed by later passes) ──

    /// Per-top-level-statement module attribution, parallel to
    /// program.globalStatements (HIRBuilder/sema read it to know which module
    /// each item belongs to).
    std::vector<StmtAttribution> stmtAttributions;

    /// Importing module path → its `impt` bindings.
    std::unordered_map<std::string, std::vector<ImportBinding>> importsByModule;

    /// Canonical module paths that have been fully loaded (dedup / cycle-free).
    std::unordered_set<std::string> loadedModules;

    /// Directories searched for `impt foo.bar;` → `<dir>/foo/bar.lis`.
    /// Order: main-file directory, then -I dirs, then the stdlib dir.
    std::vector<std::string> searchPaths;

    /// Loaded file contents keyed by absolute path — for multi-file diagnostics
    /// (Context::filePath/fileValue is a single slot, restored after parsing).
    std::unordered_map<std::string, std::string> fileContents;

    /**
     * When a source code be passed by Lexer，we will get TokenStream
     */
    TokenStream tokenStream;

    /**
     * Program is the root of AST
     * It will be generate after Parser parsed the tokenStream
     */
    Program program;

    std::shared_ptr<TypeContext> typeContext = std::make_shared<TypeContext>();

    std::unique_ptr<HIRProgram> hirProgram;

    std::unique_ptr<MIRProgram> mirProgram;

    llvm::LLVMContext llvmContext;
    std::unique_ptr<llvm::Module> module;
};