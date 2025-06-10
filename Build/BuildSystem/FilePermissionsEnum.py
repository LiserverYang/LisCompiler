# Copyright 2025, LiserverYang. All rights reserved.

from enum import Enum

class FilePermissionsEnum(Enum):
    """
    Enumed file permissions(read, write, run) for the LBS's log system.
    """

    Read  = 0b001
    Write = 0b010
    Run   = 0b100
