# Copyright 2025, LiserverYang. All rights reserved.

from .BuildContext import BuildContext
from .Logger import Logger
from .LogLevelEnum import LogLevelEnum
from .FileSystem import FileIO
from .TargetTypeEnum import TargetTypeEnum
from .BinaryTypeEnum import BinaryTypeEnum
from .Functions import GetCurrentSystem
from .SystemEnum import SystemEnum
from .TestModule import TestModule
from .FormatCheck import CheckFormat

from typing import List

import os
import sys


def BuildModule(ModuleName: str):
    """
    Build a module
    """

    ModuleID: int = BuildContext.BuildOrder.index(ModuleName)

    # If we shouldn't build this module, return
    if not BuildContext.ModuleConfiguration[ModuleID].bBuildThisModule:
        return

    # And if anymodule that this module depends on did not build, report error
    for Depend in BuildContext.ModuleConfiguration[ModuleID].ModulesDependOn:
        if not BuildContext.BuildedModule[BuildContext.BuildOrder.index(Depend)]:
            Logger.Log(LogLevelEnum.Error, "Module '" + ModuleName +
                       "' depend on module '" + Depend + "', but it didn't build.", True, -1)

    Logger.Log(LogLevelEnum.Info,
               f"[{ModuleID + 1}/{len(BuildContext.BuildOrder)}] Building module '{ModuleName}'")

    # Make file dir
    MiddleFilesDir: str = f"./Build/Intermediate/{BuildContext.TargetName}/{ModuleName}"
    BinaryFilesDir: str = "./Build/Binaries"

    try:
        os.makedirs(MiddleFilesDir)
    except:
        # The folder exsits
        pass

    WaitCompileCFilesList: list[str] = []
    WaitCompileCppFilesList: list[str] = []

    COFilesList: list[str] = []
    CxxOFilesList: list[str] = []

    def Search(Folder: FileIO):
        """
        Help to search files
        """

        for SubFileStr in Folder.GetSubFiles():
            SubFileFileIO: FileIO = FileIO(SubFileStr)
            if SubFileFileIO.IsFolder():
                Search(SubFileFileIO)
            else:
                if SubFileFileIO.EndSwitch() == ".cpp" or SubFileFileIO.EndSwitch() == ".cc":
                    WaitCompileCppFilesList.append(SubFileFileIO.FilePathStr)
                elif SubFileFileIO.EndSwitch() == ".c":
                    WaitCompileCFilesList.append(SubFileFileIO.FilePathStr)

    # Search all .c and .cpp files
    Search(FileIO(os.path.dirname(
        BuildContext.ModulePath[ModuleID]) + "/Private/"))

    # If this file has been compiled, skip
    if not BuildContext.Arguments.donot_use_o_files:
        for File in WaitCompileCFilesList + WaitCompileCppFilesList:
            FileFileIO: FileIO = FileIO(File)
            MidFileFileIO: FileIO = FileIO(
                f"{MiddleFilesDir}/{FileIO(File).FileName()}.o")
            if MidFileFileIO.Exists() and MidFileFileIO.LastChange() > FileFileIO.LastChange():
                if File in WaitCompileCFilesList:
                    WaitCompileCFilesList.remove(File)
                    COFilesList.append(
                        f"{MiddleFilesDir}/{FileIO(File).FileName()}.o")
                else:
                    WaitCompileCppFilesList.remove(File)
                    CxxOFilesList.append(
                        f"{MiddleFilesDir}/{FileIO(File).FileName()}.o")

    AllDependsSkiped: bool = True
    TargetExists: bool = True
    PlatFormEndSwitchExe: str = ".exe" if GetCurrentSystem() == SystemEnum.Windows else ""
    PlatFormEndSwitchDy: str = ".dll" if GetCurrentSystem() == SystemEnum.Windows else ".so"

    for Depend in BuildContext.ModuleConfiguration[ModuleID].ModulesDependOn:
        if not BuildContext.SkipedModule[BuildContext.BuildOrder.index(Depend)]:
            AllDependsSkiped = False
            break

    if BuildContext.ModuleConfiguration[ModuleID].BinaryType == BinaryTypeEnum.EntryPoint:
        TargetExists = FileIO(
            f"./Build/Binaries/{ModuleName}{PlatFormEndSwitchExe}").Exists()
    elif BuildContext.ModuleConfiguration[ModuleID].BinaryType == BinaryTypeEnum.DynamicLib:
        TargetExists = FileIO(
            f"./Build/Binaries/{BuildContext.TargetName}-{ModuleName}{PlatFormEndSwitchDy}").Exists()
    else:
        TargetExists = FileIO(
            f"./Build/Binaries/{BuildContext.TargetName}-{ModuleName}.a").Exists()

    # And if everything is compiled, skip this module building
    if BuildContext.ModuleConfiguration[ModuleID].AutoSkiped or (len(WaitCompileCFilesList) == len(WaitCompileCppFilesList) == 0 and AllDependsSkiped and TargetExists):
        BuildContext.BuildedModule[ModuleID] = True
        BuildContext.SkipedModule[ModuleID] = True
        return
    
    if BuildContext.ModuleConfiguration[ModuleID].EnableFormatCheck and BuildContext.Arguments.enable_format_check:
        for file in WaitCompileCppFilesList:
            if CheckFormat(file) != 0:
                Logger.Log(LogLevelEnum.Error, f"Format check faild in file {file}, see log for detailed informations", True, 1)

    # Start to compile files
    CStanderd: str = BuildContext.ModuleConfiguration[ModuleID].CStanderd
    CxxStanderd: str = BuildContext.ModuleConfiguration[ModuleID].CxxStanderd
    ModuleAddedArguments: str = ' '.join(
        BuildContext.ModuleConfiguration[ModuleID].ArgumentsAdded)
    TargetAddedArguments: str = ' '.join(
        BuildContext.TargetConfiguration.ArgumentsAdded)
    IncludePaths: str = ' -I'.join([os.path.abspath(os.path.dirname(BuildContext.ModulePath[BuildContext.BuildOrder.index(
        depend)]) + "/Public/") for depend in BuildContext.ModuleConfiguration[ModuleID].ModulesDependOn + [ModuleName]])
    DependsModules: List[str] = []

    for name in BuildContext.ModuleConfiguration[ModuleID].ModulesDependOn:
        if BuildContext.ModuleConfiguration[BuildContext.BuildOrder.index(name)].LinkThisModule:
            if BuildContext.ModuleConfiguration[BuildContext.BuildOrder.index(name)].EnableBinaryLibPrefix:
                DependsModules.append(BuildContext.TargetName + "-" + name)
            else:
                DependsModules.append(name)

    LinkDependsStr: str = ("-l" if len(DependsModules) >
                           0 else "") + " -l".join(DependsModules)
    LibPrefix: str = f"{BuildContext.TargetName}-" if BuildContext.ModuleConfiguration[
        ModuleID].EnableBinaryLibPrefix else ""

    BuildResult: int = 0

    def TransformCommand(BuildCommand: str, SourceName: str) -> dict:
        """
        Transform build command to a dictionary that can be read by clangd.
        """
        ResultValue: dict = {}

        ResultValue["file"] = FileIO(SourceName).FileName()
        ResultValue["directory"] = os.path.abspath(os.path.dirname(SourceName))
        ResultValue["arguments"] = ["clang++"] + BuildCommand.split(" ")[1:-1]

        return ResultValue

    # Build all source to .o file
    for CFile in WaitCompileCFilesList:
        TargetFileName: str = f"{MiddleFilesDir}/{FileIO(CFile).FileName()}.o"
        COFilesList.append(TargetFileName)
        BuildCommand: str = f"gcc {CFile} -o {TargetFileName} -std={CStanderd} {ModuleAddedArguments} {TargetAddedArguments} -I{IncludePaths} -c"
        BuildContext.CompileCommands.append(
            TransformCommand(BuildCommand, CFile))
        if not BuildContext.Arguments.donot_build_files:
            os.system(BuildCommand)

    for CppFile in WaitCompileCppFilesList:
        TargetFileName: str = f"{MiddleFilesDir}/{FileIO(CppFile).FileName()}.o"
        CxxOFilesList.append(TargetFileName)
        BuildCommand: str = f"g++ {CppFile} -o {TargetFileName} -std={CxxStanderd} {ModuleAddedArguments} {TargetAddedArguments} -I{IncludePaths} -c"
        BuildContext.CompileCommands.append(
            TransformCommand(BuildCommand, CppFile))
        if not BuildContext.Arguments.donot_build_files:
            os.system(BuildCommand)

    if  BuildContext.Arguments.donot_build_files:
        BuildContext.BuildedModule[ModuleID] = True
        return

    if BuildContext.ModuleConfiguration[ModuleID].BinaryType == BinaryTypeEnum.EntryPoint:
        # Build executeable
        link_command = f"g++ {' '.join(COFilesList)} {' '.join(CxxOFilesList)} -o {BinaryFilesDir}/{BuildContext.TargetName} -L./Build/Binaries/ {LinkDependsStr} {ModuleAddedArguments} {TargetAddedArguments}"
        BuildResult = os.system(link_command)
    elif BuildContext.ModuleConfiguration[ModuleID].BinaryType == BinaryTypeEnum.DynamicLib:
        # Build dynamic lib
        link_command = f"g++ {' '.join(COFilesList)} {' '.join(CxxOFilesList)} -o {BinaryFilesDir}/{LibPrefix}{ModuleName}.dll -L./Build/Binaries/ {LinkDependsStr} -fPIC -shared {ModuleAddedArguments} {TargetAddedArguments}"
        BuildResult = os.system(link_command)
    elif BuildContext.ModuleConfiguration[ModuleID].BinaryType == BinaryTypeEnum.StaticLib:
        # Build static lib
        link_command = f"ar rcs {BinaryFilesDir}/lib{LibPrefix}{ModuleName}.a {' '.join(COFilesList)} {' '.join(CxxOFilesList)}"
        for depend in DependsModules:
            if BuildContext.ModuleConfiguration[BuildContext.BuildOrder.index(depend)].BinaryType == BinaryTypeEnum.StaticLib:
                link_command += f" {BinaryFilesDir}/lib{depend}.a"
        BuildResult = os.system(link_command)

    if BuildResult == 0:
        BuildContext.BuildedModule[ModuleID] = True
    else:
        Logger.Log(LogLevelEnum.Error,
                   f"There's something error when build module '{ModuleName}' in target '{BuildContext.TargetName}', and the compiler return value '{BuildResult}' not 0.", True, -1)

        # Run test
    if BuildContext.Arguments.enable_tests and BuildContext.ModuleConfiguration[ModuleID].EnableTests:
        TestModule(ModuleName, os.path.dirname(BuildContext.ModulePath[ModuleID]), CxxOFilesList, f"{ModuleAddedArguments} {TargetAddedArguments} -I{IncludePaths} -L ./Build/Binaries/ {LinkDependsStr} -l{LibPrefix}{ModuleName}")