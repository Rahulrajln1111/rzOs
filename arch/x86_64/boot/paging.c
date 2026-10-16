/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/x86_64/boot/paging.c
 *
 * Page-table bootstrap: copy Limine's PGD, add an identity map for all
 * usable RAM, and switch CR3 to the new tables.
 *
 * Strategy (robust, no assumptions about kernel_phys or linearity):
 *   1. Read Limine's current PGD (all 512 entries) via the HHDM.
 *   2. Find a free PGD slot (P bit clear).
 *   3. Allocate identity-map PUD + PMD pages (page-aligned).
 *   4. Wire identity map into the free slot; copy all other slots verbatim.
 *   5. Switch CR3 to our new PGD's physical address.
 *
 * This preserves the kernel mapping, HHDM, and any other Limine mappings,
 * and only ADDS the identity map. The kernel continues executing at
 * PAGE_OFFSET via the preserved kernel mapping.
 *
 * CRITICAL: All page-table structures MUST be page-aligned (4KB boundary)
 * because CR3 must hold a page-aligned physical address (bits 11:0 must
 * be zero). The kernel's .bss is not guaranteed to be page-aligned, so
 * we explicitly align these arrays with __attribute__((aligned(4096))).
 */

#include <kernel/types.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/serial.h>

#define PAGE_SHIFT            12
#define PAGE_SIZE             (1UL << PAGE_SHIFT)
#define PTRS_PER_PGD          512
#define PTRS_PER_PUD          512
#define PTRS_PER_PMD          512
#define PTRS_PER_PTE          512
#define PML4_SHIFT            39
#define PUD_SHIFT             30
#define PMD_SHIFT             21
#define PTE_SHIFT             12
#define PAGE_OFFSET           0xffffffff80000000UL

#define PTE_PRESENT           (1UL << 0)
#define PTE_WRITABLE          (1UL << 1)
#define PTE_PSE               (1UL << 7)
#define PTE_GLOBAL            (1UL << 8)
#define PGD_TABLE             (1UL << 7)
#define PUD_TABLE             (1UL << 7)
#define PMD_TABLE             (1UL << 7)

/* 40-bit PFN mask: bits 51:12. Built from shifts to avoid hex-digit miscounts. */
#define PTE_PFN_MASK          (((1UL << 52) - 1) & ~((1UL << 12) - 1))

/* ---- Page-aligned page-table storage (CR3 requires page alignment) ---- */
static unsigned long m_pml4[PTRS_PER_PGD] __attribute__((aligned(PAGE_SIZE)));
static unsigned long identity_pud[PTRS_PER_PUD] __attribute__((aligned(PAGE_SIZE)));
static unsigned long identity_pmd[PTRS_PER_PMD] __attribute__((aligned(PAGE_SIZE)));

#define HHDM_OFFSET           0xffff800000000000UL

/* ---- HHDM-based physical memory access (read Limine's tables before CR3 switch) ---- */
static inline u64 *hhdm_ptr(u64 phys)
{
    return (u64 *)(phys + HHDM_OFFSET);
}

static inline u64 read_pte_phys(u64 phys_addr)
{
    return *hhdm_ptr(phys_addr);
}

static inline u64 native_read_cr3(void)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* ---- Find a free PGD slot (P bit clear) in Limine's PGD ---- */
static unsigned int find_free_pml4_slot(u64 limine_pgd_phys)
{
    for (unsigned int i = 0; i < PTRS_PER_PGD; i++) {
        u64 entry = read_pte_phys(limine_pgd_phys + i * sizeof(u64));
        if (!(entry & PTE_PRESENT))
            return i;
    }
    return 0; /* fallback: use slot 0 */
}

