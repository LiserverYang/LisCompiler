/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 * Turning the TEXT of a numeric literal into its value.
 */

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
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
inline uint64_t parseIntegerMagnitude(const std::string &text)
{
    std::string digits;
    digits.reserve(text.size());
    for (char c : text)
        if (c != '_')
            digits.push_back(c);

    const bool hex = digits.starts_with("0x") || digits.starts_with("0X");
    const size_t prefix = hex ? 2 : 0;
    if (digits.size() == prefix || digits[prefix] == '-' || digits[prefix] == '+')
        throw std::invalid_argument("invalid integer literal");
    size_t consumed = 0;
    auto value = std::stoull(digits.substr(prefix), &consumed, hex ? 16 : 10);
    if (consumed != digits.size() - prefix)
        throw std::invalid_argument("invalid integer literal");
    return value;
}

inline int64_t parseIntegerLiteral(const std::string &text)
{
    const bool negative = text.starts_with('-');
    const uint64_t magnitude = parseIntegerMagnitude(negative ? text.substr(1) : text);
    constexpr auto max = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (magnitude > max + negative)
        throw std::out_of_range("integer literal out of range");
    if (negative && magnitude == max + 1)
        return std::numeric_limits<int64_t>::min();
    const auto value = static_cast<int64_t>(magnitude);
    return negative ? -value : value;
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
