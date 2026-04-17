# Copyright 2025, LiserverYang. All rights reserved.

import Build.BuildSystem as BuildSystem

if __name__ != "__main__":
    BuildSystem.Logger.Log(
        BuildSystem.LogLevelEnum.Error,
        "You could only run this srcipt by console not import.",
        True,
        -1,
    )

if BuildSystem.GetCurrentSystem() == BuildSystem.SystemEnum.Other:
    # Log and exit
    BuildSystem.Logger.Log(
        BuildSystem.LogLevelEnum.Error,
        "Unsupported platform. Only support Windows, MacOS and Linux.",
        True,
        -1,
    )

# Don't write __pycatch__ file everywhere!!!
BuildSystem.sys.dont_write_bytecode = True

# Just build
BuildSystem.BuildApp(BuildSystem.FileIO("./Source"), ["./Source/lisc.target.py"])
