# Copyright 2025, LiserverYang. All rights reserved.

from enum import Enum

class BuildTypeEnum(Enum):
    """
    Enumed all kinds of build type.
    """

    # Release is for the user, no debugging informations and enable the optimize
    Release = 0

    # It has the most debug informations and no optimize at all
    Debug = 1

    # It has debug informations, and enable a little optimize to make developers develop easier
    Development = 2