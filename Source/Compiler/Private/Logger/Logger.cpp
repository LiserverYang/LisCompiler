/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Logger/Logger.hpp"
#include "Core/Debugging.hpp"

#include <cmath>
#include <iostream>

/**
 * The output looks like:
 *
 * ./Examples/fib.lis:15:5: error[E2012]: expected ';' after let statement
 *     15 |     ret 0;
 *        |     ^~~
 */

/**
 * This function is to help the logger log the code where have errors with loginfo and the color
 *
 * @param info the info of log stored the file value, the error position and the length
 * @param color the color of error
 */
void LogCode(Logger::LogInfo &info, const std::string &color)
{
    // 安全检查：避免空指针访问
    if (info.code == nullptr)
    {
        return;
    }

    const std::string &code = *info.code;

    // beginPosition may come from a compiler-generated node (e.g. the for-loop
    // desugar) whose position is unset/garbage — clamp it so the line scan
    // below never reads out of bounds.
    if (info.beginPosition > code.size())
        info.beginPosition = code.size();

    int lineLength = 1;
    if (info.line > 0)
    {
        lineLength = static_cast<int>(std::log10(info.line)) + 1;
    }

    // Find start of line
    size_t lineStart = info.beginPosition;
    while (lineStart > 0 && code[lineStart - 1] != '\n' && code[lineStart - 1] != '\r')
    {
        lineStart--;
    }

    // Find end of line
    size_t lineEnd = info.beginPosition;
    while (lineEnd < code.size() && code[lineEnd] != '\n' && code[lineEnd] != '\r')
    {
        lineEnd++;
    }

    // Extract the full line
    std::string line = code.substr(lineStart, lineEnd - lineStart);

    // Calculate error position within the line
    size_t errorStart = info.col > 0 ? (info.col - 1) : 0;
    size_t errorEnd = std::min(errorStart + info.length, line.length());

    // Build colored line
    std::string coloredLine = line.substr(0, errorStart)
                              + color
                              + line.substr(errorStart, errorEnd - errorStart)
                              + "\033[0m"
                              + line.substr(errorEnd);

    // Build error indicator
    std::string indicatorSpaces(lineLength + 5, ' ');
    std::string errorIndent(errorStart, ' ');
    std::string errorMark = color + '^';
    if (info.length > 1)
    {
        errorMark.append(info.length - 1, '~');
    }
    errorMark += "\033[0m";

    // Print output
    printf("    %d | %s\n", info.line, coloredLine.c_str());
    printf("%s| %s%s\n", indicatorSpaces.c_str(), errorIndent.c_str(), errorMark.c_str());
}

static int gErrorCount = 0;

int Logger::GetErrorCount()
{
    return gErrorCount;
}

void Logger::ResetErrorCount()
{
    gErrorCount = 0;
}

void Logger::SetErrorCount(int count)
{
    gErrorCount = count;
}

void Logger::Log(Logger::LogLevel level, Logger::LogInfo info)
{
    if (level == LogLevel::ERROR)
        gErrorCount++;

    printf("\033[1m%s:%d:%d:\033[0m", info.codePath.c_str(), info.line, info.col);

    std::string color = "";

    switch (level)
    {
    case Logger::LogLevel::ERROR:
        printf("\033[31m error[E%04d]:\033[0m ", info.errorId);
        color = "\033[31m"; // 红色
        break;
    case Logger::LogLevel::WARNING:
        printf("\033[33m warning:\033[0m ");
        color = "\033[33m"; // 黄色
        break;
    case Logger::LogLevel::INFO:
        printf("\033[34m info:\033[0m ");
        color = "\033[34m"; // 蓝色（修复了原错误的 \034[31m）
        break;
    default:
        break;
    }

    printf("%s\n", info.msg.c_str());

    // log the code
    if (info.logCode)
    {
        LogCode(info, color);
    }

    if (level == LogLevel::ERROR && info.exit)
    {
        // Flush before terminating — in debug builds DEBUG_POINT() (int 3)
        // would otherwise kill the process with the message still buffered.
        fflush(stdout);
#ifdef __DEBUG__
        // if it is debug, we should create debug point to get the call stack
        DEBUG_POINT();
#else
        // if it is release, exit with exitCode
        exit(info.exitCode);
#endif
    }
}

void Logger::Log(LogLevel level, const std::vector<LogInfo> &info) // 改为 const 引用，提升效率
{
    if (info.empty())
    {
        return;
    }

    LogInfo firstInfo = info[0];
    firstInfo.exit = false;

    Log(level, firstInfo);

    for (size_t index = 1; index < info.size(); index++)
    {
        Log(LogLevel::INFO, info[index]);
    }
}