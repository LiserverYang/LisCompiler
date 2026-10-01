/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 * Turning the TEXT of a numeric literal into its value.
 */

#pragma once

#include <cstdint>
#include <string>

/// Parse the text of an INT_LITERAL token.
///
/// Decimal by default, hexadecimal for a `0x`/`0X` prefix. `_` may separate digits
/// and carries no meaning (`0x4002_0000`, `1_000_000`).
///
/// Deliberately NOT `std::stoll(text, nullptr, 0)`: base 0 reads a leading zero as
/// OCTAL, which would silently turn the decimal literal `0777` into 511. The prefix
/// is checked explicitly instead.
///
/// Throws std::out_of_range / std::invalid_argument exactly like the std::stoll it
/// replaces, and every caller already catches that (an overflowing literal is
/// reported by the semantic analyzer, not fatal).
inline int64_t parseIntegerLiteral(const std::string &text)
{
    std::string digits;
    digits.reserve(text.size());
    for (char c : text)
        if (c != '_')
            digits.push_back(c);

    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        return static_cast<int64_t>(std::stoull(digits.substr(2), nullptr, 16));
    return std::stoll(digits, nullptr, 10);
}

/// Parse the text of a FLOAT_LITERAL token. Digit separators are stripped here too:
/// `std::stod("1_000.5")` would stop at the separator and silently return 1.0.
inline double parseFloatLiteral(const std::string &text)
{
    std::string digits;
    digits.reserve(text.size());
    for (char c : text)
        if (c != '_')
            digits.push_back(c);

    return std::stod(digits);
}
