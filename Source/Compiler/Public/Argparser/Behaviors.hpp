/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The definations of common argument behaviors
 */

#pragma once

#include "Argparser/ArgParseRule.hpp"
#include "Argparser/Argparser.hpp"

int setAsTrue(Argparser *parser, std::string argName);
int setAsFalse(Argparser *parser, std::string argName);
int setAsValue(Argparser *parser, std::string argName);