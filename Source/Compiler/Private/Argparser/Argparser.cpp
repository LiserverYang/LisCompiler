/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 参数解析器实现
 */

#include "ArgParser/Argparser.hpp"

#include <cstring>

std::string Argparser::normalizeKey(const std::string &name)
{
    size_t start = 0;

    while (start < name.size() && name[start] == '-')
    {
        start++;
    }

    std::string result = name.substr(start);

    for (char &c : result)
    {
        if (c == '-') c = '_';
    }

    return result;
}

void Argparser::run()
{
    if (enableHelpRule && argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        // TODO

        // if enable help, application should shut down
        exit(0);
    }

    while (pos < argc)
    {
        std::string arg = argv[pos];

        if (arg.size() > 1 && arg[0] == '-')
        {
            bool found = false;

            for (auto rule : rules)
            {
                for (auto name : rule.name)
                {
                    if (name == arg)
                    {
                        found = true;

                        pos += rule.behavior(this, normalizeKey(rule.name[0]));

                        break;
                    }
                }

                if (found) break;
            }

            if (!found)
            {
                throw std::runtime_error("Unknown option: " + arg);
            }
        }
        // 处理位置参数
        else
        {
            if (posIndex < rules.size())
            {
                auto &rule = rules[posIndex];
                if (!rule.name.empty() && rule.name[0][0] != '-')
                {
                    currentRuleKey = normalizeKey(rule.name[0]);
                    posIndex++;
                    pos++;
                    args->setArg(rule.name[0], arg);
                }
                else
                {
                    throw std::runtime_error("Unexpected positional argument: " + arg);
                }
            }
            else
            {
                throw std::runtime_error("Too many positional arguments: " + arg);
            }
        }
    }

    if (posIndex < rules.size() && !rules[posIndex].name.empty() && rules[posIndex].name[0][0] != '-')
    {
        throw std::runtime_error("Excepted positional argument: " + rules[posIndex].name[0]);
    }
}