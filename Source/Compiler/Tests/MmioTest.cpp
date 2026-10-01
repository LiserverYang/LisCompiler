// Memory-mapped registers: `#[volatile]` on a type, `#[volatile(<address>)]` /
// `#[volatile(sym = <name>)]` on a global.
//
// Two properties are the whole point, and both are asserted on the emitted IR:
// the access is a VOLATILE one (a hardware write that LLVM may delete is a
// silently wrong program), and no storage is allocated for the register (the
// base is the address constant itself). The -o 2 case is the one that matters:
// at -o 0 nothing is optimized yet, so only the optimized module shows whether
// the fact survives the pipeline.

#include "RuntimeTestFixture.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <string>
#include <vector>

namespace
{
/// A register block reached through a method (`&mut Self`), a bare register, and a
/// read-modify-write: the three shapes a driver actually writes.
const char *kRegisterProgram = R"(
#[repr(C)]
#[volatile]
struct GpioRegs
{
    pub moder: i32,
    pub odr: i32,
}

#[volatile(0x40020000)]
let GPIOA: GpioRegs;

#[volatile(0x40023800)]
let RCC_AHB1ENR: i32;

impl GpioRegs
{
    fn set_word(self: &mut Self, word: i32)
    {
        self.odr = word;
    }
}

fn main() -> i32
{
    GPIOA.moder = 0xAB;
    GPIOA.set_word(32);
    RCC_AHB1ENR = RCC_AHB1ENR | 1;
    ret 0;
}
)";

/// Every volatile load/store in the module.
std::vector<const llvm::Instruction *> volatileAccesses(llvm::Module &m)
{
    std::vector<const llvm::Instruction *> found;
    for (auto &fn : m)
        for (auto &bb : fn)
            for (auto &inst : bb)
            {
                if (auto *st = llvm::dyn_cast<llvm::StoreInst>(&inst))
                    if (st->isVolatile()) found.push_back(st);
                if (auto *ld = llvm::dyn_cast<llvm::LoadInst>(&inst))
                    if (ld->isVolatile()) found.push_back(ld);
            }
    return found;
}

/// A volatile store whose address is a COMPILE-TIME constant: the register base,
/// folded to an `inttoptr` by the builder (or by the optimizer).
bool hasVolatileStoreToConstantAddress(llvm::Module &m)
{
    for (const llvm::Instruction *inst : volatileAccesses(m))
    {
        if (!llvm::isa<llvm::StoreInst>(inst))
            continue;
        auto *st = llvm::cast<llvm::StoreInst>(inst);
        if (llvm::isa<llvm::ConstantExpr>(st->getPointerOperand()))
            return true;
    }
    return false;
}

/// A volatile store through a POINTER VALUE (the receiver of a method): the
/// access that proves the flag comes from the TYPE, not from the global.
bool hasVolatileStoreThroughPointer(llvm::Module &m)
{
    for (const llvm::Instruction *inst : volatileAccesses(m))
    {
        if (!llvm::isa<llvm::StoreInst>(inst))
            continue;
        auto *st = llvm::cast<llvm::StoreInst>(inst);
        if (!llvm::isa<llvm::ConstantExpr>(st->getPointerOperand()) &&
            !llvm::isa<llvm::AllocaInst>(st->getPointerOperand()))
            return true;
    }
    return false;
}
} // namespace

TEST_F(RuntimeTest, MmioFieldWriteIsVolatileAndAllocatesNoStorage)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(kRegisterProgram)) << "compilation failed";

    // The address is code, not a variable: nothing to allocate, nothing for the
    // linker's .bss to place.
    EXPECT_EQ(lastCompile_->module->getNamedGlobal("GPIOA"), nullptr);
    EXPECT_EQ(lastCompile_->module->getNamedGlobal("RCC_AHB1ENR"), nullptr);

    EXPECT_TRUE(hasVolatileStoreToConstantAddress(*lastCompile_->module))
        << "a register write must be a volatile store to its address";
    // The bare register is read as well (read-modify-write), and the read is volatile too.
    EXPECT_GE(volatileAccesses(*lastCompile_->module).size(), 3u);
}

// The acceptance criterion: at -o 2 the volatile store is STILL THERE. Everything
// else about this feature is bookkeeping; this is the property that decides whether
// a driver works.
TEST_F(RuntimeTest, MmioVolatileStoreSurvivesO2)
{
    emitOptLevel_ = 2;
    ASSERT_TRUE(compile(kRegisterProgram)) << "compilation failed";

    EXPECT_TRUE(hasVolatileStoreToConstantAddress(*lastCompile_->module))
        << "the register write was optimized away at -o 2";
    EXPECT_EQ(lastCompile_->module->getNamedGlobal("GPIOA"), nullptr);
}

// `self.odr = word` inside an `impl`: the receiver is a pointer, so the volatility
// has to come from the TYPE. Without it the store would be an ordinary one and could
// be sunk or deleted.
TEST_F(RuntimeTest, MmioAccessThroughAMethodIsVolatile)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(kRegisterProgram)) << "compilation failed";

    EXPECT_TRUE(hasVolatileStoreThroughPointer(*lastCompile_->module))
        << "an access through `&mut Self` must stay volatile";
}

// ── the borrow rules apply to a register exactly like to any other global ────

