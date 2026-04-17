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

from typing import List, Tuple

import subprocess
import concurrent.futures
import hashlib
import os
import sys
import threading


# --------------------------------------------------------------------------- #
# Transient status line
# --------------------------------------------------------------------------- #
#
# A single "bottom line" that is overwritten every time a new source file
# starts compiling, the way ninja / cmake show progress. Writes go through
# a lock because the compile pool is multi-threaded.

_StatusLock = threading.Lock()
_StatusLastLen: int = 0


def _PrintStatus(Text: str) -> None:
    """Overwrite the transient status line with ``Text``."""
    global _StatusLastLen
    with _StatusLock:
        Padding = max(0, _StatusLastLen - len(Text))
        sys.stdout.write("\r" + Text + (" " * Padding))
        sys.stdout.flush()
        _StatusLastLen = len(Text)


def _ClearStatus() -> None:
    """Erase the transient status line so the next real print starts clean."""
    global _StatusLastLen
    with _StatusLock:
        if _StatusLastLen > 0:
            sys.stdout.write("\r" + (" " * _StatusLastLen) + "\r")
            sys.stdout.flush()
            _StatusLastLen = 0


# --------------------------------------------------------------------------- #
# Short hash (used to disambiguate same-named sources in one module)
# --------------------------------------------------------------------------- #


def _ShortHash(Text: str, Length: int = 8) -> str:
    return hashlib.md5(Text.encode("utf-8")).hexdigest()[:Length]


# --------------------------------------------------------------------------- #
# Platform helpers
# --------------------------------------------------------------------------- #


def _ExecutableSuffix() -> str:
    return ".exe" if GetCurrentSystem() == SystemEnum.Windows else ""


def _DynamicLibSuffix() -> str:
    return ".dll" if GetCurrentSystem() == SystemEnum.Windows else ".so"


# --------------------------------------------------------------------------- #
# Source discovery
# --------------------------------------------------------------------------- #


def _CollectSourceFiles(RootFolder: FileIO) -> Tuple[List[str], List[str]]:
    """
    Recursively collect C and C++ source files under ``RootFolder``.

    :returns: ``(c_files, cpp_files)``
    """
    CFiles: List[str] = []
    CppFiles: List[str] = []

    def Walk(Folder: FileIO) -> None:
        for SubFileStr in Folder.GetSubFiles():
            SubFile: FileIO = FileIO(SubFileStr)
            if SubFile.IsFolder():
                Walk(SubFile)
                continue

            Ext = SubFile.EndsWith()
            if Ext == ".cpp" or Ext == ".cc":
                CppFiles.append(SubFile.FilePathStr)
            elif Ext == ".c":
                CFiles.append(SubFile.FilePathStr)

    Walk(RootFolder)
    return CFiles, CppFiles


# --------------------------------------------------------------------------- #
# Intermediate file paths
# --------------------------------------------------------------------------- #


def _ObjectFilePath(MiddleFilesDir: str, SourceFile: str) -> str:
    Base = FileIO(SourceFile).FileName()
    Hash = _ShortHash(os.path.abspath(SourceFile))
    return f"{MiddleFilesDir}/{Base}.{Hash}.o"


def _DependencyFilePath(MiddleFilesDir: str, SourceFile: str) -> str:
    Base = FileIO(SourceFile).FileName()
    Hash = _ShortHash(os.path.abspath(SourceFile))
    return f"{MiddleFilesDir}/{Base}.{Hash}.d"


# --------------------------------------------------------------------------- #
# Header-dependency tracking
# --------------------------------------------------------------------------- #


def _ParseDependencyFile(DepFilePath: str) -> List[str]:
    """
    Parse a ``.d`` dependency file produced by ``gcc/g++`` with
    ``-MMD -MP -MF``.

    The Make-style format looks like::

        target.o: src.cpp /path/to/header1.h \\
          /path/to/header2.h

        /path/to/header1.h:
        /path/to/header2.h:

    The trailing "phony" entries come from ``-MP`` and are harmless here
    because we only look at the RHS of the first colon.

    :returns: the list of header / source paths the object depends on
        (the right-hand side of the first rule). An empty list is
        returned if the file does not exist or cannot be read.
    """
    DepFile = FileIO(DepFilePath)
    if not DepFile.Exists():
        return []

    try:
        with open(DepFilePath, "r", encoding="UTF-8") as Handle:
            Content = Handle.read()
    except OSError:
        return []

    # Collapse line continuations so we can split on whitespace.
    Content = Content.replace("\\\r\n", " ").replace("\\\n", " ")

    ColonIndex = Content.find(":")
    if ColonIndex < 0:
        return []

    # Everything up to the next newline is the first rule's dependency
    # list. Subsequent lines (from -MP) are phony targets we ignore.
    FirstRule = Content[ColonIndex + 1 :].split("\n", 1)[0]
    return [Token for Token in FirstRule.split() if Token]


