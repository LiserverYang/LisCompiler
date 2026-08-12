/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * MIT License.
 */

#include "Lexer/Lexer.hpp"
#include "Lexer/TokenStreamPrinter.hpp"
#include "Logger/ErrorID.hpp"
#include "Logger/Logger.hpp"
#include <exception>
#include <iostream>
#include <string>

void Lexer::run()
{
    // Count lexing errors from THIS file (the Parser's error-count gate then
    // sees both lexer and parser errors — a lexer error must not compile).
    Logger::ResetErrorCount();

    TokenStream &tokens = context->tokenStream;
    std::string &source = context->fileValue;

    // process each character in source code sequentially
    while (index < source.size())
    {
        char current = source[index];

        // P9: ctype functions require an `unsigned char` (or EOF); a signed
        // char with a byte > 127 is negative → UB. Cast at every ctype call.
        unsigned char uc = (unsigned char)current;

        // skip whitespace characters (space, tab, newline, etc.)
        if (std::isspace(uc))
        {
            skipWhitespace();
            continue;
        }

        // handle comments (both line and block comments)
        if (current == '/' && index + 1 < source.size())
        {
            char next = source[index + 1];
            if (next == '/') // line comment
            {
                skipLineComment();
                continue;
            }
            if (next == '*') // block comment
            {
                skipBlockComment();
                continue;
            }
        }

        // handle string literals (double-quoted)
        if (current == '"')
        {
            tokens.push_back(lexStringLiteral());
            continue;
        }
        // handle character literals (single-quoted)
        if (current == '\'')
        {
            tokens.push_back(lexCharLiteral());
            continue;
        }
        // handle numeric literals (integers/floats)
        if (std::isdigit(uc))
        {
            tokens.push_back(lexNumber());
            continue;
        }

        // handle identifiers and keywords (start with letter or underscore)
        if (std::isalpha(uc) || current == '_')
        {
            tokens.push_back(lexIdentifier());
            continue;
        }

        // handle operators and delimiters as fallthrough
        tokens.push_back(lexOperatorOrDelimiter());
    }

    if (context->args->getArg("print_tokenstream").compare("true") == 0)
    {
        PrintTokenStream(tokens);
    }
}

// skips whitespace characters while tracking line/column position
void Lexer::skipWhitespace()
{
    std::string &source = context->fileValue;

    while (index < source.size() && std::isspace((unsigned char)source[index]))
    {
        // handle newline: increment line counter and reset column
        if (source[index] == '\n')
        {
            line++;
            lineStart = index + 1; // track start of next line
            column = 1;            // reset column counter
        }
        else
        {
            column++; // increment column for non-newline whitespace
        }
        index++; // move to next character
    }
}

// skips single-line comments (// ...)
void Lexer::skipLineComment()
{
    std::string &source = context->fileValue;

    // skip the initial '//' characters
    index += 2;
    column += 2;

    // advance until end of line or file
    while (index < source.size() && source[index] != '\n')
    {
        index++;
        column++;
    }

    // prepare for next line (if any)
    lineStart = index;
}

// skips block comments (/* ... */)
void Lexer::skipBlockComment()
{
    std::string &source = context->fileValue;

    // skip initial '/*'
    index += 2;
    column += 2;

    while (index < source.size())
    {
        // check for comment end marker '*/'
        if (source[index] == '*' && index + 1 < source.size() && source[index + 1] == '/')
        {
            index += 2; // skip '*/'
            column += 2;
            return; // exit after closing comment
        }

        // handle newlines within comment
        if (source[index] == '\n')
        {
            line++; // increment line counter
            lineStart = index + 1;
            column = 1; // reset column counter
        }
        else
        {
            column++; // track column position
        }

        index++; // move to next character
    }

    // Reached EOF without a closing "*/" — report it instead of failing silently.
    // P17: recoverable (exit=false) — see the unknown-char site for why.
    {
        Logger::LogInfo info = {&source, context->filePath, "Unclosed block comment",
                                line, column - 1, 1, lineStart, E_UnclosedBlockComment};
        info.exit = false;
        Logger::Log(Logger::LogLevel::ERROR, info);
    }
}

