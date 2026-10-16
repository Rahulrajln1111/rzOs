/*
 * lib/string.c
 *
 * Minimal string/memory routines, written in the style of Linux's
 * lib/string.c. Straightforward byte loops; no libc dependency.
 */
#include <kernel/types.h>
#include <kernel/string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;

    for (size_t i = 0; i < n; i++)
        d[i] = s[i];

    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    u8 *d = dst;

    for (size_t i = 0; i < n; i++)
        d[i] = (u8)c;

    return dst;
}

size_t strlen(const char *s)
{
    size_t len = 0;

    while (s[len])
        len++;

    return len;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }

    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}


void *memmove(void *dst, const void *src, size_t n){
   
    const u8 *s = src;
    u8 *d = dst;

    if(d < s){
        for(size_t i = 0;i<n;i++){
            d[i] = s[i];
        }
    }
    else if(d>s){
        for(size_t i=n;i>0;i--){
            d[i-1] = s[i-1];
        }
    }

    return dst;
}
