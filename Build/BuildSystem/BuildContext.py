# Copyright 2025, LiserverYang. All rights reserved.

from .ModuleBase import ModuleBase
from .TargetBase import TargetBase
from .BuildTypeEnum import BuildTypeEnum
from .SystemEnum import SystemEnum

from typing import Tuple

class TBuildContext:
    """
    Save all data when building.
    """

    BuildOrder: list[str] = []
    ModulePath: list[str] = []
    SkipedModule: list[bool] = []
    BuildedModule: list[bool] = []
    ModuleConfiguration: list[ModuleBase] = []
    TargetConfiguration: TargetBase = TargetBase()
    TargetName: str = ""

    BuildType: BuildTypeEnum = BuildTypeEnum.Debug

    # This is for clangd
    CompileCommands = []

    # For other modules
    SystemType: SystemEnum = SystemEnum.Other
    GccVersion: Tuple[int, int, int] = [0, 0, 1]
    GccVersionStr: str = "0.0.1"
    GxxVersion: Tuple[int, int, int] = [0, 0, 1]
    GxxVersionStr: str = "0.0.1"

BuildContext: TBuildContext = TBuildContext()