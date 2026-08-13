/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * lisls — the Lis language server (LSP over stdin/stdout).
 *
 * Framing: `Content-Length: N\r\n\r\n<json>` in, same out. Supported methods:
 * initialize / initialized / shutdown / exit, textDocument/didOpen/didChange/
 * didClose, textDocument/definition/hover/completion.
 */

#include "LspServer.hpp"

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;

// ── framing helpers ─────────────────────────────────────────────────────────

static bool readExact(std::string &out, size_t n)
{
    out.resize(n);
    size_t got = 0;
    while (got < n)
    {
        size_t r = fread(&out[got], 1, n - got, stdin);
        if (r == 0)
            return false;
        got += r;
    }
    return true;
}

/// Read one LSP message body; -1 on EOF, -2 on malformed header.
static int readMessage(std::string &body)
{
    std::string line;
    int contentLength = -1;
    while (true)
    {
        int ch = fgetc(stdin);
        if (ch == EOF)
            return -1;
        if (ch == '\n')
        {
            if (line.empty())
                break; // end of headers
            const char *prefix = "Content-Length:";
            if (line.rfind(prefix, 0) == 0)
                contentLength = std::stoi(line.substr(strlen(prefix)));
            line.clear();
        }
        else if (ch != '\r')
        {
            line += (char)ch;
        }
    }
    if (contentLength <= 0)
        return -2;
    if (!readExact(body, (size_t)contentLength))
        return -1;
    return contentLength;
}

static void writeMessage(const llvm::json::Value &msg)
{
    std::string out;
    llvm::raw_string_ostream os(out);
    os << msg;
    os.flush();
    std::cerr << "[lisls] write " << out.size() << " bytes\n";
    std::string header = "Content-Length: " + std::to_string(out.size()) + "\r\n\r\n";
    fwrite(header.data(), 1, header.size(), stdout);
    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
}

// ── small JSON accessors ────────────────────────────────────────────────────

static std::string getString(const llvm::json::Object *o, llvm::StringRef key)
{
    if (!o)
        return "";
    if (auto s = o->getString(key))
        return s->str();
    return "";
}

static const llvm::json::Object *getObjectField(const llvm::json::Object *o, llvm::StringRef key)
{
    const llvm::json::Value *v = o ? o->get(key) : nullptr;
    return v ? v->getAsObject() : nullptr;
}

int main(int argc, const char **argv)
{
    // Binary stdio: the CRT's text mode would translate \n → \r\n and corrupt
    // the Content-Length framing on Windows.
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // The stdlib sits next to the server (<exe dir>/lstdlib), like lisc.exe.
    fs::path exeDir = argc > 0 ? fs::path(argv[0]).parent_path() : fs::path(".");
    fs::path stdLibDir = exeDir / "lstdlib";
    if (!fs::is_directory(stdLibDir))
    {
        std::cerr << "lisls: cannot find stdlib at " << stdLibDir.string() << "\n";
        return 1;
    }

    LspServer server(stdLibDir.string());

    bool shutdownRequested = false;
    while (true)
    {
        std::string body;
        int len = readMessage(body);
        if (len < 0)
            break; // EOF / malformed — exit
        std::cerr << "[lisls] got " << body.size() << " bytes: " << body.substr(0, 80) << "\n";

        auto parsed = llvm::json::parse(body);
        if (!parsed)
            continue;
        const llvm::json::Object *req = parsed->getAsObject();
        if (!req)
            continue;

        std::string method = getString(req, "method");
        llvm::json::Value id = req->get("id") ? *req->get("id") : llvm::json::Value(nullptr);
        bool isNotification = req->get("id") == nullptr;

        llvm::json::Value result = llvm::json::Value(nullptr);

        if (method == "initialize")
        {
            llvm::json::Object sync{{"openClose", true}, {"change", 1 /* full */}};
            llvm::json::Object capabilities{
                {"textDocumentSync", std::move(sync)},
                {"hoverProvider", true},
                {"definitionProvider", true},
                {"completionProvider", llvm::json::Object{{"triggerCharacters", llvm::json::Array()}}},
            };
            result = llvm::json::Object{
                {"capabilities", std::move(capabilities)},
                {"serverInfo", llvm::json::Object{{"name", "lisls"}, {"version", "0.1.0"}}},
            };
        }
        else if (method == "initialized")
        {
            isNotification = true; // no response
        }
        else if (method == "shutdown")
        {
            shutdownRequested = true;
            result = llvm::json::Value(nullptr);
        }
        else if (method == "exit")
        {
            return shutdownRequested ? 0 : 1;
        }
        else if (method == "textDocument/didOpen" || method == "textDocument/didChange")
        {
            const llvm::json::Object *params = getObjectField(req, "params");
            const llvm::json::Object *td = getObjectField(params, "textDocument");
            std::string uri = getString(td, "uri");
            std::string text = getString(td, "text");
            if (!uri.empty())
            {
                if (method == "textDocument/didOpen")
                    server.openDocument(uri, text);
                else
                    server.changeDocument(uri, text);
                // Debounce: sleep briefly so a burst of keystrokes collapses
                // into one recompile.
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                llvm::json::Object publish = server.compileAndPublish(uri);
                writeMessage(llvm::json::Value(llvm::json::Object{
                    {"jsonrpc", "2.0"},
                    {"method", "textDocument/publishDiagnostics"},
                    {"params", std::move(publish)},
                }));
            }
            continue; // notification — no response
        }
        else if (method == "textDocument/didClose")
        {
            const llvm::json::Object *params = getObjectField(req, "params");
            const llvm::json::Object *td = getObjectField(params, "textDocument");
            server.closeDocument(getString(td, "uri"));
            continue; // notification
        }
        else if (method == "textDocument/definition")
        {
            const llvm::json::Object *params = getObjectField(req, "params");
            const llvm::json::Object *td = getObjectField(params, "textDocument");
            const llvm::json::Object *pos = getObjectField(params, "position");
            if (!td) continue;
            std::string uri = getString(td, "uri");
            if (!pos) continue;
            size_t line = 0, character = 0;
            if (const llvm::json::Value *lv = pos->get("line"))
                line = (size_t)lv->getAsInteger().value_or(0);
            if (const llvm::json::Value *cv = pos->get("character"))
                character = (size_t)cv->getAsInteger().value_or(0);
            result = server.definition(uri, line, character);
        }
        else if (method == "textDocument/hover")
        {
            const llvm::json::Object *params = getObjectField(req, "params");
            const llvm::json::Object *td = getObjectField(params, "textDocument");
            const llvm::json::Object *pos = getObjectField(params, "position");
            if (!td) continue;
            std::string uri = getString(td, "uri");
            if (!pos) continue;
            size_t line = 0, character = 0;
            if (const llvm::json::Value *lv = pos->get("line"))
                line = (size_t)lv->getAsInteger().value_or(0);
            if (const llvm::json::Value *cv = pos->get("character"))
                character = (size_t)cv->getAsInteger().value_or(0);
            result = server.hover(uri, line, character);
        }
        else if (method == "textDocument/completion")
        {
            result = server.completion();
        }
        else
        {
            continue; // unknown method — ignore
        }

        if (!isNotification)
        {
            writeMessage(llvm::json::Value(llvm::json::Object{
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", result},
            }));
        }
    }
    return 0;
}
