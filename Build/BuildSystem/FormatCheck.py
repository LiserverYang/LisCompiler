# Copyright 2025, LiserverYang. All rights reserved.

import os

def CheckFormat(FileName: str) -> int:
    return os.system(f"clang-format --Werror --fail-on-incomplete-format --dry-run {FileName}")