def _ObjectIsUpToDate(
    SourceFile: str,
    ObjectFile: FileIO,
    MiddleFilesDir: str,
) -> bool:
    """
    Decide whether ``ObjectFile`` can be reused without recompiling
    ``SourceFile``.

    An object is stale if:
      * it does not exist, or
      * the source file is newer than the object, or
      * any header listed in the matching ``.d`` file is newer than
        the object, or
      * any header listed in the matching ``.d`` file has disappeared
        (forces a safe rebuild).
    """
    if not ObjectFile.Exists():
        return False

    SourceTime = FileIO(SourceFile).LastChange()
    ObjectTime = ObjectFile.LastChange()

    if SourceTime >= ObjectTime:
        return False

    DepFilePath = _DependencyFilePath(MiddleFilesDir, SourceFile)
    for Dependency in _ParseDependencyFile(DepFilePath):
        DepFile = FileIO(Dependency)
        if not DepFile.Exists():
            return False
        if DepFile.LastChange() >= ObjectTime:
            return False

    return True


# --------------------------------------------------------------------------- #
# Output path helpers
# --------------------------------------------------------------------------- #


def _EntryPointOutputPath(BinaryFilesDir: str, TargetName: str) -> str:
    return f"{BinaryFilesDir}/{TargetName}{_ExecutableSuffix()}"


def _DynamicLibOutputPath(BinaryFilesDir: str, LibPrefix: str, ModuleName: str) -> str:
    return f"{BinaryFilesDir}/{LibPrefix}{ModuleName}{_DynamicLibSuffix()}"


def _StaticLibOutputPath(BinaryFilesDir: str, LibPrefix: str, ModuleName: str) -> str:
    return f"{BinaryFilesDir}/lib{LibPrefix}{ModuleName}.a"


# --------------------------------------------------------------------------- #
# Main entry
# --------------------------------------------------------------------------- #


