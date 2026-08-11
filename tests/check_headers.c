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
#include <kernel/atomic.h>
#include <kernel/types.h>
#include <kernel/compiler.h>

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

/* compiler.h macros are attributes - including the header is
 * the whole test; nothing to call. */
