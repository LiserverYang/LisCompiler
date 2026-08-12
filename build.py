# Copyright 2025, LiserverYang. All rights reserved.

import os
import sys

# Put the repo root on sys.path so `import Build.BuildSystem` resolves even when
# this script is invoked from another cwd (e.g. `python ../build.py` from a
# subdirectory). Root-relative paths below make the whole build cwd-independent.
_ROOT = os.path.dirname(os.path.abspath(__file__))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

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
BuildSystem.BuildApp(
    BuildSystem.FileIO(os.path.join(_ROOT, "Source")),
    [os.path.join(_ROOT, "Source", "lisc.target.py")],
)
