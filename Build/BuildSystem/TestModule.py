# Copyright 2025, LiserverYang. All rights reserved.

from .Logger import Logger
from .LogLevelEnum import LogLevelEnum
from typing import List

import os

def TestModule(ModuleName:str, ModulePath: str, ModuleOFiles: List[str], ArgumentsAdded: str):
    """
    This function can test a module with google test.
    """
    
    Logger.Log(LogLevelEnum.Info, f"Testing module '{ModuleName}'")

    TestPath: str = ModulePath + "/Tests/"

    BuildResult = os.system(f"g++ {TestPath}*.cpp {' '.join(ModuleOFiles)} -o ./Build/Intermediate/test {ArgumentsAdded} -I{ModulePath}/Public/")
    
    if BuildResult != 0:
        Logger.Log(LogLevelEnum.Error, f"Unable to build test file for module '{ModuleName}' and the compiler return {BuildResult}", True, -1)

    if os.system("powershell ./Build/Intermediate/test") != 0:
        Logger.Log(LogLevelEnum.Error, f"Test module '{ModuleName}' faild. See log to find out what happend.", True, -1)