// tokenizes string literals ("...")
Token Lexer::lexStringLiteral()
{
    std::string &source = context->fileValue;

    Token token;
    token.code = TokenCode::STRING_LITERAL;
    token.position.filePath = &context->filePath;
    token.position.line = line;
    token.position.col = column;
    token.position.pos = index;
    token.position.lineStart = lineStart;

    // skip opening quote
    index++;
    column++;

    std::string value;
    bool escape = false; // track escape sequences

    while (index < source.size())
    {
        char c = source[index];

        // handle newlines in string (update position tracking)
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

        // process escape sequences
        if (escape)
        {
            switch (c)
            {
            case 'n': value += '\n'; break;  // newline
            case 't': value += '\t'; break;  // tab
            case 'r': value += '\r'; break;  // carriage return
            case '0': value += '\0'; break;  // NUL (matches the char literal)
            case '"': value += '"'; break;   // literal quote
            case '\\': value += '\\'; break; // literal backslash
            default:                         // invalid escape
                value += '\\';
                value += c;
                break;
            }
            escape = false;
        }
        else if (c == '\\') // start escape sequence
        {
            escape = true;
        }
        else if (c == '"') // closing quote
        {
            index++; // skip closing quote
            token.value = value;
            return token;
        }
        else
        {
            value += c; // normal character
        }

        index++;
    }

    // handle unclosed string error (P17: recoverable, exit=false — see the
    // unknown-char site for why an exit=true lexer error kills Debug builds).
    {
        Logger::LogInfo info = {&source, context->filePath, "Unclosed string literal",
                                line, column - 1, 1, lineStart, E_UnClosedStringLiteral};
        info.exit = false;
        Logger::Log(Logger::LogLevel::ERROR, info);
    }

    return token; // return partial token on error
}

// tokenizes character literals ('a', '\n', etc.)
Token Lexer::lexCharLiteral()
{
    std::string &source = context->fileValue;

    Token token;
    token.code = TokenCode::CHAR_LITERAL;
    token.position.filePath = &context->filePath;
    token.position.line = line;
    token.position.col = column;
    token.position.pos = index;
    token.position.lineStart = lineStart;

    // skip opening quote
    index++;
    column++;

    // Lexer errors are RECOVERABLE (exit=false): the Parser gate decides at the
    // end, so ALL errors in a file surface instead of the first one killing the
    // process mid-lex. This mirrors the Parser's recoverable-error philosophy.
    auto logUnclosed = [&]()
    {
        Logger::LogInfo info = {&source, context->filePath, "Unclosed char literal", line, column - 1, 1, lineStart, E_UnclosedCharLiteral};
        info.exit = false;
        Logger::Log(Logger::LogLevel::ERROR, info);
    };

    // check for immediate EOF
    if (index >= source.size())
    {
        // P10: after logging the error, return — do NOT fall through to
        // `char c = source[index]` which reads out of bounds (operator[] only
        // guarantees '\0' at exactly size(); past that it is UB).
        logUnclosed();
        return token;
    }

    char c = source[index];
    if (c == '\\') // escape sequence
    {
        index++;
        column++;
        // validate escape sequence length
        if (index >= source.size())
        {
            // P10: same OOB-read guard — return instead of reading source[index].
            logUnclosed();
            return token;
        }

        char escape = source[index];
        switch (escape)
        {
        case 'n': token.value = "\n"; break;
        case 't': token.value = "\t"; break;
        case 'r': token.value = "\r"; break;
        case '\'': token.value = "'"; break;
        case '\\': token.value = "\\"; break;
        case '0': token.value = std::string(1, '\0'); break; // null char
        default: token.value = std::string("\\") + escape;   // invalid escape
        }
    }
    else // normal character
    {
        token.value = std::string(1, c);
    }

    index++;
    column++;

    // verify closing quote exists (P17: recoverable, exit=false — see the
    // unknown-char site for why an exit=true lexer error kills Debug builds).
    if (index >= source.size() || source[index] != '\'')
    {
        Logger::LogInfo info = {&source, context->filePath, "Unclosed char literal",
                                line, column - 1, 1, lineStart, E_UnclosedCharLiteral};
        info.exit = false;
        Logger::Log(Logger::LogLevel::ERROR, info);
    }

    // skip closing quote
    index++;
    column++;
    return token;
}

// tokenizes numeric literals (integers and floats)
Token Lexer::lexNumber()
{
    std::string &source = context->fileValue;

    Token token;
    token.position.filePath = &context->filePath;
    token.position.line = line;
    token.position.col = column;
    token.position.pos = index;
    token.position.lineStart = lineStart;

    std::string value;
    bool isFloat = false;     // flag for decimal points
    bool hasExponent = false; // flag for scientific notation

    while (index < source.size())
    {
        char c = source[index];

        // accumulate digits
        // P9: cast to unsigned char — ctype UB on negative char values
        if (std::isdigit((unsigned char)c))
        {
            value += c;
            index++;
            column++;
        }
        // decimal point handling (must be followed by digit)
        else if (c == '.' && !isFloat && !hasExponent)
        {
            if (index + 1 < source.size() && std::isdigit((unsigned char)source[index + 1]))
            {
                isFloat = true;
                value += c;
                index++;
                column++;
            }
            else
            {
                break; // not a float (e.g., member access)
            }
        }
        // exponent handling (e/E followed by optional sign then REQUIRED digit)
        else if ((c == 'e' || c == 'E') && !hasExponent)
        {
            hasExponent = true;
            value += c;
            index++;
            column++;

            // capture exponent sign
            if (index < source.size() && (source[index] == '+' || source[index] == '-'))
            {
                value += source[index];
                index++;
                column++;
            }

            // A float exponent MUST be followed by a digit: `1e` / `1e+` / `1e-`
            // are malformed. If we accepted them, HIRBuilder's std::stod would
            // throw std::invalid_argument → std::terminate with no diagnostic.
            // Report here (lexing continues to surface all errors; the Parser
            // gate stops the compile with a clean error), and still emit the
            // token so the stream stays in sync. `exit=false` keeps this a
            // recoverable error like the Parser's — a lexer error must NOT
            // DEBUG_POINT/exit mid-lex, or the rest of the file's errors are
            // never surfaced.
            if (index >= source.size() || !std::isdigit((unsigned char)source[index]))
            {
                Logger::LogInfo info = {&source, context->filePath, "malformed float: exponent must be followed by a digit", line, column - 1, 1, index, E_InvalidLiteralType};
                info.exit = false;
                Logger::Log(Logger::LogLevel::ERROR, info);
            }
        }
        else
        {
            break; // end of numeric literal
        }
    }

    // determine token type based on flags
    token.code = (isFloat || hasExponent)
                     ? TokenCode::FLOAT_LITERAL
                     : TokenCode::INT_LITERAL;
    token.value = value;
    return token;
}