TEST_F(RuntimeTest, TwoMutableBorrowsOfARegisterConflict)
{
    expectCompileFail(
        "#[repr(C)] #[volatile] struct Regs { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: Regs;\n"
        "fn main() -> i32 { let x = &mut REGS; let y = &mut REGS; x.a = 1; y.a = 2; ret 0; }\n",
        "E4001");
}

TEST_F(RuntimeTest, WriteToARegisterWhileSharedBorrowedIsRejected)
{
    expectCompileFail(
        "#[repr(C)] #[volatile] struct Regs { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: Regs;\n"
        "fn main() -> i32 { let r = &REGS; REGS.a = 1; let v = r.a; ret v; }\n",
        "E4003");
}

TEST_F(RuntimeTest, ReadingARegisterThroughASharedBorrowIsFine)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(
        "#[repr(C)] #[volatile] struct Regs { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: Regs;\n"
        "fn main() -> i32 { let r = &REGS; let v = r.a; ret v; }\n"))
        << "a shared read of a register must stay legal";
    EXPECT_TRUE(hasVolatileStoreToConstantAddress(*lastCompile_->module) == false)
        << "a read-only program has no store at all";
}

// ── the safety rules: what the attribute refuses to let you do ───────────────

TEST_F(RuntimeTest, VolatileTypeRequiresReprC)
{
    expectCompileFail("#[volatile] struct R { pub a: i32 }\nfn main() -> i32 { ret 0; }\n", "E3023");
}

TEST_F(RuntimeTest, RegisterBlockCannotBeConstructed)
{
    expectCompileFail(
        "#[repr(C)] #[volatile] struct R { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: R;\n"
        "fn main() -> i32 { let g = R { a: 1 }; ret 0; }\n",
        "E3024");
}

TEST_F(RuntimeTest, RegisterBlockIsNotAValue)
{
    expectCompileFail(
        "#[repr(C)] #[volatile] struct R { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: R;\n"
        "fn main() -> i32 { let g = REGS; ret 0; }\n",
        "E3024");
}

TEST_F(RuntimeTest, MemoryMappedGlobalOfANonVolatileTypeIsRejected)
{
    expectCompileFail(
        "#[repr(C)] struct R { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: R;\n"
        "fn main() -> i32 { REGS.a = 1; ret 0; }\n",
        "E3023");
}

TEST_F(RuntimeTest, BareRegisterCannotBeBorrowed)
{
    expectCompileFail(
        "#[volatile(0x40023800)] let RCC: i32;\n"
        "fn main() -> i32 { let r = &RCC; ret *r; }\n",
        "E3023");
}

TEST_F(RuntimeTest, ReferenceToARegisterFieldIsRejected)
{
    expectCompileFail(
        "#[repr(C)] #[volatile] struct R { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: R;\n"
        "fn main() -> i32 { let p = &mut REGS.a; *p = 1; ret 0; }\n",
        "E3023");
}

TEST_F(RuntimeTest, MemoryMappedGlobalCannotHaveAnInitializer)
{
    expectCompileFail(
        "#[repr(C)] #[volatile] struct R { pub a: i32 }\n"
        "#[volatile(0x40020000)] let REGS: R = R { a: 1 };\n"
        "fn main() -> i32 { ret 0; }\n",
        "E3023");
}

TEST_F(RuntimeTest, MemoryMappedGlobalNeedsADeclaredType)
{
    expectCompileFail("#[volatile(0x40020000)] let REGS;\nfn main() -> i32 { ret 0; }\n", "E3023");
}

// The gate this feature deliberately does NOT open: safe code still cannot turn an
// integer into a pointer, so a register is only reachable through a declaration.
TEST_F(RuntimeTest, AbsoluteAddressPointerCastIsStillRejected)
{
    expectCompileFail("fn main() -> i32 { let p = 0x40020000 as *mut i32; ret 0; }\n", "integer can only be cast to primitive type");
}

// ── the runtime proof ────────────────────────────────────────────────────────

// The only test that shows the ADDRESS is really written: C owns the storage, Lis
// writes it through `#[volatile(sym = <name>)]`, and C reads it back. A compiler that
// dropped the store would fail here even though every IR assertion above passed.
TEST_F(RuntimeTest, MmioSymbolWriteReachesC)
{
    const std::string helper =
        "#include <cstdint>\n"
        "extern \"C\" volatile uint32_t lis_test_regs[4] = {0u, 0u, 0u, 0u};\n"
        "extern \"C\" uint32_t lis_probe(int i) { return lis_test_regs[i]; }\n";

    expectRunWithSources(
        "#[repr(C)]\n"
        "#[volatile]\n"
        "struct Regs { pub a: i32, pub b: i32, pub c: i32, pub d: i32 }\n"
        "#[volatile(sym = \"lis_test_regs\")]\n"
        "let REGS: Regs;\n"
        "extern \"C\" fn lis_probe(i: i32) -> i32;\n"
        "fn main() -> i32\n"
        "{\n"
        "    REGS.a = 41;\n"
        "    REGS.d = 7;\n"
        "    if lis_probe(0) != 41 { ret 1; }\n"
        "    if lis_probe(3) != 7 { ret 2; }\n"
        "    if lis_probe(1) != 0 { ret 3; }\n"
        "    ret 0;\n"
        "}\n",
        {{"mmio_helper.cpp", helper}},
        0);
}
