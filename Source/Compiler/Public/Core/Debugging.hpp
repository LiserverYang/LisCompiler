/**
 * Copyrigt 2025, LiserverYang. All rights reserved.
 */

#pragma once

#if defined(_MSC_VER)
#define DEBUG_POINT() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__i386__) || defined(__x86_64__)
#define DEBUG_POINT() __asm__("int $3")
#elif defined(__arm__)
#define DEBUG_POINT() __asm__(".inst 0xe7f001f0")
#elif defined(__aarch64__)
#define DEBUG_POINT() __asm__(".inst 0xd4200000")
#else
#define DEBUG_POINT() __builtin_trap()
#endif
#else
#error "Unsupported compiler for DEBUG_POINT"
#endif