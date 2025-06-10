/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Core/Pipeline.hpp"

#include <string>

/**
 * CompilePipeline 是描述整个编译流程的 pipeline
 */
class CompilePipeline : public Pipeline
{
public:
    CompilePipeline(std::shared_ptr<Context> cnt);
    virtual ~CompilePipeline() = default;
};