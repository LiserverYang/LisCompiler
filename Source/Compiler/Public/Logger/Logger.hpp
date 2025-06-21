/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The definations of looger system
 */

#include <string>

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
        ERROR,
        WARNING,
        INFO
    };

    struct LogInfo
    {
        const std::string *code;
        std::string codePath;
        std::string msg;
        size_t line, col;
        size_t length;
        size_t beginPosition;
        bool logCode = 1;
    };

public:
    static void Log(LogLevel level, LogInfo info);
};