def BuildModule(ModuleName: str):
    """
    Build a module.

    ATTENTION: the module must have been discovered by the build system.

    :param ModuleName: the name of the module
    :type ModuleName: str
    """

    ModuleID: int = BuildContext.BuildOrder.index(ModuleName)
    ModuleConfiguration: ModuleBase = BuildContext.ModuleConfiguration[ModuleID]
    TargetName: str = BuildContext.TargetName

    if not ModuleConfiguration.BuildThisModule:
        return

    # --- Verify dependencies were built before this module --------------- #
    for Depend in ModuleConfiguration.ModulesDependOn:
        if not BuildContext.BuildedModule[BuildContext.BuildOrder.index(Depend)]:
            Logger.Log(
                LogLevelEnum.Error,
                f"Module '{ModuleName}' depend on module '{Depend}', "
                f"but it didn't build.",
                True,
                -1,
            )

    Logger.Log(
        LogLevelEnum.Info,
        f"[{ModuleID + 1}/{len(BuildContext.BuildOrder)}] "
        f"Building module '{ModuleName}'",
    )

    # --- Directory layout ------------------------------------------------- #
    MiddleFilesDir: str = f"./Build/Intermediate/{TargetName}/{ModuleName}"
    BinaryFilesDir: str = "./Build/Binaries"

    os.makedirs(MiddleFilesDir, exist_ok=True)
    os.makedirs(BinaryFilesDir, exist_ok=True)

    # --- Discover sources ------------------------------------------------- #
    ModuleRoot: str = os.path.dirname(BuildContext.ModulePath[ModuleID])
    WaitCompileCFilesList, WaitCompileCppFilesList = _CollectSourceFiles(
        FileIO(f"{ModuleRoot}/Private/")
    )

    COFilesList: List[str] = []
    CxxOFilesList: List[str] = []

    # --- Prune sources whose objects are already up to date --------------- #
    # This is where header-dependency tracking kicks in: _ObjectIsUpToDate
    # consults the matching .d file produced by a previous compile.
    if not BuildContext.Arguments.donot_use_o_files:
        FreshCFiles: List[str] = []
        FreshCppFiles: List[str] = []

        for File in WaitCompileCFilesList:
            ObjectPath = _ObjectFilePath(MiddleFilesDir, File)
            if _ObjectIsUpToDate(File, FileIO(ObjectPath), MiddleFilesDir):
                COFilesList.append(ObjectPath)
            else:
                FreshCFiles.append(File)

        for File in WaitCompileCppFilesList:
            ObjectPath = _ObjectFilePath(MiddleFilesDir, File)
            if _ObjectIsUpToDate(File, FileIO(ObjectPath), MiddleFilesDir):
                CxxOFilesList.append(ObjectPath)
            else:
                FreshCppFiles.append(File)

        WaitCompileCFilesList = FreshCFiles
        WaitCompileCppFilesList = FreshCppFiles

    # --- Decide the final output path for this module -------------------- #
    LibPrefix: str = (
        f"{TargetName}-" if ModuleConfiguration.EnableBinaryLibPrefix else ""
    )

    if ModuleConfiguration.BinaryType == BinaryTypeEnum.EntryPoint:
        TargetOutputPath = _EntryPointOutputPath(BinaryFilesDir, TargetName)
    elif ModuleConfiguration.BinaryType == BinaryTypeEnum.DynamicLib:
        TargetOutputPath = _DynamicLibOutputPath(BinaryFilesDir, LibPrefix, ModuleName)
    else:
        TargetOutputPath = _StaticLibOutputPath(BinaryFilesDir, LibPrefix, ModuleName)

    TargetExists: bool = FileIO(TargetOutputPath).Exists()

    # --- Can we skip this whole module? ---------------------------------- #
    AllDependsSkiped: bool = all(
        BuildContext.SkipedModule[BuildContext.BuildOrder.index(Depend)]
        for Depend in ModuleConfiguration.ModulesDependOn
    )
    NothingToCompile: bool = not WaitCompileCFilesList and not WaitCompileCppFilesList

    if ModuleConfiguration.AutoSkiped or (
        NothingToCompile and AllDependsSkiped and TargetExists
    ):
        BuildContext.BuildedModule[ModuleID] = True
        BuildContext.SkipedModule[ModuleID] = True
        return

    # --- Optional format check ------------------------------------------- #
    if (
        ModuleConfiguration.EnableFormatCheck
        and BuildContext.Arguments.enable_format_check
    ):
        for File in WaitCompileCppFilesList:
            if CheckFormat(File) != 0:
                Logger.Log(
                    LogLevelEnum.Error,
                    f"Format check failed in file {File}, "
                    f"see log for detailed informations",
                    True,
                    1,
                )

    # --- Assemble compile flags ------------------------------------------ #
    CStanderd: str = ModuleConfiguration.CStanderd
    CxxStanderd: str = ModuleConfiguration.CxxStanderd
    ModuleAddedArguments: str = " ".join(ModuleConfiguration.ArgumentsAdded)
    TargetAddedArguments: str = " ".join(
        BuildContext.TargetConfiguration.ArgumentsAdded
    )

    IncludePaths: str = " -I".join(
        os.path.abspath(
            os.path.dirname(
                BuildContext.ModulePath[BuildContext.BuildOrder.index(Depend)]
            )
            + "/Public/"
        )
        for Depend in ModuleConfiguration.ModulesDependOn + [ModuleName]
    )

    # -l flags for every linkable dependency.
    DependsModules: List[str] = []
    for Name in ModuleConfiguration.ModulesDependOn:
        DependConfig: ModuleBase = BuildContext.ModuleConfiguration[
            BuildContext.BuildOrder.index(Name)
        ]
        if not DependConfig.LinkThisModule:
            continue
        if DependConfig.EnableBinaryLibPrefix:
            DependsModules.append(f"{TargetName}-{Name}")
        else:
            DependsModules.append(Name)

    LinkDependsStr: str = " ".join(f"-l{Name}" for Name in DependsModules)

    # --- Per-source compile commands ------------------------------------- #
    def TransformCommand(BuildCommand: str, SourceName: str) -> dict:
        """
        Transform a build command into the clangd
        ``compile_commands.json`` schema.
        """
        return {
            "file": FileIO(SourceName).FileName(),
            "directory": os.path.abspath(os.path.dirname(SourceName)),
            "arguments": ["clang++"] + BuildCommand.split(" ")[1:-1],
        }

    CompileCommands: List[Tuple[str, str]] = []  # (source_file, command)

    for CFile in WaitCompileCFilesList:
        ObjectPath = _ObjectFilePath(MiddleFilesDir, CFile)
        DepPath = _DependencyFilePath(MiddleFilesDir, CFile)
        COFilesList.append(ObjectPath)

        BuildCommand: str = (
            f"gcc {CFile} -o {ObjectPath} -std={CStanderd} "
            f"-MMD -MP -MF {DepPath} "
            f"{ModuleAddedArguments} {TargetAddedArguments} "
            f"-I{IncludePaths} -c"
        )
        BuildContext.CompileCommands.append(TransformCommand(BuildCommand, CFile))
        CompileCommands.append((CFile, BuildCommand))

    for CppFile in WaitCompileCppFilesList:
        ObjectPath = _ObjectFilePath(MiddleFilesDir, CppFile)
        DepPath = _DependencyFilePath(MiddleFilesDir, CppFile)
        CxxOFilesList.append(ObjectPath)

        BuildCommand: str = (
            f"g++ {CppFile} -o {ObjectPath} -std={CxxStanderd} "
            f"-MMD -MP -MF {DepPath} "
            f"{ModuleAddedArguments} {TargetAddedArguments} "
            f"-I{IncludePaths} -c"
        )
        BuildContext.CompileCommands.append(TransformCommand(BuildCommand, CppFile))
        CompileCommands.append((CppFile, BuildCommand))

    # --- Run compile commands in parallel -------------------------------- #
    if not BuildContext.Arguments.donot_build_files and CompileCommands:

        def RunCompileCommand(Job: Tuple[str, str]):
            SourceFile, Cmd = Job
            _PrintStatus(f"Compiling {SourceFile}")

            Result = subprocess.run(
                Cmd,
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="UTF-8",
            )
            if Result.returncode != 0:
                return (False, Cmd, Result.stderr)
            return (True, Cmd, "")

        with concurrent.futures.ThreadPoolExecutor(
            max_workers=BuildContext.Arguments.threads
        ) as Executor:
            Futures = {
                Executor.submit(RunCompileCommand, Job): Job for Job in CompileCommands
            }

            for Future in concurrent.futures.as_completed(Futures):
                Success, Cmd, Error = Future.result()
                if Success:
                    continue

                # Cancel everything still pending and fail loudly.
                for Pending in Futures:
                    Pending.cancel()
                _ClearStatus()
                print(Error.replace("\n\n", "\n"))
                Logger.Log(
                    LogLevelEnum.Error,
                    f"Compile failed when running command {Cmd}, "
                    f"see error in the log.",
                    True,
                    -1,
                )

        _ClearStatus()

    if BuildContext.Arguments.donot_build_files:
        BuildContext.BuildedModule[ModuleID] = True
        return

    # --- Link step -------------------------------------------------------- #
    ObjectsStr: str = " ".join(COFilesList + CxxOFilesList)
    BuildResult: int = 0

    if ModuleConfiguration.BinaryType == BinaryTypeEnum.EntryPoint:
        LinkCommand = (
            f"g++ {ObjectsStr} "
            f"-o {_EntryPointOutputPath(BinaryFilesDir, TargetName)} "
            f"-L{BinaryFilesDir}/ {LinkDependsStr} "
            f"{ModuleAddedArguments} {TargetAddedArguments}"
        )
        BuildResult = os.system(LinkCommand)

    elif ModuleConfiguration.BinaryType == BinaryTypeEnum.DynamicLib:
        LinkCommand = (
            f"g++ {ObjectsStr} "
            f"-o {_DynamicLibOutputPath(BinaryFilesDir, LibPrefix, ModuleName)} "
            f"-L{BinaryFilesDir}/ {LinkDependsStr} -fPIC -shared "
            f"{ModuleAddedArguments} {TargetAddedArguments}"
        )
        BuildResult = os.system(LinkCommand)

    else:  # StaticLib
        StaticLibPath = _StaticLibOutputPath(BinaryFilesDir, LibPrefix, ModuleName)
        LinkCommand = f"ar rcs {StaticLibPath} {ObjectsStr}"

        for Name in ModuleConfiguration.ModulesDependOn:
            DependConfig: ModuleBase = BuildContext.ModuleConfiguration[
                BuildContext.BuildOrder.index(Name)
            ]
            if not (
                DependConfig.LinkThisModule
                and DependConfig.BinaryType == BinaryTypeEnum.StaticLib
            ):
                continue

            DependPrefix = (
                f"{TargetName}-" if DependConfig.EnableBinaryLibPrefix else ""
            )
            LinkCommand += f" {BinaryFilesDir}/lib{DependPrefix}{Name}.a"

        BuildResult = os.system(LinkCommand)

    if BuildResult == 0:
        BuildContext.BuildedModule[ModuleID] = True
    else:
        Logger.Log(
            LogLevelEnum.Error,
            f"There's something error when build module '{ModuleName}' in "
            f"target '{TargetName}', and the compiler return value "
            f"'{BuildResult}' not 0.",
            True,
            -1,
        )

    # --- Tests ----------------------------------------------------------- #
    if BuildContext.Arguments.enable_tests and ModuleConfiguration.EnableTests:
        TestModule(
            ModuleName,
            os.path.dirname(BuildContext.ModulePath[ModuleID]),
            CxxOFilesList,
            f"{ModuleAddedArguments} {TargetAddedArguments} "
            f"-I{IncludePaths} -L ./Build/Binaries/ {LinkDependsStr} "
            f"-l{LibPrefix}{ModuleName}",
        )
