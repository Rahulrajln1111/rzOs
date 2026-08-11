/*
 * arch/x86_64/cpu/gdt.c
 *
 * Minimal GDT, following the pattern of Linux's
 * arch/x86/kernel/cpu/common.c (gdt_page / setup_gdt).
 *
 * Limine already runs us with *some* GDT loaded, but its layout is
 * internal and undocumented. We install our own so the IDT's code
 * segment selector (0x08) is guaranteed to be a valid ring-0 kernel
 * code segment.
 *
 * Descriptor layout (8 bytes each):
 *   bits  0-15   limit[15:0]
 *   bits 16-31   base[15:0]
 *   bits 32-39   base[23:16]
 *   bits 40-47   access byte
 *   bits 48-55   limit[19:16] | flags
 *   bits 56-63   base[31:24]
 */
#include <kernel/types.h>
#include <kernel/compiler.h>
#include "gdt.h"

struct gdt_desc {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  limit_flags;
    u8  base_high;
} __packed;

/* 0 = null, 1 = kernel code, 2 = kernel data. Flat model: base 0,
 * limit 0xFFFFF (4 GiB granularity), which combined with paging
 * covers the entire 64-bit address space.
 *
 * access 0x9A = P=1, DPL=0, S=1 (code/data), X=1, R=1
 * access 0x92 = P=1, DPL=0, S=1, W=1
 * limit_flags 0xAF = G=1, D/B=0, L=1 (long mode code)
 * limit_flags 0xCF = G=1, D/B=1, L=0
 */
static struct gdt_desc gdt[3] = {
    [0] = { 0 },
    [1] = { .limit_low = 0xFFFF, .access = 0x9A, .limit_flags = 0xAF },
    [2] = { .limit_low = 0xFFFF, .access = 0x92, .limit_flags = 0xCF },
};

/* 10-byte GDTR pseudo-descriptor */
static struct {
    u16 limit;
    u64 base;
} __packed gdtr;

void gdt_init(void)
{
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (u64)gdt;

    __asm__ volatile(
        "lgdt %0\n\t"
        /* reload CS with a far return through our kernel code selector */
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        /* reload the data selectors */
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        : : "m"(gdtr) : "rax", "memory");
}
