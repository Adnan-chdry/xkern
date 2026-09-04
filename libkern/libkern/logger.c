/*
 * outdated unused legecy logging system not needed
 * but still present for future use
 */
#include "logger.h"
#include "printf.h"
#include "klibc.h"

void logger_init(void) {
}

void logger_log(const char *main_text) {
    klibc.printf("xom.%s.logger: %s %s", LOGGER_ORG, main_text, LOGGER_ENV);
}
