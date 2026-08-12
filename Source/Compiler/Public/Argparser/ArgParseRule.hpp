/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * Defination of ArgParseRule
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

class Argparser;

struct ArgParseRule
{
    using ArgBehavior = std::function<int(Argparser *, std::string)>;

    /**
     * Argument identifier
     *
     * Example:
     * When name=fileName
     * Command: app.exe test
     * Value "test" can be retrieved via Args.getArg("fileName")
     * This type is called a positional argument
     *
     * Special case: If name follows format --xxx or -xxx,
     * it is recognized as an option argument
     *
     * Example:
     * When name=--filename
     * Command: app.exe --filename test
     * Value "test" is retrievable via Args.getArg("filename")
     *
     * However, for command: app.exe test
     * Args.getArg("filename") returns empty, as option arguments require explicit identifiers
     *
     * Note:
     * For hyphenated names (xxx-xxx format),
     * access via Args.getArg("xxx_xxx") (hyphens replaced by underscores)
     *
     * Option arguments may accept multiple values
     */
    std::vector<std::string> name;

    /**
     * Behavior specification for option arguments
     * Common behaviors: setAsTrue, setAsFalse, setAsValue
     *
     * setAsTrue:
     * Sets corresponding value to string "true" when parsed
     *
     * setAsFalse:
     * Sets corresponding value to string "false" when parsed
     *
     * setAsValue:
     * Sets corresponding value to the subsequent argument,
     * which must not be an option argument
     *
     * Custom Behavior
     * You should define a function as a behavior, which the params are a `Argparser*` as the argparser and a `std::string` as the current param name
     * The return of your behavior function should be `int`, which how many args should be skip (contain the current param, so at least, if should be 1)
     *
     * Example:
     * name=filename, behavior=setAsValue
     * Command: app.exe --filename xxx
     * Args.getArg("filename") returns "xxx"
     *
     * but important,
     * Command: app.exe --filename --xxx
     * will cause parser error (--xxx cannot follow setAsValue)
     */
    ArgBehavior behavior;

    /**
     * Default argument value
     * Initialized during argument registration
     */
    std::string defaultValue;

    /**
     * Argument description
     * Documents argument behavior and purpose
     */
    std::string description;
};