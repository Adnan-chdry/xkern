#pragma once
// C++ API for udivdi3 -- gathered from libkern/libkern/udivdi3.c
// This module has no C header of its own; the public symbols are the
// 64-bit division/modulo runtime helpers the compiler emits calls to.
namespace libkern_api {
extern "C" {
    unsigned long long __udivdi3(unsigned long long num, unsigned long long den);
    unsigned long long __umoddi3(unsigned long long num, unsigned long long den);
    long long __divdi3(long long num, long long den);
    long long __moddi3(long long num, long long den);
}
}
