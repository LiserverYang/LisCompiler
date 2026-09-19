/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * MIRBorrowCheck — borrow / move / definite-assignment checking on the MIR CFG.
 *
 * WHY MIR (2026-09-19). These three analyses are CFG DATAFLOW: "is this place
 * still owned here", "is this binding assigned on every path that reaches this
 * point", "is this borrow still live". The HIR tree can only approximate them
 * with statement ordinals and scope markers, which is why the HIR checker needs
 * deferred conflicts, holder-last-use tables and a promoted-borrow resolve pass.
 * MIR already has everything the precise versions need:
 *   - a real CFG (blocks + terminators), so liveness is a dataflow problem;
 *   - MOVE vs COPY spelled out on every operand (MIRBuilder::buildOperand),
 *     instead of re-deriving it from types;
 *   - locals with isTemp / isMutable / isArg, so "is this a temporary borrow"
 *     is a property of the destination local;
 *   - PLACES with field/index/deref projections, the same shape the checker
 *     reasons about, plus a source span (MIRPlace::pos) for diagnostics.
 *
 * SCOPE. This pass owns: borrow conflicts (E4001-E4004), borrow-of-moved
 * (E4005), dangling returns (E4007), move/ownership (E3005, E3016, E3017) and
 * definite assignment (E3011). It does NOT own mutability (E3004/E4006) or
 * "a non-Copy binding needs an initializer" (E3012) — those are place/type
 * properties, not dataflow, and stay in HIRSemanticAnalyzer.
 *
 * SWITCH. The environment variable LIS_BORROW_CHECK=mir disables the HIR-side
 * implementations and enables this one; the default is still 'hir' while the
 * port is in progress. The 246 source-level BorrowCheckerTest cases and the
 * runtime suite are the differential oracle for the port: with the switch on, a
 * missed OR spurious diagnostic fails a test.
 *
 * Known gap: diagnostics use Context::filePath (the main file). The HIR checker
 * attributes module items through Context::stmtAttributions; MIR does not carry
 * that yet, so an error inside a stdlib module would name the main file.
 */
#pragma once

#include "Core/Pass.hpp"
#include "IR/MIR.hpp"

#include <memory>

class MIRBorrowCheck : public Pass
{
public:
    MIRBorrowCheck() = default;
    explicit MIRBorrowCheck(std::shared_ptr<Context> cnt) { context = cnt; }

    void run() override;

    /// True when LIS_BORROW_CHECK=mir selected the MIR implementation.
    static bool enabled();
};
