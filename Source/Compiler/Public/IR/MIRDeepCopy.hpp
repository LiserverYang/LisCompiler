/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License,
 */

#pragma once

#include "IR/MIR.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mir::deep_copy
{

// ====================== 工具函数前置声明 ======================
template <typename T>
std::enable_if_t<
    std::is_fundamental_v<T> || std::is_same_v<T, std::string> || std::is_enum_v<T>,
    T>
copy(const T &val);

template <typename T>
std::optional<T> copy(const std::optional<T> &opt);
template <typename T>
std::vector<T> copy(const std::vector<T> &vec);
template <typename T>
std::shared_ptr<T> copy(const std::shared_ptr<T> &ptr);
template <typename A, typename B>
std::pair<A, B> copy(const std::pair<A, B> &p);
template <typename... Ts>
std::variant<Ts...> copy_variant(const std::variant<Ts...> &var);

// ====================== 基础类型/标准库模板深拷贝 ======================
template <typename T>
std::enable_if_t<
    std::is_fundamental_v<T> || std::is_same_v<T, std::string> || std::is_enum_v<T>,
    T>
copy(const T &val)
{
    return val;
}

inline std::shared_ptr<Type> copy(const std::shared_ptr<Type> &ptr)
{
    return ptr; // Types are structural/interned — safe to share
}

// ====================== MIR 类型深拷贝 ======================
inline Projection copy(const Projection &p)
{
    return {
        .kind = copy(p.kind),
        .field = copy(p.field),
        .localIndex = copy(p.localIndex)};
}

inline MIRPlace copy(const MIRPlace &p)
{
    return {
        .base = copy(p.base),
        .index = copy(p.index),
        .name = copy(p.name),
        .projections = copy(p.projections),
        .type = copy(p.type)};
}

inline MIRConst copy(const MIRConst &c)
{
    return {
        .kind = copy(c.kind),
        .value = copy_variant(c.value),
        .type = copy(c.type)};
}

inline MIRCopy copy(const MIRCopy &c)
{
    return {.place = copy(c.place)};
}

inline MIRMove copy(const MIRMove &m)
{
    return {.place = copy(m.place)};
}

inline MIROperand copy(const MIROperand &op)
{
    return copy_variant(op);
}

inline MIRRValueUse copy(const MIRRValueUse &u)
{
    return {.operand = copy(u.operand)};
}

inline MIRRValueBinaryOp copy(const MIRRValueBinaryOp &op)
{
    return {
        .op = copy(op.op),
        .left = copy(op.left),
        .right = copy(op.right),
        .type = copy(op.type)};
}

inline MIRRValueUnaryOp copy(const MIRRValueUnaryOp &op)
{
    return {
        .op = copy(op.op),
        .operand = copy(op.operand),
        .type = copy(op.type)};
}

inline MIRRValueCast copy(const MIRRValueCast &c)
{
    return {
        .operand = copy(c.operand),
        .targetType = copy(c.targetType)};
}

inline MIRRValueRef copy(const MIRRValueRef &r)
{
    return {
        .place = copy(r.place),
        .isMut = copy(r.isMut)};
}

inline MIRRValueAddrOf copy(const MIRRValueAddrOf &a)
{
    return {.place = copy(a.place)};
}

inline MIRRValueStructInit copy(const MIRRValueStructInit &s)
{
    return {
        .structName = copy(s.structName),
        .fields = copy(s.fields),
        .type = copy(s.type)};
}

inline MIRRValue copy(const MIRRValue &rv)
{
    return copy_variant(rv);
}

// ====================== 语句/终止符 ======================
inline MIRStmtAssign copy(const MIRStmtAssign &stmt)
{
    return {
        .lhs = copy(stmt.lhs),
        .rhs = copy(stmt.rhs)};
}

inline MIRStmtCall copy(const MIRStmtCall &call)
{
    return {
        .dest = copy(call.dest),
        .callee = copy(call.callee),
        .funcName = copy(call.funcName),
        .args = copy(call.args),
        .genericParams = copy(call.genericParams),
        .genericOpFallback = copy(call.genericOpFallback)};
}

inline MIRStmtDrop copy(const MIRStmtDrop &d)
{
    return {.place = copy(d.place)};
}

inline MIRStmtNop copy(const MIRStmtNop &)
{
    return {};
}

inline MIRStatement copy(const MIRStatement &stmt)
{
    return copy_variant(stmt);
}

inline MIRTermGoto copy(const MIRTermGoto &t)
{
    return {.target = copy(t.target)};
}

inline MIRTermBranch copy(const MIRTermBranch &t)
{
    return {
        .cond = copy(t.cond),
        .thenBlock = copy(t.thenBlock),
        .elseBlock = copy(t.elseBlock)};
}

inline MIRTermReturn copy(const MIRTermReturn &t)
{
    return {.value = copy(t.value)};
}

inline MIRTermCall copy(const MIRTermCall &t)
{
    return {
        .call = copy(t.call),
        .normalDest = copy(t.normalDest),
        .unwindDest = copy(t.unwindDest)};
}

inline MIRTermUnreachable copy(const MIRTermUnreachable &)
{
    return {};
}

inline MIRTerminator copy(const MIRTerminator &t)
{
    return copy_variant(t);
}

inline MIRBasicBlock copy(const MIRBasicBlock &bb)
{
    return {
        .id = copy(bb.id),
        .label = copy(bb.label),
        .stmts = copy(bb.stmts),
        .terminator = copy(bb.terminator)};
}

inline MIRLocal copy(const MIRLocal &l)
{
    return {
        .index = copy(l.index),
        .name = copy(l.name),
        .type = copy(l.type),
        .isMutable = copy(l.isMutable),
        .isTemp = copy(l.isTemp),
        .isArg = copy(l.isArg)};
}

inline MIRBody copy(const MIRBody &body)
{
    return {
        .funcName = copy(body.funcName),
        .locals = copy(body.locals),
        .argCount = copy(body.argCount),
        .blocks = copy(body.blocks),
        .returnType = copy(body.returnType)};
}

inline MIRFunction copy(const MIRFunction &func)
{
    return {
        .name = copy(func.name),
        .body = copy(func.body),
        .isMethod = copy(func.isMethod),
        .isStatic = copy(func.isStatic),
        .associatedStruct = copy(func.associatedStruct),
        .associatedTrait = copy(func.associatedTrait),
        .genericParams = copy(func.genericParams)};
}

inline MIRGlobal copy(const MIRGlobal &g)
{
    return {
        .name = copy(g.name),
        .type = copy(g.type),
        .init = copy(g.init)};
}

inline MIRProgram copy_program(const MIRProgram &prog)
{
    return {
        .functions = copy(prog.functions),
        .globals = copy(prog.globals)};
}

template <typename T>
std::optional<T> copy(const std::optional<T> &opt)
{
    if (!opt) return std::nullopt;
    return copy(*opt);
}

template <typename T>
std::vector<T> copy(const std::vector<T> &vec)
{
    std::vector<T> res;
    res.reserve(vec.size());
    for (const auto &item : vec)
        res.push_back(copy(item));
    return res;
}

template <typename T>
std::shared_ptr<T> copy(const std::shared_ptr<T> &ptr)
{
    if (!ptr) return nullptr;
    return std::make_shared<T>(copy(*ptr));
}

template <typename A, typename B>
std::pair<A, B> copy(const std::pair<A, B> &p)
{
    return {copy(p.first), copy(p.second)};
}

template <typename... Ts>
std::variant<Ts...> copy_variant(const std::variant<Ts...> &var)
{
    return std::visit(
        [](const auto &val) -> std::variant<Ts...>
        {
            return copy(val);
        },
        var);
}

} // namespace mir::deep_copy