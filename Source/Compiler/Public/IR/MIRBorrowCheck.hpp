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
 * This is the ONLY implementation of those checks (2026-09-19): the tree-based
 * checker that used to live in HIRSemanticAnalyzer is gone, along with the
 * LIS_BORROW_CHECK escape hatch. The 246 source-level BorrowCheckerTest cases
 * and the runtime suite are its oracle: a missed OR spurious diagnostic fails a
 * test.
 *
 * Diagnostics name the ITEM's source file (MIRFunction::sourceFilePath, stamped
 * by MIRBuilder from Context::stmtAttributions), like the HIR analyzer; the
 * source TEXT still comes from Context::fileValue, which is the main file —
 * HIRSemanticAnalyzer has the same limitation.
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

    /**
     * Run the checks and REPORT — without the end-of-pass gate. The test
     * harnesses call this: they own the error count and must survive a rejected
     * program (run() exits, exactly like HIRSemanticAnalyzer::run).
     */
    void check();
};
