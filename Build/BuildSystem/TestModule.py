# Copyright 2025, LiserverYang. All rights reserved.

from .Logger import Logger
from .LogLevelEnum import LogLevelEnum
from .BuildContext import BuildContext
from .SystemEnum import SystemEnum
from typing import List

import subprocess

import os


def TestModule(
    ModuleName: str, ModulePath: str, ModuleOFiles: List[str], ArgumentsAdded: str
):
    """
    This function can test a module with google test.
    """

    Logger.Log(LogLevelEnum.Info, f"Testing module '{ModuleName}'")

    TestPath: str = ModulePath + "/Tests/"

    BuildResult = os.system(
        f"g++ {TestPath}*.cpp {' '.join(ModuleOFiles)} -o ./Build/Intermediate/test {ArgumentsAdded} -I{ModulePath}/Public/"
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

    if subprocess.run([ExePosition]).returncode != 0:
        Logger.Log(
            LogLevelEnum.Error,
            f"Test module '{ModuleName}' faild. See log to find out what happend.",
            True,
            -1,
        )
