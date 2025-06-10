# Copyright 2025, LiserverYang. All rights reserved.

from .FilePermissionsEnum import FilePermissionsEnum
from . import Functions
import os
import time

class FileIO:
    """
    Provides basic operations for files.
    """
    
    FilePathStr: str = ""
    BasePermissions: int = 0b0

    def QueryPermissions(self) -> int:
        """
        Get a number that this file's permissions.
        """

        BasePermissionsTemp: int = 0b0

        self.BasePermissions = Functions.AddPermissions(BasePermissionsTemp, FilePermissionsEnum.Read)  if os.access(self.FilePathStr, os.R_OK) else self.BasePermissions
        self.BasePermissions = Functions.AddPermissions(BasePermissionsTemp, FilePermissionsEnum.Write) if os.access(self.FilePathStr, os.W_OK) else self.BasePermissions
        self.BasePermissions = Functions.AddPermissions(BasePermissionsTemp, FilePermissionsEnum.Run)   if os.access(self.FilePathStr, os.X_OK) else self.BasePermissions

    def QueryPermission(self, Permission: FilePermissionsEnum) -> bool:
        """
        Get if you have this file's one permission.
        Call this function before you called inited the class and gave it a true path.
        """

        return Functions.HasPermissions(self.BasePermissions, Permission)

    def IsFolder(self) -> bool:
        """
        Return if this file is a folder.
        """

        return os.path.isdir(self.FilePathStr)
    
    def GetSubFiles(self) -> list[str]:
        """
        Get child files' path.
        """

        # If it's not folder, return
        if not self.IsFolder():
            return []

        return [os.path.join(self.FilePathStr, file) for file in os.listdir(self.FilePathStr)]
    
    def EndSwitch(self) -> str:
        """
        Get the endswitch of this file.
        """

        return os.path.splitext(self.FilePathStr)[1]
    
    def FileName(self) -> str:
        """
        Get the file's name.
        """

        return os.path.basename(self.FilePathStr)
    
    def Exists(self) -> bool:
        """
        Return if this file exists.
        """

        return os.path.exists(self.FilePathStr)
    
    def LastChange(self) -> int:
        """
        The the last change time of this file.
        """
        
        return os.stat(self.FilePathStr).st_mtime

    def __init__(self, FilePathStr: str):
        """
        Init this class, FileIO.
        """

        self.FilePathStr = FilePathStr.replace("\\", "/")
