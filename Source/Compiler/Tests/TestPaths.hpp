#pragma once

#include <filesystem>

#ifdef _WIN32
extern char *_pgmptr;
#endif

inline std::filesystem::path findStdlibDir()
{
    namespace fs = std::filesystem;
    fs::path executable;
#ifdef _WIN32
    if (_pgmptr)
        executable = _pgmptr;
#else
    std::error_code error;
    executable = fs::read_symlink("/proc/self/exe", error);
#endif
    // The test executable lives in Build/Intermediate.
    if (!executable.empty())
    {
        const auto candidate = executable.parent_path().parent_path() / "Binaries" / "lstdlib";
        if (fs::is_directory(candidate))
            return candidate;
    }
    const auto candidate = fs::current_path() / "Build" / "Binaries" / "lstdlib";
    return fs::is_directory(candidate) ? candidate : fs::path{};
}

inline std::string compilerPathForThisTest()
{
#ifdef _WIN32
    constexpr auto name = "lisc.exe";
#else
    constexpr auto name = "lisc";
#endif
    return (findStdlibDir().parent_path() / name).string();
}
