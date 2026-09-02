#pragma once
/*
 * libcpp -- C++ API for the xkern kernel.
 *
 * This is the single header to include from new C++ kernel features. It
 * pulls in the whole libkern C API (gathered per-module under api/) and
 * re-exports it into the `kernel` namespace with C linkage preserved, so
 * existing C functions are callable directly from C++, e.g.:
 *
 *     #include "libkern/libcpp/libcpp.hpp"
 *     kernel::memcpy(dst, src, n);
 *     kernel::klog("driver", "hello from C++\n");
 */
#include <cstdint>
#include <cstddef>

#include "libkern_api.hpp"

namespace kernel {
    // Re-export the gathered libkern C API into the kernel namespace for
    // qualified C++ use. Functions retain their C linkage (declared
    // extern "C" inside the api/*.hpp headers).
    using namespace libkern_api;
}
