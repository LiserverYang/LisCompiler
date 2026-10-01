// Cross-compilation: `--target` (plus --cpu / --features), and the invariant that
// NOT passing it changes nothing.
//
// Two kinds of assertion live here. The in-process ones exercise the ONE resolver
// the whole compiler uses (Core/TargetInit.hpp): the analyzer's ABI rules, the
// Emitter's TargetMachine and the command line all have to agree on the triple and
// the data layout, so a divergence is a silently mis-compiled object, not a
// cosmetic mismatch. The object-file ones read the emitted bytes: the ELF class and
// e_machine fields say WHICH target the object was produced for, and neither
// depends on the host the compiler runs on.

#include "RuntimeTestFixture.hpp"

#include "Core/CompilePipeline.hpp"
#include "Core/TargetInit.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

extern char *_pgmptr; // full path of the running executable (MinGW CRT)

namespace
{
/// A program with nothing target-specific in it: this suite is about the target the
/// compiler RESOLVES, not about what the program does.
const char *kTrivialSource = R"(
fn main() -> i32
{
    ret 0;
}
)";

// The pipeline looks for <argv[0]>/lstdlib next to the compiler; the test binary
// lives in Build/Intermediate, so lisc.exe is its sibling under Binaries.
std::string compilerPathForThisTest()
{
    if (_pgmptr)
    {
        fs::path candidate = fs::path(_pgmptr).parent_path().parent_path() / "Binaries" / "lisc.exe";
        return candidate.string();
    }
    return "lisc.exe";
}

std::string readFileBytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct ElfHeaderInfo
{
    uint8_t elfClass = 0;  // 1 = ELF32, 2 = ELF64
    uint16_t machine = 0;  // 40 = EM_ARM, 183 = EM_AARCH64
};

ElfHeaderInfo elfHeaderOf(const fs::path &path)
{
    const std::string bytes = readFileBytes(path);
    ElfHeaderInfo info;
    if (bytes.size() < 20 || bytes[0] != '\x7f' || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F')
        return info; // the caller's EXPECTs report the empty header
    info.elfClass = static_cast<uint8_t>(bytes[4]);
    // Little endian, which is what both cross targets under test and the host use.
    info.machine = static_cast<uint16_t>(static_cast<uint8_t>(bytes[18])) |
                   static_cast<uint16_t>(static_cast<uint8_t>(bytes[19]) << 8);
    return info;
}

/// Write a throw-away .lis file. The pipeline constructor reads its input file
/// BEFORE it resolves the target, so a test that only cares about the target still
/// has to hand it a real file (otherwise it fails for the wrong reason).
fs::path writeTempSource(const std::string &name)
{
    fs::path path = fs::temp_directory_path() / ("lis_target_" + std::to_string(rtProcessId()) + "_" + name);
    {
        std::ofstream out(path);
        out << kTrivialSource;
    } // the destructor flushes: `close` is #defined to _close by the fixture
    return path;
}
} // namespace

// The default path is the host, and it keeps the host CPU and features: this is
// what every compile did before --target existed, and what the other 1400 tests
// depend on.
TEST(TargetTest, DefaultTargetIsTheHost)
{
    const ResolvedTarget host = resolveTarget("", "", "");
    EXPECT_TRUE(host.host);
    EXPECT_EQ(host.triple, llvm::sys::getDefaultTargetTriple());
    EXPECT_EQ(host.dataLayout, hostDataLayout()); // the compat shim agrees
    EXPECT_EQ(host.reloc, llvm::Reloc::PIC_);

    // The host path has to keep handing createTargetMachine EXACTLY what it used to:
    // the host CPU and the host feature string. These four values are the whole
    // input to that call, so pinning them is what makes "the default path is
    // untouched" a checked property instead of a hope.
    EXPECT_EQ(host.cpu, llvm::sys::getHostCPUName().str());
    llvm::StringMap<bool> featureMap = llvm::sys::getHostCPUFeatures();
    llvm::SubtargetFeatures features;
    for (auto &[name, enabled] : featureMap)
        features.AddFeature(name, enabled);
    EXPECT_EQ(host.features, features.getString());
}

// A cross target gets what was asked for, and NOT the host's CPU: LLVM would reject
// a host CPU name on another architecture, and a 32-bit data layout is the evidence
// that the sizes the ABI rules use are the target's.
TEST(TargetTest, CrossTargetIsResolvedForThatTarget)
{
    const ResolvedTarget thumb = resolveTarget("thumbv7m-none-eabi", "", "");
    EXPECT_FALSE(thumb.host);
    EXPECT_EQ(thumb.triple, "thumbv7m-none-eabi");
    EXPECT_EQ(thumb.cpu, "generic");
    EXPECT_EQ(thumb.features, "");
    EXPECT_EQ(thumb.reloc, llvm::Reloc::Static) << "bare metal has no dynamic loader";
    EXPECT_NE(thumb.dataLayout.find("p:32:32"), std::string::npos) << thumb.dataLayout;

    // An explicit CPU and features string travel verbatim.
    const ResolvedTarget m4 = resolveTarget("thumbv7em-none-eabi", "cortex-m4", "+thumb2,+hwdiv");
    EXPECT_EQ(m4.cpu, "cortex-m4");
    EXPECT_EQ(m4.features, "+thumb2,+hwdiv");
}

