/* things the 3DS C library / libctru do not provide */
#ifndef _N3DS_COMPAT_H
#define _N3DS_COMPAT_H
#include <stddef.h>
#ifndef ESHUTDOWN
#define ESHUTDOWN 108
#endif
#ifdef __cplusplus
extern "C" {
#endif
struct servent { char *s_name; char **s_aliases; int s_port; char *s_proto; };
static inline struct servent *getservbyname(const char *name, const char *proto) { (void)name; (void)proto; return NULL; }
#ifdef __cplusplus
}
#endif
#endif
