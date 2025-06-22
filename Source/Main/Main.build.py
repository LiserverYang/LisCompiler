# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

class MainModule(BuildSystem.ModuleBase):
    """
    """

    def Configuration(self) -> None:
        """
        """

        self.BinaryType = BuildSystem.BinaryTypeEnum.EntryPoint
        self.ModulesDependOn = ["Compiler", "Gtest"]
        self.ArgumentsAdded = []