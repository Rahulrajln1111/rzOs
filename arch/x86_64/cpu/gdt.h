/*
 * arch/x86_64/cpu/gdt.h
 */
#ifndef _ASM_GDT_H
#define _ASM_GDT_H

/* GDT selector values (index << 3). Matches the classic Linux kernel
 * layout: entry 1 = kernel code, entry 2 = kernel data. */
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10

void gdt_init(void);

#endif /* _ASM_GDT_H */
