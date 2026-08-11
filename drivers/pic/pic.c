/*
 * drivers/pic/pic.c
 *
 * 8259 Programmable Interrupt Controller: two cascaded chips
 * (master + slave). After BIOS/bootloader, master IRQs arrive as
 * vectors 8..15 - which COLLIDES with CPU exceptions (#DF=8,
 * #GP=13, #PF=14...). We reprogram both chips so that IRQ n is
 * delivered as vector 32+n. This is mandatory before any external
 * interrupt can be used.
 *
 * Linux equivalent: arch/x86/kernel/i8259.c
 */
#include <kernel/types.h>
#include <kernel/pic.h>
#include <x86_64/cpu/io.h>

#define PIC1_CMD   0x20   /* master: command port  */
#define PIC1_DATA  0x21   /* master: data/mask port */
#define PIC2_CMD   0xA0   /* slave:  command port  */
#define PIC2_DATA  0xA1   /* slave:  data/mask port */

#define ICW1_INIT  0x10   /* ICW1 bit 4: begin the init sequence */
#define ICW1_ICW4  0x01   /* ICW1 bit 0: an ICW4 will follow      */
#define ICW4_8086  0x01   /* 8086/88 mode (not MCS-80/8259A raw)  */

/*
 * ICW1..ICW4 = Initialization Command Words. The PIC must be told
 * exactly four things, in order, after the init pulse:
 *   ICW1: "initialization starts"
 *   ICW2: the base vector of this chip (IRQ 0 -> 0x20 here)
 *   ICW3: cascade wiring (master: which IRQ the slave sits on;
 *         slave: its own identity, 0x02)
 *   ICW4: operating mode (8086)
 */
void pic_remap(void)
{
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, 0x20);   /* master IRQs 0..7  -> vectors 32..39 */
    io_wait();
    outb(PIC2_DATA, 0x28);   /* slave IRQs  8..15 -> vectors 40..47 */
    io_wait();

    outb(PIC1_DATA, 0x04);   /* master: slave is wired on IRQ2      */
    io_wait();
    outb(PIC2_DATA, 0x02);   /* slave:  cascade identity            */
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask EVERYTHING. Nothing may interrupt until we explicitly
     * unmask the one IRQ we care about. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

/* Clear the mask bit for one master IRQ (0..7), letting it through.
 * The mask register is read-modify-write so we keep other IRQs
 * masked. (Slave IRQs get their own unmask in a later milestone.) */
void pic_unmask(int irq)
{
    u8 mask = inb(PIC1_DATA);
    mask &= (u8)~(1 << irq);
    outb(PIC1_DATA, mask);
}
