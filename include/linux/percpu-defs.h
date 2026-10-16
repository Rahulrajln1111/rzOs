#ifndef _LINUX_PERCPU_DEFS_H
#define _LINUX_PERCPU_DEFS_H

/* Every CPU gets its own copy of `name`, all in .data..percpu. */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data..percpu"))) type name
 
#define DECLARE_PER_CPU(type, name) \
    extern __attribute__((__section__(".data..percpu"))) type name
 


#endif