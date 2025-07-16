/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Core/Context.hpp"

#include <memory>

/**
 * Pass is a step of compile process, with the pass, we can control the whole compile process easily
 * Common Pass： Lexer, Parser and Analyzer
 */
class Pass
{
protected:
    friend class Pipeline;

    std::shared_ptr<Context> context;

public:
    Pass() = default;
    Pass(std::shared_ptr<Context> cnt) : context(cnt) {}

    virtual void run() = 0;
};