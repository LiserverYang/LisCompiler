# Copyright 2025, LiserverYang. All rights reserved.

from Build import BuildSystem

import os
import sys
import shutil

class StdModule(BuildSystem.ModuleBase):
    """ """

    def Configuration(self) -> None:
        """ """

        self.BinaryType = BuildSystem.BinaryTypeEnum.StaticLib
        self.EnableBinaryLibPrefix = False
        self.EnableTests = False
        self.AutoSkiped = True

        src_dir = os.path.dirname(os.path.abspath(__file__))

        script_name = os.path.basename(__file__)

        dst_dir = "./Build/Binaries/lstdlib/"

        os.makedirs(dst_dir, exist_ok=True)

        def ignore_self(dirname, names):
            if dirname == src_dir:
                return [script_name] if script_name in names else []
            return []

        shutil.copytree(src_dir, dst_dir, ignore=ignore_self, dirs_exist_ok=True)