// tokenizes identifiers and keywords
Token Lexer::lexIdentifier()
{
    std::string &source = context->fileValue;

    Token token;
    token.position.filePath = &context->filePath;
    token.position.line = line;
    token.position.col = column;
    token.position.pos = index;
    token.position.lineStart = lineStart;

    std::string value;
    // accumulate alphanumeric + underscore characters
    while (index < source.size())
    {
        char c = source[index];
        // P9: cast to unsigned char — ctype UB on negative char values
        if (std::isalnum((unsigned char)c) || c == '_')
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

    // check if identifier is a reserved keyword
    if (auto pos = getKeywordPoistion(value); pos)
    {
        // convert keyword index to TokenCode
        // (offset by IMPT enum value)
        token.code = static_cast<TokenCode>(
            *pos + static_cast<size_t>(TokenCode::IMPT));
    }
    else
    {
        token.code = TokenCode::IDENTIFIER;
    }

    token.value = value;
    return token;
}

// tokenizes operators and delimiters
Token Lexer::lexOperatorOrDelimiter()
{
    std::string &source = context->fileValue;

    Token token;
    token.position.filePath = &context->filePath;
    token.position.line = line;
    token.position.col = column;
    token.position.pos = index;
    token.position.lineStart = lineStart;

    char current = source[index];

    // handle multi-character operators first
    if (index + 1 < source.size())
    {
        char next = source[index + 1];
        std::string twoChars = std::string(1, current) + next;

        // check for known two-character operators
        if (twoChars == "::")
        {
            token.code = TokenCode::DOUBLE_COLON;
        }
        else if (twoChars == "==")
        {
            token.code = TokenCode::EQ_EQ;
        }
        else if (twoChars == "!=")
        {
            token.code = TokenCode::NOT_EQ;
        }
        else if (twoChars == "<=")
        {
            token.code = TokenCode::LT_EQ;
        }
        else if (twoChars == ">=")
        {
            token.code = TokenCode::GT_EQ;
        }
        else if (twoChars == "->")
        {
            token.code = TokenCode::ARROW;
        }
        else if (twoChars == "=>")
        {
            token.code = TokenCode::DOUBLE_ARROW;
        }
        else if (twoChars == "||")
        {
            token.code = TokenCode::OR;
        }
        else if (twoChars == "&&")
        {
            token.code = TokenCode::AND;
        }
        else if (twoChars == "#[")
        {
            token.code = TokenCode::ATTRIBUTE_START;
        }
        else
            goto single_char; // not multi-character operator

        // handle matched two-character operator
        token.value = twoChars;
        index += 2;
        column += 2;
        return token;
    }

single_char:
    // handle single-character operators/delimiters
    switch (current)
    {
    case '+': token.code = TokenCode::PLUS; break;
    case '-': token.code = TokenCode::MINUS; break;
    case '*': token.code = TokenCode::STAR; break;
    case '/': token.code = TokenCode::SLASH; break;
    case '%': token.code = TokenCode::MOD; break;
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
    case '[': token.code = TokenCode::LBRACKET; break;
    case ']': token.code = TokenCode::RBRACKET; break;
    default: // unknown character
        // P17: a lexer error must be RECOVERABLE (exit=false), not exit=true.
        // LogInfo::exit defaults to true, and in Debug builds an exit=true error
        // hits DEBUG_POINT() (int 3) and KILLS the process — so any source with
        // an unknown character (`@`, `$`, ...) crashed the compiler instead of
        // surfacing the error and letting the Parser gate stop the build. Same
        // rule as the P1/P10 lexer-error fixes: report and continue lexing.
        {
            Logger::LogInfo info = {&source, context->filePath,
                "Unknown character '" + std::string(1, current) + "'",
                line, column, 1, lineStart, E_UnknownCharacter};
            info.exit = false;
            Logger::Log(Logger::LogLevel::ERROR, info);
        }
    }

    token.value = std::string(1, current);
    index++;
    column++;
    return token;
}