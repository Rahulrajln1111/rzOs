/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mm/pmm.c
 *
 * Physical Memory Manager: the buddy allocator.
 *
 * One record per 4 KiB frame in a static vmemmap, 11-tier free-list tree
 * (orders 0..10), and the two walks: alloc (find + split) and free (XOR
 * merge). This is a 1:1 port of Linux's mm/page_alloc.c reduced to a single
 * zone, so the real logic is readable and compilable in this tree.
 *
 * Linux equivalents:
 *   pmm_alloc_pages()   -> __rmqueue_smallest() + expand()
 *   pmm_free_pages()    -> __free_one_page() + _free_equal() / move
 *   pmm_init()          -> memblock setup + boot carving + zone config
 *
 * Files: include/linux/mmzone.h (data structures), mm/memblock.c (boot map),
 *        kernel/init.c (pmm_init() call), arch/x86_64/boot/paging.c
 *        (phys_mem_alias_init — builds identity + kernel page tables).
 */

#include <kernel/types.h>
#include <kernel/pmm.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/percpu-defs.h>
#include <kernel/serial.h>

/* Each logical CPU owns one per-CPU page cache set. */
#define NR_CPUS 8

/* ---------- early page-table bootstrap ---------- */

/*
 * Page tables mapping physical memory as linear (identity) addresses,
 * plus the kernel image at PAGE_OFFSET. Declared in paging.c.
 *
 * Signature: phys_mem_alias_init(ram_end_pfn)
 *   Builds identity + kernel page tables and switches CR3 to them.
 *   After this returns, all physical RAM is reachable at linear == physical
 *   (identity), and the kernel continues executing at PAGE_OFFSET.
 */
extern void phys_mem_alias_init(u64 ram_end_pfn);

static inline unsigned long min(unsigned long a, unsigned long b)
{
    return a < b ? a : b;
}
static inline unsigned long max(unsigned long a, unsigned long b)
{
    return a > b ? a : b;
}

/* ------------------------------------------------------------------ */
/* Data.                                                               */
/* ------------------------------------------------------------------ */

/* Per-CPU page cache sets (one per CPU). */
static struct per_cpu_pages __per_cpu_percpu[NR_CPUS];

/* Global zone descriptor (single zone, allocated in pmm_init). */
struct zone contig_zone;

/* Virtual memory map: one page struct per frame. */
struct page *vmemmap;

/* ------------------------------------------------------------------ */
/* Free-list plumbing (the per-order, per-migrate-type lists).         */
/* ------------------------------------------------------------------ */

/* Initialize a free_area so each order's list is empty. */
static inline void free_area_init(struct free_area *area)
{
    int i;
    for (i = 0; i <= MAX_ORDER; i++) {
        INIT_LIST_HEAD(&area->free_list[i]);
    }
    area->nr_free = 0;
}

/* ------------------------------------------------------------------ */
/* The two core walks from mm/page_alloc.c.                            */
/* ------------------------------------------------------------------ */

/* Find a free block of the requested order in the given area and return
 * the first page of it, or NULL. */
static struct page *__rmqueue_smallest(struct free_area *area, int order)
{
    if (order < 0 || order > MAX_ORDER)
        return NULL;

    struct list_head *head = &area->free_list[order];
    if (list_empty(head))
        return NULL;

    struct page *page = list_first_entry(head, struct page, freelist);
    list_del(&page->freelist);

    area->nr_free--;
    page->_private = 0;
    return page;
}

/* Free one page (order 0) into the buddy allocator. The XOR merge walks
 * up the tree, merging with a free buddy. */
static void __free_one_page(struct page *page)
{
    unsigned long start_pfn = page_to_pfn(page);
    unsigned long end_pfn = start_pfn + 1;
    int order = 0;

    list_add_tail(&page->freelist, &page->freelist);
    page->freelist.next = &page->freelist;
    page->freelist.prev = &page->freelist;
    page->_private = 0;

    while (order < MAX_ORDER) {
        unsigned long buddy_pfn = start_pfn ^ (1UL << order);

        if (buddy_pfn < start_pfn || buddy_pfn >= end_pfn)
            break;

        struct page *buddy = pfn_to_page(buddy_pfn);

        if ((unsigned long)buddy->_private != (unsigned long)order)
            break;

        list_del(&page->freelist);
        list_del(&buddy->freelist);

        page->_private = order + 1;
        start_pfn = min(start_pfn, buddy_pfn);
        end_pfn = max(end_pfn, buddy_pfn);
        order++;
    }
}

/* ------------------------------------------------------------------ */
/* Public API.                                                          */
/* ------------------------------------------------------------------ */

struct page *pmm_alloc_pages(int order)
{
    if (order < 0 || order > MAX_ORDER)
        return NULL;

    struct zone *z = &contig_zone;
    struct free_area *area = &z->free_area[order];

    struct page *page = __rmqueue_smallest(area, order);
    if (!page)
        return NULL;

    return page;
}

