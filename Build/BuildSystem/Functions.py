# Copyright 2025, LiserverYang. All rights reserved.

from .FilePermissionsEnum import FilePermissionsEnum
from .SystemEnum import SystemEnum
from .FileSystem import FileIO
from .BuildContext import BuildContext
from .Config import LLVMConfig

import importlib
import importlib.util
import sys
import subprocess

def GetCurrentSystem() -> SystemEnum:
    """
    Get the kind of current operating system.
    """

    match sys.platform:
        case "win32":
            return SystemEnum.Windows
        case "linux":
            return SystemEnum.Linux
        case "darwin":
            return SystemEnum.MacOS
    
    return SystemEnum.Other

def HasPermissions(PermissionsNumber: int, Permissions: FilePermissionsEnum) -> bool:
    """
    Return if PermissionsNumber has permission, Permissions.
    """
    
    return (PermissionsNumber & Permissions.value) == Permissions.value

def AddPermissions(PermissionsNumber: int, Permissions: FilePermissionsEnum) -> int:
    """
    Add permission, Permissions to PermissionsNumber and return a new PermissionsNumber
    """

    return (PermissionsNumber | Permissions.value)

def GetAllUnits(Folder: FileIO, Suffix: str) -> list[str]:
    """
    Get all units in folder.
    """

    # The result of the function
    Result: list[str] = []

    def helper(path):
        """
        To help search
        """

        tFileIO: FileIO = FileIO(path)

        if not tFileIO.Exists():
            return

        if tFileIO.FileName().endswith("." + Suffix + '.py'):
            Result.append(tFileIO.FilePathStr)
        
        if tFileIO.IsFolder():
            sub_files = tFileIO.GetSubFiles()
            for sub_file in sub_files:
                helper(sub_file)
    
    helper(Folder.FilePathStr)

    # Return value
    return Result

def GetClassFromFileIO(FilePath: FileIO, ClassName: str):
    """
    Get class from file path.
    """

    Spec = importlib.util.spec_from_file_location(ClassName, FilePath.FilePathStr)
    Module = importlib.util.module_from_spec(Spec)
    sys.modules[ClassName] = Module
    Spec.loader.exec_module(Module)

    return getattr(Module, ClassName)

def GetInformations():
    """
    All informations like system type, compiler version.
    """

    BuildContext.SystemType = GetCurrentSystem()
    
    BuildContext.GccVersionStr = subprocess.check_output(["gcc", "--version"]).decode("utf-8").split("\n")[0].split(" ")[-1]
    SplitedGccVersion = BuildContext.GccVersionStr.split(".")
    BuildContext.GccVersion = [SplitedGccVersion[0], SplitedGccVersion[1], SplitedGccVersion[2]]

    BuildContext.GxxVersionStr = subprocess.check_output(["g++", "--version"]).decode("utf-8").split("\n")[0].split(" ")[-1]
    SplitedGxxVersion = BuildContext.GxxVersionStr.split(".")
    BuildContext.GxxVersion = [SplitedGxxVersion[0], SplitedGxxVersion[1], SplitedGxxVersion[2]]

    if not BuildContext.Arguments.donot_build_files:
        LLVMConfig.InitLLVMConfig(BuildContext.Arguments.llvm_position)