#ifndef _X86_64_CPU_PERCPU_H
#define _X86_64_CPU_PERCPU_H
 
#include <kernel/types.h>
#include <linux/percpu-defs.h>
#include <x86_64/cpu/msr.h>

extern char __per_cpu_start[];
 
/* The current CPU's per-cpu area base == its GS base. */
static inline unsigned long this_cpu_base(void)
{
    return rdmsr(MSR_GS_BASE);
}
#define this_cpu_ptr(pcp)\
  (void*)(this_cpu_base()+(unsigned long)(pcp) - (unsigned long)__per_cpu_start)


#define this_cpu_read(var)      (*(typeof(var) *)this_cpu_ptr(&(var)))
#define this_cpu_write(var, v)  (*(typeof(var) *)this_cpu_ptr(&(var)) = (v))
#define this_cpu_inc(var)       (++(*(typeof(var) *)this_cpu_ptr(&(var))))
#define this_cpu_add(var, v)    (*(typeof(var) *)this_cpu_ptr(&(var)) += (v))

DECLARE_PER_CPU(int, cpu_number);
 
static inline int smp_processor_id(void)
{
    return this_cpu_read(cpu_number);
}

void setup_per_cpu(void);
 
#endif