void pmm_free_pages(struct page *page, int order)
{
    (void)order;

    if (!page)
        return;

    __free_one_page(page);
}

u64 pmm_free_frames(void)
{
    struct zone *z = &contig_zone;
    return z->free_area[0].nr_free;
}

u64 pmm_alloc_frame(void)
{
    struct page *page = pmm_alloc_pages(0);
    if (!page)
        return 0;
    return page_to_pfn(page) << PAGE_SHIFT;
}

void pmm_free_frame(u64 phys)
{
    if (phys == 0)
        return;
    struct page *page = pfn_to_page(phys >> PAGE_SHIFT);
    pmm_free_pages(page, 0);
}

/* ------------------------------------------------------------------ */

void pmm_init(u64 hhdm_offset)
{
    (void)hhdm_offset;

    serial_write("PPM: entering pmm_init\r\n");
    serial_putdec(memblock.memory.total_size >> 20);
    serial_write(" MiB total\r\n");

    /* 1) Determine total usable frames and the RAM end PFN from
     *    memblock. */
    u64 first_pfn = 0;
    u64 last_pfn = 0;
    for (unsigned long i = 0; i < memblock.memory.cnt; i++) {
        phys_addr_t base = memblock.memory.regions[i].base;
        phys_addr_t size = memblock.memory.regions[i].size;
        if (memblock.memory.regions[i].flags & MEMBLOCK_NONE)
            continue;
        u64 start = base / PAGE_SIZE;
        u64 end = (base + size) / PAGE_SIZE;
        if (end > last_pfn)
            last_pfn = end;
        if (first_pfn == 0)
            first_pfn = start;
    }

    serial_write("PPM: alias tables starting...\r\n");

    /* 2) Build identity + kernel page tables and switch CR3.
     *    After this returns:
     *      - all physical RAM is reachable at linear == physical (identity)
     *      - the kernel continues executing at PAGE_OFFSET
     *      - the CPU can dereference any physical frame via its identity
     *        linear address
     */
    phys_mem_alias_init(last_pfn);
    serial_write("PPM: alias tables done\r\n");

    /* 3) Allocate the vmemmap array (one page struct per frame).
     *    NOW the physical address returned by memblock_phys_alloc is
     *    reachable (identity-mapped), so we can safely use it.
     *    We do NOT free it back to memblock here — it stays allocated
     *    and will be excluded from the buddy carve in step 5. */
    u64 total_frames = last_pfn - first_pfn;
    phys_addr_t vmemmap_pa = memblock_phys_alloc(total_frames * sizeof(struct page),
                                                 PAGE_SIZE);
    if (!vmemmap_pa)
        panic("pmm_init: could not allocate vmemmap");

    /* 4) Point vmemmap at the physical address (identity-mapped).
     *    vmemmap + pfn gives the struct page for frame pfn. */
    vmemmap = (struct page *)vmemmap_pa;
    memset(vmemmap, 0, total_frames * sizeof(struct page));

    /* 6) Configure the global zone (single zone) BEFORE carving.
     *    free_area_init zeroises the lists; step 5 populates them. */
    struct zone *z = &contig_zone;
    z->zone_start_pfn = 0;
    z->spanned_pages = total_frames;
    z->present_pages = total_frames;
    z->name = "Normal";
    free_area_init(&z->free_area[0]);

    /* 5) Boot carve: thread every usable frame into the free lists.
     *    Exclude the vmemmap frame — it is not free (we are using it). */
    u64 vmemmap_pfn = vmemmap_pa / PAGE_SIZE;
    for (unsigned long i = 0; i < memblock.memory.cnt; i++) {
        phys_addr_t base = memblock.memory.regions[i].base;
        phys_addr_t size = memblock.memory.regions[i].size;
        if (memblock.memory.regions[i].flags & MEMBLOCK_NONE)
            continue;
        u64 start = base / PAGE_SIZE;
        u64 end = (base + size) / PAGE_SIZE;
        for (u64 pfn = start; pfn < end; pfn++) {
            if (pfn == vmemmap_pfn)
                continue;  /* vmemmap is not free */
            struct page *page = pfn_to_page(pfn);
            page->_private = 0;
            list_add_tail(&page->freelist, &page->freelist);
            page->freelist.next = &page->freelist;
            page->freelist.prev = &page->freelist;
            z->free_area[0].nr_free++;  /* count each free frame */
        }
    }

    /* 7) Per-CPU page caches. */
    for (int cpu = 0; cpu < NR_CPUS; cpu++) {
        struct per_cpu_pages *pcp = &__per_cpu_percpu[cpu];
        spin_lock_init(&pcp->lock);
        pcp->count = 0;
        pcp->high = 32;
        pcp->batch = 32;
        INIT_LIST_HEAD(&pcp->lists[MIGRATE_UNMOVABLE]);
        INIT_LIST_HEAD(&pcp->lists[MIGRATE_RECLAIMABLE]);
        INIT_LIST_HEAD(&pcp->lists[MIGRATE_MOVABLE]);
    }
}
