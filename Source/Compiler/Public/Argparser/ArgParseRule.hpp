/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * 定义了参数解析规则
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

class Argparser;

struct ArgParseRule
{
    using ArgBehavior = std::function<int(Argparser*, std::string)>;

    /**
     * 参数的名字
     *
     * 例如
     * name=fileName 时
     * 程序参数 app.exe test
     * 则可以在 Args.getArg("fileName") 获取到值 test
     * 这种类型的参数称为位置参数
     *
     * 特别地，如果 name 是形如 --xxx 或 -xxx 的形式
     * 则认为 name 是一个选项参数
     *
     * 例子：
     * 当 name=--filename 时
     * 程序参数 app.exe --filename test
     * 则可以在 Args.getArg("filename") 获取到值 test
     *
     * 并且当程序参数为 app.exe test
     * Args.getArg("filename") 获取不到 test 并且返回空，因为选项参数需要有 --xxx 作为标识
     *
     * 另外，
     * 如果 name 是 xxx-xxx 的形式
     * 那么需要通过 Args.getArg("xxx_xxx") 的形式获取这个参数的值
     *
     * 如果是一个选项参数时，可以有多个值
     */
    std::vector<std::string> name;

    /**
     * 当参数是一个选项参数时
     * 可以指定这个参数的行为，通常有 setAsTrue setAsFalse setAsValue 三个常用行为
     *
     * setAsTrue：
     * 解析到这个参数，就会将这个参数对应的值设置为 "true" 注意类型是字符串！
     *
     * setAsFalse：
     * 解析到这个参数，就会将这个参数对应的值设置为 "false" 注意类型是字符串！
     *
     * setAsValue：
     * 解析到这个参数，就会将这个参数对应的值设置为下一个的值，并且下一个参数不能是选项参数
     *
     * 例子：
     * 如果 name=filename behavior=setAsValue
     * 当程序参数为 app.exe --filename xxx 时
     * 就可以通过 Args.getArg("filename") 获取到值 "xxx"
     *
     * 并且，
     * 如果程序参数为 app.exe --filename --xxx 时
     * 解析器就会报错
     */
    ArgBehavior behavior;

    /**
     * 这个参数的默认值，在这个参数被注册时初始化
     */
    std::string defaultValue;

    /**
     * 参数的描述信息，描述这个参数的行为和作用
     */
    std::string description;
};