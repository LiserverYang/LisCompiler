# Copyright 2025, LiserverYang. All rights reserved.

from .BinaryTypeEnum import BinaryTypeEnum

class ModuleBase:
    """
    The base class of all modules.
    """

    # Dynamic or Static
    BinaryType: BinaryTypeEnum = BinaryTypeEnum.DynamicLib

    # Is or not build this module
    bBuildThisModule: bool = True
    
    # All modules despend on
    ModulesDependOn: list[str] = []

    CStanderd: str = "c2x"
    CxxStanderd: str = "c++20"

    ArgumentsAdded: list[str] = []

    AutoSkiped = False
    LinkThisModule = True

    EnableReflectionGeneric = False
    EnableBinaryLibPrefix = True
    EnableTests = False

    def Configuration(self) -> None:
        """
        Config all settings.
        Called after the function __init__.
        """

        pass