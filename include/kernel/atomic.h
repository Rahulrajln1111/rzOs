#ifndef _KERNEL_ATOMIC_H
#define _KERNEL_ATOMIC_H
 
#include <kernel/types.h>
typedef struct 
{
    volatile int counter;
}atomic_t;

#define ATOMIC_INIT(i) { (i) }
 
static inline int atomic_read(const atomic_t *v)
{
    return v->counter;
}

static inline void atomic_inc(atomic_t *v)
{
    __asm__ volatile("lock incl %0":"+m" (v->counter) :: "memory");
}
static inline void atomic_dec(atomic_t *v){
    __asm__ volatile("lock decl %0":"+m" (v->counter):: "memory");
}


/* Spinlock */

typedef struct {
volatile unsigned char locked;
}spinlock_t;

#define SPIN_LOCK_UNLOCKED { 0 }

static inline void spin_lock_init(spinlock_t *lock)
{
    lock->locked = 0;
}


static inline void spin_lock(spinlock_t *lock){
    unsigned char want = 1;

    __asm__ volatile(
        "1: lock xchgb %0 ,%1\n"
        "testb %0, %0\n"
        "jz 2f\n"
        "pause\n"
        "jmp 1b\n"
        "2:"
        : "+q" (want) ,"+m" (lock->locked)
        :
        : "memory"
    );
}

static inline void spin_unlock(spinlock_t *lock){
    __asm__ volatile(
        "movb $0 ,%0\n"
        : "+m" (lock->locked) :: "memory"
    );
}

static inline unsigned long spin_lock_irqsave(spinlock_t *lock){

    unsigned long flag;

    __asm__ volatile(
        "pushfq\n"
        "pop %0\n"
        "cli\n"
        : "=r" (flag)
        ::"memory"
    );

    spin_lock(lock);
    return flag;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock,unsigned long flag){
spin_unlock(lock);
    __asm__ volatile("push %0;popfq":: "r"(flag):"memory");
    
}
 
#endif
