# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

class CompilerModule(BuildSystem.ModuleBase):
    """
    """

    def Configuration(self) -> None:
        """
        """

        self.BinaryType = BuildSystem.BinaryTypeEnum.StaticLib
        self.ModulesDependOn = ["Gtest"]
        self.EnableBinaryLibPrefix = False
        self.EnableTests = True