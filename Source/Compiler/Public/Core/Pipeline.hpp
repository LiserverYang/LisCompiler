/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Pass.hpp"
#include "Context.hpp"

#include <memory>

/**
 * Pipeline 描述了不同 Pass 之间执行的先后关系，也就是整个流程
 * 常见的 Pipeline 有：CompilePipeline（完整的编译流程） ParseMoudulePipeline（解析模块流程）
 */
class Pipeline
{
protected:
    std::shared_ptr<Context> context;
    std::vector<std::unique_ptr<Pass>> passes;

public:
    Pipeline() = default;
    ~Pipeline() = default;

    void run()
    {
        for (auto &pass : passes)
        {
            pass->run();
        }
    }
};