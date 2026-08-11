/*
 * include/kernel/panic.h
 */
#ifndef _KERNEL_PANIC_H
#define _KERNEL_PANIC_H

#include <kernel/types.h>
#include <kernel/compiler.h>

__noreturn void panic(const char *msg);

#endif /* _KERNEL_PANIC_H */
