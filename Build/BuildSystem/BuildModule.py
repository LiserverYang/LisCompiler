# Copyright 2025, LiserverYang. All rights reserved.

from .BuildContext import BuildContext
from .Logger import Logger
from .LogLevelEnum import LogLevelEnum
from .FileSystem import FileIO
from .BinaryTypeEnum import BinaryTypeEnum
from .Functions import GetCurrentSystem
from .SystemEnum import SystemEnum
from .TestModule import TestModule
from .FormatCheck import CheckFormat
from .ModuleBase import ModuleBase

from typing import List

import subprocess
import concurrent.futures
import os


def BuildModule(ModuleName: str):
    """
    Build a module.

    ATTENTION: the module bust have been discoverd by build system.

    :param ModuleName: the name of the module
    :type ModuleName: str
    """

    # Get the id of module
    ModuleID: int = BuildContext.BuildOrder.index(ModuleName)
    ModuleConfiguation: ModuleBase = BuildContext.ModuleConfiguration[ModuleID]
    TargetName: str = BuildContext.TargetName

    # If we shouldn't build this module, return
    if not ModuleConfiguation.BuildThisModule:
        return

    # Check if anymodule that this module depends on have not built
    for Depend in ModuleConfiguation.ModulesDependOn:
        if not BuildContext.BuildedModule[BuildContext.BuildOrder.index(Depend)]:
            Logger.Log(
                LogLevelEnum.Error,
                "Module '"
                + ModuleName
                + "' depend on module '"
                + Depend
                + "', but it didn't build.",
                True,
                -1,
            )

    Logger.Log(
        LogLevelEnum.Info,
        f"[{ModuleID + 1}/{len(BuildContext.BuildOrder)}] Building module '{ModuleName}'",
    )

    # Get some paths of binary files
    MiddleFilesDir: str = f"./Build/Intermediate/{TargetName}/{ModuleName}"
    BinaryFilesDir: str = "./Build/Binaries"

    try:
        os.makedirs(MiddleFilesDir)
    except:
        # The folder already exsits
        pass

    # The list of every c++ files waiting to build
    WaitCompileCFilesList: list[str] = []
    WaitCompileCppFilesList: list[str] = []

    COFilesList: list[str] = []
    CxxOFilesList: list[str] = []

    def Search(Folder: FileIO):
        """
        Search c and cpp files in Folder.

        :param Folder: the folder to search
        :type Folder: FileIO
        """

        for SubFileStr in Folder.GetSubFiles():
            SubFileFileIO: FileIO = FileIO(SubFileStr)
            if SubFileFileIO.IsFolder():
                Search(SubFileFileIO)
                continue

            # If cpp files
            if (
                SubFileFileIO.EndSwitch() == ".cpp"
                or SubFileFileIO.EndSwitch()
                == ".cc"
            ):
                WaitCompileCppFilesList.append(SubFileFileIO.FilePathStr)
            # If c files
            elif SubFileFileIO.EndSwitch() == ".c":
                WaitCompileCFilesList.append(SubFileFileIO.FilePathStr)

    # Search all .c and .cpp files
    Search(FileIO(os.path.dirname(BuildContext.ModulePath[ModuleID]) + "/Private/"))

    # If a source file has been compiled, skip
    if not BuildContext.Arguments.donot_use_o_files:
        for File in WaitCompileCFilesList + WaitCompileCppFilesList:
            FileFileIO: FileIO = FileIO(File)
            MidFileFileIO: FileIO = FileIO(
                f"{MiddleFilesDir}/{FileIO(File).FileName()}.o"
            )
            if (
                MidFileFileIO.Exists()
                and MidFileFileIO.LastChange()
                > FileFileIO.LastChange()  # Check if it's newer
            ):
                if File in WaitCompileCFilesList:
                    WaitCompileCFilesList.remove(File)
                    COFilesList.append(f"{MiddleFilesDir}/{FileIO(File).FileName()}.o")
                else:
                    WaitCompileCppFilesList.remove(File)
                    CxxOFilesList.append(
                        f"{MiddleFilesDir}/{FileIO(File).FileName()}.o"
                    )

    AllDependsSkiped: bool = True
    TargetExists: bool = True
    PlatFormEndSwitchExe: str = (
        ".exe" if GetCurrentSystem() == SystemEnum.Windows else ""
    )
    PlatFormEndSwitchDy: str = (
        ".dll" if GetCurrentSystem() == SystemEnum.Windows else ".so"
    )

    for Depend in ModuleConfiguation.ModulesDependOn:
        if not BuildContext.SkipedModule[BuildContext.BuildOrder.index(Depend)]:
            AllDependsSkiped = False
            break

    # Check if the target exists
    if ModuleConfiguation.BinaryType == BinaryTypeEnum.EntryPoint:
        TargetExists = FileIO(
            f"./Build/Binaries/{ModuleName}{PlatFormEndSwitchExe}"
        ).Exists()
    elif ModuleConfiguation.BinaryType == BinaryTypeEnum.DynamicLib:
        if ModuleConfiguation.EnableBinaryLibPrefix:
            TargetExists = FileIO(
                f"./Build/Binaries/{TargetName}-{ModuleName}{PlatFormEndSwitchDy}"
            ).Exists()
        else:
            TargetExists = FileIO(
                f"./Build/Binaries/{ModuleName}{PlatFormEndSwitchDy}"
            ).Exists()
    else:
        if ModuleConfiguation.EnableBinaryLibPrefix:
            TargetExists = FileIO(
                f"./Build/Binaries/{TargetName}-{ModuleName}.a"
            ).Exists()
        else:
            TargetExists = FileIO(f"./Build/Binaries/{ModuleName}.a").Exists()

    # And if everything is compiled, skip this module building
    if ModuleConfiguation.AutoSkiped or (
        len(WaitCompileCFilesList) == len(WaitCompileCppFilesList) == 0
        and AllDependsSkiped
        and TargetExists
    ):
        BuildContext.BuildedModule[ModuleID] = True
        BuildContext.SkipedModule[ModuleID] = True
        return

    if (
        ModuleConfiguation.EnableFormatCheck
        and BuildContext.Arguments.enable_format_check
    ):
        for file in WaitCompileCppFilesList:
            if CheckFormat(file) != 0:
                Logger.Log(
                    LogLevelEnum.Error,
                    f"Format check faild in file {file}, see log for detailed informations",
                    True,
                    1,
                )

    # Start to compile files

    # Some variables in configuation
    CStanderd: str = ModuleConfiguation.CStanderd
    CxxStanderd: str = ModuleConfiguation.CxxStanderd
    ModuleAddedArguments: str = " ".join(ModuleConfiguation.ArgumentsAdded)
    TargetAddedArguments: str = " ".join(
        BuildContext.TargetConfiguration.ArgumentsAdded
    )
    IncludePaths: str = " -I".join(
        [
            os.path.abspath(
                os.path.dirname(
                    BuildContext.ModulePath[BuildContext.BuildOrder.index(depend)]
                )
                + "/Public/"
            )
            for depend in ModuleConfiguation.ModulesDependOn + [ModuleName]
        ]
    )
    DependsModules: List[str] = []

    for name in ModuleConfiguation.ModulesDependOn:
        if BuildContext.ModuleConfiguration[
            BuildContext.BuildOrder.index(name)
        ].LinkThisModule:
            if BuildContext.ModuleConfiguration[
                BuildContext.BuildOrder.index(name)
            ].EnableBinaryLibPrefix:
                DependsModules.append(TargetName + "-" + name)
            else:
                DependsModules.append(name)

    LinkDependsStr: str = ("-l" if len(DependsModules) > 0 else "") + " -l".join(
        DependsModules
    )
    LibPrefix: str = (
        f"{TargetName}-"
        if BuildContext.ModuleConfiguration[ModuleID].EnableBinaryLibPrefix
        else ""
    )

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

    CompileCommands: List[str] = []

    for CFile in WaitCompileCFilesList:
        TargetFileName: str = f"{MiddleFilesDir}/{FileIO(CFile).FileName()}.o"
        COFilesList.append(TargetFileName)
        BuildCommand: str = (
            f"gcc {CFile} -o {TargetFileName} -std={CStanderd} {ModuleAddedArguments} {TargetAddedArguments} -I{IncludePaths} -c"
        )
        BuildContext.CompileCommands.append(TransformCommand(BuildCommand, CFile))
        CompileCommands.append(BuildCommand)

    for CppFile in WaitCompileCppFilesList:
        TargetFileName: str = f"{MiddleFilesDir}/{FileIO(CppFile).FileName()}.o"
        CxxOFilesList.append(TargetFileName)
        BuildCommand: str = (
            f"g++ {CppFile} -o {TargetFileName} -std={CxxStanderd} {ModuleAddedArguments} {TargetAddedArguments} -I{IncludePaths} -c"
        )
        BuildContext.CompileCommands.append(TransformCommand(BuildCommand, CppFile))
        CompileCommands.append(BuildCommand)

    if not BuildContext.Arguments.donot_build_files and CompileCommands:

        def RunCompileCommand(cmd):
            """
            Execute single command and check the result
            """

            result = subprocess.run(
                cmd,
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            if result.returncode != 0:
                return (False, cmd, result.stderr)
            return (True, cmd, "")

        with concurrent.futures.ThreadPoolExecutor(
            max_workers=BuildContext.Arguments.threads
        ) as executor:
            futures = {
                executor.submit(RunCompileCommand, cmd): cmd for cmd in CompileCommands
            }

            for future in concurrent.futures.as_completed(futures):
                success, cmd, error = future.result()
                if not success:
                    # Cancle all not finished job
                    for f in futures:
                        f.cancel()
                    print(error.replace("\n\n", "\n"))
                    Logger.Log(
                        LogLevelEnum.Error,
                        f"Compile faild when running command {cmd}, see error in the log.",
                        True,
                        -1,
                    )

    if BuildContext.Arguments.donot_build_files:
        BuildContext.BuildedModule[ModuleID] = True
        return

    if ModuleConfiguation.BinaryType == BinaryTypeEnum.EntryPoint:
        # Build executeable
        link_command = f"g++ {' '.join(COFilesList)} {' '.join(CxxOFilesList)} -o {BinaryFilesDir}/{TargetName} -L./Build/Binaries/ {LinkDependsStr} {ModuleAddedArguments} {TargetAddedArguments}"
        BuildResult = os.system(link_command)
    elif ModuleConfiguation.BinaryType == BinaryTypeEnum.DynamicLib:
        # Build dynamic lib
        link_command = f"g++ {' '.join(COFilesList)} {' '.join(CxxOFilesList)} -o {BinaryFilesDir}/{LibPrefix}{ModuleName}.dll -L./Build/Binaries/ {LinkDependsStr} -fPIC -shared {ModuleAddedArguments} {TargetAddedArguments}"
        BuildResult = os.system(link_command)
    elif ModuleConfiguation.BinaryType == BinaryTypeEnum.StaticLib:
        # Build static lib
        link_command = f"ar rcs {BinaryFilesDir}/lib{LibPrefix}{ModuleName}.a {' '.join(COFilesList)} {' '.join(CxxOFilesList)}"
        for name in ModuleConfiguation.ModulesDependOn:
            Configuation: ModuleBase = BuildContext.ModuleConfiguration[
                BuildContext.BuildOrder.index(name)
            ]
            if (
                Configuation.LinkThisModule
                and Configuation.BinaryType == BinaryTypeEnum.StaticLib
            ):
                if BuildContext.ModuleConfiguration[
                    BuildContext.BuildOrder.index(name)
                ].EnableBinaryLibPrefix:
                    link_command += f" {BinaryFilesDir}/lib{TargetName}-{name}.a"
                else:
                    link_command += f" {BinaryFilesDir}/lib{name}.a"
        BuildResult = os.system(link_command)

    if BuildResult == 0:
        BuildContext.BuildedModule[ModuleID] = True
    else:
        Logger.Log(
            LogLevelEnum.Error,
            f"There's something error when build module '{ModuleName}' in target '{TargetName}', and the compiler return value '{BuildResult}' not 0.",
            True,
            -1,
        )

    # Run test
    if BuildContext.Arguments.enable_tests and ModuleConfiguation.EnableTests:
        TestModule(
            ModuleName,
            os.path.dirname(BuildContext.ModulePath[ModuleID]),
            CxxOFilesList,
            f"{ModuleAddedArguments} {TargetAddedArguments} -I{IncludePaths} -L ./Build/Binaries/ {LinkDependsStr} -l{LibPrefix}{ModuleName}",
        )
