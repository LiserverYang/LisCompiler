# Copyright 2025, LiserverYang. All rights reserved.

from enum import Enum

class LogLevelEnum(Enum):
    """
    Enumed levels(info, warning, error) for the LBS's log system.
    """

    Info = 1
    Warning = 2
    Error = 3