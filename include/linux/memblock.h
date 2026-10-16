#ifndef _LINUX_MEMBLOCK_H
#define _LINUX_MEMBLOCK_H
 
#include <kernel/types.h>
 
enum memblock_flags {
    MEMBLOCK_NONE = 0x0,
};

// one contiguous region of memory, with a base address, size, and flags.
struct memblock_region {
    phys_addr_t base;
    phys_addr_t size;
    enum memblock_flags flags;
};

// a list of memory regions, with a count, max size, total size, and a pointer to the array of regions.
struct memblock_type {
    unsigned long cnt;     // number of regions in the list   
    unsigned long max;         // maximum number of regions the list can hold
    phys_addr_t total_size;   // sum of all region sizes
    struct memblock_region *regions; // pointer to the array of regions
    char *name; // name of the list (for debugging)
};
 
// the global memblock structure, which contains the memory and reserved lists, as well as a flag for bottom-up allocation and a current limit for allocation.
struct memblock {
    bool bottom_up;             // allocate from the low end
    phys_addr_t current_limit;  // never allocate above this
    struct memblock_type memory;
    struct memblock_type reserved;
};
 
extern struct memblock memblock;
 

int memblock_add(phys_addr_t base, phys_addr_t size);
int memblock_reserve(phys_addr_t base, phys_addr_t size);
 

phys_addr_t memblock_phys_alloc(phys_addr_t size, phys_addr_t align);
void memblock_phys_free(phys_addr_t base, phys_addr_t size);
 
#endif
