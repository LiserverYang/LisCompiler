/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Core/Pipeline.hpp"

#include <string>

/**
 * CompilePipeline is a pipline that describe the whole compile procedure
 */
class CompilePipeline : public Pipeline
{
public:
    CompilePipeline(std::shared_ptr<Context> cnt, int argc, const char **argv);
    ~CompilePipeline() = default;
};