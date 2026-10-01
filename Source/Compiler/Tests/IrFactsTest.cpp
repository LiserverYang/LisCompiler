// Facts the LANGUAGE knows and LLVM cannot derive: which functions C can reach
// (linkage) and which references are exclusive (parameter attributes).
//
// The module under test is the one the last compile() left in lastCompile_, so
// these tests assert on the emitted IR itself. They set emitOptLevel_ = 0 for a
// blunt reason: at -o 2 the inliner folds a one-call helper into main and
// globaldce (now that non-entry functions are internal) removes it, so
// `module->getFunction("helper")` would be null and the assertion would be
// about the optimizer rather than about the fact we emit.

#include "RuntimeTestFixture.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

namespace
{
// helper is called twice so it is unambiguously part of the program.
const char *kInternalLinkageSource = R"(
fn helper(x: i32) -> i32
{
    ret x + 1;
}

fn main()
{
    let a = helper(20);
    let b = helper(a);
    print(b);
}
)";

const char *kExportedSource = R"(
export fn lis_probe(x: i32) -> i32
{
    ret x * 2;
}

fn main()
{
    print(lis_probe(21));
}
)";
} // namespace

// The object is linked into an executable (or a shared library) by C, which can
// only name `main` and the `export fn` entry points. Everything else is
// module-private, and saying so in the IR is what lets globaldce drop the
// stdlib routines a program never calls instead of emitting all of them.
TEST_F(RuntimeTest, InternalLinkageForOrdinaryFunctions)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(kInternalLinkageSource)) << "compilation failed";

    llvm::Function *helper = lastCompile_->module->getFunction("helper");
    ASSERT_TRUE(helper) << "helper must be emitted";
    EXPECT_TRUE(helper->hasInternalLinkage());

    // The C runtime calls main, so it stays external in every case.
    llvm::Function *entry = lastCompile_->module->getFunction("main");
    ASSERT_TRUE(entry);
    EXPECT_TRUE(entry->hasExternalLinkage());
}

// An `export fn` is the OTHER thing C can call: it must keep external linkage
// (its emitted name is the C symbol, `#[link_name]` included).
TEST_F(RuntimeTest, ExportedFunctionsStayExternal)
{
    emitOptLevel_ = 0;
    ffiForThisTest_ = true;
    ASSERT_TRUE(compile(kExportedSource)) << "compilation failed";
    ffiForThisTest_ = false;

    llvm::Function *exported = lastCompile_->module->getFunction("lis_probe");
    ASSERT_TRUE(exported) << "the exported symbol must be emitted under its C name";
    EXPECT_TRUE(exported->hasExternalLinkage());

    llvm::Function *entry = lastCompile_->module->getFunction("main");
    ASSERT_TRUE(entry);
    EXPECT_TRUE(entry->hasExternalLinkage());
}

// Compiler-synthesized drop glue is not a symbol anybody can name from C.
TEST_F(RuntimeTest, DropGlueIsInternal)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(R"(
struct Boxed
{
    pub s: String
}

fn main()
{
    let b = Boxed { s: String::from_lit("hi") };
    print(b.s.to_cstr());
}
)")) << "compilation failed";

    llvm::Function *glue = lastCompile_->module->getFunction("__drop_Boxed");
    ASSERT_TRUE(glue) << "drop glue for Boxed must be emitted";
    EXPECT_TRUE(glue->hasInternalLinkage());
}
