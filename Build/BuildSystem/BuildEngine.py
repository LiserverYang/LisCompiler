# Copyright 2025, LiserverYang. All rights reserved.

from .FileSystem import FileIO
from .Logger import Logger
from .LogLevelEnum import LogLevelEnum
from .BuildTarget import BuildTarget
from .Functions import GetInformations
from .GenericJson import GenericJson
from .BuildContext import BuildContext
from .TimeSolver import FormatDuration
from .BuildTypeEnum import BuildTypeEnum
from typing import List

import sys
import time

def BuildEngine(SourceFolder: FileIO, TargetList: List[str]) -> None:
    """
    Build the engine
    """

    # Do some checks
    if not SourceFolder.Exists():
        Logger.Log(LogLevelEnum.Error, "Could not found Source folder, please check your engine source.", True, -1)

    if not SourceFolder.IsFolder():
        Logger.Log(LogLevelEnum.Error, "The source is not a folder, please check your engine source.", True, -1)

    Logger.Log(LogLevelEnum.Info, f"Python version {sys.version}")

    # Get Build type
    if "--build-type=Release" in sys.argv:
        BuildContext.BuildType = BuildTypeEnum.Release

    elif "--build-type=Debug" in sys.argv:
        BuildContext.BuildType = BuildTypeEnum.Debug

    elif "--build-type=Development" in sys.argv:
        BuildContext.BuildType = BuildTypeEnum.Development

    Logger.Log(LogLevelEnum.Info ,f"Build type is {BuildContext.BuildType.name}.")

    GetInformations()

    Logger.Log(LogLevelEnum.Info, "Reading all targets.")

    # Start timing
    StartTime = time.time()

    Logger.Log(LogLevelEnum.Info, "Found target: " + ", ".join(TargetList))

    for target in TargetList:
        BuildTarget(FileIO(target))

    # For clangd, we generic some files
    GenericJson(BuildContext.CompileCommands)

    Logger.Log(LogLevelEnum.Info, f"Build done. Use time toal: {FormatDuration(time.time() - StartTime)}")