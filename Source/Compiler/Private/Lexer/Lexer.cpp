#include "Lexer/Lexer.hpp"
#include "Logger/Logger.hpp"

#include <iostream>

void Lexer::run()
{
    TokenStream &tokens = context->tokenStream;
    std::string &source = context->fileValue;

    while (index < source.size())
    {
        char current = source[index];

        // 跳过空白字符
        if (std::isspace(current))
        {
            skipWhitespace();
            continue;
        }

        // 处理注释
        if (current == '/' && index + 1 < source.size())
        {
            char next = source[index + 1];
            if (next == '/')
            {
                skipLineComment();
                continue;
            }
            if (next == '*')
            {
                skipBlockComment();
                continue;
            }
        }

        // 处理字面量
        if (current == '"')
        {
            tokens.push_back(lexStringLiteral());
            continue;
        }
        if (current == '\'')
        {
            tokens.push_back(lexCharLiteral());
            continue;
        }
        if (std::isdigit(current))
        {
            tokens.push_back(lexNumber());
            continue;
        }

        // 处理标识符和关键字
        if (std::isalpha(current) || current == '_')
        {
            tokens.push_back(lexIdentifier());
            continue;
        }

        // 处理运算符和分隔符
        tokens.push_back(lexOperatorOrDelimiter());
    }
}

void Lexer::skipWhitespace()
{
    std::string &source = context->fileValue;

    while (index < source.size() && std::isspace(source[index]))
    {
        if (source[index] == '\n')
        {
            line++;
            lineStart = index + 1;
            column = 1;
        }
        else
        {
            column++;
        }
        index++;
    }
}

void Lexer::skipLineComment()
{
    std::string &source = context->fileValue;

    // 跳过 '//'
    index += 2;
    column += 2;

    // 跳过直到行尾
    while (index < source.size() && source[index] != '\n')
    {
        index++;
        column++;
    }

    lineStart = index;
}

void Lexer::skipBlockComment()
{
    std::string &source = context->fileValue;

    // 跳过 '/*'
    index += 2;
    column += 2;

    while (index < source.size())
    {
        if (source[index] == '*' && index + 1 < source.size() && source[index + 1] == '/')
        {
            // 找到结束标记 '*/'
            index += 2;
            column += 2;
            return;
        }

        if (source[index] == '\n')
        {
            line++;
            lineStart = index + 1;
            column = 1;
        }
        else
        {
            column++;
        }

        index++;
    }
}

Token Lexer::lexStringLiteral()
{
    std::string &source = context->fileValue;

    Token token;
    token.code = TokenCode::STRING_LITERAL;
    token.filePath = context->filePath;
    token.line = line;
    token.col = column;
    token.pos = index;
    token.lineStart = lineStart;

    // 跳过开头的双引号
    index++;
    column++;

    std::string value;
    bool escape = false;

    while (index < source.size())
    {
        char c = source[index];

        if (c == '\n')
        {
            line++;
            lineStart = index + 1;
            column = 1;
        }
        else
        {
            column++;
        }

        if (escape)
        {
            // 处理转义字符
            switch (c)
            {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            default:
                value += '\\';
                value += c;
                break;
            }
            escape = false;
        }
        else if (c == '\\')
        {
            escape = true;
        }
        else if (c == '"')
        {
            // 结束字符串
            index++;
            token.value = value;
            return token;
        }
        else
        {
            value += c;
        }

        index++;
    }

    // 如果到达这里，说明字符串未闭合
    Logger::Log(Logger::LogLevel::ERROR, {&source, context->filePath, "Unclosed string literal", line, column - 1, 1, lineStart});

    return token;
}

Token Lexer::lexCharLiteral()
{
    std::string &source = context->fileValue;

    Token token;
    token.code = TokenCode::CHAR_LITERAL;
    token.filePath = context->filePath;
    token.line = line;
    token.col = column;
    token.pos = index;
    token.lineStart = lineStart;

    // 跳过开头的单引号
    index++;
    column++;

    if (index >= source.size())
    {
        Logger::Log(Logger::LogLevel::ERROR, {&source, context->filePath, "Unclosed char literal", line, column - 1, 1, lineStart});
    }

    char c = source[index];
    if (c == '\\')
    {
        // 处理转义字符
        index++;
        column++;
        if (index >= source.size())
        {
            Logger::Log(Logger::LogLevel::ERROR, {&source, context->filePath, "Unclosed char literal", line, column - 1, 1, lineStart});
        }

        char escape = source[index];
        switch (escape)
        {
        case 'n': token.value = "\n"; break;
        case 't': token.value = "\t"; break;
        case 'r': token.value = "\r"; break;
        case '\'': token.value = "'"; break;
        case '\\': token.value = "\\"; break;
        default: token.value = std::string("\\") + escape; break;
        }
    }
    else
    {
        token.value = std::string(1, c);
    }

    index++;
    column++;

    // 检查结束单引号
    if (index >= source.size() || source[index] != '\'')
    {
        Logger::Log(Logger::LogLevel::ERROR, {&source, context->filePath, "Unclosed char literal", line, column - 1, 1, lineStart});
    }

    index++;
    column++;
    return token;
}

