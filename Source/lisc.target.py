# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

class liscTarget(BuildSystem.TargetBase):
    """
    The target of compiler.
    """

    def Configuration(self) -> None:
        """
        Config renderer's config.
        """

        self.TargetType = BuildSystem.TargetTypeEnum.Program
        self.bBuildAllmodules = True
        self.ModulesSubFolder = [""]
        self.ArgumentsAdded = ["-std=c++20", "-Wno-deprecated-declarations", "-Wno-deprecated-enum-enum-conversion", "-finput-charset=UTF-8", "-fexec-charset=UTF-8", "-DUNICODE"]

        match BuildSystem.BuildContext.BuildType:
            case BuildSystem.BuildTypeEnum.Release:
                self.ArgumentsAdded += ["-O3", "-D__RELEASE__"]
            case BuildSystem.BuildTypeEnum.Debug:
                self.ArgumentsAdded += ["-O0", "-g", "-D__DEBUG__"]
            case BuildSystem.BuildTypeEnum.Development:
                # Developemt is also a kind of debug
                self.ArgumentsAdded += ["-O1", "-g", "-D__DEBUG__"]