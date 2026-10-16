/*
 * include/kernel/string.h
 *
 * String/memory helpers, mirroring the subset of Linux's
 * include/linux/string.h that a kernel needs before any libc exists.
 */
#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include <kernel/types.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void *memmove(void *dst, const void *src, size_t n);


#endif /* _KERNEL_STRING_H */
