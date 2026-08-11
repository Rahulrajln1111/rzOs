/*
 * include/kernel/pic.h
 *
 * Public interface of the 8259 PIC (Programmable Interrupt
 * Controller) driver. Linux equivalent: arch/x86/kernel/i8259.c.
 */
#ifndef _KERNEL_PIC_H
#define _KERNEL_PIC_H

void pic_remap(void);

/* Unmask a master IRQ (0..7). NOTE: slave IRQs (8..15) will need a
 * slave-mask variant when the keyboard/mouse milestones arrive. */
void pic_unmask(int irq);

#endif /* _KERNEL_PIC_H */
