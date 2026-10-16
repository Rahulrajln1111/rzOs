#include <linux/memblock.h>
#include <linux/mmzone.h>  
#include <kernel/string.h>
 
#define MEMBLOCK_MAX_REGIONS 64
 

static struct memblock_region memblock_memory_init_regions[MEMBLOCK_MAX_REGIONS];
static struct memblock_region memblock_reserved_init_regions[MEMBLOCK_MAX_REGIONS];
 
struct memblock memblock = {
    .bottom_up = true,
    .current_limit = ~(phys_addr_t)0,  
    .memory = {
        .regions = memblock_memory_init_regions,
        .max = MEMBLOCK_MAX_REGIONS,
        .name = "memory",
    },
    .reserved = {
        .regions = memblock_reserved_init_regions,
        .max = MEMBLOCK_MAX_REGIONS,
        .name = "reserved",
    },
};
 

static void memblock_insert_region(struct memblock_type *type, unsigned long idx,
                                   phys_addr_t base, phys_addr_t size,
                                   enum memblock_flags flags)
{
    struct memblock_region *rgn = &type->regions[idx];
    // shift the tail of the array up to make room for the new region
    memmove(rgn + 1, rgn, (type->cnt - idx) * sizeof(*rgn));

    rgn->base = base;
    rgn->size = size;
    rgn->flags = flags;
    type->cnt++;
    type->total_size += size;
}
 
static void memblock_merge_regions(struct memblock_type *type)
{
    unsigned long i = 0;
 
    while (i + 1 < type->cnt) {
        struct memblock_region *rgn = &type->regions[i];
        struct memblock_region *next = &type->regions[i + 1];
 
        if (rgn->base + rgn->size == next->base &&
            rgn->flags == next->flags) {
            rgn->size += next->size;
            // slide the tail of the array down to remove the next region
            memmove(next, next + 1,(type->cnt - (i + 1)) * sizeof(*rgn));

            type->cnt--;
            continue;   // don't advance i - the next region slid into this slot
        }
        i++;
    }
 
    type->total_size = 0;
    for (i = 0; i < type->cnt; i++)
        type->total_size += type->regions[i].size;
}
 

static int memblock_add_range(struct memblock_type *type,
                              phys_addr_t base, phys_addr_t size,
                              enum memblock_flags flags)
{
    unsigned long i;
 
    if (size == 0)
        return 0;
 
    // Check for overlap with existing regions. If the new region is
    // completely contained within an existing region, we don't need to add it.
    for (i = 0; i < type->cnt; i++) {
        if (base >= type->regions[i].base &&
            base + size <= type->regions[i].base + type->regions[i].size)
            return 0;
    }
 
    // Find the insertion point for the new region. The list is sorted by base address.
    for (i = 0; i < type->cnt; i++)
        if (type->regions[i].base >= base + size)
            break;
 
    // if the list is full, we can't add a new region
    if (type->cnt == type->max)
        return -1; 
 
    memblock_insert_region(type, i, base, size, flags);
    memblock_merge_regions(type);
    return 0;
}
 
int memblock_add(phys_addr_t base, phys_addr_t size)
{
    return memblock_add_range(&memblock.memory, base, size, MEMBLOCK_NONE);
}
 
int memblock_reserve(phys_addr_t base, phys_addr_t size)
{
    return memblock_add_range(&memblock.reserved, base, size, MEMBLOCK_NONE);
}
 

static void memblock_remove_region(struct memblock_type *type, unsigned long idx)
{
    struct memblock_region *rgn = &type->regions[idx];
 
    memmove(rgn, rgn + 1, (type->cnt - idx - 1) * sizeof(*rgn));
    type->cnt--;
}
 
static void memblock_remove_range(struct memblock_type *type,
                                  phys_addr_t base, phys_addr_t size)
{
    phys_addr_t end = base + size;
    unsigned long i = 0;
 
    while (i < type->cnt) {
        struct memblock_region *rgn = &type->regions[i];
        phys_addr_t rstart = rgn->base;
        phys_addr_t rend = rstart + rgn->size;
        
        // if the region is completely before the range to remove, skip it
        if (rend <= base) {
            i++;                   
            continue;
        }

        // if the region is completely after the range to remove, we're done
        if (rstart >= end)
            break;                  
 
        // if the region overlaps with the range to remove, we need to trim or remove it
        if (rstart < base && rend > end) {
            rgn->size = base - rstart;                     
            memblock_add_range(type, end, rend - end, rgn->flags);
            i++;
        } else if (rstart < base) {
            rgn->size = base - rstart;  // trim the tail
            i++;
        } else if (rend > end) {
            rgn->base = end;             // trim the head
            rgn->size = rend - end;
            i++;
        } else {
            memblock_remove_region(type, i); // remove the whole region
        }
    }
 
    // recalculate the total size of the list
    type->total_size = 0;
    for (i = 0; i < type->cnt; i++)
        type->total_size += type->regions[i].size;
}

// Find a free region of memory of the given size and alignment, below the current limit.
static phys_addr_t memblock_find_in_range(phys_addr_t size, phys_addr_t align)
{
    unsigned long i, j;
 
    for (i = 0; i < memblock.memory.cnt; i++) {
        phys_addr_t rstart = memblock.memory.regions[i].base;
        phys_addr_t rend = rstart + memblock.memory.regions[i].size;
        
        // if the region is above the current limit, skip it
        if (rend > memblock.current_limit)
            rend = memblock.current_limit;
        
        /* Linux mm/memblock.c L302-303:
         *     // avoid allocating the first page
         *     start = max_t(phys_addr_t, start, PAGE_SIZE);
         * Without this, a map whose usable RAM starts at address 0
         * (this QEMU's does!) hands out address 0 - which collides
         * with 0 meaning "nothing found" in memblock_phys_alloc(). */
        if (rstart < PAGE_SIZE)
            rstart = PAGE_SIZE;

        // if the region is too small, skip it
        if (rstart + size > rend)
            continue;
        
        // find the first aligned address in the region
        phys_addr_t cand = (rstart + align - 1) & ~(align - 1);
 
        // check if the candidate address is within the region and does not overlap with any reserved regions
        while (cand + size <= rend) {

            int free = 1;
            // check for overlap with reserved regions
            for (j = 0; j < memblock.reserved.cnt; j++) {
                phys_addr_t s = memblock.reserved.regions[j].base;
                phys_addr_t e = s + memblock.reserved.regions[j].size;
 
                if (cand < e && cand + size > s) {
                    free = 0;
                    break;
                }
            }
            if (free)
                return cand;
            // move to the next aligned address , if the current candidate is not free
            cand += align;
        }
    }
    return 0; 
}
 
phys_addr_t memblock_phys_alloc(phys_addr_t size, phys_addr_t align)
{
    phys_addr_t found = memblock_find_in_range(size, align);
 
    if (found == 0)
        return 0;
 
    if (memblock_reserve(found, size) < 0)
        return 0;
 
    return found;
}
 
void memblock_phys_free(phys_addr_t base, phys_addr_t size)
{
    memblock_remove_range(&memblock.reserved, base, size);
}
