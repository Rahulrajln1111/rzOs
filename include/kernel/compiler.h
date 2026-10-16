/*
 * include/kernel/compiler.h
 *
 * Compiler attribute macros, mirroring the subset of Linux's
 * include/linux/compiler_types.h that we actually use.
 */
#ifndef _KERNEL_COMPILER_H
#define _KERNEL_COMPILER_H

#define __packed        __attribute__((packed))
#define __aligned(x)    __attribute__((aligned(x)))
#define __noreturn      __attribute__((noreturn))
#define __always_inline inline __attribute__((always_inline))
#define __unused        __attribute__((unused))

#define __percpu                        /* sparse-only qualifier; empty for us  */
#define ____cacheline_aligned_in_smp \
        __attribute__((__aligned__(64)))  /* SMP: keep per-CPU counters off one cache line */

#endif /* _KERNEL_COMPILER_H */
