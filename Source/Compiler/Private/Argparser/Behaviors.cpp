/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 定义了常见的参数行为
 */

#include "Argparser/Behaviors.hpp"

int setAsTrue(Argparser *parser, std::string argName)
{
    parser->args->setArg(argName, "true");
    return 1;
}

int setAsFalse(Argparser *parser, std::string argName)
{
    parser->args->setArg(argName, "false");
    return 1;
}

int setAsValue(Argparser *parser, std::string argName)
{
    if (parser->pos >= parser->argc - 1)
    {
        throw std::runtime_error("Except a value for argument: " + argName);
    }

    parser->args->setArg(argName, parser->argv[parser->pos + 1]);

    return 2;
}