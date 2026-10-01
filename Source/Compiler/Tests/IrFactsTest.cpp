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

// ── `noalias` on an exclusive reference ──────────────────────────────────────
//
// The borrow checker proves exclusivity; LLVM's alias analysis cannot re-derive
// it (measured: FunctionAttrs emits `noalias` on a parameter essentially never).
// It IS a claim about every caller, so each condition has a counterexample here.

TEST_F(RuntimeTest, NoAliasOnASoleExclusiveReferenceParameter)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(R"(
fn bump(x: &mut i32) -> i32
{
    *x = *x + 1;
    ret *x;
}

fn main()
{
    let mut v = 1;
    let a = bump(&mut v);
    let b = bump(&mut v);
    print(a + b);
}
)")) << "compilation failed";

    llvm::Function *bump = lastCompile_->module->getFunction("bump");
    ASSERT_TRUE(bump);
    EXPECT_TRUE(bump->hasParamAttribute(0, llvm::Attribute::NoAlias));
}

// Two SHARED references may alias: `f(&v, &v)` is legal, and `noalias` would
// make that undefined behaviour.
TEST_F(RuntimeTest, NoNoAliasOnSharedReferences)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(R"(
fn peek(x: &i32) -> i32
{
    ret *x;
}

fn main()
{
    let v = 3;
    print(peek(&v) + peek(&v));
}
)")) << "compilation failed";

    llvm::Function *peek = lastCompile_->module->getFunction("peek");
    ASSERT_TRUE(peek);
    EXPECT_FALSE(peek->hasParamAttribute(0, llvm::Attribute::NoAlias));
}

// A raw pointer is not a borrow at all: it carries no exclusivity guarantee.
TEST_F(RuntimeTest, NoNoAliasOnRawPointers)
{
    emitOptLevel_ = 0;
    ffiForThisTest_ = true;
    // `wipe` is never called: at -o 0 nothing is deleted, and a call would only
    // need a raw pointer VALUE (which the language does not hand out freely).
    ASSERT_TRUE(compile(R"(
fn wipe(p: *mut i32) -> i32
{
    ret 7;
}

fn main()
{
    print(1);
}
)")) << "compilation failed";
    ffiForThisTest_ = false;

    llvm::Function *wipe = lastCompile_->module->getFunction("wipe");
    ASSERT_TRUE(wipe);
    EXPECT_FALSE(wipe->hasParamAttribute(0, llvm::Attribute::NoAlias));
}

// Two-phase borrows keep a reserved `&mut x` readable, so a sibling `&x` may
// point at the very object the exclusive reference points at.
TEST_F(RuntimeTest, NoNoAliasWhenASiblingCanAlias)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(R"(
fn add_in(a: &mut i32, b: &i32) -> i32
{
    *a = *a + *b;
    ret *a;
}

fn main()
{
    let mut v = 1;
    print(add_in(&mut v, &v));
}
)")) << "compilation failed";

    llvm::Function *fn = lastCompile_->module->getFunction("add_in");
    ASSERT_TRUE(fn);
    EXPECT_FALSE(fn->hasParamAttribute(0, llvm::Attribute::NoAlias));
}

// A by-value aggregate can CARRY a reference (`VecIter { src: &v }` does), which
// aliases the exclusive reference just as well as one passed directly.
TEST_F(RuntimeTest, NoNoAliasWhenASiblingCarriesAReference)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(R"(
struct Holder
{
    pub r: &i32
}

fn add_holder(a: &mut i32, h: Holder) -> i32
{
    *a = *a + *h.r;
    ret *a;
}

fn main()
{
    let mut v = 1;
    let w = 2;
    let h = Holder { r: &w };
    print(add_holder(&mut v, h));
}
)")) << "compilation failed";

    llvm::Function *fn = lastCompile_->module->getFunction("add_holder");
    ASSERT_TRUE(fn);
    EXPECT_FALSE(fn->hasParamAttribute(0, llvm::Attribute::NoAlias));
}

// A global is reachable with no parameter at all: `touched(&mut G)` would alias
// through G, which the function cannot see from the inside. The rule is per
// function -- `clean` in the SAME program still gets the attribute.
TEST_F(RuntimeTest, NoNoAliasWhenTheBodyTouchesAGlobal)
{
    emitOptLevel_ = 0;
    // A module-level `let` is a global; it is mutable storage (no `mut`
    // keyword -- assignment to it is what makes it a global ACCESS).
    ASSERT_TRUE(compile(R"(
let G = 0;

fn touched(a: &mut i32) -> i32
{
    G = G + 1;
    ret *a + G;
}

fn clean(a: &mut i32) -> i32
{
    *a = *a + 1;
    ret *a;
}

fn main()
{
    let mut v = 1;
    print(touched(&mut v) + clean(&mut v));
}
)")) << "compilation failed";

    llvm::Function *touched = lastCompile_->module->getFunction("touched");
    ASSERT_TRUE(touched);
    EXPECT_FALSE(touched->hasParamAttribute(0, llvm::Attribute::NoAlias));

    llvm::Function *clean = lastCompile_->module->getFunction("clean");
    ASSERT_TRUE(clean);
    EXPECT_TRUE(clean->hasParamAttribute(0, llvm::Attribute::NoAlias));
}

// C callers are not bound by the borrow checker: an exported entry point cannot
// promise that a `&mut` argument is the only way into its object.
TEST_F(RuntimeTest, NoNoAliasOnExportedFunctions)
{
    emitOptLevel_ = 0;
    ffiForThisTest_ = true;
    ASSERT_TRUE(compile(R"(
export fn poke(a: &mut i32) -> i32
{
    *a = *a + 1;
    ret *a;
}

fn main()
{
    let mut v = 1;
    print(poke(&mut v));
}
)")) << "compilation failed";
    ffiForThisTest_ = false;

    llvm::Function *poke = lastCompile_->module->getFunction("poke");
    ASSERT_TRUE(poke);
    EXPECT_TRUE(poke->hasExternalLinkage());
    EXPECT_FALSE(poke->hasParamAttribute(0, llvm::Attribute::NoAlias));
}
