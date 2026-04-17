/**
 * Copyright 2026, LiserverYang. All rights reserved.
 */

#include "IR/MIRMonomorphization.hpp"
#include "Analysiser/SymbolTable.hpp"
#include "IR/MIRDeepCopy.hpp"
#include "IR/MIRPrinter.hpp"

std::shared_ptr<Type> MIRMonomorphization::replaceGenericType(std::shared_ptr<Type> origin, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable)
{
    if (!origin)
        return nullptr;

    switch (origin->getKind())
    {
    case Type::Kind::GenericParam:
    {
        auto genericTy = std::static_pointer_cast<GenericParamType>(origin);
        const std::string &name = genericTy->getParamName();
        auto it = replaceTable.find(name);

        if (it == replaceTable.end())
        {
            throw std::runtime_error("Generic parameter '" + name + "' not found in replace table");
        }
        return it->second;
    }
    case Type::Kind::Custom:
    {
        auto ct = std::static_pointer_cast<CustomType>(origin);
        if (ct->getGenericArgs().empty())
        {
            // Non-generic or already-resolved nominal type — nothing to do.
            return origin;
        }
        // Substitute each arg, then re-instantiate via the type context (cached).
        std::vector<std::shared_ptr<Type>> newArgs;
        for (auto &a : ct->getGenericArgs())
            newArgs.push_back(replaceGenericType(a, replaceTable));

        auto orig = ct->genericOrigin ? ct->genericOrigin : ct;
        return context->typeContext->instantiateCustom(orig, std::move(newArgs));

        // TODO: when methods on generic structs are lowered to MIR-generic functions
        // (with the struct's gParams copied into MIRFunction::genericParams), the
        // existing function-monomorphization path will pick them up automatically;
        // no extra work needed here.
    }
    case Type::Kind::Trait:
        return origin;
    case Type::Kind::Primitive:
        return origin;
    case Type::Kind::Reference:
    {
        auto refTy = std::static_pointer_cast<ReferenceType>(origin);
        // replace base type
        auto newBase = replaceGenericType(refTy->getBaseType(), replaceTable);
        return context->typeContext->getReference(newBase, refTy->isMutableRef());
    }
    case Type::Kind::Function:
    {
        auto funcTy = std::static_pointer_cast<FunctionType>(origin);
        std::vector<std::shared_ptr<Type>> params;

        for (const auto &param : funcTy->getParams())
        {
            params.push_back(replaceGenericType(param, replaceTable));
        }

        auto newRet = replaceGenericType(funcTy->getReturnType(), replaceTable);

        return context->typeContext->getFunction(std::move(params), newRet);
    }
    default:
        throw std::runtime_error("unhandled type '" + origin->toString() + "'");
    }
}

std::string MIRMonomorphization::makeMonoFuncName(const std::string &baseName, const std::vector<std::shared_ptr<Type>> &args)
{
    std::stringstream ss;
    ss << baseName << "_Mono_";
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0) ss << "_";
        ss << args[i]->toString();
    }
    return ss.str();
}

void MIRMonomorphization::run()
{
    buildFunctionMap();
    collection();
    process();

    if (context->args->getArg("print_mir").compare("true") == 0)
    {
        printMIRProgram(*context->mirProgram.get(), std::cout);
    }
}

void MIRMonomorphization::collection()
{
    for (auto &function : context->mirProgram->functions)
    {
        if (function->genericParams.empty())
        {
            collection(function.get());
        }
    }
}

void MIRMonomorphization::collection(MIRFunction *func)
{
    if (!func) return;

    std::unordered_map<std::string, std::shared_ptr<Type>> funcReplaceTable;

    for (auto &block : func->body.blocks)
    {
        for (auto &stmt : block.stmts)
        {
            if (auto *callStmt = std::get_if<MIRStmtCall>(&stmt))
            {
                if (!callStmt->genericParams.empty())
                {
                    MIRFunction *genericFunc = functionMap[callStmt->funcName];

                    if (!genericFunc)
                    {
                        throw std::runtime_error("can not find generic function: " + callStmt->funcName);
                    }

                    MonoInfo info;
                    info.genericFuncName = callStmt->funcName;
                    info.genericArgs = callStmt->genericParams;
                    info.monoFuncName = makeMonoFuncName(callStmt->funcName, callStmt->genericParams);
                    info.genericFunc = genericFunc;

                    if (generatedMonoFuncs.find(info.monoFuncName) == generatedMonoFuncs.end())
                    {
                        monoInfos.push(std::move(std::make_unique<MonoInfo>(info)));
                        generatedMonoFuncs.insert(info.monoFuncName);
                    }

                    std::unordered_map<std::string, std::shared_ptr<Type>> localReplaceTable;
                    for (size_t i = 0; i < genericFunc->genericParams.size(); ++i)
                    {
                        localReplaceTable[genericFunc->genericParams[i]] = callStmt->genericParams[i];
                        funcReplaceTable[genericFunc->genericParams[i]] = callStmt->genericParams[i];
                    }

                    // Rewrite the callee operand and destination *before* clearing genericParams
                    rewriteOperand(callStmt->callee, localReplaceTable);

                    if (callStmt->dest.has_value())
                    {
                        callStmt->dest->type = replaceGenericType(callStmt->dest->type, localReplaceTable);
                    }

                    callStmt->funcName = info.monoFuncName;
                    callStmt->genericParams.clear();
                }
            }
        }
    }

    if (!funcReplaceTable.empty())
    {
        for (auto &local : func->body.locals)
            local.type = replaceGenericType(local.type, funcReplaceTable);

        for (auto &block : func->body.blocks)
            for (auto &stmt : block.stmts)
                rewriteStatement(stmt, funcReplaceTable);
    }
}

