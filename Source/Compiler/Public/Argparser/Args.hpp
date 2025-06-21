/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * The implementation of Args
 */

#pragma once

#include <string>
#include <unordered_map>

/**
 * Args stored all values of arguments by the way of key-value.
 * Use function `getArg` to get argument value
 * Use function `setArg` to set argument value
 */
class Args : public std::unordered_map<std::string, std::string>
{
public:
    /**
     * Visit a argument value by the argument name
     * If it is undefined, return null string ("")
     * 
     * @param argName the argument name
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

    /**
     * Set the argument value by argument name
     * 
     * @param argName the argument name
     * @param value the value to set
     */
    void setArg(std::string argName, std::string value)
    {
        this->operator[](argName) = value;
    }
};