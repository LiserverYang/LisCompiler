// The process status of `fn main()` -- the first thing the in-process JIT cannot
// check.
//
// A VOID main used to be emitted as `void @main()`, so the C runtime read
// whatever the last libc call left in eax as the process status: measured,
// `fn main() { print("hello"); }` exited 5 (fwrite's byte count) and an empty
// body exited 1. The JIT path calls a void main as `void(*)()` and reports 0
// (RuntimeJit.cpp), which is exactly why a fully green suite never noticed --
// so every test here LINKS the program and runs it as a child process.

#include "RuntimeTestFixture.hpp"

// `print` is the sharpest probe: it leaves fwrite's byte count in the register
// the CRT reads.
TEST_F(RuntimeTest, VoidMainExitsZeroAfterPrinting)
{
    expectRunProc(R"(
fn main()
{
    print("hello");
}
)",
        0);
}

TEST_F(RuntimeTest, VoidMainWithNoLibcCallAtTheEndExitsZero)
{
    expectRunProc(R"(
fn main()
{
    let a = 1 + 1;
}
)",
        0);
}

// The explicit form keeps working: for `main() -> i32` the returned value IS the
// status (that is how every Examples baseline is written).
TEST_F(RuntimeTest, IntMainStillReturnsItsValue)
{
    expectRunProc(R"(
fn main() -> i32
{
    print("hi");
    ret 42;
}
)",
        42);
}
