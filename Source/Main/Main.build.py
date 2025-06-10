# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

class MainModule(BuildSystem.ModuleBase):
    """
    """

    def Configuration(self) -> None:
        """
        """

        self.BinaryType = BuildSystem.BinaryTypeEnum.EntryPoint
        self.ModulesDependOn = ["llvm", "Compiler", "Gtest"]
        self.ArgumentsAdded = [BuildSystem.Config.LLVMConfig.LLVMCommand]