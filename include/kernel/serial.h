/*
 * include/kernel/serial.h
 *
 * Public interface of the 16550 (COM1) serial driver.
 *
 * This is the kernel's early debug console. In Linux terms this
 * interface plays the role of the console/printk output path, while
 * drivers/serial/serial.c is the actual UART driver
 * (Linux: drivers/tty/serial/8250/).
 */
#ifndef _KERNEL_SERIAL_H
#define _KERNEL_SERIAL_H

#include <kernel/types.h>

void serial_init(void);
void serial_write(const char *s);
void serial_puthex(u64 value);
void serial_putdec(u64 value);

#endif /* _KERNEL_SERIAL_H */
