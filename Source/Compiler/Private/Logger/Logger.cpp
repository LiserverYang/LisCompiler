#include "Logger/Logger.hpp"

#include <cmath>
#include <iostream>

/**
 * This function is to help the logger log the code where have errors with loginfo and the color 
 * 
 * @param info the info of log stored the file value, the error position and the length
 * @param color the color of error
 */
void LogCode(Logger::LogInfo &info, std::string color)
{
    // first we stored the spaces of the line number
    const int lineLength = std::log10(info.line) + 1;

    std::string codeStr = "";
    std::string errStr = "";

    for (int i = 0; i <= i + 15; i++)
    {
        // to avoid outing of bounds
        if (info.beginPosition + i >= info.code->size())
        {
            break;
        }

        char code = info.code->at(info.beginPosition + i);

        // if the line end, break
        if (code == '\r' || code == '\n')
        {
            break;
        }

        // add the color ascii code
        if (i == info.col - 1)
        {
            codeStr.append(color);
        }

        // end the color ascii code with "\033[0m" code
        if (i == info.col - 1 + info.length)
        {
            codeStr.append("\033[0m");
        }

        codeStr.push_back(code);
    }

    for (int i = 0; i < info.length - 1; i++)
        errStr += '~';

    codeStr.append("\033[0m");

    printf("    %d | %s\n", info.line, codeStr.c_str());

    if (info.col == 1)
    {
        printf("%*s| ^", lineLength + 5, " ");
    }
    else
    {
        printf("%*s| %*s%s^%s\033[0m\n", lineLength + 5, " ", info.col - 1, " ", color.c_str(), errStr.c_str());
    }
}

void Logger::Log(Logger::LogLevel level, Logger::LogInfo info)
{
    printf("\033[1m%s:%d:%d:\033[0m", info.codePath.c_str(), info.line, info.col);

    std::string color = "";

    switch (level)
    {
    case Logger::LogLevel::ERROR:
        printf("\033[31m error:\033[0m ");
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
        // if it is debug, we should throw error to let the developer debug the compiler and get the call stack
        throw std::runtime_error("Create debug point");
#else
        // if it is release, exit with error code 1
        exit(1);
#endif
    }
}