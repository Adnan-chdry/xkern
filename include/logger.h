#ifndef LOGGER_H
#define LOGGER_H

#include "types.h"

#define LOGGER_ORG "zane"
#define LOGGER_ENV "2026-07\n"

void logger_log(const char *main_text);
void logger_init(void);

#endif