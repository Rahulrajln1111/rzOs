/*
 * drivers/pit/pit.c
 *
 * 8253/8254 Programmable Interval Timer, channel 0.
 *
 * The PIT is a 16-bit down-counter fed by a 1.19318 MHz clock.
 * We load it with a divisor; when it counts down to zero it raises
 * IRQ 0 and (in mode 2, "rate generator") reloads itself - giving
 * us a precise periodic interrupt: the kernel's heartbeat.
 *
 * Linux equivalent: drivers/clocksource/i8253.c + kernel/time/.
 */
#include <kernel/types.h>
#include <kernel/pit.h>
#include <x86_64/cpu/io.h>

#define PIT_CMD        0x43   /* command register */
#define PIT_CH0        0x40   /* channel 0 data port */

#define PIT_BASE_FREQ  1193182UL   /* the PIT's input clock, in Hz */

volatile u64 jiffies = 0;

/*
 * Program channel 0. The command byte 0x36 means:
 *   bits 7-6 = 00  channel 0
 *   bits 5-4 = 11  access mode: write low byte, then high byte
 *   bits 3-1 = 010 mode 2 (rate generator: auto-reload, periodic)
 *   bit  0   = 0   binary counting (not BCD)
 *
 * The divisor is written as two 8-bit writes - LOW byte first,
 * then HIGH byte - because that is what "lobyte/hibyte" access
 * mode promises the PIT.
 */
void pit_init(u32 frequency_hz)
{
    u16 divisor = (u16)(PIT_BASE_FREQ / frequency_hz);

    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, divisor & 0xFF);         /* low byte  */
    outb(PIT_CH0, (divisor >> 8) & 0xFF);  /* high byte */
}

/*
 * Runs in interrupt context: IF=0, on the interrupted code's stack.
 * GOLDEN RULE of interrupt handlers: never print, allocate, or
 * sleep in here - the interrupted code could be in the middle of
 * anything. Just record state; let normal context do the printing.
 */
void pit_irq_handler(void)
{
    jiffies++;
}
