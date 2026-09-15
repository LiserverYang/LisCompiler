/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "LspServer.hpp"

#include "Analysiser/SymbolTable.hpp"
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
    // Defensive: collapse a duplicated drive prefix ("F:/F:/x" → "F:/x") that
    // a broken upstream path construction may produce.
    if (p.size() >= 6 && std::isalpha((unsigned char)p[0]) && p[1] == ':'
        && p[2] == '/' && std::isalpha((unsigned char)p[3]) && p[4] == ':' && p[5] == '/')
        p = p.substr(3);
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

// ── pretty names ─────────────────────────────────────────────────────────────

std::string LspServer::prettyName(const std::string &s)
{
    // Strip the module prefix from every identifier token: "math$max" → "max",
    // "math$Option$i32" → "Option$i32" (the mono suffix stays). `$` is not a
    // legal source character, so a token containing it is internal.
    std::string out;
    for (size_t i = 0; i < s.size();)
    {
        char c = s[i];
        if (std::isalpha((unsigned char)c) || c == '_')
        {
            size_t j = i;
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '$'))
                ++j;
            std::string tok = s.substr(i, j - i);
            size_t dollar = tok.find('$');
            if (dollar != std::string::npos)
                tok = tok.substr(dollar + 1);
            out += tok;
            i = j;
        }
        else
        {
            out += c;
            ++i;
        }
    }
    return out;
}

// ── ctor / document sync ─────────────────────────────────────────────────────

LspServer::LspServer(std::string stdLibDir)
    : stdLibDir_(std::move(stdLibDir))
{
}

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
    // Same directory = the unsafe-core boundary (heap primitives + raw-pointer
    // ops are stdlib-only), so stdlib modules are not misreported.
    context->stdLibDirs.push_back(stdLibDir_);

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

// ── symbol index ─────────────────────────────────────────────────────────────
//
// The index is rebuilt after every compile by walking the WHOLE HIR tree (not
// just top-level items): sema has already resolved every node's type, so
// variables, params, uses and member calls all carry their type information.

void LspServer::rebuildIndex()
{
    index_.clear();
    if (!lastContext_ || !lastContext_->hirProgram)
        return;

    const auto &attrs = lastContext_->stmtAttributions;
    const auto &items = lastContext_->hirProgram->items;

    auto add = [&](const std::string &name, const std::string &uri,
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

        if (auto *f = dynamic_cast<HIRFunction *>(node))
        {
            std::string nm = prettyName(displayName(f->name));
            if (f->isMethod)
                nm = prettyName(displayName(f->associatedStruct)) + "::" + prettyName(f->name);
            add(nm, uri, f->position.line, f->position.col, prettyName(f->name).size(),
                f->type ? prettyName(f->type->toString()) : "", f->isMethod ? "method" : "function");
            // Params have no source position in HIR — index them for
            // completion/hover only (empty uri).
            for (const auto &[pname, ptype] : f->params)
                add(prettyName(pname), "", 0, 0, pname.size(),
                    ptype ? prettyName(ptype->toString()) : "", "param");
            if (f->body)
                walkBlock(f->body.get(), uri);
        }
        else if (auto *s = dynamic_cast<HIRStruct *>(node))
        {
            add(prettyName(displayName(s->name)), uri, s->position.line, s->position.col,
                prettyName(displayName(s->name)).size(), prettyName(s->name), "struct");
        }
        else if (auto *e = dynamic_cast<HIREnum *>(node))
        {
            add(prettyName(displayName(e->name)), uri, e->position.line, e->position.col,
                prettyName(displayName(e->name)).size(), prettyName(e->name), "enum");
        }
        else if (auto *t = dynamic_cast<HIRTrait *>(node))
        {
            add(prettyName(displayName(t->name)), uri, t->position.line, t->position.col,
                prettyName(displayName(t->name)).size(), prettyName(t->name), "trait");
        }
        else if (auto *v = dynamic_cast<HIRVarDecl *>(node))
        {
            if (v->isGlobal)
                add(prettyName(displayName(v->name)), uri, v->position.line, v->position.col,
                    v->name.size(), v->type ? prettyName(v->type->toString()) : "", "variable");
        }
        else if (auto *impl = dynamic_cast<HIRImpl *>(node))
        {
            std::string structName = prettyName(displayName(impl->structName));
            for (const auto &m : impl->methods)
            {
                add(structName + "::" + prettyName(m->name), uri, m->position.line,
                    m->position.col, m->name.size(),
                    m->type ? prettyName(m->type->toString()) : "", "method");
                for (const auto &[pname, ptype] : m->params)
                    add(prettyName(pname), "", 0, 0, pname.size(),
                        ptype ? prettyName(ptype->toString()) : "", "param");
                if (m->body)
                    walkBlock(m->body.get(), uri);
            }
        }
    }
}

