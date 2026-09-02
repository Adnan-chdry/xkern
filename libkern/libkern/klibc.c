#include "klibc.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "ctype.h"
#include "math.h"

struct klibc klibc = {
    .printf    = printf,
    .snprintf  = snprintf,
    .vsnprintf = vsnprintf,
    .sprintf   = sprintf,
    .putchar   = putchar,
    .puts      = puts,
    .getchar   = getchar,
    .scanf     = scanf,
    .sscanf    = sscanf,

    .memcpy  = memcpy,
    .memmove = memmove,
    .memset  = memset,
    .memcmp  = memcmp,
    .memchr  = memchr,

    .strlen  = strlen,
    .strcmp  = strcmp,
    .strncmp = strncmp,
    .strcpy  = strcpy,
    .strncpy = strncpy,
    .strcat  = strcat,
    .strncat = strncat,
    .strchr  = strchr,
    .strrchr = strrchr,
    .strstr  = strstr,

    .atoi = atoi,
    .atol = atol,
    .abs  = abs,
    .labs = labs,
    .atof   = atof,
    .strtod = strtod,
    .strtof = strtof,

    .fabs  = fabs,
    .fmod  = fmod,
    .pow10 = pow10,

    .isdigit  = isdigit,
    .isalpha  = isalpha,
    .isalnum  = isalnum,
    .isupper  = isupper,
    .islower  = islower,
    .isspace  = isspace,
    .isprint  = isprint,
    .isxdigit = isxdigit,
    .toupper  = toupper,
    .tolower  = tolower,
};
