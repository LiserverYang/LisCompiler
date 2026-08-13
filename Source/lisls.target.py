# Copyright 2026, LiserverYang. All rights reserved.

from Build import BuildSystem

class lislsTarget(BuildSystem.TargetBase):
    """
    The target of the LSP language server.
    """

    def Configuration(self) -> None:
        """
        Config lisls's config.
        """

        self.TargetType = BuildSystem.TargetTypeEnum.Program
        # Only the Lsp module and its transitive deps (Compiler → Gtest +
        # MagicEnum); the shared archives are rebuilt idempotently.
        self.bBuildAllmodules = False
        self.BuildModulesList = ["Gtest", "MagicEnum", "Compiler", "Lsp"]
        self.ModulesSubFolder = [""]
        self.ArgumentsAdded = ["-std=c++20", "-Wno-deprecated-declarations", "-Wno-deprecated-enum-enum-conversion", "-finput-charset=UTF-8", "-fexec-charset=UTF-8", "-DUNICODE", "-fdiagnostics-color=always"]

        match BuildSystem.BuildContext.BuildType:
            case BuildSystem.BuildTypeEnum.Release:
                self.ArgumentsAdded += ["-O3", "-D__RELEASE__"]
            case BuildSystem.BuildTypeEnum.Debug:
                self.ArgumentsAdded += ["-O0", "-g", "-D__DEBUG__"]
            case BuildSystem.BuildTypeEnum.Development:
                # Developemt is also a kind of debug
                self.ArgumentsAdded += ["-O1", "-g", "-D__DEBUG__"]
