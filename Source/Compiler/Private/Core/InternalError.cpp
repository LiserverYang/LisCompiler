/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Core/InternalError.hpp"

#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <exception>

namespace
{
/// Where a compiler bug should be reported. Kept in one place so the text and
/// the URL cannot drift apart.
constexpr const char *kIssueUrl = "https://github.com/LiserverYang/LisCompiler/issues/new";

/// The command line that triggered the failure (set by InstallCrashHandlers).
/// A reproduction is the single most useful thing in a bug report.
std::string gCommandLine;
} // namespace

void ReportInternalCompilerError(const std::string &what)
{
    llvm::raw_ostream &os = llvm::errs();
    os << "\n"
       << "================================================================================\n"
       << "internal compiler error: " << what << "\n"
       << "\n"
       << "The Lis compiler reached a state it cannot report as a normal diagnostic." << "\n"
       << "This is a bug in the COMPILER, not in your program: your source does not have" << "\n"
       << "to change for the compiler to be fixed." << "\n"
       << "\n"
       << "Please report it at" << "\n"
       << "    " << kIssueUrl << "\n"
       << "and paste this whole message, including the stack trace below, together with" << "\n"
       << "the source file (or a minimal snippet) that triggered it." << "\n"
       << "\n"
       << "compiler build: " << __DATE__ << " " << __TIME__ << "\n";
    if (!gCommandLine.empty())
        os << "command line:   " << gCommandLine << "\n";
    os << "================================================================================\n"
       << "\n"
       << "Stack trace:\n";
    os.flush();

    // LLVM's printer symbolizes through the image's symbol table (dbghelp on
    // Windows), so a Debug build gives function names plus offsets; a stripped
    // Release build still gives module+offset, which is enough to correlate two
    // reports of the same bug.
    llvm::sys::PrintStackTrace(os);
    os << "\n";
    os.flush();
}

void InstallCrashHandlers(int argc, const char *const *argv)
{
    for (int i = 0; i < argc; ++i)
    {
        if (i > 0) gCommandLine += ' ';
        gCommandLine += argv[i] ? argv[i] : "";
    }

    // Hard faults (access violation, illegal instruction, stack overflow where
    // the handler can still run): print an LLVM stack trace instead of dying
    // with a bare exception code and no output at all.
    // DisableCrashReporting must stay FALSE: LLVM installs the Windows/Linux
    // fatal-signal handler that PRINTS the trace only on the crash-reporting
    // path. With it disabled a segfault still died silently (measured: a
    // 0xC0000005 in the analyzer produced no output at all).
    llvm::sys::PrintStackTraceOnErrorSignal(gCommandLine, /*DisableCrashReporting=*/false);

    // An exception that escapes every frame (or is thrown while unwinding) would
    // otherwise print `terminate called after throwing an instance of ...` and
    // abort, which tells the user nothing and does not say it is a compiler bug.
    std::set_terminate([]()
        {
            ReportInternalCompilerError("std::terminate() — an exception escaped the compiler, or one was thrown while unwinding another");
            std::_Exit(kInternalErrorExitCode); });
}
