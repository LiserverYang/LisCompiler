# Copyright 2025, LiserverYang. All rights reserved.

# Import buildsystem
import Build.BuildSystem as BuildSystem

# We could only run this script by command line not import
if __name__ != '__main__':
    BuildSystem.Logger.Log(BuildSystem.LogLevelEnum.Error, "You could only run this srcipt by console not import.", True, -1)

# And we should check the type of current operating system
if BuildSystem.GetCurrentSystem() == BuildSystem.SystemEnum.Other:
    # Log and exit
    BuildSystem.Logger.Log(BuildSystem.LogLevelEnum.Error, "Unsupported platform. We only support Windows, MacOS and Linux.", True, -1)

# Don't write __pycatch__
BuildSystem.sys.dont_write_bytecode = True

# Just build
BuildSystem.BuildEngine(BuildSystem.FileIO("./Source"), ["./Source/lisc.target.py"])