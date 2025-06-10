# Copyright 2025, LiserverYang. All rights reserved.

from .TargetTypeEnum import TargetTypeEnum

class TargetBase:
    """
    The base class of all targets, to give api for LBS.
    """

    # The target type, enumed by TargetTypeEnum
    TargetType: TargetTypeEnum = TargetTypeEnum.Program

    # If you wanna build all moudules
    bBuildAllmodules: bool = True
    bBuildThisTarget: bool = True

    ModulesSubFolder: list[str] = []

    # Only can chenge it when bBuildAllmodules = False
    BuildModulesList: list[str] = []

    ArgumentsAdded: list[str] = []

    def Configuration(self) -> None:
        """
        Config all settings.
        Called after the function __init__.
        """

        pass
