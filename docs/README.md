# rzOs — Milestone 6: The Buddy Allocator (Physical Memory Manager)

> Read this **while** typing `mm/pmm.c`. Every function below is explained,
> including what it does, why it exists, and how Linux does the same thing.

---

## 0. Big picture: what is the PMM?

Physical memory = the actual RAM chips, addressed from `0x0` upward.
The kernel cuts it into **4 KiB pieces called frames** — the same size as a
page, so that the paging milestone (next) can map frames into pages.

The Physical Memory Manager (PMM) answers two questions:
- *"Give me a free frame (or N contiguous frames)"* → `pmm_alloc_pages()`
- *"This block is done"* → `pmm_free_pages()`

The algorithm we use is the **buddy allocator** — the same one in
`mm/page_alloc.c` of the Linux kernel (and therefore Android, which is a
Linux kernel). The core of it is ~150 lines.

### Files involved

| File | Role |
|---|---|
| `include/kernel/pmm.h` | public API |
| `mm/pmm.c` | the allocator (everything explained below) |
| `kernel/init.c` | calls `pmm_init()` + prints a test |

---

## 1. The Limine memory-map request

```c
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};
```

**What:** at boot, the bootloader scans the kernel image for requests with
known magic values (this is the same mechanism as the framebuffer and HHDM
requests already in `kernel/init.c`) and fills in `memmap_request.response`
with a list of memory regions:

```
{ base = 0x100000, length = 0xFE00000, type = USABLE }   ← RAM we may use
{ base = ...,     length = ...,        type = RESERVED }  ← NOT ours
{ base = ...,     length = ...,        type = KERNEL+MODULES } ← the kernel itself
```

**Why it lives here and not in `init.c`:** the *driver that needs the data
owns the request* — the Linux pattern. Limine scans `.limine_requests`
regardless of which file defines the entry.

**Rule of the allocator:** only regions with `type == LIMINE_MEMMAP_USABLE`
may ever be handed out. Everything else (BIOS areas, your kernel, the
bootloader) is marked used and never touched.

---

## 2. The core data structures

### `struct page` — one record per frame

```c
struct page {
    u8  order;      /* block size 2^order, meaningful only when free */
    u8  flags;      /* bit 0 = PAGE_FREE: "head of a free block"    */
    u32 next;       /* next free block index in this order's list   */
    u32 reserved;
};
```

**What:** one of these exists for *every frame in the machine*. For 256 MiB
that's 65536 records × 16 bytes ≈ 1 MiB of metadata.

**Why:** the allocator needs to ask *"is this frame free, and what block
size is it part of?"* — that's the information the merge step reads. Linux
has the exact same idea: `struct page` in `include/linux/mm_types.h`
(vmemmap). Linux's is 64 bytes; ours is 16. Same role.

**Mental model:** `pages` is an array — `pages[frame_number]` is the record
for that frame. It lives in physical memory and is reached through the HHDM
window (`hhdm + pages_phys`), exactly like `lapic.c` reached the LAPIC.

### The 11 free lists

```c
#define MAX_ORDER    11       /* orders 0..10: 4 KiB .. 4 MiB */
static u32 free_head[MAX_ORDER];
```

**What:** `free_head[order]` is the head of a singly-linked list of free
*blocks*, where every block on list `order` is exactly `2^order` frames
(contiguous):

```
order 0:  1 frame   =  4 KiB
order 1:  2 frames  =  8 KiB
order 2:  4 frames  = 16 KiB
  ⋮
order 10: 1024 frames = 4 MiB
```

**Why powers of two?** Because any size can be built from them, and because
*mergeability* is guaranteed: two adjacent same-size blocks always form one
block of the next size. That's the entire foundation of the buddy idea.

### The list plumbing (`list_push`, `list_pop`, `list_remove`)

```c
static void list_push(u32 idx, int order) { ... }   /* add block to a list */
static u32 list_pop(int order)            { ... }   /* take head off a list */
static void list_remove(u32 idx, int order) { ... } /* unlink a known block */
```

**What:** the free lists are linked *by frame index* (`u32`), not by
pointer — a frame's physical address is just `index * 4096`, so storing
indices keeps everything simple and avoids pointer math on physical memory.
`NO_BLOCK` (`~0U`) is the end-of-list marker.

- `list_push` marks the block free (`PAGE_FREE`), records its order, and
  puts it at the head of its list.
- `list_pop` takes the head block off, **clears** `PAGE_FREE` (it's being
  allocated now), and returns it.
- `list_remove` walks a list to unlink a *specific* block — needed by the
  merge step, which must pull a buddy out of its list without popping the
  head. (Linux uses doubly-linked lists; singly-linked is our simplification.)

---

## 3. Freeing + merging — `__free_block()` (the heart)

