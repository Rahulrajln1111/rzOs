/*
 * arch/x86_64/cpu/idt.h
 */
#ifndef _ASM_IDT_H
#define _ASM_IDT_H

#include <kernel/types.h>

/*
 * Register frame captured by the entry stubs when an exception fires.
 * Layout matches the push order in idt_stubs.S, followed by what the
 * CPU pushed (vector, error code, and the interrupt frame). This is
 * the same idea as Linux's struct pt_regs.
 */
struct trap_frame {
    /* pushed by the stub (lowest address first) */
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* pushed by the stub */
    u64 vector;
    u64 error_code;
    /* pushed by the CPU on interrupt delivery */
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

void idt_init(void);

/* Install a gate for any vector 0..255 (PIC/APIC interrupts etc.). */
void idt_set_gate(unsigned int n, u64 addr, unsigned int ist,
                  unsigned int dpl);

/* Dump a CPU exception and panic (called by the entry stubs for
 * vectors 0..31; declared here so kernel/irq.c can route to it). */
void exception_handler(struct trap_frame *f);

#endif /* _ASM_IDT_H */
