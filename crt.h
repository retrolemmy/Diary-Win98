/* See crt.c: the C library functions Diary provides itself. */
#ifndef DIARY_CRT_H
#define DIARY_CRT_H

#include <string.h>

/* Copies a string into a buffer of size bytes, cutting it off if needed. */
void CopyString(char *to, const char *from, size_t size);

#endif
