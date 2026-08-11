#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

/* ==================================================================
 * Limine boot protocol requests (v11, base revision 6).
 *
 * The bootloader scans the kernel image for these magic values and
 * fills in each .response pointer BEFORE our entry point runs.
 * They must not be optimised away, hence "used" + a non-empty
 * dedicated section.
 * ================================================================== */

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

/* ==================================================================
 * COM1 (0x3F8) serial output - our debug console.
 * Visible on the host with: -serial stdio
 * ================================================================== */

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb(COM1 + 1, 0x00);            /* disable interrupts            */
    outb(COM1 + 3, 0x80);            /* enable DLAB                   */
    outb(COM1 + 0, 0x01);            /* divisor low  (115200 baud)     */
    outb(COM1 + 1, 0x00);            /* divisor high                  */
    outb(COM1 + 3, 0x03);            /* 8N1, DLAB disabled            */
    outb(COM1 + 2, 0xC7);            /* FIFO on, clear, 14-byte thr.   */
    outb(COM1 + 4, 0x0B);            /* IRQs, RTS/DSR set             */
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0)
        __asm__ volatile("pause");
    outb(COM1, (uint8_t)c);
}

static void serial_write(const char *s) {
    while (*s)
        serial_putc(*s++);
}

static void serial_puthex(uint64_t value) {
    const char digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xF]);
}

static void serial_putdec(uint64_t value) {
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

/* ================================================================== */

static void hcf(void) {
    for (;;)
        __asm__ volatile("hlt");
}

/* ==================================================================
 * Kernel entry point. Entered by Limine in long mode, ring 0, with
 * paging enabled and a stack already set up.
 * ================================================================== */

void _start(void) {
    serial_init();

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        serial_write("rzOs: bootloader base revision too old!\r\n");
        hcf();
    }

    serial_write("rzOs: Limine handoff OK (higher-half, long mode)\r\n");

    /* HHDM offset: physical address = virtual address - offset */
    if (hhdm_request.response != NULL) {
        serial_write("hhdm offset: ");
        serial_puthex(hhdm_request.response->offset);
        serial_write("\r\n");
    } else {
        serial_write("no HHDM response\r\n");
    }

    /* Framebuffer: first mode handed to us by the bootloader */
    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count >= 1) {
        struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

        serial_write("framebuffer: ");
        serial_putdec(fb->width);
        serial_write("x");
        serial_putdec(fb->height);
        serial_write(", bpp=");
        serial_putdec(fb->bpp);
        serial_write(", pitch=");
        serial_putdec(fb->pitch);
        serial_write(", address=");
        serial_puthex((uint64_t)(uintptr_t)fb->address);
        serial_write("\r\n");

        /* Paint the whole screen dark blue to prove we own it.
         * Only supported for 32-bit RGB; other formats are logged
         * and skipped rather than silently corrupted. */
        if (fb->bpp != 32 || fb->memory_model != LIMINE_FRAMEBUFFER_RGB) {
            serial_write("framebuffer format is not RGB32, skipping fill\r\n");
        } else {
            volatile uint32_t *pixels = fb->address;
            for (uint64_t y = 0; y < fb->height; y++)
                for (uint64_t x = 0; x < fb->width; x++)
                    pixels[y * (fb->pitch / 4) + x] = 0x00002A;
        }
    } else {
        serial_write("no framebuffer\r\n");
    }

    serial_write("rzOs kernel is alive. entering idle loop.\r\n");

    hcf();
}
