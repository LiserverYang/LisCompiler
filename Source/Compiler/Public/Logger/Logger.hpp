/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 定义输出系统
 */

#include <string>

/*
 * Logger 定义了编译器的三种输出：
 *     1. 错误（error）
 *     2. 警告（warning）
 *     3. 信息 (info)
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
    };

public:
    static void Log(LogLevel level, LogInfo info);
};