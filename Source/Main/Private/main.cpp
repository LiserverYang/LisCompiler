/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The entrypoint of compiler
 */

#include "Argparser/Argparser.hpp"
#include "Core/CompilePipeline.hpp"
#include "Core/InternalError.hpp"
#include "Parser/ASTPrinter.hpp"

#include <exception>
#include <iostream>
#include <memory>

int main(int argc, const char **argv)
{
    // Anything the compiler cannot express as a diagnostic lands here: a hard
    // fault (access violation) prints an LLVM stack trace, and std::terminate()
    // is routed through the internal-error banner (see Core/InternalError.hpp).
    InstallCrashHandlers(argc, argv);

    std::shared_ptr<Context> context = std::make_shared<Context>();

    try
    {
        CompilePipeline compilePipeline{context, argc, argv};
        compilePipeline.run();
    }
    catch (const ArgParseError &e)
    {
        // A malformed command line is a USER error: plain message, exit 2.
        std::cerr << "lisc: " << e.what() << "\n";
        return 2;
    }
    catch (const std::exception &e)
    {
        ReportInternalCompilerError(e.what());
        return kInternalErrorExitCode;
    }
    catch (...)
    {
        ReportInternalCompilerError("unknown exception (not derived from std::exception)");
        return kInternalErrorExitCode;
    }

    return 0;
}