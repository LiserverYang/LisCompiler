#include "Argparser/Argparser.hpp"
#include "Argparser/ArgParseRule.hpp"
#include "Argparser/Args.hpp"
#include "Argparser/Behaviors.hpp"
#include "Core/Pass.hpp"

#include <gtest/gtest.h>

using namespace testing;

// The test class for argparser
class ArgparserTest : public ::testing::Test
{
protected:
    std::shared_ptr<Context> context;
    std::shared_ptr<Args> args;

    void SetUp() override
    {
        context = std::make_shared<Context>();
        args = std::make_shared<Args>();
    }

    // create the parser with arguments
    std::unique_ptr<Argparser> createParser(
        std::vector<const char *> cmdArgs,
        bool enableHelp = true,
        std::vector<ArgParseRule> rules = {})
    {
        ArgparserCreateInfo info;
        info.argc = static_cast<int>(cmdArgs.size());
        info.argv = cmdArgs.data();
        info.enableHelpRule = enableHelp;
        info.args = args;

        auto parser = std::make_unique<Argparser>(context, info);
        for (const auto &rule : rules)
        {
            parser->registRule(rule);
        }
        return parser;
    }

    // common rules
    ArgParseRule createBoolRule(
        const std::string &name,
        ArgParseRule::ArgBehavior behavior,
        const std::string &defaultValue = "false")
    {
        return {
            .name = {name},
            .behavior = behavior,
            .defaultValue = defaultValue,
            .description = ""};
    }

    ArgParseRule createValueRule(
        const std::string &name,
        const std::string &defaultValue = "")
    {
        return {
            .name = {name},
            .behavior = setAsValue,
            .defaultValue = defaultValue,
            .description = ""};
    }

    ArgParseRule createPositionalRule(
        const std::string &name,
        const std::string &defaultValue = "")
    {
        return {
            .name = {name},
            .behavior = [](Argparser *, std::string)
            { return 1; },
            .defaultValue = defaultValue,
            .description = ""};
    }
};

// Test case
TEST_F(ArgparserTest, HandlesHelpOption)
{
    const char *argv[] = {"app", "--help"};
    auto parser = createParser({argv, argv + 2});

    EXPECT_EXIT(parser->run(), ExitedWithCode(0), "");
}

TEST_F(ArgparserTest, HandlesDisabledHelp)
{
    const char *argv[] = {"app", "--help"};
    auto parser = createParser({argv, argv + 2}, false);

    EXPECT_THROW(parser->run(), std::runtime_error);
}

TEST_F(ArgparserTest, HandlesSetAsTrue)
{
    ArgParseRule verboseRule = createBoolRule("--verbose", setAsTrue);

    const char *argv[] = {"app", "--verbose"};
    auto parser = createParser({argv, argv + 2}, true, {verboseRule});
    parser->run();

    EXPECT_EQ(args->getArg("verbose"), "true");
}

TEST_F(ArgparserTest, HandlesSetAsFalse)
{
    ArgParseRule quietRule = createBoolRule("--quiet", setAsFalse, "true");

    const char *argv[] = {"app", "--quiet"};
    auto parser = createParser({argv, argv + 2}, true, {quietRule});
    parser->run();

    EXPECT_EQ(args->getArg("quiet"), "false");
}

TEST_F(ArgparserTest, HandlesSetAsValue)
{
    ArgParseRule fileRule = createValueRule("--file");

    const char *argv[] = {"app", "--file", "config.yaml"};
    auto parser = createParser({argv, argv + 3}, true, {fileRule});
    parser->run();

    EXPECT_EQ(args->getArg("file"), "config.yaml");
}

TEST_F(ArgparserTest, NormalizesArgumentKeys)
{
    ArgParseRule rule = createBoolRule("--output-file", setAsTrue);

    const char *argv[] = {"app", "--output-file"};
    auto parser = createParser({argv, argv + 2}, true, {rule});
    parser->run();

    // should be converted to output_file
    EXPECT_EQ(args->getArg("output_file"), "true");
}

TEST_F(ArgparserTest, ThrowsOnUnknownOption)
{
    const char *argv[] = {"app", "--unknown"};
    auto parser = createParser({argv, argv + 2});

    EXPECT_THROW(parser->run(), std::runtime_error);
}

TEST_F(ArgparserTest, ThrowsOnMissingPositionalArg)
{
    ArgParseRule requiredRule = createPositionalRule("input");

    const char *argv[] = {"app"};
    auto parser = createParser({argv, argv + 1}, true, {requiredRule});

    EXPECT_THROW(parser->run(), std::runtime_error);
}

TEST_F(ArgparserTest, ThrowsOnExtraPositionalArgs)
{
    ArgParseRule rule = createPositionalRule("input");

    const char *argv[] = {"app", "file1", "file2"};
    auto parser = createParser({argv, argv + 3}, true, {rule});

    EXPECT_THROW(parser->run(), std::runtime_error);
}

TEST_F(ArgparserTest, HandlesMixedArguments)
{
    ArgParseRule verboseRule = createBoolRule("--verbose", setAsTrue);
    ArgParseRule fileRule = createValueRule("--config");
    ArgParseRule inputRule = createPositionalRule("input");

    const char *argv[] = {"app", "--verbose", "data.bin", "--config", "settings.cfg"};
    auto parser = createParser({argv, argv + 5}, true, {inputRule, verboseRule, fileRule});
    parser->run();

    EXPECT_EQ(args->getArg("verbose"), "true");
    EXPECT_EQ(args->getArg("config"), "settings.cfg");
    EXPECT_EQ(args->getArg("input"), "data.bin");
}

TEST_F(ArgparserTest, HandlesMultipleNamesForRule)
{
    ArgParseRule helpRule{
        .name = {"--help", "-h", "-help"},
        .behavior = [](Argparser *p, std::string)
        {
            p->args->setArg("help", "true");
            return 1;
        }};

    const char *argv[] = {"app", "-h"};
    auto parser = createParser({argv, argv + 2}, false, {helpRule});
    parser->run();

    EXPECT_EQ(args->getArg("help"), "true");
}