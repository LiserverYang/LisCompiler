/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Core/Context.hpp"

#include <memory>

/**
 * Pass 代表了编译过程的一个步骤，通过 Pass 可以实现灵活的编译流程控制
 * 常见的 Pass： Lexer（词法分析器） Parser（语法分析器） Analyzer（语义分析器）
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