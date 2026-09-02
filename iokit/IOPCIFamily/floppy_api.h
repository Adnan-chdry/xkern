/*
 * API bridge for generic_floppy.cpp
 * Exposed to the C language.
 */

#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLOPPY_NONE     0
#define FLOPPY_PRESENT  1
#define FLOPPY_CHANGED  2

int floppy_detect_controller(void);
int floppy_detect_disk(u8 drive);

#ifdef __cplusplus
}
#endif
