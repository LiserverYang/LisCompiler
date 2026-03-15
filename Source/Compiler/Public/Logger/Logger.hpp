/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The definations of looger system
 */

#pragma once

#include <string>
#include <vector>

/*
 * Logger defined three log level:
 *     1. error
 *     2. warning
 *     3. info
 */
class Logger
{
public:
    enum class LogLevel
    {
        ERROR = 0,
        WARNING,
        INFO
    };

    struct LogInfo
    {
        const std::string *code; // the string of source code
        std::string codePath; // the path of source code
        std::string msg; // the error message
        size_t line, col; // the line, column of error code
        size_t length; // the length of error code
        size_t beginPosition; // this is different from line and col, it is the index of the code
        size_t errorId = 1; // the error id
        bool logCode = true; // if print the source code
        bool exit = true; // if exit after error
        int exitCode = 1; // the exit code (if `exit` is `true`)
    };

public:
    static void Log(LogLevel level, LogInfo info);
    static void Log(LogLevel level, const std::vector<LogInfo> &info);
};