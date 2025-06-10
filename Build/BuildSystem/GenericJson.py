# Copyright 2025, LiserverYang. All rights reserved.

from typing import List, Dict

from .FileSystem import FileIO

import json
import sys
import os

def GenericJson(Commands: List[Dict[str, any]]):
    """
    For clangd, generic compile_command.json
    """

    if "--donot-generic-cc-json" in sys.argv:
        return
    
    if not FileIO("./compile_commands.json").Exists():
        with open("./compile_commands.json", "w") as f:
            json.dump(Commands, f)
        return
    
    hash_map: Dict[str, int] = {}
    commands = []

    with open("./compile_commands.json", "r") as f:
        commands = json.load(f)

        for i in range(0, len(commands)):
            hash_map[commands[i]["directory"] + commands[i]["file"]] = i + 1

        for item in Commands:
            index = item["directory"] + item["file"]

            if not(index in hash_map.keys()):
                commands.append(item)

            elif hash_map[index] != 0:
                commands[hash_map[index] - 1] = item

    with open("./compile_commands.json", "w") as f:
        json.dump(commands, f)