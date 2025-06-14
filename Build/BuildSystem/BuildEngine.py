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
import argparse

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

    # Get arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-type', help='The build type of engine (Release, Debug or Development)', choices=["Release", "Debug", "Development"])
    parser.add_argument('--donot-build-files', help='If enabled, the build system will not execute the compile/link command, but something like format check will be executed', action="store_true")
    parser.add_argument('--donot-use-o-files', help='If enabled, the build system will not use cache (.o files) to build module', action="store_true")
    parser.add_argument('--enable-tests', help='If enabled, the build system will execute the unit test with google test (all test file should be at ModuolePath/Test/**)', action="store_true")
    parser.add_argument('--enable-format-check', help='If enabled, the build system will check the code format with clang-format', action="store_true")
    BuildContext.Arguments = parser.parse_args()

    # Get Build type
    match BuildContext.Arguments.build_type:
        case "Debug":
            BuildContext.BuildType = BuildTypeEnum.Debug
        case "Release":
            BuildContext.BuildType = BuildTypeEnum.Release
        case "Development":
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