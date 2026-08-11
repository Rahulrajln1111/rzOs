/*
 * drivers/serial/serial.c
 *
 * Minimal 16550 UART (COM1, port 0x3F8) driver - polling mode.
 * This is the "drivers/tty/serial/8250" layer: raw port access,
 * init, and character output. Everything above it (printing) lives
 * in kernel/ and uses the API from include/kernel/serial.h.
 */
#include <kernel/types.h>
#include <kernel/serial.h>

#define COM1        0x3F8

#define REG_DATA    0     /* R/W: data register            */
#define REG_IER     1     /* W:   interrupt enable         */
#define REG_IIR     2     /* R:   interrupt identification */
#define REG_FCR     2     /* W:   FIFO control             */
#define REG_LCR     3     /* W:   line control             */
#define REG_MCR     4     /* W:   modem control            */
#define REG_LSR     5     /* R:   line status              */

#define LSR_THRE    0x20  /* transmit holding register empty */

static void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static u8 inb(u16 port)
{
    u8 value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void)
{
    outb(COM1 + REG_IER, 0x00);     /* disable interrupts           */
    outb(COM1 + REG_LCR, 0x80);     /* enable DLAB                  */
    outb(COM1 + REG_DATA, 0x01);    /* divisor low: 115200 baud     */
    outb(COM1 + REG_IER, 0x00);     /* divisor high                 */
    outb(COM1 + REG_LCR, 0x03);     /* 8N1, DLAB disabled           */
    outb(COM1 + REG_FCR, 0xC7);     /* FIFO on, clear, 14-byte thr. */
    outb(COM1 + REG_MCR, 0x0B);     /* IRQs, RTS/DSR set            */
}

static void serial_putc(char c)
{
    while ((inb(COM1 + REG_LSR) & LSR_THRE) == 0)
        __asm__ volatile("pause");

    outb(COM1 + REG_DATA, (u8)c);
}

void serial_write(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

void serial_puthex(u64 value)
{
    static const char digits[] = "0123456789abcdef";

    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xF]);
}

void serial_putdec(u64 value)
{
    char buf[21];
    int i = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }

    while (value) {
        buf[i++] = '0' + (char)(value % 10);
        value /= 10;
    }

    while (i > 0)
        serial_putc(buf[--i]);
}
