/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * InternalError.hpp — the last-resort path for compiler BUGS.
 *
 * Every failure the compiler can understand is a diagnostic (Logger + E-codes).
 * What is left is the compiler reaching a state it cannot express: an LLVM
 * module it refuses to verify, an unhandled node kind, an invariant assert.
 * Those are bugs in the compiler, and the user can do nothing about them — so
 * the contract here is: never leave the user with a bare `terminate called` and
 * an abort. Say what happened, show a stack trace, and say where to report it.
 */

#pragma once

#include <string>

/** Exit status of an internal compiler error (sysexits.h EX_SOFTWARE). Chosen
 *  so a build script can tell "your program is invalid" (1) from "the compiler
 *  broke" (70). */
inline constexpr int kInternalErrorExitCode = 70;

/** Print the internal-error banner: what failed, a stack trace, the build stamp
 *  and the issue URL. Safe to call from a terminate handler (it does not throw
 *  and does not allocate beyond the strings it prints). */
void ReportInternalCompilerError(const std::string &what);

/** Install the crash/terminate handlers and remember the command line for the
 *  banner. Call once, first thing in main():
 *    - a HARD fault (access violation, illegal instruction) gets an LLVM stack
 *      trace instead of a silent exception code;
 *    - std::terminate() (an exception escaping every frame, or thrown while
 *      unwinding) is routed through ReportInternalCompilerError. */
void InstallCrashHandlers(int argc, const char *const *argv);