void MIRMonomorphization::process()
{
    while (!monoInfos.empty())
    {
        auto infoPtr = std::move(monoInfos.front());
        monoInfos.pop();
        const MonoInfo &info = *infoPtr;

        assert(info.genericFunc && "Generic function is null");
        assert(info.genericFunc->genericParams.size() == info.genericArgs.size() && "Generic params/args count mismatch");

        std::unordered_map<std::string, std::shared_ptr<Type>> replaceMap;
        for (size_t i = 0; i < info.genericFunc->genericParams.size(); ++i)
        {
            replaceMap[info.genericFunc->genericParams[i]] = info.genericArgs[i];
        }

        MIRFunction newFunc = monomorphizeFunction(info, replaceMap);

        context->mirProgram->functions.push_back(std::make_shared<MIRFunction>(newFunc));
        MIRFunction *insertedFunc = context->mirProgram->functions.back().get();
        functionMap[insertedFunc->name] = insertedFunc;

        auto &symTable = SymbolTable::getInstance();
        if (!symTable.lookupSymbol(insertedFunc->name))
        {
            Symbol newSym;
            newSym.kind = SymbolKind::Function;
            newSym.name = insertedFunc->name;
            newSym.type = replaceGenericType(symTable.lookupSymbol(info.genericFuncName)->type, replaceMap);
            symTable.insertSymbol(insertedFunc->name, std::make_unique<Symbol>(newSym));
        }

        collection(insertedFunc);
    }
}

void MIRMonomorphization::rewriteRValue(MIRRValue &rvalue, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable)
{
    std::visit([&](auto &&arg)
        {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, MIRRValueUse>)
        {
            rewriteOperand(arg.operand, replaceTable);
        }
        else if constexpr (std::is_same_v<T, MIRRValueBinaryOp> || std::is_same_v<T, MIRRValueUnaryOp>)
        {
            arg.type = replaceGenericType(arg.type, replaceTable);
        }
        else if constexpr (std::is_same_v<T, MIRRValueCast>)
        {
            arg.targetType = replaceGenericType(arg.targetType, replaceTable);
        }
        else if constexpr (std::is_same_v<T, MIRRValueRef>)
        {
            arg.place.type = replaceGenericType(arg.place.type, replaceTable);
        }
        else if constexpr (std::is_same_v<T, MIRRValueAddrOf>)
        {
            arg.place.type = replaceGenericType(arg.place.type, replaceTable);
        }
        else if constexpr (std::is_same_v<T, MIRRValueStructInit>)
        {
            arg.type = replaceGenericType(arg.type, replaceTable);

            if (auto ct = std::dynamic_pointer_cast<CustomType>(arg.type))
                arg.structName = ct->getName();
        } },
        rvalue);
}

void MIRMonomorphization::rewriteStatement(MIRStatement &stmt, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable)
{
    std::visit([&](auto &&arg)
        {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, MIRStmtAssign>)
        {
            arg.lhs.type = replaceGenericType(arg.lhs.type, replaceTable);
            rewriteRValue(arg.rhs, replaceTable);
        }

        else if constexpr (std::is_same_v<T, MIRStmtCall>)
        {
            for (auto &argOperand : arg.args)
            {
                rewriteOperand(argOperand, replaceTable);
            }

            for (auto &argTy : arg.genericParams)
            {
                argTy = replaceGenericType(argTy, replaceTable);
            }

            if (arg.dest.has_value())
            {
                arg.dest->type = replaceGenericType(arg.dest->type, replaceTable);
            }

            if (!arg.genericParams.empty())
            {
                std::string monoName = makeMonoFuncName(arg.funcName, arg.genericParams);
                arg.funcName = monoName;
                rewriteOperand(arg.callee, replaceTable);
                arg.genericParams.clear();
            }
        }

        else if constexpr (std::is_same_v<T, MIRStmtDrop>)
        {
            arg.place.type = replaceGenericType(arg.place.type, replaceTable);
        } },
        stmt);
}

void MIRMonomorphization::rewriteFunctionBody(MIRBody &body, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable)
{
    for (auto &local : body.locals)
    {
        local.type = replaceGenericType(local.type, replaceTable);
    }

    body.returnType = replaceGenericType(body.returnType, replaceTable);

    for (auto &block : body.blocks)
    {
        for (auto &stmt : block.stmts)
        {
            rewriteStatement(stmt, replaceTable);
        }
    }
}

MIRFunction MIRMonomorphization::monomorphizeFunction(const MonoInfo &info, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable)
{
    MIRFunction monoFunc = mir::deep_copy::copy(*info.genericFunc);

    monoFunc.name = info.monoFuncName;
    monoFunc.genericParams.clear();
    monoFunc.body.funcName = info.monoFuncName;

    rewriteFunctionBody(monoFunc.body, replaceTable);

    return monoFunc;
}

void MIRMonomorphization::buildFunctionMap()
{
    functionMap.clear();
    for (auto &f : context->mirProgram->functions)
    {
        functionMap[f->name] = f.get();
    }
}

void MIRMonomorphization::rewriteOperand(MIROperand &operand,
    std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable)
{
    std::visit([&](auto &&arg)
        {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, MIRConst>)
        {
            arg.type = replaceGenericType(arg.type, replaceTable);
        }
        else if constexpr (std::is_same_v<T, MIRCopy> || std::is_same_v<T, MIRMove>)
        {
            arg.place.type = replaceGenericType(arg.place.type, replaceTable);
        } },
        operand);
}