/* ---- Walk the CURRENT (Limine) page tables to find phys addr of a virtual addr ---- */
static u64 current_va_to_phys(u64 va)
{
    u64 cr3 = native_read_cr3();
    u64 pgd_phys = cr3 & ~(PAGE_SIZE - 1);

    unsigned int pml4i = (va >> PML4_SHIFT) & (PTRS_PER_PGD - 1);
    u64 pgd_entry = read_pte_phys(pgd_phys + pml4i * sizeof(u64));
    if (!(pgd_entry & PTE_PRESENT)) return 0;
    u64 pud_phys = pgd_entry & PTE_PFN_MASK;

    unsigned int pudi = (va >> PUD_SHIFT) & (PTRS_PER_PUD - 1);
    u64 pud_entry = read_pte_phys(pud_phys + pudi * sizeof(u64));
    if (!(pud_entry & PTE_PRESENT)) return 0;
    if (pud_entry & PTE_PSE)
        return (pud_entry & PTE_PFN_MASK) | (va & (PAGE_SIZE * 512 * 512 - 1));

    u64 pmd_phys = pud_entry & PTE_PFN_MASK;
    unsigned int pmi = (va >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
    u64 pmd_entry = read_pte_phys(pmd_phys + pmi * sizeof(u64));
    if (!(pmd_entry & PTE_PRESENT)) return 0;
    if (pmd_entry & PTE_PSE)
        return (pmd_entry & PTE_PFN_MASK) | (va & (PAGE_SIZE * 512 - 1));

    u64 pte_phys = pmd_entry & PTE_PFN_MASK;
    unsigned int pti = (va >> PTE_SHIFT) & (PTRS_PER_PTE - 1);
    u64 pte_entry = read_pte_phys(pte_phys + pti * sizeof(u64));
    if (!(pte_entry & PTE_PRESENT)) return 0;

    return (pte_entry & PTE_PFN_MASK) | (va & (PAGE_SIZE - 1));
}

/* ---- Page-table entry constructors ---- */
/*
 * x86_64 4-level paging entry layout:
 *
 * PML4 entry (PGT): bit 7 = reserved (must be 0). bits 12+: physical addr of PUD.
 * PUD entry (PDT): bit 7 = PS (1=1GB page, 0=points to PD).
 * PMD entry (PD):  bit 7 = PS (1=2MB page, 0=points to PT).
 *
 * So "points to next level" is encoded as PS=0 (bit 7 clear) + P=1 + R/W.
 * We do NOT set bit 7 for PML4 and PUD entries (they are NOT large pages).
 * We DO set bit 7 for the PMD large-page entries (2MB pages).
 */
static inline unsigned long mk_pml4_entry(u64 pud_phys)
{
    return pud_phys | PTE_PRESENT | PTE_WRITABLE;  /* bit 7 = 0 (reserved on 4-level) */
}

static inline unsigned long mk_pud_entry(u64 pmd_phys)
{
    return pmd_phys | PTE_PRESENT | PTE_WRITABLE;  /* bit 7 = 0 (points to PMD, not 1GB page) */
}

static inline unsigned long mk_pmd_large(u64 phys)
{
    return (phys & ~(PAGE_SIZE * 512 - 1))
           | PTE_PRESENT | PTE_WRITABLE | PTE_PSE | PTE_GLOBAL;  /* bit 7 = 1 (2MB page) */
}

/* ---- Main entry point ---- */
void phys_mem_alias_init(u64 ram_end_pfn)
{
    /* Step 1: read Limine's CR3 and PGD physical address */
    u64 limine_cr3 = native_read_cr3();
    u64 limine_pgd_phys = limine_cr3 & ~(PAGE_SIZE - 1);

    /* Step 2: copy all 512 PGD entries from Limine's PGD into our m_pml4.
     * We read them via the HHDM (which maps physical RAM at virt = phys + HHDM_OFFSET). */
    for (unsigned int i = 0; i < PTRS_PER_PGD; i++) {
        m_pml4[i] = read_pte_phys(limine_pgd_phys + i * sizeof(u64));
    }

    /* Step 3: find a free PGD slot and wire the identity map into it */
    unsigned int identity_slot = find_free_pml4_slot(limine_pgd_phys);

    /* Get physical addresses of our identity-map structures (via Limine's tables) */
    u64 identity_pud_phys = current_va_to_phys((u64)identity_pud);
    u64 identity_pmd_phys = current_va_to_phys((u64)identity_pmd);

    if (!identity_pud_phys || !identity_pmd_phys)
        panic("phys_mem_alias_init: cannot determine physical address of identity map structures");

    /* Wire: identity_slot -> identity_pud -> identity_pmd */
    m_pml4[identity_slot] = mk_pml4_entry(identity_pud_phys);
    identity_pud[0] = mk_pud_entry(identity_pmd_phys);

    /* Fill identity_pmd with 2 MiB huge pages covering 0 .. identity_end.
     * identity_end covers all usable RAM plus the kernel's physical pages
     * (which may be above the memblock-reported ram_end if the kernel is
     * loaded in a region that memblock counts as "reserved"). */
    u64 ram_end = ram_end_pfn * PAGE_SIZE;
    u64 identity_end = ram_end;

    u64 kernel_phys = current_va_to_phys(PAGE_OFFSET);
    if (kernel_phys) {
        u64 kernel_end = kernel_phys + 64UL * 1024 * 1024;
        if (kernel_end > identity_end)
            identity_end = kernel_end;
    }

    u64 addr = 0;
    unsigned int pmd_idx = 0;
    while (addr < identity_end) {
        identity_pmd[pmd_idx++] = mk_pmd_large(addr);
        addr += 2UL * 1024 * 1024;
    }

    /* Step 4: get physical address of our m_pml4 (for CR3).
     * m_pml4 is page-aligned, so its physical address is also page-aligned. */
    u64 m_pml4_phys = current_va_to_phys((u64)m_pml4);
    if (!m_pml4_phys)
        panic("phys_mem_alias_init: cannot determine physical address of m_pml4");

    /* Step 5: switch CR3 to our new PGD */
    __asm__ volatile("mov %0, %%cr3\n\t" : : "r"(m_pml4_phys) : "memory");
}
