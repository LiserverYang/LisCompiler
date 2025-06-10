# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

import subprocess

class llvmModule(BuildSystem.ModuleBase):
    """
    """

    def Configuration(self) -> None:
        """
        """

        self.BinaryType = BuildSystem.BinaryTypeEnum.DynamicLib
        self.LinkThisModule = False # Add link command yourself
        self.AutoSkiped = True