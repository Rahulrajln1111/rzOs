/*
 * arch/x86_64/cpu/io.h
 *
 * Port I/O helpers, mirroring Linux's arch/x86/include/asm/io.h.
 * "static inline" means every includer gets its own private copy of
 * the function body - no link conflicts, no call overhead.
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <kernel/types.h>

/* "a"(value): put value in AL. "Nd"(port): port is an 8-bit
 * immediate (N) or DX (d), no segment override. volatile: this is
 * real I/O, never reorder, merge, or delete it. */
static inline void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Writing to port 0x80 (a legacy "post code" port with no side
 * effects on x86) takes a few cycles. The 8259 PIC requires small
 * delays between its initialization writes, so we use this as a
 * portable "sleep a microsecond-ish" primitive. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif /* _ASM_IO_H */
