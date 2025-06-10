# Copyright 2025, LiserverYang. All rights reserved.

import time
from . import LogLevelEnum

import colorama

class TLogger:
    """
    The logger, includes function log.
    """

    def Log(self, LogLevel: LogLevelEnum, Msg: str, bExit: bool = False, ExitCode: int = 1):
        """
        Output log.
        """

        OutputStr: str = "${TIME} [${LEVEL}]: ${MSG}"
        LevelMarco: str = "${LEVEL}"
        TimeMarco: str = "${TIME}"
        MsgMarco: str = "${MSG}"

        match LogLevel:
            case LogLevelEnum.Info:
                OutputStr = OutputStr.replace(LevelMarco, colorama.Fore.BLUE + "Info" + colorama.Fore.RESET)
            case LogLevelEnum.Warning:
                OutputStr = OutputStr.replace(LevelMarco, colorama.Fore.YELLOW + "Warning" + colorama.Fore.RESET)
            case LogLevelEnum.Error:
                OutputStr = OutputStr.replace(LevelMarco, "Error")

        OutputStr = OutputStr.replace(TimeMarco, time.strftime('%Y-%m-%d %H:%M:%S', time.localtime()))
        OutputStr = OutputStr.replace(MsgMarco, Msg)

        if LogLevel == LogLevelEnum.Error:
            print(colorama.Fore.LIGHTRED_EX)

        print(OutputStr + colorama.Fore.RESET)

        if bExit:
            exit(ExitCode)

Logger = TLogger()