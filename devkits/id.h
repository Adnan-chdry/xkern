#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct IDdriver
{
    const char *name;
    const char *ver;
    const char *type;
};

#ifdef __cplusplus
}
#endif

/*
 * cant make it pure cpp for now
 */
