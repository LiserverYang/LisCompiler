/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * The command line is read at CONSTRUCTION time by several pass objects, so the
 * pipeline has to parse argv before it builds any of them. These tests pin that
 * ordering: they assert what the CONTEXT looks like right after the
 * CompilePipeline constructor returns.
 *
 * The bug they exist for: Args::getArg returns the default that registRule()
 * stored until the Argparser pass runs, and every pass object is constructed
 * before any pass runs. So -o was read as "2" forever (-o0/-o1/-o3 emitted
 * byte-identical -O2 objects) and every -I directory was silently dropped.
 */

#include "Core/CompilePipeline.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

extern char *_pgmptr; // full path of the running executable (MinGW CRT)

namespace
{
// The pipeline looks for <argv[0]>/lstdlib next to the compiler. The test
// binary lives in Build/Intermediate, so lisc.exe is its sibling under Binaries.
std::string compilerPathForThisTest()
{
    if (_pgmptr)
    {
        fs::path candidate =
            fs::path(_pgmptr).parent_path().parent_path() / "Binaries" / "lisc.exe";
        return candidate.string();
    }
    return "lisc.exe";
}

bool searchPathContains(const std::shared_ptr<Context> &ctx, const std::string &dir)
{
    for (const auto &p : ctx->searchPaths)
        if (p == dir) return true;
    return false;
}
} // namespace

TEST(PipelineTest, CommandLineIsParsedBeforeAnyPassIsBuilt)
{
    const std::string self = compilerPathForThisTest();
    std::vector<const char *> argv{
        self.c_str(), "prog.lis", "-o", "0", "-I", "inc_a;inc_b", "--allow-ffi"};

    auto context = std::make_shared<Context>();
    CompilePipeline pipeline(context, static_cast<int>(argv.size()), argv.data());

    // Before the fix both of these were still the registered defaults.
    EXPECT_EQ(context->args->getArg("o"), "0");
    EXPECT_EQ(context->args->getArg("allow_ffi"), "true");

    // -I carries ';'-separated dirs; every one of them must be searched.
    EXPECT_TRUE(searchPathContains(context, "inc_a"));
    EXPECT_TRUE(searchPathContains(context, "inc_b"));

    // The stdlib directory is still derived from argv[0].
    EXPECT_TRUE(searchPathContains(
        context, (fs::path(self).parent_path() / "lstdlib").string()));
}

TEST(PipelineTest, OptLevelDefaultsToTwo)
{
    const std::string self = compilerPathForThisTest();
    std::vector<const char *> argv{self.c_str(), "prog.lis"};

    auto context = std::make_shared<Context>();
    CompilePipeline pipeline(context, static_cast<int>(argv.size()), argv.data());

    EXPECT_EQ(context->args->getArg("o"), "2");
    EXPECT_EQ(context->args->getArg("I"), "");
}
