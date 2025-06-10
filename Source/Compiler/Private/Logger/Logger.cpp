#include "Logger/Logger.hpp"

#include <cmath>
#include <iostream>

void LogCode(Logger::LogInfo &info, std::string color)
{
    const int lineLength = std::log10(info.line) + 1;

    std::string codeStr = "";
    std::string errStr = "";

    for (int i = 0; i <= i + 15; i++)
    {
        if (info.beginPosition + i >= info.code->size())
        {
            break;
        }

        char code = info.code->at(info.beginPosition + i);

        if (code == '\t' || code == '\n')
        {
            break;
        }

        if (i == info.col - 1)
        {
            codeStr.append(color);
        }

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

    LogCode(info, color);

    if (level == LogLevel::ERROR)
    {
#ifdef __DEBUG__
        throw std::runtime_error("Create debug point");
#else
        exit(1);
#endif
    }
}