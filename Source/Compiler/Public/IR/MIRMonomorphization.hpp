/**
 * Copyright 2026, LiserverYang. All rights reserved.
 */

#pragma once

#include "Core/Pass.hpp"

#include <queue>
#include <unordered_set>

class MIRMonomorphization : public Pass
{
private:
    struct MonoInfo
    {
        std::string genericFuncName;                    // 原始泛型函数名
        std::vector<std::shared_ptr<Type>> genericArgs; // 具体类型参数
        std::string monoFuncName;                       // 生成的单态化函数名
        MIRFunction *genericFunc;                       // 原始泛型函数指针
    };

    std::queue<std::unique_ptr<MonoInfo>> monoInfos;
    std::unordered_set<std::string> generatedMonoFuncs;
    std::unordered_map<std::string, MIRFunction *> functionMap;

public:
    MIRMonomorphization() = default;
    MIRMonomorphization(std::shared_ptr<Context> cnt)
    {
        context = cnt;
    }

    ~MIRMonomorphization() {}

    virtual void run() override;
    std::shared_ptr<Type> replaceGenericType(std::shared_ptr<Type> origin, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable);
    MIRFunction monomorphizeFunction(const MonoInfo &info, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable);
    void process();
    void collection();
    void buildFunctionMap();
    void collection(MIRFunction *func);

    /// Queue monomorphization of the `drop` method for every generic struct
    /// instantiation that implements the Drop trait. The drop glue is generated
    /// at the LLVM level (after mono), so nothing at a call site ever triggers
    /// it — this is the only place the drop method gets monomorphized.
    void seedDropMethods();

    static std::string makeMonoFuncName(const std::string &baseName, const std::vector<std::shared_ptr<Type>> &args);
    void rewriteFunctionBody(MIRBody &body, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable);
    void rewriteStatement(MIRStatement &stmt, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable);
    void rewriteRValue(MIRRValue &rvalue, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable);
    void rewriteOperand(MIROperand &operand, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable);
};