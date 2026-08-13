# Copyright 2026, LiserverYang. All rights reserved.

from Build import BuildSystem

class LspModule(BuildSystem.ModuleBase):
    """
    The LSP language server (lisls.exe): stdin/stdout JSON-RPC over the
    compiler's Lexer/Parser/sema (libCompiler.a).
    """

    def Configuration(self) -> None:
        """
        """

        self.BinaryType = BuildSystem.BinaryTypeEnum.EntryPoint
        self.ModulesDependOn = ["Compiler"]
        self.ArgumentsAdded = [BuildSystem.Config.LLVMConfig.LLVMCommand]
