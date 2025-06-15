# Copyright 2025, LiserverYang. All rights reserved.

import subprocess

LLVMLibs: str = ""
LLVMCommand: str = ""

def InitLLVMConfig(LLVMPosition: str) -> None:
    global LLVMLibs, LLVMCommand
    LLVMLibs = " -l".join(lib.split(".")[0] for lib in subprocess.run(f"{LLVMPosition}llvm-config --system-libs --libnames core", stdout=subprocess.PIPE)
                            .stdout.decode('utf-8')
                            .split(" ")) + " -lwinpthread -lmingwex -lmsvcr120"

    LLVMCommand = (subprocess.run(f"{LLVMPosition}llvm-config --cxxflags --ldflags", stdout=subprocess.PIPE).stdout.decode('utf-8').replace("-std:c++17", "-std=c++17")
                        .replace("/EHs-c- /GR-", "")
                        .replace("-LIBPATH:", "-L") + f"-l{LLVMLibs}"
                    ).replace("\n", " ").replace("-llibxml2s", "")