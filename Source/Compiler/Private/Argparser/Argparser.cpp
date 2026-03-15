/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The implementation of argparser
 */

#include "Argparser/Argparser.hpp"

#include <cstring>

std::string Argparser::normalizeKey(const std::string &name)
{
    // here is to skip "-"" or "--"" and we save the index of the first letter is not '-'
    // for example, the start of "--help" is 2
    size_t start = 0;

    while (start < name.size() && name[start] == '-')
    {
        start++;
    }

    // here we get the true argument name
    // the result of "--help" is "help"
    std::string result = name.substr(start);

    // and here we turn every '-' into '_'
    // likes "--a-b-c" will be "a_b_c"
    for (char &c : result)
    {
        if (c == '-') c = '_';
    }

    return result;
}

void Argparser::run()
{
    // show help informations when need
    if (enableHelpRule && argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        printHelpInformation();

        // if enable help, application should exit with code 0
        exit(0);
    }

    // loop all arguments
    // attention: there's two type of arguments
    // one is positional argument and the other is option argument
    // we should distinguish what kind it is
    while (pos < argc)
    {
        std::string arg = argv[pos];

        // if the first letter is '-', it is option argument
        // we will search all rules for the same argument name as the argument value
        // if we can't find it, throw error
        if (arg.size() > 1 && arg[0] == '-')
        {
            // this variable shows if we found the related rule
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

            // if not found, we should throw error
            // beacuse it is a unkown argument
            if (!found)
            {
                throw std::runtime_error("Unknown option: " + arg);
            }
        }
        // otherwise, it is the positional argument
        else
        {
            // to avoid outing of bounds
            if (posIndex < rules.size())
            {
                auto &rule = rules[posIndex];

                // we should also judge if the rule is positional argument
                if (!rule.name.empty() && rule.name[0][0] != '-')
                {
                    // move to next positional index
                    posIndex++;
                    // move to next argument
                    pos++;
                    // set the argument value that we get
                    args->setArg(rule.name[0], arg);
                }
                else
                {
                    throw std::runtime_error("Unexpected positional argument: " + arg);
                }
            }
            // it must be a illegal positional argument
            else
            {
                throw std::runtime_error("Too many positional arguments: " + arg);
            }
        }
    }

    // here we process the ungiven positional argument
    if (posIndex < rules.size() && !rules[posIndex].name.empty() && rules[posIndex].name[0][0] != '-')
    {
        throw std::runtime_error("Excepted positional argument: " + rules[posIndex].name[0]);
    }
}

void Argparser::printHelpInformation()
{
    // the usage
    printf("%s\n", usageStr.c_str());

    // options
    printf("Options:\n");

    for (auto &rule : rules)
    {
        if (rule.name[0][0] != '-') continue;

        printf("  %s\t %s\n", rule.name[0].c_str(), rule.description.c_str());
    }
}