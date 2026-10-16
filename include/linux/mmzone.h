/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/mmzone.h
 *
 * The physical memory layout for the zone allocator (the buddy allocator in
 * the kernel's page allocator). It is SMP-shaped from birth, exactly like
 * upstream Linux's include/linux/mmzone.h (which lives alongside
 * include/linux/mm_types.h for struct page).
 *
 * Linux equivalent:
 *   include/linux/mmzone.h (zone, zoneref, zonelist, free_area, per_cpu_pages,
 *   per_cpu_pageset), include/linux/mm_types.h (struct page).
 */

#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#include <kernel/types.h>
#include <linux/list.h>
#include <kernel/atomic.h>
#include <kernel/compiler.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))

/* nr_order_cnt in upstream. 11 orders (0..10) mirrors the classic x86
 * physical allocator range: 4 KiB .. 8 MiB. */
#define NR_PAGE_ORDERS 11
#define MAX_ORDER (NR_PAGE_ORDERS - 1)
#define MAX_ORDER_NR_PAGES (1UL << MAX_ORDER)

/* ------------------------------------------------------------------ */
/* Zone types                                                          */
/* ------------------------------------------------------------------ */
enum zone_type {
	ZONE_DMA,
	ZONE_DMA32,
	ZONE_NORMAL,
	ZONE_MOVABLE,
	__MAX_NR_ZONES
};

#define MAX_NR_ZONES    (__MAX_NR_ZONES)

/* ------------------------------------------------------------------ */
/* Watermarks                                                          */
/* ------------------------------------------------------------------ */
enum zone_watermarks {
	WMARK_MIN,
	WMARK_LOW,
	WMARK_HIGH,
	NR_WMARK
};

/* ------------------------------------------------------------------ */
/* Migrate types - the "aisle" a free block belongs to.               */
/* ------------------------------------------------------------------ */
#define MIGRATE_UNMOVABLE   0
#define MIGRATE_RECLAIMABLE 1
#define MIGRATE_MOVABLE     2
#define MIGRATE_TYPES       3

#define MIGRATE_PCPTYPES    MIGRATE_TYPES

/* --------------------------------------------------------------- */
/* struct page - one record per frame.
 *
 * Upstream: include/linux/mm_types.h. We keep the realistic fields the
 * page allocator and slab code rely on, plus the compound-page fields
 * used by hugetlb/HugeTLB and the free-list helper used by the buddy
 * allocator below. The per-order/per-migrate type free lists themselves
 * live in struct free_area, not in struct page.
 *
 * For a static 1:1 port we keep the compound-node bookkeeping small.
 * --------------------------------------------------------------- */
struct page {
	/* (SPIN) flags: order vs compound, PageSignaled, etc. */
	unsigned long flags;

	/* Reference counter. In upstream this is the first element of the
	 * compound head; we keep it as a standalone atomic here. */
	atomic_t _refcount;

	/* Virtual address, if a VM object maps this page. */
	void *virtual;

	/* Index in the zone's free lists; used by the buddy allocator
	 * (`_page_order`'s sibling bookkeeping is kept in free_area). */
	unsigned long _private;

	/* Compound-page bookkeeping. */
	struct list_head compound_head;  /* head of this compound page */
	struct list_head freelist;       /* free list link for buddy search */
};



/* --------------------------------------------------------------- */
/* struct free_area - one shelf per order, one free list per      */
/* migrate type.  This is the real "tree" of the buddy allocator.  */
/* --------------------------------------------------------------- */
struct free_area {
	struct list_head free_list[MIGRATE_TYPES];
	unsigned long nr_free;
};

/* --------------------------------------------------------------- */
/* struct per_cpu_pages / per_cpu_pageset - per-CPU fast path.     */
/* --------------------------------------------------------------- */
struct per_cpu_pages {
	spinlock_t lock;
	int count;
	int high;
	int batch;
	struct list_head lists[MIGRATE_PCPTYPES];
} ____cacheline_aligned_in_smp;

struct per_cpu_pageset {
	struct per_cpu_pages pcp;
};

/* --------------------------------------------------------------- */
/* struct zone - the floor of the building.  Real Linux zone layout.*/
/* --------------------------------------------------------------- */
struct zone {
	/* Watermarks: the fuel gauge that wakes kswapd. */
	unsigned long _watermark[NR_WMARK];

	/* The zone descriptor: this is the "building" (NUMA node) that */
	/* owns this zone. */
	struct pglist_data *zone_pgdat;

	/* HugeTLB and NUMA-aware free pages per migrate type. */
	struct per_cpu_pages __percpu *per_cpu_pageset;

	/* Lock for the zone's free lists. */
	spinlock_t lock;

	/* Free lists per order (0..MAX_ORDER). */
	struct free_area free_area[MAX_ORDER + 1];

	/* The zone range, in pages. */
	unsigned long zone_start_pfn;
	unsigned long spanned_pages;
	unsigned long present_pages;

	/* ZONE_DEVICE handling (reserved for device mappings). */
	unsigned long *zone_device_soft_dirty;

	/* The name of the zone (for /proc/zoneinfo). */
	const char *name;

	/* Number of pages in the zone that are free via the per-cpu */
	/* fast path. */
	unsigned long free_pages;

	/* Number of pages in the zone that are dirty / writeback. */
	unsigned long nr_zone_watermark_updates;
} ____cacheline_aligned_in_smp;

/* --------------------------------------------------------------- */
/* struct zoneref / struct zonelist - the allocator's walk.        */
/* --------------------------------------------------------------- */
#define MAX_ZONES_PER_ZONELIST  MAX_NR_ZONES

struct zoneref {
	struct zone *zone;
	int zone_idx;
};

struct zonelist {
	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST + 1];
};

enum zonelist_type {
	ZONELIST_FALLBACK,
	ZONELIST_NOFALLBACK,
	MAX_ZONELISTS
};

/* --------------------------------------------------------------- */
/* struct pglist_data - the "building" that holds one node's       */
/* zones and its mem_map. NUMA: one per node, NODE_DATA(nid).      */
/* --------------------------------------------------------------- */
typedef struct pglist_data {
	struct zone *node_zones;
	struct zonelist *node_zonelists;
	int nr_zones;
	struct page *node_mem_map;
	unsigned long node_start_pfn;
	unsigned long node_spanned_pages;
	unsigned long node_present_pages;
} pglist_data;

typedef struct pglist_data pg_data_t;

/* --------------------------------------------------------------- */
/* Global symbols.                                                   */
/* --------------------------------------------------------------- */
extern struct page *vmemmap;
extern struct pglist_data contig_page_data;

#define NODE_DATA(nid)      (&contig_page_data)
#define pfn_to_page(pfn)    (vmemmap + (pfn))
#define page_to_pfn(page)   ((unsigned long)((page) - vmemmap))

#endif /* _LINUX_MMZONE_H */
