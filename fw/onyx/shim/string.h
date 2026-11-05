/* Juice-Box: the two string.h functions Onyx's engine uses; fw/felucca/src/libc.c defines them (no libc in this build). */
#ifndef JB_SHIM_STRING_H
#define JB_SHIM_STRING_H
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif
