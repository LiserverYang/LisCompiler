/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The implementation of argparser
 */

#pragma once

#include "Argparser/ArgParseRule.hpp"
#include "Argparser/Args.hpp"
#include "Core/Pass.hpp"

#include <cctype>
#include <memory>
#include <stdexcept>

/**
 * The create info of argparser
 */
struct ArgparserCreateInfo
{
    int argc;
    const char **argv;

    bool enableHelpRule = true;

    std::shared_ptr<Args> args;
};

/**
 * `Argparser` is the core of argparse module
 * By given the `ArgParseRule`, the Argparser parse the arguments in argv with rules
 * ArgParseRule defined when met a argument, how parser works
 * The result will store in Args, a `std::unordered_map`
 *
 * The pipeline create Argparser by Context and CreateInfo
 */
class Argparser : public Pass
{
public:
    Argparser() = default;
    Argparser(std::shared_ptr<Context> cnt, ArgparserCreateInfo createInfo)
        : argc(createInfo.argc), argv(createInfo.argv), enableHelpRule(createInfo.enableHelpRule), args(createInfo.args)
    {
        context = cnt;
    }

    ~Argparser() {}

    /**
     * Start to parse all arguments
     */
    virtual void run() override;

public:
    int pos = 1;
    int posIndex = 0;
    int argc;
    const char **argv;
    std::string currentRuleKey;
    std::shared_ptr<Args> args;

    void registRule(ArgParseRule rule)
    {
        rules.push_back(rule);
        args->setArg(normalizeKey(rule.name[0]), rule.defaultValue);
    }

    void setArgValue(const std::string &key, const std::string &value)
    {
        (*args)[key] = value;
    }

private:
    bool enableHelpRule = true;

    std::vector<ArgParseRule> rules;

    std::string normalizeKey(const std::string &name);
};