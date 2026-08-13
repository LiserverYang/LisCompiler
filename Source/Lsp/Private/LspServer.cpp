/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "LspServer.hpp"

#include "Core/ModuleUtils.hpp"
#include "IR/HIRBuilder.hpp"
#include "IR/HIRSemanticAnalyzer.hpp"
#include "Lexer/Lexer.hpp"
#include "Logger/Logger.hpp"
#include "Parser/Parser.hpp"

#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

// ── URI ↔ path ──────────────────────────────────────────────────────────────

std::string LspServer::uriToPath(const std::string &uri)
{
    std::string p = uri;
    // file:///C:/dir/file.lis → C:/dir/file.lis ; file:///home/x → /home/x
    if (p.rfind("file://", 0) == 0)
    {
        p = p.substr(7);
        // Windows: strip the slash BEFORE the drive letter (/F:/x → F:/x);
        // POSIX keeps the leading slash (/home/x).
        if (!p.empty() && p[0] == '/' && p.size() >= 3 && p[2] == ':')
            p = p.substr(1);
    }
    // Percent-decode.
    std::string out;
    for (size_t i = 0; i < p.size(); ++i)
    {
        if (p[i] == '%' && i + 2 < p.size() && std::isxdigit((unsigned char)p[i + 1]) && std::isxdigit((unsigned char)p[i + 2]))
        {
            out += (char)std::stoi(p.substr(i + 1, 2), nullptr, 16);
            i += 2;
        }
        else
        {
            out += p[i];
        }
    }
    return out;
}

std::string LspServer::pathToUri(const std::string &path)
{
    std::string p = path;
    for (auto &c : p)
        if (c == '\\') c = '/';
    // Windows drive-letter path ("F:/...") is already absolute — file:///F:/...
    // (fs::absolute on such paths is unreliable across standard libraries).
    if (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':')
        return "file:///" + p;
    fs::path abs = fs::absolute(path);
    p = abs.string();
    for (auto &c : p)
        if (c == '\\') c = '/';
    if (!p.empty() && p[0] != '/')
        p = "/" + p;
    return "file://" + p;
}

// ── ctor ─────────────────────────────────────────────────────────────────────

LspServer::LspServer(std::string stdLibDir)
    : stdLibDir_(std::move(stdLibDir))
{
}

// ── document sync ───────────────────────────────────────────────────────────

void LspServer::openDocument(const std::string &uri, const std::string &text)
{
    documents_[uri] = text;
}

void LspServer::changeDocument(const std::string &uri, const std::string &text)
{
    documents_[uri] = text;
}

void LspServer::closeDocument(const std::string &uri)
{
    documents_.erase(uri);
}

// ── compile driver ───────────────────────────────────────────────────────────

static std::string parentDir(const std::string &path)
{
    fs::path p(path);
    fs::path dir = p.parent_path();
    return dir.empty() ? "." : dir.string();
}

llvm::json::Object LspServer::makeRange(size_t line, size_t col, size_t length)
{
    // 1-based line/col → 0-based LSP Position; guard zero (module-level
    // diagnostics carry 0:0).
    size_t l = line > 0 ? line - 1 : 0;
    size_t c = col > 0 ? col - 1 : 0;
    llvm::json::Object start{{"line", l}, {"character", c}};
    llvm::json::Object end{{"line", l}, {"character", c + length}};
    return llvm::json::Object{{"start", std::move(start)}, {"end", std::move(end)}};
}

