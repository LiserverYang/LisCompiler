# Copyright 2025, LiserverYang. All rights reserved.

import subprocess

LLVMLibs: str = ""
LLVMCommand: str = ""
LLVMIncludeCommand: str = ""

def InitLLVMConfig(LLVMPosition: str) -> None:
    global LLVMLibs, LLVMCommand, LLVMIncludeCommand
    LLVMLibs = " -l".join(lib.split(".")[0] for lib in subprocess.run(f"{LLVMPosition}bin/llvm-config --system-libs --libnames --link-static all".split(" "), stdout=subprocess.PIPE)
                            .stdout.decode('utf-8')
                            .replace(" libxml2s.lib", "")
                            .split(" ")) + " -lwinpthread -lmingwex -lmsvcr120 -lz -lzstd"
    
    LLVMIncludeCommand = f"-I{LLVMPosition}include"

    LLVMCommand = f"{LLVMIncludeCommand} -L{LLVMPosition}lib/ -l{LLVMLibs}"