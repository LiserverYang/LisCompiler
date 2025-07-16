# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

import shutil

class GtestModule(BuildSystem.ModuleBase):
    """
    """

    def Configuration(self) -> None:
        """
        """

        self.BinaryType = BuildSystem.BinaryTypeEnum.StaticLib
        self.LinkThisModule = True
        self.EnableBinaryLibPrefix = False
        self.ModulesDependOn = []
        self.ArgumentsAdded = ["-I./Source/Gtest/Private"]
        self.EnableFormatCheck = False

        if BuildSystem.BuildContext.BuildType == BuildSystem.BuildTypeEnum.Release:
            self.AutoSkiped = True
            self.LinkThisModule = False

        # You can enable this argument when you have had a build
        # self.AutoSkiped = True