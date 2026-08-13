/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * LspServer — the Lis language server state: open-document cache, the
 * Lexer→Parser→sema compile driver (gate-free, mirroring RuntimeTest), and
 * the definition/hover/completion index rebuilt after each compile.
 */

#pragma once

#include "Core/Context.hpp"

#include "llvm/Support/JSON.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class LspServer
{
public:
    /// `stdLibDir` must be the resolved `<exe dir>/lstdlib` directory.
    LspServer(std::string stdLibDir);

    /// file:// URI ↔ filesystem path (Windows drive letters handled).
    static std::string uriToPath(const std::string &uri);
    static std::string pathToUri(const std::string &path);

    // ── document sync ─────────────────────────────────────────────────────
    void openDocument(const std::string &uri, const std::string &text);
    void changeDocument(const std::string &uri, const std::string &text);
    void closeDocument(const std::string &uri);

    /// Recompile the document and return a publishDiagnostics payload
    /// {uri, diagnostics:[...]}. Only OPEN documents receive diagnostics.
    llvm::json::Object compileAndPublish(const std::string &uri);

    // ── symbol queries (0-based line/character) ────────────────────────────
    llvm::json::Value definition(const std::string &uri, size_t line, size_t character);
    llvm::json::Value hover(const std::string &uri, size_t line, size_t character);
    llvm::json::Value completion();

private:
    /// One indexable top-level definition (rebuilt after every compile).
    struct DefEntry
    {
        std::string name;   // display name (module prefix stripped)
        std::string uri;    // definition's file
        size_t line, col;   // 1-based, from HIR position
        size_t length;
        std::string typeStr; // for hover
        std::string kindStr; // "function" / "struct" / "enum" / ...
    };

    void rebuildIndex();
    /// The word at (line, character) in `text` (identifier chars).
    static std::string wordAt(const std::string &text, size_t line, size_t character);
    static llvm::json::Object makeRange(size_t line, size_t col, size_t length);

    std::string stdLibDir_;
    std::map<std::string, std::string> documents_; // uri -> text
    std::shared_ptr<Context> lastContext_;         // context of the last compile
    std::vector<DefEntry> index_;                  // rebuilt per compile
};