```c
static void __free_block(u64 pfn, int order)
{
    free_count += 1ULL << order;   /* this block becomes free */

    while (order < MAX_ORDER - 1) {
        u64 buddy = pfn ^ (1ULL << order);      /* ← THE XOR TRICK */
        struct page *b = &pages[buddy];

        if (!(b->flags & PAGE_FREE) || b->order != order)
            break;                              /* buddy busy → stop */

        list_remove((u32)buddy, order);
        pfn &= ~(1ULL << order);                /* keep the lower buddy */
        order++;
    }

    list_push((u32)pfn, order);
}
```

### The XOR trick — the single most important line in the allocator

The **buddy** of a block is the adjacent block of the same size. Because
every free block is aligned to its own size, the buddy's frame number is
always the block's frame number with exactly **one bit flipped**:

```
buddy_pfn = pfn ^ (1 << order)
```

Example: a 2-frame (order 1) block at frame 6 (phys `0x6000`):
`buddy = 6 ^ 2 = 4` — frame 4 is the adjacent 2-frame block (frames 4–5).
Flip the bit, land on the buddy. One instruction. This is literally
Linux's `__free_one_page()`.

### What the loop does

1. Add the block's frames to `free_count`.
2. Look at my buddy: is it **free** AND **exactly my size**?
   - No → stop, just push myself onto my order's list.
   - Yes → unlink the buddy, take the *lower* of the two as the new base
     (`pfn &= ~(1 << order)`), and **move up one order** — I'm now a block
     twice as big. Repeat.
3. Push the (possibly merged) block onto its final list.

This loop is what **kills fragmentation**: scattered small frees
automatically recombine into big blocks the moment two buddies are both
free.

### Worked trace — free frame 8 (phys `0x8000`) as order 0

State before: order-0 list has frame 9; order-1 has frame 10; order-2 has
frame 12 (leftovers from a split — see section 4).

```
free frame 8, order 0:
  buddy = 8 ^ 1 = 9 → frame 9 free, order 0?  YES → unlink 9, base = 8&~1 = 8, order 1
  buddy = 8 ^ 2 = 10 → frame 10 free, order 1? YES → unlink 10, base = 8&~2 = 8, order 2
  buddy = 8 ^ 4 = 12 → frame 12 free, order 2? YES → unlink 12, base = 8, order 3
  buddy = 8 ^ 8 = 0  → frame 0 free, order 3?   NO  → stop
  push frame 8 at order 3  → 8 contiguous frames recombined!
```

The original 8-frame block is reconstructed perfectly.

---

## 4. Allocating + splitting — `pmm_alloc_pages()` (Linux's `expand`)

```c
u64 pmm_alloc_pages(int order)
{
    for (int o = order; o < MAX_ORDER; o++) {
        u32 idx = list_pop(o);

        if (idx == NO_BLOCK)
            continue;                       /* nothing this size — try bigger */

        /* Split: while the block is bigger than asked, push each upper
         * buddy down to the next-lower order's list. */
        while (o > order) {
            o--;
            list_push(idx + (1U << o), o);
        }

        free_count -= 1ULL << order;
        return (u64)idx * FRAME_SIZE;
    }
    return 0;   /* out of memory */
}
```

### The two phases

**Phase 1 — find a block.** Start at the requested order; if that list is
empty, climb up until you find a non-empty list (a bigger block).

