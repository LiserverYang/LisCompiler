/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Context.hpp"
#include "Pass.hpp"

#include <memory>

/**
 * Pipeline is the order of passes, that is the whole compile process
 * Common Pipeline: CompilePipeline and ParseMoudulePipeline
 */
class Pipeline
{
protected:
    std::shared_ptr<Context> context;
    std::vector<std::unique_ptr<Pass>> passes;

public:
    Pipeline() = default;
    virtual ~Pipeline() = default;

    void run()
    {
        for (auto &pass : passes)
        {
            pass->run();
        }
    }
};