llvm::json::Object LspServer::compileAndPublish(const std::string &uri)
{
    auto docIt = documents_.find(uri);
    if (docIt == documents_.end())
        return llvm::json::Object{{"uri", uri}, {"diagnostics", llvm::json::Array()}};

    std::string path = uriToPath(uri);

    auto context = std::make_shared<Context>();
    context->args->setArg("filePath", path);
    // Search order mirrors CompilePipeline: the stdlib FIRST (a user file must
    // not shadow a stdlib module), then the document's directory.
    context->searchPaths.push_back(stdLibDir_);
    context->searchPaths.push_back(parentDir(path));

    context->filePath = path;
    context->fileValue = docIt->second;

    // Gate-free compile driver (mirrors RuntimeTest::compile): Lexer::run +
    // Parser::parseAll + HIRBuilder::run + sema.visit. Diagnostics are captured
    // (never printed, never exit).
    Logger::BeginCapture();
    Lexer lexer(context);
    lexer.run();
    Parser parser(context);
    parser.parseAll();
    HIRBuilder builder(context);
    builder.run();
    HIRSemanticAnalyzer sema(context);
    sema.visit(context->hirProgram.get());
    std::vector<Logger::Captured> captured = Logger::EndCapture();

    lastContext_ = context;
    rebuildIndex();

    llvm::json::Array diags;
    for (const auto &c : captured)
    {
        // Only OPEN documents receive diagnostics (module files the user
        // hasn't opened are skipped — the client ignores them anyway).
        std::string diagUri = pathToUri(c.codePath);
        if (!documents_.count(diagUri))
            continue;
        llvm::json::Object d{
            {"range", makeRange(c.line, c.col, c.length)},
            {"severity", c.level == Logger::LogLevel::ERROR ? 1
                         : c.level == Logger::LogLevel::WARNING ? 2 : 3},
            {"message", c.msg},
            {"source", "lisc"},
        };
        if (c.errorId != 0)
            d["code"] = "E" + std::to_string(c.errorId);
        diags.push_back(std::move(d));
    }

    return llvm::json::Object{{"uri", uri}, {"diagnostics", std::move(diags)}};
}

// ── definition index ─────────────────────────────────────────────────────────

void LspServer::rebuildIndex()
{
    index_.clear();
    if (!lastContext_ || !lastContext_->hirProgram)
        return;

    const auto &attrs = lastContext_->stmtAttributions;
    const auto &items = lastContext_->hirProgram->items;

    auto addEntry = [&](const std::string &name, const std::string &uri,
                        size_t line, size_t col, size_t length,
                        const std::string &typeStr, const std::string &kindStr)
    {
        DefEntry e;
        e.name = name;
        e.uri = uri;
        e.line = line;
        e.col = col;
        e.length = length;
        e.typeStr = typeStr;
        e.kindStr = kindStr;
        index_.push_back(std::move(e));
    };

    for (size_t i = 0; i < items.size(); ++i)
    {
        std::string file = (i < attrs.size()) ? attrs[i].filePath : std::string();
        std::string uri = file.empty() ? "" : pathToUri(file);
        HIRNode *node = items[i].get();
        std::string typeStr, kindStr, name;

        if (auto *f = dynamic_cast<HIRFunction *>(node))
        {
            name = displayName(f->name);
            kindStr = "function";
            typeStr = f->type ? f->type->toString() : "";
            if (f->isMethod)
                name = displayName(f->associatedStruct) + "::" + f->name;
        }
        else if (auto *s = dynamic_cast<HIRStruct *>(node))
        {
            name = displayName(s->name);
            kindStr = "struct";
            typeStr = s->name;
        }
        else if (auto *e = dynamic_cast<HIREnum *>(node))
        {
            name = displayName(e->name);
            kindStr = "enum";
            typeStr = e->name;
        }
        else if (auto *t = dynamic_cast<HIRTrait *>(node))
        {
            name = displayName(t->name);
            kindStr = "trait";
            typeStr = t->name;
        }
        else if (auto *v = dynamic_cast<HIRVarDecl *>(node))
        {
            if (!v->isGlobal) continue;
            name = displayName(v->name);
            kindStr = "variable";
            typeStr = v->type ? v->type->toString() : "";
        }
        else if (auto *impl = dynamic_cast<HIRImpl *>(node))
        {
            // Index impl methods as `Struct::method`.
            std::string structName = displayName(impl->structName);
            for (const auto &m : impl->methods)
            {
                addEntry(structName + "::" + m->name, uri, m->position.line,
                         m->position.col, m->length,
                         m->type ? m->type->toString() : "", "method");
            }
            continue;
        }
        else
        {
            continue;
        }

        // The range covers just the NAME (HIR node length spans the whole
        // declaration, which would highlight far past the identifier).
        addEntry(name, uri, node->position.line, node->position.col,
                 name.size(), typeStr, kindStr);
    }
}

