/*
 * The few C library functions Diary uses. Diary doesn't link a C runtime:
 * current MinGW runtimes call functions that Windows 98 doesn't have, so
 * the program imports nothing but the Windows API itself. GCC may also
 * call memcpy and memset on its own, for copying structures.
 */

#include <stddef.h>

void *memcpy(void *to, const void *from, size_t n)
{
    char *d = to;
    const char *s = from;

    while (n-- > 0)
        *d++ = *s++;
    return to;
}

void *memmove(void *to, const void *from, size_t n)
{
    char *d = to;
    const char *s = from;

    if (d < s) {
        while (n-- > 0)
            *d++ = *s++;
    } else {
        while (n-- > 0)
            d[n] = s[n];
    }
    return to;
}

void *memset(void *to, int c, size_t n)
{
    char *d = to;

    while (n-- > 0)
        *d++ = (char)c;
    return to;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;

    for (; n > 0; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;

    while (*p != 0)
        p++;
    return p - s;
}

char *strcpy(char *to, const char *from)
{
    char *d = to;

    while ((*d++ = *from++) != 0)
        ;
    return to;
}

char *strcat(char *to, const char *from)
{
    strcpy(to + strlen(to), from);
    return to;
}

/* Copies a string into a buffer of size bytes, cutting it off if needed. */
void CopyString(char *to, const char *from, size_t size)
{
    size_t i;

    for (i = 0; i + 1 < size && from[i] != 0; i++)
        to[i] = from[i];
    if (size > 0)
        to[i] = 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == 0)
            return NULL;
    }
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n > 0; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (*a == 0)
            return 0;
    }
    return 0;
}