void LspServer::walkBlock(HIRBlock *block, const std::string &uri)
{
    if (!block)
        return;
    for (auto &s : block->stmts)
        walkStmt(s.get(), uri);
}

void LspServer::walkStmt(HIRStmt *stmt, const std::string &uri)
{
    if (!stmt)
        return;
    if (auto *v = dynamic_cast<HIRVarDecl *>(stmt))
    {
        if (!v->isGlobal)
            index_.push_back(DefEntry{prettyName(v->name), uri, v->position.line,
                                      v->position.col, v->name.size(),
                                      v->type ? prettyName(v->type->toString()) : "",
                                      "variable"});
        if (v->init && *v->init)
            walkExpr(v->init->get(), uri);
    }
    else if (auto *a = dynamic_cast<HIRAssign *>(stmt))
    {
        walkExpr(a->target.get(), uri);
        walkExpr(a->value.get(), uri);
    }
    else if (auto *e = dynamic_cast<HIRExprStmt *>(stmt))
    {
        walkExpr(e->expr.get(), uri);
    }
    else if (auto *i = dynamic_cast<HIRIf *>(stmt))
    {
        walkExpr(i->cond.get(), uri);
        if (i->thenBlock)
            walkBlock(i->thenBlock.get(), uri);
        if (i->elseBlock && *i->elseBlock)
            walkBlock(i->elseBlock->get(), uri);
    }
    else if (auto *l = dynamic_cast<HIRLoop *>(stmt))
    {
        if (l->cond && *l->cond)
            walkExpr(l->cond->get(), uri);
        if (l->body)
            walkBlock(l->body.get(), uri);
    }
    else if (auto *r = dynamic_cast<HIRReturn *>(stmt))
    {
        if (r->value && *r->value)
            walkExpr(r->value->get(), uri);
    }
    else if (auto *b = dynamic_cast<HIRBlock *>(stmt))
    {
        walkBlock(b, uri);
    }
}