std::string LspServer::wordAt(const std::string &text, size_t line, size_t character)
{
    // Find the line's byte span.
    size_t lineStart = 0;
    size_t cur = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (cur == line) { lineStart = i; break; }
        if (text[i] == '\n') ++cur;
    }
    if (cur != line && line != 0)
        return "";
    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = text.size();

    size_t pos = lineStart + character;
    if (pos >= lineEnd)
        return "";
    // Extend left/right over identifier chars.
    size_t l = pos, r = pos;
    auto isWord = [](char c)
    {
        return std::isalnum((unsigned char)c) || c == '_';
    };
    while (l > lineStart && isWord(text[l - 1])) --l;
    while (r < lineEnd && isWord(text[r])) ++r;
    if (l == r) return "";
    return text.substr(l, r - l);
}

// ── queries ──────────────────────────────────────────────────────────────────

llvm::json::Value LspServer::definition(const std::string &uri, size_t line, size_t character)
{
    auto docIt = documents_.find(uri);
    if (docIt == documents_.end())
        return llvm::json::Value(nullptr);
    std::string word = wordAt(docIt->second, line, character);
    if (word.empty())
        return llvm::json::Value(nullptr);

    for (const auto &e : index_)
    {
        if (e.name != word)
            continue;
        llvm::json::Array locs;
        llvm::json::Object loc{
            {"uri", e.uri},
            {"range", makeRange(e.line, e.col, e.length)},
        };
        locs.push_back(std::move(loc));
        return llvm::json::Value(std::move(locs));
    }
    return llvm::json::Value(nullptr);
}

llvm::json::Value LspServer::hover(const std::string &uri, size_t line, size_t character)
{
    auto docIt = documents_.find(uri);
    if (docIt == documents_.end())
        return llvm::json::Value(nullptr);
    std::string word = wordAt(docIt->second, line, character);
    if (word.empty())
        return llvm::json::Value(nullptr);

    for (const auto &e : index_)
    {
        if (e.name != word)
            continue;
        std::string value = "```lis\n" + e.kindStr;
        if (!e.typeStr.empty())
            value += " " + e.typeStr;
        value += "\n```";
        llvm::json::Object content{{"kind", "markdown"}, {"value", value}};
        return llvm::json::Value(llvm::json::Object{{"contents", std::move(content)}});
    }
    return llvm::json::Value(nullptr);
}

llvm::json::Value LspServer::completion()
{
    static const std::vector<std::pair<std::string, int>> keywords = {
        {"impt", 14}, {"struct", 22}, {"enum", 13}, {"trait", 11}, {"impl", 11},
        {"fn", 3}, {"let", 13}, {"mut", 14}, {"pub", 14}, {"ret", 14},
        {"if", 14}, {"else", 14}, {"while", 14}, {"for", 14}, {"in", 14},
        {"match", 14}, {"break", 14}, {"continue", 14}, {"as", 14},
        {"self", 6}, {"move", 14}, {"true", 16}, {"false", 16},
        {"i8", 7}, {"i16", 7}, {"i32", 7}, {"i64", 7}, {"f32", 7}, {"f64", 7},
        {"bool", 17}, {"char", 7}, {"void", 7},
    };

    llvm::json::Array items;
    for (const auto &[kw, kind] : keywords)
    {
        items.push_back(llvm::json::Object{{"label", kw}, {"kind", kind}});
    }
    // Indexed definitions (display names, deduped).
    std::vector<std::string> seen;
    for (const auto &e : index_)
    {
        if (std::find(seen.begin(), seen.end(), e.name) != seen.end())
            continue;
        seen.push_back(e.name);
        int kind = e.kindStr == "function" ? 3 : e.kindStr == "struct" ? 22
                   : e.kindStr == "enum" ? 13 : e.kindStr == "trait" ? 11
                   : e.kindStr == "variable" ? 6 : e.kindStr == "method" ? 2 : 6;
        items.push_back(llvm::json::Object{{"label", e.name}, {"kind", kind}});
    }
    return llvm::json::Value(std::move(items));
}
