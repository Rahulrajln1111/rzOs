/*
 * kernel/init.c
 *
 * start_kernel(): the architecture-independent kernel bootstrap,
 * mirroring Linux's init/main.c. Called from _start (arch/x86_64/
 * boot/entry.c) once Limine has handed over the machine.
 *
 * Order matters, as in Linux: serial console first (early debug),
 * then GDT, then IDT - so that by the time anything can fault, we
 * can print the fault instead of triple-faulting. The memory
 * managers come next, in bring-up order:
 *
 *   serial -> GDT -> IDT -> per-CPU/GS -> memblock (RAM map)
 *          -> paging (identity alias) -> buddy allocator -> IRQ/PIT
 *
 * memblock describes the machine's RAM, paging makes it reachable by
 * linear address, and the buddy allocator turns it into allocatable
 * frames. IRQ setup is last so the timer can exercise the allocator
 * from the idle loop.
 */
#include <kernel/types.h>
#include <kernel/serial.h>
#include <kernel/panic.h>
#include <kernel/pic.h>
#include <kernel/pit.h>
#include <kernel/irq.h>
#include <x86_64/cpu/gdt.h>
#include <x86_64/cpu/idt.h>
#include <x86_64/cpu/lapic.h>
#include <limine.h>
#include <x86_64/cpu/percpu.h>
#include <linux/memblock.h>



DEFINE_PER_CPU(u64, per_cpu_counter) = 0;

// limine requests must be in a section called ".limine_requests" and must be marked as used, so the linker doesn't discard them. The base revision request must be first, so the bootloader can check it before parsing the rest of the requests. The start and end markers are used to find the requests in memory, so they must be in their own sections.
__attribute__((used, section(".limine_requests")))
static volatile u64 limine_base_revision[] = LIMINE_BASE_REVISION(6);

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

// to request the memory map from the bootloader limine.
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};


__attribute__((used, section(".limine_requests_start")))
static volatile u64 limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile u64 limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

/* ------------------------------------------------------------------ */

static void print_handoff_info(void)
{
    if (hhdm_request.response != NULL) {
        serial_write("hhdm offset: ");
        serial_puthex(hhdm_request.response->offset);
        serial_write("\r\n");
    }

    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count >= 1) {
        struct limine_framebuffer *fb =
            framebuffer_request.response->framebuffers[0];

        serial_write("framebuffer: ");
        serial_putdec(fb->width);
        serial_write("x");
        serial_putdec(fb->height);
        serial_write(", bpp=");
        serial_putdec(fb->bpp);
        serial_write(", pitch=");
        serial_putdec(fb->pitch);
        serial_write(", address=");
        serial_puthex((u64)(uintptr_t)fb->address);
        serial_write("\r\n");

        /* Fill the screen so we can see the framebuffer is ours.
         * (Will move to drivers/ when we build a real renderer.) */
        if (fb->bpp == 32 && fb->memory_model == LIMINE_FRAMEBUFFER_RGB) {
            volatile u32 *pixels = fb->address;
            for (u64 y = 0; y < fb->height; y++)
                for (u64 x = 0; x < fb->width; x++)
                    pixels[y * (fb->pitch / 4) + x] = 0x00002A;
        }
    } else {
        serial_write("no framebuffer\r\n");
    }
}


static void memblock_init(void)
{
    if (memmap_request.response == NULL) {
        serial_write("MEMBLOCK: no memory map, allocator disabled\r\n");
        return;
    }
 
    for (u64 i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry *e = memmap_request.response->entries[i];
 
        if (e->type == LIMINE_MEMMAP_USABLE)
            memblock_add(e->base, e->length);
        else
            memblock_reserve(e->base, e->length);
    }

}


void start_kernel(void)
{
    serial_init();

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        serial_write("rzOs: bootloader base revision too old!\r\n");
        panic("Bootloader base revision not supported");
    }

    serial_write("rzOs: Limine handoff OK (higher-half, long mode)\r\n");

    /* CPU setup: our own GDT, then IDT with the 32 exception gates. */
    gdt_init();
    serial_write("GDT: installed (kernel code/data selectors)\r\n");

    idt_init();
    serial_write("IDT: 256 gates installed\r\n");
 
    setup_per_cpu();
    serial_write("PERCPU: GS base = ");
    serial_puthex(rdmsr(MSR_GS_BASE));
    serial_write(", smp_processor_id() = ");
    serial_putdec(smp_processor_id());
    serial_write("\r\n");

    print_handoff_info();


    memblock_init();
 
    serial_write("MEMBLOCK: memory=");
    serial_putdec(memblock.memory.total_size >> 20);
    serial_write(" MiB (");
    serial_putdec(memblock.memory.cnt);
    serial_write(" region), reserved=");
    serial_putdec(memblock.reserved.total_size >> 20);
    serial_write(" MiB (");
    serial_putdec(memblock.reserved.cnt);
    serial_write(" region)\r\n");
 

    phys_addr_t probe = memblock_phys_alloc(64 * 1024, 4096);
    serial_write("MEMBLOCK: phys_alloc(64 KiB) -> ");
    serial_puthex(probe);
    serial_write("\r\n");
 
    memblock_phys_free(probe, 64 * 1024);

    phys_addr_t probe2 = memblock_phys_alloc(64 * 1024, 4096);
    serial_write("MEMBLOCK: re-alloc after free -> ");
    serial_puthex(probe2);
    serial_write(probe2 == probe ? " (same block reused)\r\n"
                                 : " (different block)\r\n");

    /* External interrupts: remap the PIC (IRQ n -> vector 32+n),
     * unmask the LAPIC's LVT0 so the PIC's output can reach the CPU
     * (Limine masks it at handoff), program the PIT to tick IRQ 0
     * at 100 Hz, and connect its handler. Only IRQ0 is unmasked. */
    pic_remap();
    lapic_init(hhdm_request.response->offset);
    pit_init(100);
    irq_register(0, pit_irq_handler);
    pic_unmask(0);

    serial_write("IRQs: PIC remapped, LAPIC LVT0 unmasked, PIT at 100 Hz\r\n");

    /* The big moment: enable interrupts for the first time since
     * Limine handed over. From here on the CPU wakes up 100x/sec
     * to run pit_irq_handler(). */
    __asm__ volatile("sti");

    /* Idle loop. hlt() parks the CPU until the next interrupt
     * wakes it; jiffies climbs; we print once per second.
     * Unsigned subtraction wraps safely, so this works even when
     * jiffies rolls past 2^64. */
    u64 last = 0;
    for (;;) {
        if (jiffies - last >= 100) {
            this_cpu_inc(per_cpu_counter);
            serial_write("tick: ");
            serial_putdec(jiffies);
            serial_write("  percpu[");
            serial_putdec(smp_processor_id());
            serial_write("]=");
            serial_putdec(this_cpu_read(per_cpu_counter));

            serial_write("\r\n");
            last = jiffies;
        }
        __asm__ volatile("hlt");
    }
}
