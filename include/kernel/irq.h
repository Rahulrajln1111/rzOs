/*
 * include/kernel/irq.h
 *
 * Generic external-interrupt dispatch core. Linux equivalent:
 * kernel/irq/ - drivers "request" an IRQ, the core calls their
 * handler when the interrupt arrives.
 */
#ifndef _KERNEL_IRQ_H
#define _KERNEL_IRQ_H

/* Forward declaration: a header only needs to know the type EXISTS
 * to declare a pointer to it. The full definition lives in
 * arch/x86_64/cpu/idt.h, included only by the .c files that need it. */
struct trap_frame;

void irq_register(int irq, void (*handler)(void));
void interrupt_handler(struct trap_frame *f);

#endif /* _KERNEL_IRQ_H */