void LspServer::walkExpr(HIRExpr *expr, const std::string &uri)
{
    if (!expr)
        return;
    if (auto *n = dynamic_cast<HIRNameRef *>(expr))
    {
        // A variable USE: sema has written back the resolved name and set the
        // type — index both for hover (type display).
        if (n->symbol)
            index_.push_back(DefEntry{prettyName(displayName(n->name)), uri,
                                      n->position.line, n->position.col,
                                      prettyName(displayName(n->name)).size(),
                                      n->type ? prettyName(n->type->toString()) : "",
                                      "use"});
        return;
    }
    if (auto *c = dynamic_cast<HIRCall *>(expr))
    {
        if (c->callKind == HIRCall::CallKind::Method)
        {
            // Member call: index the SHORT name so `x.push_char` hovers match.
            index_.push_back(DefEntry{prettyName(c->methodName), uri,
                                      c->position.line, c->position.col,
                                      c->methodName.size(),
                                      c->type ? prettyName(c->type->toString()) : "",
                                      "method"});
            if (c->object)
                walkExpr(c->object.get(), uri);
        }
        else if (c->callKind == HIRCall::CallKind::Static)
        {
            index_.push_back(DefEntry{prettyName(displayName(c->staticTypeName)) + "::" + prettyName(c->methodName),
                                      uri, c->position.line, c->position.col,
                                      c->methodName.size(), "", "method"});
        }
        else if (c->callee)
        {
            walkExpr(c->callee.get(), uri);
        }
        for (auto &a : c->args)
            walkExpr(a.get(), uri);
        return;
    }
    if (auto *m = dynamic_cast<HIRMemberAccess *>(expr))
    {
        walkExpr(m->object.get(), uri);
        return;
    }
    if (auto *ix = dynamic_cast<HIRIndexAccess *>(expr))
    {
        walkExpr(ix->object.get(), uri);
        walkExpr(ix->index.get(), uri);
        return;
    }
    if (auto *b = dynamic_cast<HIRBinaryOp *>(expr))
    {
        walkExpr(b->left.get(), uri);
        walkExpr(b->right.get(), uri);
        return;
    }
    if (auto *cs = dynamic_cast<HIRCast *>(expr))
    {
        walkExpr(cs->expr.get(), uri);
        return;
    }
    if (auto *r = dynamic_cast<HIRRef *>(expr))
    {
        walkExpr(r->expr.get(), uri);
        return;
    }
    if (auto *t = dynamic_cast<HIRTry *>(expr))
    {
        // `expr?`: index the operand too (the operator itself defines no symbol).
        walkExpr(t->expr.get(), uri);
        return;
    }
    if (auto *s = dynamic_cast<HIRStructInit *>(expr))
    {
        for (auto &[field, val] : s->members)
            walkExpr(val.get(), uri);
        return;
    }
    if (auto *v = dynamic_cast<HIRVariantInit *>(expr))
    {
        for (auto &a : v->args)
            walkExpr(a.get(), uri);
        return;
    }
    if (auto *arr = dynamic_cast<HIRArrayLiteral *>(expr))
    {
        for (auto &e : arr->elements)
            walkExpr(e.get(), uri);
        return;
    }
    if (auto *match = dynamic_cast<HIRMatch *>(expr))
    {
        walkExpr(match->scrutinee.get(), uri);
        for (auto &arm : match->arms)
        {
            if (arm.body)
                walkBlock(arm.body.get(), uri);
            if (arm.tailValue)
                walkExpr(arm.tailValue.get(), uri);
        }
        return;
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

    // A declaration (variable/function/type/method) with a real location wins;
    // a USE falls back to the same-named variable declaration.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto &e : index_)
        {
            bool isUse = e.kindStr == "use";
            bool isParam = e.kindStr == "param";
            if ((pass == 0 && (isUse || isParam)) || (pass == 1 && !isUse))
                continue;
            if (e.uri.empty())
                continue;
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

    auto markdown = [](const std::string &kind, const std::string &typeStr)
    {
        std::string value = "```lis\n" + kind;
        if (!typeStr.empty())
            value += " " + typeStr;
        value += "\n```";
        llvm::json::Object content{{"kind", "markdown"}, {"value", value}};
        return llvm::json::Value(llvm::json::Object{{"contents", std::move(content)}});
    };

    // 1. Exact declaration (variable / function / type / param).
    for (const auto &e : index_)
    {
        if (e.kindStr == "use" || e.kindStr == "method")
            continue;
        if (e.name == word)
            return markdown(e.kindStr, e.typeStr);
    }
    // 2. A use site — show the resolved type.
    for (const auto &e : index_)
    {
        if (e.kindStr == "use" && e.name == word)
            return markdown("variable", e.typeStr);
    }
    // 3. Member call by short name (`push_char` → `String::push_char`).
    for (const auto &e : index_)
    {
        if (e.kindStr != "method")
            continue;
        size_t cc = e.name.rfind("::");
        std::string shortName = (cc == std::string::npos) ? e.name : e.name.substr(cc + 2);
        if (shortName == word)
            return markdown("method " + e.name, e.typeStr);
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
    std::vector<std::string> seen;
    auto emit = [&](const std::string &label, int kind)
    {
        if (std::find(seen.begin(), seen.end(), label) != seen.end())
            return;
        seen.push_back(label);
        items.push_back(llvm::json::Object{{"label", label}, {"kind", kind}});
    };

    for (const auto &[kw, kind] : keywords)
        emit(kw, kind);

    // Indexed symbols (variables, params, functions, types, methods).
    for (const auto &e : index_)
    {
        if (e.kindStr == "use")
            continue;
        int kind = e.kindStr == "function" ? 3 : e.kindStr == "struct" ? 22
                   : e.kindStr == "enum" ? 13 : e.kindStr == "trait" ? 11
                   : e.kindStr == "variable" ? 6 : e.kindStr == "param" ? 6
                   : e.kindStr == "method" ? 2 : 6;
        emit(e.name, kind);
    }

    // Global-scope symbols from the SymbolTable: covers selective-import
    // alias symbols (`impt math { max }` promotes `max`) that live outside the
    // HIR items.
    if (auto scope = SymbolTable::getInstance().getCurrentScope())
    {
        for (const auto &[slotName, sym] : scope->getSymbols())
        {
            const Symbol *target = sym.get();
            while (target && target->aliasTarget)
                target = target->aliasTarget;
            std::string label = prettyName(displayName(slotName));
            if (label.empty())
                continue;
            int kind = 6;
            if (target)
            {
                switch (target->kind)
                {
                case SymbolKind::Function: kind = 3; break;
                case SymbolKind::Struct: kind = 22; break;
                case SymbolKind::Trait: kind = 11; break;
                case SymbolKind::GlobalVar: kind = 6; break;
                default: break;
                }
            }
            emit(label, kind);
        }
    }
    return llvm::json::Value(std::move(items));
}
