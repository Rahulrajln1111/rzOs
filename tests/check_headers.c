/*
 * tests/check_headers.c
 *
 * Header-check harness.
 *
 * GCC never compiles a header that nothing includes, so a broken
 * header can sit invisible forever while 'make' stays silent.
 * This file includes every public header and force-CALLS every
 * function in it, so `make check` proves the headers are valid.
 *
 * Rule: whenever you add a public header, add its #include and a
 * force-use block here. A header with no user is unverifiable.
 * (Linux analogue: 'make headers_check', subsystem selftests.)
 */
#include <linux/list.h>
#include <linux/memblock.h>
#include <kernel/atomic.h>
#include <kernel/types.h>
#include <kernel/compiler.h>
#include <x86_64/cpu/percpu.h>

/* A stand-in object that owns the list node + atomic counter,
 * like struct page will when the buddy allocator lands. */
struct foo {
    int data;
    struct list_head link;
    atomic_t count;
};

void check_list_helpers(void)
{
    struct foo a, b;
    struct list_head head;
    struct list_head *pos, *n;
    LIST_HEAD(static_list);

    INIT_LIST_HEAD(&head);
    list_add(&a.link, &head);
    list_add_tail(&b.link, &head);
    if (!list_empty(&head))
        list_del(&a.link);
    list_for_each(pos, &head)
        (void)pos;
    list_for_each_safe(pos, n, &head)
        (void)pos;
    (void)list_entry(&b.link, struct foo, link);
    (void)static_list.next;
    (void)head.prev;
}

void check_atomic_helpers(void)
{
    atomic_t c = ATOMIC_INIT(0);
    atomic_inc(&c);
    atomic_dec(&c);
    (void)atomic_read(&c);

    spinlock_t lock = SPIN_LOCK_UNLOCKED;
    spin_lock_init(&lock);
    spin_lock(&lock);
    unsigned long flags = spin_lock_irqsave(&lock);
    spin_unlock_irqrestore(&lock, flags);
    spin_unlock(&lock);
}

/* per-cpu machinery: a per-cpu variable plus every accessor.
 * These expand to real rdmsr + GS-offset code, so a regression
 * in the macros must not be able to hide. setup_per_cpu() is
 * declared here; the definition lives in arch/x86_64/cpu/percpu.c. */
DEFINE_PER_CPU(int, check_cpu_counter) = 0;

void check_percpu_helpers(void)
{
    this_cpu_write(check_cpu_counter, 1);
    this_cpu_add(check_cpu_counter, 2);
    (void)this_cpu_inc(check_cpu_counter);
    (void)this_cpu_read(check_cpu_counter);
    (void)this_cpu_base();
    (void)this_cpu_ptr(&check_cpu_counter);
    (void)smp_processor_id();
    (void)setup_per_cpu;
}

/* memblock: force-use the boot allocator API so signature drift
 * can't hide. Calls are safe here: this TU is only COMPILED by
 * make check, never linked into kernel.elf - nothing executes. */
void check_memblock_helpers(void)
{
    phys_addr_t got = memblock_phys_alloc(4096, 4096);
    memblock_phys_free(got, 4096);
    (void)memblock_add(got, 4096);
    (void)memblock_reserve(got, 4096);

    struct memblock_region r = { .base = 0, .size = 0, .flags = MEMBLOCK_NONE };
    (void)r.base;
    (void)memblock.memory.cnt;
    (void)memblock.reserved.total_size;
    (void)memblock.bottom_up;
    (void)memblock.current_limit;
}

/* compiler.h macros are attributes - including the header is
 * the whole test; nothing to call. */
