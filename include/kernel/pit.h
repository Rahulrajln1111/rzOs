/*
 * include/kernel/pit.h
 *
 * Public interface of the 8253/8254 PIT timer driver. Linux
 * equivalent: drivers/clocksource/i8253.c (the "clocksource").
 */
#ifndef _KERNEL_PIT_H
#define _KERNEL_PIT_H

#include <kernel/types.h>

void pit_init(u32 frequency_hz);
void pit_irq_handler(void);

/* Number of timer ticks since boot. Written by pit_irq_handler()
 * in INTERRUPT CONTEXT, read by the idle loop - two different
 * execution worlds - so it must be volatile: the compiler must
 * re-read it from memory every time and never cache it in a
 * register. This is your first taste of concurrency. */
extern volatile u64 jiffies;

#endif /* _KERNEL_PIT_H */
