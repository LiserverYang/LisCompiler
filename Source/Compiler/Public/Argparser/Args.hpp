/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 参数容器实现
 */

#pragma once

#include <string>
#include <unordered_map>

/**
 * Args 存储了所有参数的信息，以 键-值 的方式存储
 */
class Args : public std::unordered_map<std::string, std::string>
{
public:
    /**
     * 通过下标访问参数列表上的一个位置
     * 如果下标不存在则返回为空字符串
     */
    std::string getArg(std::string argName)
    {
        auto pos = find(argName);

        if (pos != end())
        {
            return pos->second;
        }

        return "";
    }

    void setArg(std::string argName, std::string value)
    {
        this->operator[](argName) = value;
    }
};