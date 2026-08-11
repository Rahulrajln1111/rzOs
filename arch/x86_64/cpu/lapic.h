/*
 * arch/x86_64/cpu/lapic.h
 */
#ifndef _ASM_LAPIC_H
#define _ASM_LAPIC_H

#include <kernel/types.h>

void lapic_init(u64 hhdm_offset);

#endif /* _ASM_LAPIC_H */
