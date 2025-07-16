#include "Logger/Logger.hpp"
#include "Core/Debugging.hpp"

#include <cmath>
#include <iostream>

/**
 * The output looks like:
 *
 * .\Examples\helloworld.lis:15:5: error: expected ';' after let statement
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
    const int lineLength = static_cast<int>(std::log10(info.line)) + 1;
    const std::string &code = *info.code;

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
    size_t errorStart = info.col - 1;
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

void Logger::Log(Logger::LogLevel level, Logger::LogInfo info)
{
    printf("\033[1m%s:%d:%d:\033[0m", info.codePath.c_str(), info.line, info.col);

    std::string color = "";

    switch (level)
    {
    case Logger::LogLevel::ERROR:
        printf("\033[31m error[E%04d]:\033[0m ", info.errorId);
        color = "\033[31m";
        break;
    case Logger::LogLevel::WARNING:
        printf("\033[33m warning:\033[0m ");
        color = "\033[33m";
        break;
    case Logger::LogLevel::INFO:
        printf("\033[34m info:\033[0m ");
        color = "\034[31m";
        break;
    default:
        break;
    }

    printf((info.msg + "\n").c_str());

    // log the code
    if (info.logCode)
    {
        LogCode(info, color);
    }

    if (level == LogLevel::ERROR)
    {
#ifdef __DEBUG__
        // if it is debug, we should create debug point to get the call stack
        DEBUG_POINT();
#else
        // if it is release, exit with error code 1
        exit(1);
#endif
    }
}