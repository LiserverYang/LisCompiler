# Copyright 2025, LiserverYang. All rights reserved.

from enum import Enum

class BinaryTypeEnum(Enum):
    """
    Enumed all types of module target binaries.
    """

    DynamicLib = 1
    StaticLib = 2
    EntryPoint = 3