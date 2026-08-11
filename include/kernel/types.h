/*
 * include/kernel/types.h
 *
 * Basic kernel typedefs, following the style of Linux's
 * include/linux/types.h. Uses the same u8/u16/u32/u64 naming that
 * every piece of Linux documentation and code uses, so anything you
 * read on kernel.org reads naturally in this tree.
 */
#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <stddef.h>
#include <stdbool.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

typedef signed char    s8;
typedef signed short   s16;
typedef signed int     s32;
typedef signed long long s64;

typedef unsigned long ulong;
typedef unsigned int  uint;

#endif /* _KERNEL_TYPES_H */
