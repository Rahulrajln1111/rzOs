/*
 * arch/x86_64/boot/entry.c
 *
 * Kernel entry point. Limine enters here in long mode, ring 0, with
 * paging enabled and a stack already set up.
 *
 * This plays the role of Linux's arch/x86/kernel/head_64.S: an arch
 * bootstrap that hands off to the architecture-independent
 * start_kernel() in kernel/init.c.
 */
#include <kernel/types.h>

extern void start_kernel(void);

void _start(void)
{
    start_kernel();

    /* start_kernel() never returns; loop defensively anyway. */
    for (;;)
        __asm__ volatile("hlt");
}
