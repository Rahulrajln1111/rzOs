/*
 * kernel/panic.c
 *
 * The kernel's last resort: print a message and stop.
 * Mirrors the role of Linux's kernel/panic.c (minus the 1000 lines
 * of crash-dump machinery we don't need yet).
 */
#include <kernel/types.h>
#include <kernel/serial.h>
#include <kernel/panic.h>

__noreturn void panic(const char *msg)
{
    serial_write("\r\nKERNEL PANIC: ");
    serial_write(msg);
    serial_write("\r\nHalting.\r\n");

    for (;;)
        __asm__ volatile("hlt");
}
