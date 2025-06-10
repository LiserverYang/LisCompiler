# Copyright 2025, LiserverYang. All rights reserved.

import os
from .Functions import GetClassFromFileIO, GetAllUnits
from .FileSystem import FileIO
from .TargetBase import TargetBase
from .ModuleBase import ModuleBase
from .Logger import Logger
from .LogLevelEnum import LogLevelEnum
from .DependsLinks import TGraph
from .BuildContext import BuildContext
from .BuildModule import BuildModule

def BuildTarget(TargetPath: FileIO) -> None:
    """
    Build targets(Reflection, Editor, ...)
    """

    TargetInstance: TargetBase = GetClassFromFileIO(TargetPath, TargetPath.FileName()[0:-10] + "Target")()
    TargetInstance.Configuration()

    if not TargetInstance.bBuildThisTarget:
        return
    
    # Scan all modules and generic depends links
    Graph: TGraph = TGraph()

    ModulesList: list[str] = []
    ModulesPathList: list[str] = []
    ModulesConfigurations: list[ModuleBase] = []

    HashMap: dict[str, int] = {}

    # Scan
    for Folder in TargetInstance.ModulesSubFolder:
        # Get all modules in folder
        Modules: list[str] = GetAllUnits(FileIO(os.path.dirname(TargetPath.FilePathStr) + "/" + Folder), "build")
        # Check all modules
        for Module in Modules:
            # Get instance
            MouduleName: str = FileIO(Module).FileName()[0:-9]
            ModuleInstance: ModuleBase = GetClassFromFileIO(FileIO(Module), MouduleName + "Module")()
            ModuleInstance.Configuration()
            # Check if should build
            if (not ModuleInstance.bBuildThisModule) or ((not TargetInstance.bBuildAllmodules) and not (MouduleName in TargetInstance.BuildModulesList)):
                continue
            # Add to list
            HashMap[MouduleName] = len(ModulesList)
            ModulesList.append(MouduleName)
            ModulesPathList.append(Module)
            ModulesConfigurations.append(ModuleInstance)

    # Build depends links
    for i in range(0, len(ModulesList)):
        Graph.AddLinks(i, [HashMap[name] for name in ModulesConfigurations[i].ModulesDependOn])

    BuildOrder, Circled = Graph.TopologicalSort()

    if Circled:
        Logger.Log(LogLevelEnum.Error, "Depends links circled.", True, -1)

    # Change the values
    BuildOrder: list[str] = [ModulesList[i] for i in BuildOrder]

    # And we should add module that no one depend on it, and don't depend on anyone
    for Module in ModulesList:
        if not Module in BuildOrder:
            BuildOrder.append(Module)

    # Save all datas to BuildContext
    BuildContext.BuildOrder = BuildOrder
    BuildContext.TargetConfiguration = TargetInstance
    BuildContext.SkipedModule = [False] * len(BuildOrder)
    BuildContext.BuildedModule = [False] * len(BuildOrder)
    BuildContext.TargetName = TargetPath.FileName()[0:-10]
    BuildContext.ModulePath = [ModulesPathList[HashMap[i]] for i in BuildOrder]
    BuildContext.ModuleConfiguration = [ModulesConfigurations[HashMap[i]] for i in BuildOrder]

    Logger.Log(LogLevelEnum.Info, f"Building target '{TargetPath.FileName()[0:-10]}', contains {len(BuildOrder)} modules wait for build.")

    for Module in BuildOrder:
        BuildModule(Module)