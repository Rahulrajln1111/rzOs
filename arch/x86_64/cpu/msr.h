#ifndef _X86_64_CPU_MSR_H
#define _X86_64_CPU_MSR_H
 
#include <kernel/types.h>
 
#define MSR_FS_BASE        0xC0000100UL
#define MSR_GS_BASE        0xC0000101UL
#define MSR_KERNEL_GS_BASE 0xC0000102UL

static inline void wrmsr(u32 msr, u64 val)
{
    __asm__ volatile("wrmsr"
                     : : "c"(msr), "a"((u32)val), "d"((u32)(val >> 32))
                     : "memory");
}
 
static inline u64 rdmsr(u32 msr)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

#endif