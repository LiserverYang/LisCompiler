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
from .Config.LLVMConfig import InitLLVMConfig
from typing import List

import sys
import time
import argparse


def BuildApp(SourceFolder: FileIO, TargetList: List[str]) -> None:
    """
    Build the application.
    
    :param SourceFolder: the folder's FILEIO where stored source file
    :type SourceFolder: FileIO
    :param TargetList: a list of target's configuation file path(*.target.py), build system will build the target in the order of the list.
    :type TargetList: List[str]
    
    For example, if can call it:

    ```BuildApp(FileIO("./Source/"), ["./Source/project.target.py", "./Source/Plugins/plugin_api.target.py"])
    """

    # Do some checks
    if not SourceFolder.Exists():
        Logger.Log(
            LogLevelEnum.Error,
            "Could not found Source folder, please check your source.",
            True,
            -1,
        )

    if not SourceFolder.IsFolder():
        Logger.Log(
            LogLevelEnum.Error,
            "The source is not a folder, please check your source.",
            True,
            -1,
        )

    # Get arguments
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-type",
        help="The build type of application (Release, Debug or Development)",
        choices=["Release", "Debug", "Development"],
    )
    parser.add_argument(
        "--donot-build-files",
        help="If enabled, the build system will not execute the compile/link command, but something like format check will be executed",
        action="store_true",
    )
    parser.add_argument(
        "--donot-use-o-files",
        help="If enabled, the build system will not use cache (.o files) to build module",
        action="store_true",
    )
    parser.add_argument(
        "--enable-tests",
        help="If enabled, the build system will execute the unit test with google test (all test file should be at ModuolePath/Test/**)",
        action="store_true",
    )
    parser.add_argument(
        "--enable-format-check",
        help="If enabled, the build system will check the code format with clang-format",
        action="store_true",
    )
    parser.add_argument(
        "--llvm-position",
        help="The position of llvm.",
        default="",
    )
    parser.add_argument(
        "--donot-generic-cc-json",
        help="If eanbled, the build system will not generate compile_commands.json in the root folder.",
        action="store_true"
    )
    parser.add_argument("--threads", help="Set the thread number", type=int, default=1)
    BuildContext.Arguments = parser.parse_args()

    if BuildContext.Arguments.llvm_position != "":
        InitLLVMConfig(BuildContext.Arguments.llvm_position)

    Logger.Log(LogLevelEnum.Info, f"Python version {sys.version}")

    # Get Build type
    match BuildContext.Arguments.build_type:
        case "Debug":
            BuildContext.BuildType = BuildTypeEnum.Debug
        case "Release":
            BuildContext.BuildType = BuildTypeEnum.Release
        case "Development":
            BuildContext.BuildType = BuildTypeEnum.Development

    Logger.Log(LogLevelEnum.Info, f"Build type is {BuildContext.BuildType.name}.")

    GetInformations()

    Logger.Log(LogLevelEnum.Info, f"System is {BuildContext.SystemType.name}.")

    Logger.Log(LogLevelEnum.Info, "Reading all targets.")

    # Start timing
    StartTime = time.time()

    Logger.Log(LogLevelEnum.Info, "Found target: " + ", ".join(TargetList))

    for target in TargetList:
        BuildTarget(FileIO(target))

    # For clangd, we generic some files
    GenericJson(BuildContext.CompileCommands)

    Logger.Log(
        LogLevelEnum.Info,
        f"Build done. Use time in toal: {FormatDuration(time.time() - StartTime)}",
    )
