/**
 * Copyright 2026, LiserverYang. All rights reserved.
 */

#include "IR/MIRMonomorphization.hpp"
#include "Analysiser/SymbolTable.hpp"
#include "IR/MIRDeepCopy.hpp"
#include "IR/MIRPrinter.hpp"

std::shared_ptr<Type> MIRMonomorphization::replaceGenericType(std::shared_ptr<Type> origin, std::unordered_map<std::string, std::shared_ptr<Type>> &replaceTable)
{
    // Delegates to the single TypeContext::substitute. Mono passes strict=true:
    // after substitution every type must be concrete, and a leaked GenericParam
    // would produce a degenerate toString() that collides two instantiations in
    // makeMonoFuncName — so a missing param fails loudly.
    return context->typeContext->substitute(std::move(origin), replaceTable, /*strict=*/true);
}

std::string MIRMonomorphization::makeMonoFuncName(const std::string &baseName, const std::vector<std::shared_ptr<Type>> &args)
{
    // Guard: a raw generic param (toString() is degenerate, e.g. "T") would
    // collide two different instantiations into one mono name. This should be
    // unreachable today (args are substituted before naming), but fail loudly
    // rather than silently reusing the wrong instantiation.
    for (auto &arg : args)
    {
        if (arg && arg->getKind() == Type::Kind::GenericParam)
            throw std::runtime_error("makeMonoFuncName: generic param not substituted: " + arg->toString());
    }

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
    // The drop glue (LLVM level) has no call site to trigger mono of a generic
    // Drop method, so seed it here for every Drop-implementing instantiation.
    seedDropMethods();
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

                    // Rewrite THIS call's carriers with its OWN table. A whole-body
                    // rewrite with one aggregate table would corrupt an earlier
                    // call when two generic callees share a generic-param name but
                    // map it to different concrete args (Item A).
                    std::unordered_map<std::string, std::shared_ptr<Type>> localReplaceTable;
                    for (size_t i = 0; i < genericFunc->genericParams.size(); ++i)
                        localReplaceTable[genericFunc->genericParams[i]] = callStmt->genericParams[i];

                    // The call's own operands / dest.
                    rewriteOperand(callStmt->callee, localReplaceTable);
                    for (auto &arg : callStmt->args)
                        rewriteOperand(arg, localReplaceTable);
                    if (callStmt->dest.has_value())
                        callStmt->dest->type = replaceGenericType(callStmt->dest->type, localReplaceTable);

                    // The carrier locals that back those operands (callee name-ref
                    // temp, self-ref arg temps, struct-typed dests) and the assigns
                    // that define them.
                    std::unordered_set<size_t> carriers;
                    auto collectRoot = [&carriers](const MIROperand &op)
                    {
                        if (auto *c = std::get_if<MIRCopy>(&op))
                            carriers.insert(c->place.index);
                        else if (auto *m = std::get_if<MIRMove>(&op))
                            carriers.insert(m->place.index);
                    };
                    collectRoot(callStmt->callee);
                    for (const auto &arg : callStmt->args)
                        collectRoot(arg);
                    if (callStmt->dest.has_value())
                        carriers.insert(callStmt->dest->index);

                    for (size_t idx : carriers)
                        if (idx < func->body.locals.size())
                            func->body.locals[idx].type = replaceGenericType(func->body.locals[idx].type, localReplaceTable);

                    for (auto &blk : func->body.blocks)
                        for (auto &s : blk.stmts)
                            if (auto *as = std::get_if<MIRStmtAssign>(&s))
                                if (carriers.count(as->lhs.index))
                                    rewriteRValue(as->rhs, localReplaceTable);

                    callStmt->funcName = info.monoFuncName;
                    callStmt->genericParams.clear();
                }
            }
        }
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

