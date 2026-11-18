# Copyright 2025, LiserverYang. All rights reserved.

import importlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Put the repo root on sys.path so `import Build.BuildSystem` resolves even when
# this script is invoked from another cwd (e.g. `python ../build.py` from a
# subdirectory). Root-relative paths below make the whole build cwd-independent.
_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

import Build.BuildSystem as BuildSystem

if __name__ != "__main__":
    BuildSystem.Logger.Log(
        BuildSystem.LogLevelEnum.Error,
        "You could only run this srcipt by console not import.",
        True,
        -1,
    )

if BuildSystem.GetCurrentSystem() == BuildSystem.SystemEnum.Other:
    # Log and exit
    BuildSystem.Logger.Log(
        BuildSystem.LogLevelEnum.Error,
        "Unsupported platform. Only support Windows, MacOS and Linux.",
        True,
        -1,
    )

# The pinned LisBuilder expects Windows archive names and runtime libraries.
# Use llvm-config's native link flags for Linux until LisBuilder supports them.
if sys.platform.startswith("linux"):
    def configure_linux_llvm(position: str, EnableLld: bool = True) -> None:
        config = BuildSystem.Config.LLVMConfig
        llvm_config = Path(position) / "bin" / "llvm-config"

        def flags(*args: str) -> str:
            return " ".join(
                subprocess.check_output([llvm_config, *args], text=True).split()
            )

        config.LLVMIncludeCommand = f"-I{flags('--includedir')}"
        link_mode = "--link-static" if flags("--shared-mode") == "static" else "--link-shared"
        config.LLVMLibs = flags(link_mode, "--ldflags", "--libs", "--system-libs")
        config.LLVMCommand = f"{config.LLVMIncludeCommand} {config.LLVMLibs}"
        llvm_bin = llvm_config.parent
        config.LLVMLinkerFlags = (
            f"-B{llvm_bin} -fuse-ld=lld"
            if EnableLld and shutil.which("ld.lld", path=str(llvm_bin))
            else ""
        )

    importlib.import_module("Build.BuildSystem.BuildApp").InitLLVMConfig = configure_linux_llvm

# Don't write __pycatch__ file everywhere!!!
BuildSystem.sys.dont_write_bytecode = True

# Just build
BuildSystem.BuildApp(
    BuildSystem.FileIO(os.path.join(_ROOT, "Source")),
    [
        os.path.join(_ROOT, "Source", "lisc.target.py"),
        os.path.join(_ROOT, "Source", "lisls.target.py"),
    ],
)
