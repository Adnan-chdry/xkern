/*
 * DONT EDIT UNLESS THERE IS A NEW ADDON 'version_maker.sh'
 * add a extern const <> ; here if theres something new
 */

#ifndef _XK_VERSION_H_
#define _XK_VERSION_H_


#ifdef __cplusplus
extern "C" {
#endif

extern const char version[];
extern const char ostype[];
extern const char osrelease[];
extern const char arch[];
extern const char lang[];

extern const int osrelease_major;
extern const int osrelease_minor;
extern const int osrelease_rev;

#ifdef __cplusplus
}
#endif

#endif