**Phase 2 — split (Linux's `expand`).** While the block is bigger than
asked, cut it in half: keep the *lower* half, push the *upper* half (its
buddy) onto the next-lower list. Repeat until the kept piece is the size
you want.

### Worked trace — allocate 1 frame (order 0) from an order-3 block at frame 8

```
pop order 3 → frame 8 (frames 8..15, phys 0x8000..0x9FFF)
split:
  o=3 → o=2: push buddy frame 12 (0xC000) at order 2   [frames 12..15]
  o=2 → o=1: push buddy frame 10 (0xA000) at order 1   [frames 10..11]
  o=1 → o=0: push buddy frame 9  (0x9000) at order 0   [frame 9]
return 0x8000   [frame 8]
```

The caller gets frame 8; the three upper halves wait on their lists —
ready to be handed out, or recombined later by the merge.

---

## 5. The public API + the double-free guard

```c
void pmm_free_pages(u64 phys, int order)
{
    u64 pfn = phys / FRAME_SIZE;

    if (pages[pfn].flags & PAGE_FREE)   /* double-free guard */
        return;

    __free_block(pfn, order);
}

u64 pmm_alloc_frame(void) { return pmm_alloc_pages(0); }
void pmm_free_frame(u64 phys) { pmm_free_pages(phys, 0); }
u64 pmm_free_frames(void) { return free_count; }
```

**The guard:** the head page of any free block has `PAGE_FREE` set — so if
someone frees an address that's *already free*, we detect it and ignore it
instead of corrupting the lists. This is the same "is this block ours?"
check Linux does on every free path (`PageBuddy()`). Remove it and
double-freeing silently corrupts the allocator — the bug class that fills
kernel CVE lists.

`pmm_alloc_frame()` / `pmm_free_frame()` are order-0 convenience wrappers,
so the rest of the kernel can think in "one frame" terms.

---

## 6. Building the free lists at boot — `free_range()` + `pmm_init()`

### Carving a region into aligned power-of-2 blocks

```c
static void free_range(u64 base, u64 end)
{
    while (base < end) {
        int order = 0;
        while (order + 1 < MAX_ORDER &&
               (base & (((1ULL << (order + 1)) * FRAME_SIZE) - 1)) == 0 &&
               base + ((1ULL << (order + 1)) * FRAME_SIZE) <= end)
            order++;
        __free_block(base / FRAME_SIZE, order);
        base += (1ULL << order) * FRAME_SIZE;
    }
}
```

**What:** takes a usable range `[base, end)` and carves it into the biggest
possible *aligned* blocks, largest first. The alignment check matters:
a block of `2^order` frames must start at a frame number divisible by
`2^order`, or the XOR buddy math breaks.

**Worked trace — 10 frames at frame 16 (phys `0x10000`..`0x127FF`):**

```
base=16: can we use order 4? needs 16 frames (16+16=32 > 26) → no.
         order 3? aligned (16 % 8 == 0), fits (16+8=24 ≤ 26) → free frames 16..23
base=24: order 1? aligned (24 % 2 == 0), fits (24+2=26 ≤ 26) → free frames 24..25
done: one order-3 block + one order-1 block
```

### `pmm_init()` — the boot dance (Linux's `memblock_free_all`)

```c
void pmm_init(u64 hhdm_offset) { ... }
```

In order:
1. **Count frames:** find the highest usable address → `total_frames`.
2. **Reset lists:** every `free_head[order] = NO_BLOCK`.
3. **Place metadata:** the `struct page` array goes in the *first usable
   region* (same trick Linux uses: its `mem_map` lives in direct-mapped
   memory). Zero it — the flags must be clean before anything is freed.
4. **Free the world:** for every usable region, skip the metadata array's
   own frames, then `free_range()` the rest.
5. `free_count` now holds the truth: how much RAM the kernel can actually
   use.

---

## 7. The test and break-tests

### Expected boot output (256 MiB)

```
PMM (buddy): 648xx free frames (25x MiB)
PMM test: 0x1000 0x2000 big=0x3000
PMM test: freed, free frames = 648xx
tick: 100
tick: 200 ...
```

Free count after the test equals free count before → alloc/free round-trips
perfectly.

### Break-tests (do these once it works)

1. **Merge proof:** allocate `order-3` (8 frames), free it, then allocate
   8 single frames — you must get the *same 8 consecutive addresses back*.
   Only a merging allocator can promise consecutiveness.
2. **Fragmentation behavior:** allocate every other frame, then try
   `pmm_alloc_pages(1)` — it fails while many frames are free (no two
   *adjacent* free frames). Free everything, and `order-3` works again.
   That is the buddy's whole point, demonstrated.
3. **Double-free:** `pmm_free_frame(f1)` twice — the guard catches it, the
   free count stays correct.

---

## 8. Honest map to the real Linux kernel

| Our code | Linux equivalent |
|---|---|
| `free_head[MAX_ORDER]` | `struct free_area free_area[MAX_ORDER]` (`include/linux/mmzone.h`) |
| `pmm_alloc_pages()` split | `__rmqueue_smallest()` + `expand()` (`mm/page_alloc.c`) |
| `__free_block()` XOR merge | `__free_one_page()` — same `pfn ^ (1 << order)` |
| `struct page` array | `struct page` + vmemmap (`include/linux/mm_types.h`) |
| `pmm_init()` carving | `memblock_free_all()` / `free_all_bootmem()` |
| double-free guard | `PageBuddy()` checks in the free path |

### What we deliberately simplified (so you can't be fooled later)

- **One zone.** Linux splits memory into DMA / NORMAL / MOVABLE zones with
  per-zone free lists (`zonelists`).
- **No per-CPU page lists.** Linux keeps a hot cache of order-0 pages per
  CPU for speed.
- **No NUMA, no CMA, no cgroup accounting** (Android layers the latter on
  top of this same buddy).
- **Singly-linked lists** instead of doubly-linked `struct page` links.

Each of those is an "add one layer" upgrade later — the algorithm underneath
is exactly what you typed here.

---

## 9. Glossary (learn these cold)

| Term | Meaning |
|---|---|
| **Frame** | one 4 KiB chunk of physical memory |
| **Page** | one 4 KiB chunk of *virtual* memory (mapped to a frame in the next milestone) |
| **Order** | block size as `2^order` frames: order 0 = 1 frame, order 10 = 1024 |
| **Buddy** | the adjacent same-size block; found with `pfn ^ (1 << order)` |
| **Free list** | linked list of free blocks of one specific order |
| **HHDM** | the bootloader's virtual window that maps *all* physical memory (`phys + offset`), used to touch physical memory from C |
| **Memmap** | the bootloader's list of memory regions with types (usable/reserved/…) |
| **vmemmap / struct page** | the array of per-frame records |
| **Split / Expand** | cutting a big free block in half during allocation |
| **Merge** | recombining two free buddies into a bigger block during free |
