# rzOs — the page allocator: deep dive on `mm/pmm.c`

> Read this **while** reading `mm/pmm.c`, `include/linux/mmzone.h` and
> `include/kernel/pmm.h`. Every structure and step below is explained with what
> it does, why it exists, and how upstream Linux does the same thing — plus an
> honest list of what is still missing in this port (section 8).

---

## 0. Big picture

Physical memory is the actual RAM chips, addressed from `0x0` upward. The
kernel cuts it into **4 KiB frames** — the same size as a page, so the paging
layer can map one frame per page. The page allocator (Linux: the buddy
allocator in `mm/page_alloc.c`) answers two questions:

- *"Give me N contiguous frames"* → `pmm_alloc_pages(order)`
- *"These frames are done"* → `pmm_free_pages(page, order)`

`order` is the block size as a power of two: order 0 = 1 frame (4 KiB), order 1
= 2 frames, order 10 = 1024 frames (4 MiB). `NR_PAGE_ORDERS` is 11, `MAX_ORDER`
is 10.

### Files involved

| File | Role |
|---|---|
| `include/linux/mmzone.h` | `struct page`, `struct free_area`, `struct zone`, `pg_data_t`, `pfn_to_page()` — the data layout, mirroring Linux `include/linux/mmzone.h` + `mm_types.h` |
| `include/linux/list.h` | the intrusive doubly-linked list the free areas are built from (Linux `include/linux/list.h`) |
| `mm/pmm.c` | the allocator itself |
| `include/kernel/pmm.h` | public API |
| `kernel/init.c` | calls `memblock_init()` then `pmm_init()`, and exercises the allocator from the idle loop |
| `mm/memblock.c` | the boot memory map this allocator is sized from |
| `arch/x86_64/boot/paging.c` | the identity alias that makes physical frames dereferenceable |

---

## 1. Data structures

### `struct page` — one record per frame

```c
struct page {
    unsigned long flags;             /* state bits                 */
    atomic_t _refcount;              /* reference count            */
    void *virtual;                   /* if a VM object maps it     */
    unsigned long _private;          /* this kernel: the block order */
    struct list_head compound_head;  /* compound-page bookkeeping  */
    struct list_head freelist;       /* link on a free list        */
};
```

One of these exists for *every* frame in the machine: at 256 MiB that is 65536
records. Linux calls the array `vmemmap` (its `struct page` is 64 bytes, this
one is smaller); here `struct page *vmemmap` is a global, and

```c
#define pfn_to_page(pfn) (vmemmap + (pfn))
#define page_to_pfn(page) ((unsigned long)((page) - vmemmap))
```

are just pointer arithmetic on that array. `_private` is where this port stores
the **order** of a free block, so the free path can ask *"is my buddy free at
the same order?"*.

### `struct free_area` — one shelf per order, one list per migrate type

```c
struct free_area {
    struct list_head free_list[MIGRATE_TYPES];   /* UNMOVABLE, RECLAIMABLE, MOVABLE */
    unsigned long nr_free;
};
```

Linux keeps `free_area[MAX_ORDER + 1]` inside each zone, and inside each order
separate lists per *migrate type* so that movable and unmovable allocations do
not fragment each other. This port copies the shape (`MIGRATE_TYPES == 3`,
`nr_free` per area). Only the order-0 list is used so far.

### `struct zone`, `pg_data_t`, `per_cpu_pages`

`struct zone` holds `free_area[MAX_ORDER + 1]`, watermarks, the zone page
range and the per-CPU page cache pointer; `pg_data_t` (`contig_page_data`) is
the single NUMA node that owns the zone (`NODE_DATA(n)`); `struct per_cpu_pages`
is the per-CPU hot cache of order-0 pages (`pcp->lock`, `count`, `high`,
`batch`, one list per migrate type), aligned to 64 bytes so two CPUs' counters
never share a cache line. All of this is SMP-shaped from birth — see section 7.

---

## 2. `pmm_init(hhdm_offset)` — the boot sequence

Order is the whole point of this function.

1. **Size the frame range.** Walk `memblock.memory` regions and record the
   lowest and highest usable PFN (`first_pfn`, `last_pfn`). Memblock has
   already classified firmware/kernel/bootloader regions as reserved, so only
   usable RAM is considered.
2. **Switch to the identity alias.** `phys_mem_alias_init(last_pfn)` copies
   Limine's PML4, adds a 2 MiB-page identity map of RAM plus the kernel image,
   and loads `CR3`. This must happen **before** any physical address is
   dereferenced, because the `struct page` array lives at a *physical* address
   handed out by memblock.
3. **Allocate and clear the vmemmap.** `memblock_phys_alloc(total_frames *
   sizeof(struct page), PAGE_SIZE)` reserves the array (Linux places `mem_map`
   in direct-mapped memory instead) and `memset()` zeroes it — every flag must
   be clean before anything counts as free.
4. **Configure the zone.** `zone_start_pfn`, `spanned_pages`, `present_pages`,
   `name = "Normal"`, and `free_area_init()` to empty the per-order lists.
