#pragma once
/*
 * libkern_api.hpp -- aggregated C++ API for the xkern libkern C library.
 *
 * This header gathers the public API declared by every libkern C source file
 * (ctype, string, stdlib, stdio, scanf, printf, klog, logger, klibc,
 * stdarg, stdint, udivdi3) into a single C++-usable surface. Every function
 * keeps its C linkage, so C++ kernel code links directly against the C
 * objects built from libkern/libkern/*.c.
 *
 * Usage from a new .cpp kernel feature:
 *     #include "libkern_api.hpp"
 *     kernel::memcpy(dst, src, n);
 */
#include "ctype.hpp"
#include "string.hpp"
#include "stdlib.hpp"
#include "stdio.hpp"
#include "scanf.hpp"
#include "printf.hpp"
#include "klog.hpp"
#include "logger.hpp"
#include "klibc.hpp"
#include "stdarg.hpp"
#include "stdint.hpp"
#include "udivdi3.hpp"
#include "crypto_hash.hpp"
#include "crypto_cipher.hpp"
#include "crypto_key.hpp"
#include "crypto_util.hpp"
