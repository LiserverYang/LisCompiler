# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

class MagicEnumModule(BuildSystem.ModuleBase):
    """
    """

    def Configuration(self) -> None:
        """
        """

        self.AutoSkiped = True
        self.LinkThisModule = False
        # Vendored header-only library: never reformat/check it with our
        # clang-format (it would drift from upstream).
        self.EnableFormatCheck = False