// A typo on the command line is a USAGE error (the driver reports it and exits 2),
// never an internal compiler error with a stack trace (exit 70).
TEST(TargetTest, UnknownTargetIsAUsageError)
{
    EXPECT_THROW(resolveTarget("definitely-not-a-triple", "", ""), ArgParseError);
}

// The command line reaches the same resolver, and the pipeline refuses the one
// combination that cannot work: --shared is linked by the HOST toolchain.
TEST(TargetTest, PipelineAcceptsCrossTargetAndRefusesSharedWithIt)
{
    const std::string self = compilerPathForThisTest();
    const std::string source = writeTempSource("cli.lis").string();

    std::vector<const char *> ok{self.c_str(), source.c_str(), "--target", "thumbv7m-none-eabi"};
    auto context = std::make_shared<Context>();
    {
        CompilePipeline pipeline(context, static_cast<int>(ok.size()), ok.data());
        EXPECT_EQ(context->targetTriple, "thumbv7m-none-eabi");
        EXPECT_EQ(context->args->getArg("target"), "thumbv7m-none-eabi");
    }

    std::vector<const char *> bad{self.c_str(), source.c_str(), "--target", "thumbv7m-none-eabi", "--shared"};
    auto sharedContext = std::make_shared<Context>();
    EXPECT_THROW(
        CompilePipeline(sharedContext, static_cast<int>(bad.size()), bad.data()), ArgParseError);

    fs::remove(source);
}

// The hard guarantee for everything else in the suite and for the Examples
// baseline: asking for the HOST triple explicitly produces the SAME object as not
// asking at all. (The resolver treats "== host" as the host path, so the CPU and
// feature detection still run; if that ever diverges, this compares the bytes.)
TEST_F(RuntimeTest, HostTripleIsByteIdenticalToNoTarget)
{
    emitOptLevel_ = 0;
    ASSERT_TRUE(compile(kTrivialSource)) << "compilation failed";
    const std::string withoutFlag = readFileBytes(objPath);
    ASSERT_FALSE(withoutFlag.empty());

    targetTriple_ = llvm::sys::getDefaultTargetTriple();
    ASSERT_TRUE(compile(kTrivialSource)) << "compilation failed";
    const std::string withFlag = readFileBytes(objPath);

    EXPECT_EQ(withFlag.size(), withoutFlag.size())
        << "an explicit host triple must not change the object size";
    EXPECT_TRUE(withFlag == withoutFlag)
        << "--target <host triple> must emit the same bytes as no --target";
}

TEST_F(RuntimeTest, ThumbV7mEmitsElf32ArmObject)
{
    emitOptLevel_ = 0;
    targetTriple_ = "thumbv7m-none-eabi";
    ASSERT_TRUE(compile(kTrivialSource)) << "cross compilation failed";

    // The module and the context agree with the request, and the data layout is the
    // 32-bit one (this is what the FFI ABI classification reads).
    EXPECT_EQ(lastCompile_->targetTriple, "thumbv7m-none-eabi");
    ASSERT_TRUE(lastCompile_->module);
    EXPECT_EQ(lastCompile_->module->getTargetTriple(), "thumbv7m-none-eabi");
    // The layout is the target's, not the host's: 32-bit pointers decide how every
    // size and offset the FFI rules read is computed. (Asserted through the
    // DataLayout API: the string form only spells out what differs from the
    // target's default, so `p:32:32` is present on ARM but a 64-bit target omits
    // `p:64:64` entirely.)
    const llvm::DataLayout layout{lastCompile_->module->getDataLayoutStr()};
    EXPECT_EQ(layout.getPointerSizeInBits(0), 32u);

    // And the object really is an ARM ELF, not a host object with a different triple
    // printed on it.
    const ElfHeaderInfo elf = elfHeaderOf(objPath);
    EXPECT_EQ(elf.elfClass, 1) << "ELF32";
    EXPECT_EQ(elf.machine, 40) << "EM_ARM";
}

TEST_F(RuntimeTest, Aarch64EmitsElf64Object)
{
    emitOptLevel_ = 0;
    targetTriple_ = "aarch64-unknown-linux-gnu";
    ASSERT_TRUE(compile(kTrivialSource)) << "cross compilation failed";

    const ElfHeaderInfo elf = elfHeaderOf(objPath);
    EXPECT_EQ(elf.elfClass, 2) << "ELF64";
    EXPECT_EQ(elf.machine, 183) << "EM_AARCH64";
    const llvm::DataLayout layout{lastCompile_->module->getDataLayoutStr()};
    EXPECT_EQ(layout.getPointerSizeInBits(0), 64u);
}