void MIRMonomorphization::seedDropMethods()
{
    for (const auto &[name, inst] : context->typeContext->getInstantiatedCustoms())
    {
        if (!inst->genericOrigin) continue; // only generic instantiations
        if (!inst->implementsTrait("Drop")) continue;

        // The drop method lives on the origin under "Struct::drop"; it was made
        // generic (carries the struct's gParams) by MIRBuilder so mono can
        // substitute this instance's args into it.
        std::string dropBase = inst->getOriginName() + "::drop";
        auto funcIt = functionMap.find(dropBase);
        if (funcIt == functionMap.end()) continue; // no drop impl lowered
        MIRFunction *dropFn = funcIt->second;

        const auto &args = inst->getGenericArgs();
        // Arity must match the method's (struct) generic params; a Drop impl
        // with extra generics falls back to field-recursion glue.
        if (dropFn->genericParams.size() != args.size()) continue;

        std::string monoName = makeMonoFuncName(dropBase, args);
        if (generatedMonoFuncs.count(monoName)) continue;

        MonoInfo info;
        info.genericFuncName = dropBase;
        info.genericArgs = args;
        info.monoFuncName = monoName;
        info.genericFunc = dropFn;
        monoInfos.push(std::make_unique<MonoInfo>(info));
        generatedMonoFuncs.insert(monoName);
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
    // A generic-param operator call (`<T>::add`) whose concrete type is a
    // primitive must become a direct binary op (primitives have no `add`
    // method). Built here and assigned after the visit (can't reassign the
    // variant while `arg` is a reference into it).
    std::optional<MIRStatement> replacement;

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

            // A generic-param OPERATOR call (`<T>::add` from `a + b` on
            // `T: Add`) resolves by concrete type: a struct retargets to its
            // method; a primitive has no method and falls back to a binary op.
            if (!arg.funcName.empty() && arg.funcName[0] == '<' && arg.genericOpFallback.has_value())
            {
                size_t sep = arg.funcName.find("::");
                if (sep != std::string::npos)
                {
                    std::string recv = arg.funcName.substr(1, sep - 2); // strip < >
                    auto it = replaceTable.find(recv);
                    if (it != replaceTable.end()
                        && !std::dynamic_pointer_cast<CustomType>(it->second))
                    {
                        if (arg.dest.has_value() && arg.args.size() == 2)
                        {
                            MIRRValueBinaryOp binOp{
                                .op = *arg.genericOpFallback,
                                .left = std::move(arg.args[0]),
                                .right = std::move(arg.args[1]),
                                .type = arg.dest->type,
                            };
                            replacement = MIRStmtAssign{
                                .lhs = std::move(*arg.dest),
                                .rhs = std::move(binOp),
                            };
                        }
                        return;
                    }
                }
            }

            // Retarget a generic-param method call (`<T>::next`) to the concrete
            // struct method once T is substituted (`Range::next`). Sema emits the
            // placeholder for `it.next()` where `it: T: Iterator<i32>`. Test the
            // '<' prefix first (O(1)) before scanning the string.
            if (!arg.funcName.empty() && arg.funcName[0] == '<')
            {
                size_t sep = arg.funcName.find("::");
                if (sep != std::string::npos)
                {
                    std::string recv = arg.funcName.substr(1, sep - 2); // strip < >
                    auto it = replaceTable.find(recv);
                    if (it != replaceTable.end())
                    {
                        if (auto ct = std::dynamic_pointer_cast<CustomType>(it->second))
                        {
                            std::string base = ct->getOriginName();
                            arg.funcName = base + arg.funcName.substr(sep);

                            // If the concrete struct is a GENERIC instantiation
                            // (e.g. Range$i32 of struct Range<T>), the retargeted
                            // method `Range::next` carries the struct's params —
                            // set its args here so the mono-queue block below
                            // materializes `Range::next_Mono_i32`. A non-generic
                            // struct has empty args → no change (direct call).
                            if (!ct->getGenericArgs().empty())
                            {
                                auto mIt = functionMap.find(arg.funcName);
                                if (mIt != functionMap.end()
                                    && mIt->second->genericParams.size() == ct->getGenericArgs().size())
                                    arg.genericParams = ct->getGenericArgs();
                            }
                        }
                    }
                }
            }

            if (!arg.genericParams.empty())
            {
                std::string monoName = makeMonoFuncName(arg.funcName, arg.genericParams);

                // A monomorphized body calling ANOTHER generic function: after
                // the rename below, genericParams is cleared, so collection()
                // (which re-scans inserted mono functions) can never see this
                // call again. Queue the nested callee here, with the already
                // substituted args, or its mono function is never created.
                auto funcIt = functionMap.find(arg.funcName);
                if (funcIt != functionMap.end()
                    && !funcIt->second->genericParams.empty()
                    && funcIt->second->genericParams.size() == arg.genericParams.size()
                    && generatedMonoFuncs.find(monoName) == generatedMonoFuncs.end())
                {
                    MonoInfo info;
                    info.genericFuncName = arg.funcName;
                    info.genericArgs = arg.genericParams;
                    info.monoFuncName = monoName;
                    info.genericFunc = funcIt->second;
                    monoInfos.push(std::make_unique<MonoInfo>(info));
                    generatedMonoFuncs.insert(monoName);
                }

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

    if (replacement.has_value())
        stmt = std::move(*replacement);
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