Token Lexer::lexNumber()
{
    std::string &source = context->fileValue;

    Token token;
    token.filePath = context->filePath;
    token.line = line;
    token.col = column;
    token.pos = index;
    token.lineStart = lineStart;

    std::string value;
    bool isFloat = false;
    bool hasExponent = false;

    while (index < source.size())
    {
        char c = source[index];

        if (std::isdigit(c))
        {
            value += c;
            index++;
            column++;
        }
        else if (c == '.' && !isFloat && !hasExponent)
        {
            // 处理浮点数的小数点
            if (index + 1 < source.size() && std::isdigit(source[index + 1]))
            {
                isFloat = true;
                value += c;
                index++;
                column++;
            }
            else
            {
                // 不是浮点数，可能是成员访问
                break;
            }
        }
        else if ((c == 'e' || c == 'E') && !hasExponent)
        {
            // 处理指数部分
            hasExponent = true;
            value += c;
            index++;
            column++;

            // 检查指数符号
            if (index < source.size() && (source[index] == '+' || source[index] == '-'))
            {
                value += source[index];
                index++;
                column++;
            }
        }
        else
        {
            break;
        }
    }

    token.code = isFloat || hasExponent ? TokenCode::FLOAT_LITERAL : TokenCode::INT_LITERAL;
    token.value = value;
    return token;
}

Token Lexer::lexIdentifier()
{
    std::string &source = context->fileValue;

    Token token;
    token.filePath = context->filePath;
    token.line = line;
    token.col = column;
    token.pos = index;
    token.lineStart = lineStart;

    std::string value;
    while (index < source.size())
    {
        char c = source[index];
        if (std::isalnum(c) || c == '_')
        {
            value += c;
            index++;
            column++;
        }
        else
        {
            break;
        }
    }

    // 检查是否是关键字
    if (auto pos = getKeywordPoistion(value); pos)
    {
        // 将索引转换为TokenCode
        token.code = static_cast<TokenCode>(*pos + static_cast<size_t>(TokenCode::IMPT));
    }
    else
    {
        token.code = TokenCode::IDENTIFIER;
    }

    token.value = value;
    return token;
}

Token Lexer::lexOperatorOrDelimiter()
{
    std::string &source = context->fileValue;

    Token token;
    token.filePath = context->filePath;
    token.line = line;
    token.col = column;
    token.pos = index;
    token.lineStart = lineStart;

    char current = source[index];

    // 处理双字符运算符
    if (index + 1 < source.size())
    {
        char next = source[index + 1];
        std::string twoChars = std::string(1, current) + next;

        if (twoChars == "::")
        {
            token.code = TokenCode::DOUBLE_COLON;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == "==")
        {
            token.code = TokenCode::EQ_EQ;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == "!=")
        {
            token.code = TokenCode::NOT_EQ;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == "<=")
        {
            token.code = TokenCode::LT_EQ;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == ">=")
        {
            token.code = TokenCode::GT_EQ;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == "->")
        {
            token.code = TokenCode::ARROW;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == "=>")
        {
            token.code = TokenCode::DOUBLE_ARROW;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == "||")
        {
            token.code = TokenCode::OR;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
        if (twoChars == "&&")
        {
            token.code = TokenCode::AND;
            token.value = twoChars;
            index += 2;
            column += 2;
            return token;
        }
    }

    // 处理单字符运算符和分隔符
    switch (current)
    {
    case '+': token.code = TokenCode::PLUS; break;
    case '-': token.code = TokenCode::MINUS; break;
    case '*': token.code = TokenCode::STAR; break;
    case '/': token.code = TokenCode::SLASH; break;
    case '<': token.code = TokenCode::LT; break;
    case '>': token.code = TokenCode::GT; break;
    case '=': token.code = TokenCode::ASSIGN; break;
    case '{': token.code = TokenCode::LBRACE; break;
    case '}': token.code = TokenCode::RBRACE; break;
    case '(': token.code = TokenCode::LPAREN; break;
    case ')': token.code = TokenCode::RPAREN; break;
    case ',': token.code = TokenCode::COMMA; break;
    case ':': token.code = TokenCode::COLON; break;
    case ';': token.code = TokenCode::SEMI; break;
    case '.': token.code = TokenCode::DOT; break;
    case '&': token.code = TokenCode::REFERENCE; break;
    case '!': token.code = TokenCode::NOT; break;
    case '|': token.code = TokenCode::BOR; break;
    default:
        Logger::Log(Logger::LogLevel::ERROR, {&source, context->filePath, "Unknown character '" + std::string(1, current) + "'", line, column, 1, lineStart});
    }

    token.value = std::string(1, current);
    index++;
    column++;
    return token;
}
