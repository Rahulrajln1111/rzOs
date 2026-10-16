#include <kernel/types.h>
#include <x86_64/cpu/msr.h>
#include <x86_64/cpu/percpu.h>
 
DEFINE_PER_CPU(int, cpu_number) = 0;
 
void setup_per_cpu(void)
{
    /* CPU 0: GS base = runtime address of the per-cpu section. */
    wrmsr(MSR_GS_BASE, (u64)(unsigned long)__per_cpu_start);
}