/*
 * arch/x86_64/cpu/lapic.c
 *
 * Local APIC setup. Linux equivalent: arch/x86/kernel/apic/.
 *
 * WHY THIS FILE EXISTS (a Limine gotcha hit at runtime):
 * The 8259 PIC's interrupt output reaches the CPU through the local
 * APIC's LINT0 input, in the ExtINT delivery mode. The LAPIC only
 * accepts that input while its LVT0 entry is unmasked - and the
 * firmware/bootloader leaves LVT0 MASKED. Result: the PIT fires,
 * the PIC has the IRQ pending (IRR bit set), IF=1, but the CPU
 * silently never takes the interrupt. The fix is one 32-bit write
 * to LVT0 (APIC offset 0x350): value 0x700 = ExtINT, unmasked.
 * * COMPLICATION: the LAPIC lives at physical MMIO 0xfee00000, which
 * Limine does NOT map (its HHDM covers RAM only). Until we build
 * our own VMM (the paging milestone), map_one_2mb_page() pokes a
 * 2MB mapping into Limine's page tables - the classic early-MMIO
 * trick.
 *
 * TODO (paging milestone): this mapping reserves virtual
 * 0xffffffff80200000 + 2MB inside the kernel's 1GB region. The VMM
 * MUST replace it with a proper MMIO region, and real hardware
 * would need uncached (PCD/PWT) attributes for LAPIC MMIO - QEMU
 * tolerates the cached mapping. Do not let kernel growth (.bss,
 * heap) silently collide with this address before then. */
#include <kernel/types.h>
#include <x86_64/cpu/lapic.h>

#define LAPIC_PHYS   0xFEE00000ULL   /* standard local APIC base     */
#define LAPIC_LVT0   0x350           /* LVT0: the LINT0 input (PIC)  */
#define LVT_EXTINT   0x700           /* delivery=ExtINT, edge, on    */

/*
 * TEMPORARY stopgap (replaced by the real VMM in the paging
 * milestone): map the LAPIC's MMIO page by adding a 2MB page to
 * Limine's page tables. The kernel lives at PML4[511] -> PDPT[510]
 * -> PD; we saw PD[1] is a free slot, so:
 *
 *   virtual 0xffffffff80200000  ->  physical 0xfee00000 (2MB page)
 *
 * Flags 0x87 = present | writable | PS (2MB page). We deliberately
 * do NOT set NX yet: EFER.NXE must be verified first, and this
 * mapping is replaced soon anyway.
 */
static u64 map_lapic_phys(u64 hhdm)
{
    u64 cr3, pml4e, pdpte;
    volatile u64 *pml4, *pdpt, *pd;
    u64 kpml4 = (0xffffffff80000000ULL >> 39) & 0x1FF;
    u64 kpdpt = (0xffffffff80000000ULL >> 30) & 0x1FF;

    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    pml4  = (volatile u64 *)(hhdm + (cr3   & ~0xFFFULL));
    pml4e = pml4[kpml4];
    pdpt  = (volatile u64 *)(hhdm + (pml4e & ~0xFFFULL));
    pdpte = pdpt[kpdpt];
    pd    = (volatile u64 *)(hhdm + (pdpte & ~0xFFFULL));

    pd[1] = LAPIC_PHYS | 0x87;

    /* Flush the TLB entry for the new virtual address. */
    __asm__ volatile("invlpg (%0)" : : "r"(0xffffffff80200000ULL));

    return 0xffffffff80200000ULL;
}

void lapic_init(u64 hhdm_offset)
{
    u64 lapic_va = map_lapic_phys(hhdm_offset);
    volatile u32 *lapic = (volatile u32 *)lapic_va;

    /* Unmask LVT0 and route LINT0 to ExtINT: the PIC can now reach
     * the CPU, so IRQ n arrives on vector 32+n as we remapped. */
    lapic[LAPIC_LVT0 / 4] = LVT_EXTINT;
}
