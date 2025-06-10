/**
 * Copyright 2025, LiserverYang. All rights reserved.
 */

#include <cxxabi.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <typeinfo>
#include <vector>

#include "Parser/AST.hpp"

// 辅助函数：获取可读的类型名
std::string demangle(const char *mangled);

// 辅助函数：格式化指针地址
std::string formatAddress(const void *addr);

void printAST(const ASTNode *node, const std::string &prefix = "", bool isLast = true);

// 处理类型节点
void printTypeInfo(const Type *type, std::ostream &os);

// 处理各种AST节点
void printNodeInfo(const ASTNode *node, std::ostream &os);

// 获取节点的子节点列表
std::vector<const ASTNode *> getChildren(const ASTNode *node);

// 主打印函数
void printAST(const ASTNode *node, const std::string &prefix, bool isLast);

// 入口函数
void printAST(const Program &program);