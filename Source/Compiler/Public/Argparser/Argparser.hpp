/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 参数解析器实现
 */

#pragma once

#include "Argparser/ArgParseRule.hpp"
#include "Argparser/Args.hpp"
#include "Core/Pass.hpp"

#include <cctype>
#include <memory>
#include <stdexcept>

/**
 * 定义了所有创建 Argparser 用到的参数
 */
struct ArgparserCreateInfo
{
    int argc;
    const char **argv;

    bool enableHelpRule = true;

    std::shared_ptr<Args> args;
};

/**
 * Argparse 通过 argc, argv 解析程序的参数
 * Pipeline 通过 createInfo 来创建 Argparser
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
     * 开始进行参数解析
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

    // 设置参数值
    void setArgValue(const std::string &key, const std::string &value)
    {
        (*args)[key] = value;
    }

private:
    bool enableHelpRule = true;

    std::vector<ArgParseRule> rules;

    std::string normalizeKey(const std::string &name);
};