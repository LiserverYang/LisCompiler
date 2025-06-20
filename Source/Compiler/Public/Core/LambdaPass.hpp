/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#pragma once

#include "Core/Pass.hpp"

#include <functional>

class LambdaPass : public Pass
{
private:
    std::function<void()> lambda;

public:
    LambdaPass(std::shared_ptr<Context> ctx, std::function<void()> func)
        : Pass(ctx), lambda(std::move(func)) {}

    explicit LambdaPass(std::function<void()> func)
        : lambda(std::move(func)) {}

    template <typename Func>
    LambdaPass(std::shared_ptr<Context> ctx, Func &&func)
        : Pass(ctx),
          lambda([ctx, func = std::forward<Func>(func)]
              { func(ctx); })
    {
    }

    template <typename Func>
    LambdaPass(Func &&func)
        : lambda([this, func = std::forward<Func>(func)]
              { func(this); })
    {
    }

    void run() override
    {
        if (lambda)
        {
            lambda();
        }
    }
};
