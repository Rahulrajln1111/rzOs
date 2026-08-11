/*
 * kernel/irq.c
 *
 * Generic external-interrupt dispatch.
 *
 * The CPU delivers IRQ n on vector 32+n (after pic_remap). The stub
 * in idt_stubs.S saves registers and calls interrupt_handler(),
 * which routes exceptions (0..31) to the panic path and IRQs
 * (32..255) to irq_handler(), which calls the registered C handler
 * and then sends the EOI (End Of Interrupt) so the PIC can raise
 * the next interrupt.
 *
 * Linux equivalent: kernel/irq/irqdesc.c (the irq_desc table).
 */
#include <kernel/types.h>
#include <kernel/irq.h>
#include <x86_64/cpu/io.h>
#include <x86_64/cpu/idt.h>

#define PIC1_CMD  0x20
#define PIC2_CMD  0xA0
#define PIC_EOI   0x20   /* "end of interrupt" acknowledgement */

/* Dispatch table: irq number -> handler function.
 * Read inside-out: irq_handlers is an array [16] of pointers
 * (*) to functions that take (void) and return void.
 * Zero-initialized (all NULL) because it's static. */
static void (*irq_handlers[16])(void);

/* Register (or replace) the handler for one IRQ. This is the
 * "request_irq" moment: the driver owns the number, the core
 * owns the dispatch. */
void irq_register(int irq, void (*handler)(void))
{
    irq_handlers[irq] = handler;
}

/* Runs for every external IRQ (vectors 32..47 after the remap).
 * "static": private to this file, only called from below. */
static void irq_handler(struct trap_frame *f)
{
    int irq = (int)f->vector - 32;

    /* Defensive: gates exist only for vectors 32..47 today, but a
     * stray delivery must never index the table out of bounds. */
    if (irq < 0 || irq >= 16)
        return;

    if (irq_handlers[irq] != NULL)
        irq_handlers[irq]();        /* e.g. pit_irq_handler() */

    /* EOI: tell the PIC the IRQ is handled so it can deliver the
     * next one. Slave IRQs (8..15) must ack the slave first - the
     * slave's EOI alone doesn't clear the master's latch. */
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/* Common C entry for EVERY interrupt and exception; called from
 * isr_common in idt_stubs.S with rdi = &trap_frame.
 * Vectors 0..31 are CPU exceptions; 32..255 are external IRQs. */
void interrupt_handler(struct trap_frame *f)
{
    if (f->vector < 32)
        exception_handler(f);   /* prints the dump, panics, never returns */
    else
        irq_handler(f);         /* dispatches, EOIs, and returns          */
}
