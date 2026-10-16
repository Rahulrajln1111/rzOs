#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include <kernel/types.h>

void pmm_init(u64 hhdm_offset);
struct page *pmm_alloc_pages(int order);
void pmm_free_pages(struct page *page,int order);
u64 pmm_alloc_frame(void); //return phy address.
void pmm_free_frame(u64 phys);
u64 pmm_free_frames(void); // return count of frames;

#endif


