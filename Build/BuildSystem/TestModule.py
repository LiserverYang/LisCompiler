# Copyright 2025, LiserverYang. All rights reserved.

from .Logger import Logger
from .LogLevelEnum import LogLevelEnum
from .BuildContext import BuildContext
from .SystemEnum import SystemEnum
from .Config import LLVMConfig
from typing import List

import shutil
import subprocess

import os


def TestModule(
    ModuleName: str,
    ModulePath: str,
    ModuleOFiles: List[str],
    ArgumentsAdded: str,
    CxxStanderd: str,
):
    """
    This function can test a module with google test.

    The g++ flags must match the module's own compile flags: the module objects
    are built with `-std={CxxStanderd}` and (for the Compiler module) link
    against LLVM. Rebuilding the test TUs under the default standard would
    change the layout of internal-linkage statics in headers (e.g.
    Token.hpp's `keywords`/`keywordsMap`), crashing during static init.
    """

    Logger.Log(LogLevelEnum.Info, f"Testing module '{ModuleName}'")

    TestPath: str = ModulePath + "/Tests/"

    BuildResult = os.system(
        f"g++ -std={CxxStanderd} {TestPath}*.cpp {' '.join(ModuleOFiles)} -o ./Build/Intermediate/test {ArgumentsAdded} -I{ModulePath}/Public/ {LLVMConfig.LLVMCommand}"
    )

    if BuildResult != 0:
        Logger.Log(
            LogLevelEnum.Error,
            f"Unable to build test file for module '{ModuleName}' and the compiler return {BuildResult}",
            True,
            -1,
        )

    ExePosition = (
        "./Build/Intermediate/test.exe"
        if BuildContext.SystemType == SystemEnum.Windows
        else "./Build/Intermediate/test"
    )

    # The test binary links the MinGW runtime (libstdc++-6.dll & friends) found
    # next to g++. That directory is not necessarily on the inherited PATH, so
    # prepend it or Windows aborts the process with STATUS_ENTRYPOINT_NOT_FOUND
    # (0xC0000139).
    RunEnv = dict(os.environ)
    GxxDir = os.path.dirname(shutil.which("g++") or "")
    if GxxDir and RunEnv.get("PATH", ""):
        RunEnv["PATH"] = GxxDir + os.pathsep + RunEnv["PATH"]

    if subprocess.run([ExePosition], env=RunEnv).returncode != 0:
        Logger.Log(
            LogLevelEnum.Error,
            f"Test module '{ModuleName}' faild. See log to find out what happend.",
            True,
            -1,
        )