5. **Carve the free lists.** For every usable region, walk frames and (for each
   frame except the vmemmap's own) initialise its `struct page` and count it
   into `free_area[0].nr_free`. Linux does the same walk in
   `memblock_free_all()` / `free_all_bootmem()`.
6. **Per-CPU page caches.** Initialise `per_cpu_pages` for `NR_CPUS` entries
   (`high = batch = 32`).

Observed on QEMU at 256 MiB: `PPM: total free frames after init = ~53050` (the exact
count varies by a frame or two between builds, since the vmemmap lives inside the
first usable region), and it stays put across ticks.

---

## 3. Two counts of truth: `free_area[].nr_free` vs `vmemmap`

There are two ways to know how much memory is free, and they must agree:

- **Global:** sum of `nr_free` over zones/orders (what `pmm_free_frames()`
  reports today, from `free_area[0]`).
- **Per block:** `struct page::_private` holding the order of a free block, and
  the block being linked on that order's list — this is what makes the buddy
  walk possible at all. Linux uses `PageBuddy()` + `page_order()` for the same
  purpose.

Every alloc must decrement exactly one `nr_free` (the order it hands out), and
every free must increment exactly one (`__free_one_page()` adds
`1 << order` once, at the end). Counting per *frame* at carve time (as this port
does today) is only correct while nothing is ever allocated.

---

## 4. Allocating — `__rmqueue_smallest()`

```c
static struct page *__rmqueue_smallest(struct free_area *area, int order)
{
    struct list_head *head = &area->free_list[order];
    if (list_empty(head))
        return NULL;

    struct page *page = list_first_entry(head, struct page, freelist);
    list_del(&page->freelist);
    area->nr_free--;
    page->_private = 0;
    return page;
}
```

Two things to notice:

- The list is **per order**, so a request for order 0 must look at order 0's
  list; if it is empty, the caller is supposed to climb to higher orders and
  **split** the smallest block it finds — Linux's `__rmqueue_smallest()` loops
  over orders and `expand()` splits the block while pushing each upper buddy
  down to the next-lower list. That loop is the missing half here (section 8).
- `pmm_free_frame(phys)` converts a physical address back to a `struct page`
  with `pfn_to_page(phys >> PAGE_SHIFT)`, which is why "0" is treated as "no
  frame" and never freed.

---

## 5. Freeing — the XOR buddy walk in `__free_one_page()`

```c
while (order < MAX_ORDER) {
    unsigned long buddy_pfn = start_pfn ^ (1UL << order);   /* THE trick */

    if (buddy_pfn < start_pfn || buddy_pfn >= end_pfn)
        break;                                   /* buddy is outside this block */

    struct page *buddy = pfn_to_page(buddy_pfn);
    if ((unsigned long)buddy->_private != (unsigned long)order)
        break;                                   /* buddy not free at this order */

    list_del(&page->freelist);
    list_del(&buddy->freelist);
    page->_private = order + 1;
    start_pfn = min(start_pfn, buddy_pfn);
    end_pfn   = max(end_pfn,   buddy_pfn);
    order++;
}
```

**The buddy of a block is the adjacent block of the same size.** Because every
free block is aligned to its own size, the buddy's frame number is the block's
frame number with exactly one bit flipped:

```
buddy_pfn = pfn ^ (1 << order)
```

A 2-frame block at frame 6 has buddy `6 ^ 2 = 4` — frames 4..5. One `xor`. This
is literally Linux's `__free_one_page()`.

Walked example — freeing frame 8 (order 0) when frames 9, 10 and 12 are free at
orders 0, 1 and 2:

```
buddy = 8 ^ 1 = 9  → free, order 0 → merge, base 8, order 1
buddy = 8 ^ 2 = 10 → free, order 1 → merge, base 8, order 2
buddy = 8 ^ 4 = 12 → free, order 2 → merge, base 8, order 3
buddy = 8 ^ 8 = 0  → not free       → stop
```

Eight contiguous frames (frames 8..15) are back as one order-3 block. The merge
loop is what keeps fragmentation from accumulating: scattered small frees
recombine automatically.

**What is missing in this port:** after the walk the merged block must be
re-linked on `free_area[order].free_list[...]` (and `nr_free` incremented by
`1 << order`, once). `__free_one_page()` as written currently `list_del()`s the
page and the buddy and then stops, so a freed block ends up on no list at all.

---

## 6. Building the lists at boot

`free_range()`-style carving is *not* in this port yet, but the two rules it has
to obey are worth writing down, because getting either wrong breaks the XOR
math quietly:

- A block of `2^order` frames must start at a frame number **divisible by
  `2^order`** (alignment), or `pfn ^ (1 << order)` lands on a stranger's frame.
- Blocks have to be carved **largest first**, otherwise the leftovers cannot
  form aligned power-of-two chunks.

Linux carves with `memblock_alloc_range_nid()`/`memmap_init()` and then
`__free_pages_core()` for each aligned chunk; the equivalent here is: for each
usable range, find the largest aligned `order` that fits at the current base,
free it, advance, repeat.

---

## 7. SMP shape, and what is deliberately simplified

The structs (`per_cpu_pages` with a spinlock and per-migrate lists, cache-line
alignment, `pg_data_t`, `zonelist`) mirror upstream so that the SMP and NUMA
work later is "fill in the lists", not "rewrite the allocator". Deliberate
simplifications, in Linux terms:

| Simplification here | Linux has |
|---|---|
| One zone (`contig_zone`, "Normal") | DMA / DMA32 / NORMAL / MOVABLE zones with per-zone lists |
| Per-CPU caches initialised but unused | hot order-0 page cache per CPU, refilled in batches |
| Single NUMA node | `pgdat` per node, `zonelist` fallback order |
| No migrate-type separation yet | `MIGRATE_UNMOVABLE/RECLAIMABLE/MOVABLE` chosen per allocation |
| No watermarks / reclaim | `WMARK_MIN/LOW/HIGH`, `kswapd`, `reclaim` |
| No compaction, CMA, memcg accounting | all layered on the same buddy core |

---

## 8. Status: what works, what to wire up next

**Works today:** the structures, the zone/vmemmap setup, the memblock → paging →
`struct page` boot sequence, the 2 MiB identity alias switched in by the kernel
itself, the frame count (~53050 frames / 207 MiB usable in QEMU), and the
compile-time header harness (`make check`) that force-uses every one of these
shapes.

**Outstanding, in the order to do it:**

1. **Link the carved frames.** At carve time, put each frame (or better, each
   aligned chunk) on the right order's list instead of leaving it
   self-linked. `list_add_tail(&page->freelist, &area->free_list[migrate])`.
2. **Split on allocation.** Loop from the requested order upward; on the first
   non-empty list, split while the block is bigger than requested, pushing each
   upper half onto the next-lower order's list (Linux `expand()`), and decrement
   `nr_free` for the order actually handed out.
3. **Re-insert merged blocks.** After the XOR walk in `__free_one_page()`, link
   the final block on `free_area[order].free_list[...]` and add `1 << order` to
   that area's `nr_free` (move counting off order 0).
4. **Fix the two indexing slips.** `free_area_init()` loops `i = 0..MAX_ORDER`
   over `free_list[i]` of a *single* area, which walks past that member's three
   migrate types — it should iterate the migrate types of each area (or take
   the whole `free_area[]` array). And `pmm_alloc_pages()` passes
   `&z->free_area[order]` into a function that then indexes
   `area->free_list[order]`; the intended shape is one area per candidate
   order, `area = &z->free_area[o]`.
5. **A double-free guard.** Linux checks `PageBuddy()` before free; without a
   check, freeing an already-free page silently corrupts the lists.
6. **Tests.** Allocate an order-3 block, free it, then allocate 8 single
   frames and assert the *same* contiguous addresses come back — only a merging
   allocator can promise that. Then fragment on purpose (allocate every other
   frame) and show `pmm_alloc_pages(1)` failing while frames are still free.

---

## 9. Map to the real Linux kernel

| Here | Linux |
|---|---|
| `struct page *vmemmap` + `pfn_to_page()` | `vmemmap` / `struct page` in `include/linux/mm_types.h` |
| `struct zone contig_zone`, `contig_page_data` | `struct zone`, `pg_data_t`, `NODE_DATA(n)` |
| `struct free_area free_list[MIGRATE_TYPES]` | `struct free_area` in `include/linux/mmzone.h` |
| `pmm_alloc_pages()` → `__rmqueue_smallest()` | `__rmqueue_smallest()` + `expand()` in `mm/page_alloc.c` |
| `pmm_free_pages()` → `__free_one_page()` | `__free_one_page()`, same `pfn ^ (1 << order)` |
| `pmm_init()` carve | `memblock_free_all()` / `free_all_bootmem()` |
| `struct per_cpu_pages`, `per_cpu_pageset` | per-CPU page lists in `struct zone` |
| `include/linux/list.h` | the same intrusive list, trimmed to what is used here |

## 10. Glossary

| Term | Meaning |
|---|---|
| **Frame** | one 4 KiB chunk of physical memory |
| **Page** | one 4 KiB chunk of virtual memory (mapped to a frame) |
| **PFN** | page frame number: frame index = physical address / 4096 |
| **Order** | block size as `2^order` frames; order 0 = 1 frame, order 10 = 1024 |
| **Buddy** | the adjacent same-size block, found with `pfn ^ (1 << order)` |
| **Free area** | per-order (and per-migrate-type) list of free blocks + `nr_free` |
| **vmemmap** | the array of per-frame `struct page` records |
| **Zone** | a region of memory the allocator manages with its own lists/watermarks |
| **Split / Expand** | cutting a bigger free block down during allocation |
| **Merge / Coalesce** | recombining two free buddies into one bigger block |
| **HHDM** | the bootloader's window mapping all physical RAM (`phys + offset`) |
| **Identity map** | virtual address == physical address, as built by `paging.c` |
