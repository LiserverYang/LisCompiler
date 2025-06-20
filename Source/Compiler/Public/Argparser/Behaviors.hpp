/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 定义了常见的参数行为
 */

#pragma once

#include "Argparser/ArgParseRule.hpp"
#include "Argparser/Argparser.hpp"

int setAsTrue(Argparser *parser, std::string argName);
int setAsFalse(Argparser *parser, std::string argName);
int setAsValue(Argparser *parser, std::string argName);