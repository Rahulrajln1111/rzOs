# rzOs — module guide

rzOs is a small x86_64 kernel written from scratch, in the file layout of the
Linux tree: `arch/`, `kernel/`, `mm/`, `drivers/`, `lib/` and
`include/<subsystem>/`. It is booted by [Limine](https://github.com/limine-bootloader/limine)
in long mode as a higher-half kernel, and it runs on QEMU with OVMF. Every
subsystem below is explained as a module: what it owns, which files implement
it, what its API is, and how it is wired into the boot path.

The deep dive for the page allocator lives in [docs/README.md](README.md);
the longer-term learning roadmap lives in the [project README](../README.md).

---

## 1. Build and run

```bash
make            # build kernel.elf and assemble the bootable image/ tree
make run        # boot it in QEMU (256 MiB RAM, serial on stdio)
make debug      # same, but with -s -S so gdb can attach
make check      # compile the header harness: every public header is force-used
make clean      # remove objects, kernel.elf and image/EFI
make distclean  # also remove the vendored Limine clone and image/
```

Host tools: `gcc`, `ld`, `qemu-system-x86_64`, an OVMF firmware blob
(`/usr/share/qemu/OVMF.fd`), and network access on the first build — `limine/`
is cloned at `v11.x-binary` by the Makefile and is not committed.

The kernel is compiled freestanding (`-ffreestanding -mcmodel=kernel
-mno-red-zone -mno-sse`) and linked by `arch/x86_64/boot/linker.ld` at
`0xffffffff80000000` with four PT_LOAD segments (requests, text, rodata, data).

---

## 2. Module index

| Module | Files | One-line job |
|---|---|---|
| Boot handoff | `kernel/init.c` (requests), `arch/x86_64/boot/entry.c`, `linker.ld`, `limine.conf` | Take the machine from Limine: base revision, framebuffer, HHDM, memory map |
| Hand-written UEFI loader | `bootloader/bmain.c`, `kernel/elf.hpp` | Standalone GNU-EFI app: open ESP, size, read and verify `kernel.elf` (not in the kernel build) |
| Console | `drivers/serial/serial.c`, `include/kernel/serial.h` | 16550 COM1 TX in polling mode — the kernel's debug output |
| CPU tables | `arch/x86_64/cpu/gdt.c`, `idt.c`, `idt_stubs.S`, `trapnr.h`, `include/kernel/panic.h` | Own GDT, 256 IDT gates, trap frame, exception dump + panic |
| Interrupt core | `kernel/irq.c`, `drivers/pic/pic.c` | Remap the 8259 PIC to vectors 32..47, dispatch, EOI |
| Timer | `drivers/pit/pit.c`, `arch/x86_64/cpu/lapic.c` | PIT at 100 Hz → `jiffies`; unmask LAPIC LVT0 (ExtINT) and map its MMIO |
| Concurrency | `include/kernel/atomic.h` | `atomic_t`, `spinlock_t`, `spin_lock_irqsave/irqrestore` |
| Per-CPU storage | `arch/x86_64/cpu/percpu.c`, `percpu.h`, `msr.h`, `include/linux/percpu-defs.h` | `DEFINE_PER_CPU`, `this_cpu_*` via the GS base, `smp_processor_id()` |
| Page tables (early) | `arch/x86_64/boot/paging.c` | Identity-map all usable RAM and switch CR3 |
| Boot memory map | `mm/memblock.c`, `include/linux/memblock.h` | Limine memmap → memory/reserved region lists + first-fit early allocator |
| Page allocator | `mm/pmm.c`, `include/kernel/pmm.h`, `include/linux/mmzone.h`, `include/linux/list.h` | `struct page` vmemmap, zone + per-order free areas, the frame allocator |
| Base library | `include/kernel/types.h`, `compiler.h`, `string.h`, `lib/string.c` | Freestanding `u8..u64`, attribute macros, `memcpy/memset/memmove/strlen/strcmp` |
| Header harness | `tests/check_headers.c` | Forces a compile of every public header (`make check`) |

---

## 3. Boot handoff — from firmware to `start_kernel()`

**Files:** `limine.conf`, `arch/x86_64/boot/linker.ld`,
`arch/x86_64/boot/entry.c`, `kernel/init.c`.

Limine scans the kernel image for boot-protocol requests: structures marked
`__attribute__((used, section(".limine_requests")))` carrying a magic `id`.
`kernel/init.c` declares four of them — base revision, framebuffer, HHDM offset
and memory map — and the linker script keeps the section and its start/end
markers, so the bootloader can find them without any convention on the entry
point's arguments.

The entry point is `_start()` in `arch/x86_64/boot/entry.c`, the analogue of
Linux's `arch/x86/kernel/head_64.S`: paging is already on, the stack is set,
and it calls `start_kernel()`. `start_kernel()` (Linux: `init/main.c`) then
brings subsystems up **in dependency order**, which is the whole design of this
kernel:

```
serial → GDT → IDT → per-CPU/GS → framebuffer report
       → memblock (RAM map) → paging (identity alias) → buddy allocator
       → PIC/LAPIC/PIT → sti → idle loop
```

Anything that can fault (IDT) or print (serial) comes before the code that can
fault or print. The memory managers come next because everything after them may
need to allocate; the timer comes last, because from the first `sti` the CPU can
interrupt at any instruction.

## 4. Hand-written UEFI loader — `bootloader/`

**Files:** `bootloader/bmain.c`, `kernel/elf.hpp`.

This is where the project started: a from-scratch UEFI application (GNU-EFI,
`efi_main`) that locates the ESP through `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`,
opens `kernel.elf`, asks the firmware for its **real** size with `GetInfo`
(instead of hardcoding a buffer), allocates exactly enough pages with
`AllocatePages`, reads it in a short-read-safe loop, and validates the ELF image
by magic plus `e_entry`. `kernel/elf.hpp` carries the ELF64 structures it needs
(`Elf64_Ehdr`, `Elf64_Phdr`, `Elf64_Dyn`, `Elf64_Rela` and the x86_64 relocation
types) — the same definitions a segment loader will use.

**Status:** it is deliberately *not* part of the kernel build (the Makefile
compiles `kernel lib drivers mm arch` only), and the kernel today boots through
Limine. `bmain.c` is the loader experiment: it stops at "valid ELF, here is the
entry point" and does not yet map segments or call `ExitBootServices()`.

## 5. Console — `drivers/serial`

**Files:** `drivers/serial/serial.c`, `include/kernel/serial.h`.

A polling 16550 UART driver on COM1 (`0x3F8`): divisor programming for 115200
baud, 8N1, FIFO on, then `serial_write()` (THRE-bounded), `serial_puthex()` and
`serial_putdec()` on top. It plays the role of Linux's early console —
`drivers/tty/serial/8250` below `printk` — and it is the only output device in
the kernel today (`limine.conf` also sends the bootloader's own console to the
serial port). Being interrupt-free, it is safe from the very first instruction
of `start_kernel()`.

## 6. CPU tables — GDT, IDT and exceptions

**Files:** `arch/x86_64/cpu/gdt.c|h`, `idt.c|h`, `idt_stubs.S`, `trapnr.h`,
`include/kernel/panic.h`, `kernel/panic.c`.

- **GDT** (`gdt.c`): flat 64-bit descriptors — null, kernel code `0x08`, kernel
  data `0x10` — installed with `lgdt` plus a far-return to reload `CS` and
  re-select the data segments. Limine already left *a* GDT behind, but the
  kernel should not depend on an undocumented one.
- **IDT** (`idt.c`, `idt_stubs.S`): 256 gates. Each vector has an assembly stub
  that pushes the rest of the register state and calls `interrupt_handler()` in
  C with a pointer to `struct trap_frame` (this kernel's `struct pt_regs`).
  `idt_stubs.S` also carries the vectors that push an error code, so the frame
  layout is uniform.
- **Exceptions** (`trapnr.h`, `idt.c`): vectors 0..31 dump the register frame
  and panic. `trapnr.h` takes Linux's `X86_TRAP_*` names verbatim so the
  dump reads like upstream.
- **Panic** (`kernel/panic.c`): print and `hlt` forever — a small
  `kernel/panic.c` with none of the crash-dump machinery.

## 7. Interrupt core — PIC remapping and dispatch

**Files:** `drivers/pic/pic.c`, `kernel/irq.c`, `include/kernel/irq.h`.

The 8259 pair is remapped (`ICW1..ICW4`) so IRQ *n* arrives as vector 32 + *n*
— without it, IRQs collide with the CPU's own exception vectors 8..15 — and
every line is masked until a driver asks for it (`pic_unmask()`), which is the
Linux "mask everything, opt in per driver" policy.

`kernel/irq.c` is the dispatch core (`kernel/irq/irqdesc.c` in Linux): a
16-entry handler table, `irq_register(irq, fn)` for drivers, `irq_handler()`
that derives the IRQ number from `trap_frame->vector`, calls the handler, and
sends the EOI (`0x20`) to the slave PIC as well when the IRQ came from the
cascade. `interrupt_handler()` routes vectors < 32 to the exception path and
everything else to the IRQ path.

## 8. Timer — PIT and the LAPIC gate

**Files:** `drivers/pit/pit.c`, `include/kernel/pit.h`,
`arch/x86_64/cpu/lapic.c`.

`pit_init(frequency_hz)` programs PIT channel 0 in mode 2 (rate generator) with
divisor `1193182 / hz` — 100 Hz here — and `pit_irq_handler()` increments the
`volatile u64 jiffies`. The handler is intentionally one line: interrupt
context may not print, allocate or sleep, so it records state and the idle loop
does the printing.

`arch/x86_64/cpu/lapic.c` exists because of a real bring-up bug: the PIC's
output reaches the CPU through LAPIC LINT0 in ExtINT mode, and the firmware
leaves that LVT entry masked — so the PIT would fire, IF would be set, and the
CPU would silently never take the interrupt. The fix is one write of `0x700`
(ExtINT, unmasked) to LVT0 at offset `0x350`. Reaching that register needs the
LAPIC's MMIO page at `0xfee00000`, which Limine does not map outside RAM, so for
now `map_lapic_phys()` pokes a 2 MiB page into Limine's tables at
`0xffffffff80200000` and `invlpg`s it. That is a **documented stopgap**: the
upcoming virtual memory manager must replace it with a proper (ideally
uncached) MMIO mapping, and the comment in the file says so.

## 9. Concurrency primitives — `include/kernel/atomic.h`

`atomic_t` with `lock inc/dec`, and a `spinlock_t` built from `lock xchgb` in a
test-and-set loop with `pause` on the contended path, plus
`spin_lock_irqsave()`/`spin_unlock_irqrestore()` that save `rflags`, `cli`, and
restore it afterwards — the standard "don't let an interrupt deadlock against a
lock held by the code it interrupted" discipline (`kernel/locking/` in Linux).
Single-CPU today, SMP-shaped from birth.

## 10. Per-CPU storage — GS base and `smp_processor_id()`

**Files:** `arch/x86_64/cpu/percpu.c|h`, `msr.h`,
`include/linux/percpu-defs.h`, `linker.ld`, `include/kernel/compiler.h`.

`DEFINE_PER_CPU(type, name)` places a variable in `.data..percpu`, which the
linker script bounds with `__per_cpu_start`/`__per_cpu_end`. Each CPU keeps the
base of its own copy in `MSR_GS_BASE` (`wrmsr`, via `msr.h` with matching
`rdmsr`), so `this_cpu_ptr()` is `base + (&var - __per_cpu_start)` — one `rdmsr`
and a subtraction — and `this_cpu_read/write/inc/add` are built on it.
`smp_processor_id()` reads the per-CPU `cpu_number` instead of returning a
constant, which is what CPU bring-up will need. `setup_per_cpu()` points GS at
the CPU 0 copy before anything reads a per-CPU variable.

## 11. Page tables (early alias) — `arch/x86_64/boot/paging.c`

`phys_mem_alias_init(ram_end_pfn)` builds a first set of tables *without*
assuming anything about where the kernel is: it copies all 512 entries of
Limine's PML4 into its own page-aligned array (so the kernel, HHDM and
framebuffer mappings survive untouched), finds a free PML4 slot, wires a fresh
PUD/PMD into it, fills the PMD with 2 MiB pages identity-mapping
`0 .. max(RAM end, kernel image + 64 MiB)`, resolves the physical addresses of
its own arrays by walking the *current* tables, and switches `CR3`.

Why it exists: the buddy allocator wants to touch the physical addresses
memblock hands out. With this alias in place, "physical address" is directly
dereferenceable, so the `struct page` vmemmap can live at its physical address
with no separate linear-mapping window and no assumptions about
`kernel_phys`. The `CRITICAL` note in the file is worth keeping: `CR3` requires
a page-aligned physical address, hence the `aligned(4096)` arrays.

## 12. Boot memory map — `mm/memblock.c`

**Files:** `mm/memblock.c`, `include/linux/memblock.h`.

`memblock_init()` in `kernel/init.c` walks Limine's memory map: usable regions
go to `memblock_add()`, everything else to `memblock_reserve()`. Two region
lists (`memory`, `reserved`), each a sorted, capped-at-64 array of
`{base, size, flags}` with a running `total_size`:

- `memblock_insert_region()` shifts the tail with `memmove()` to insert in
  address order; `memblock_merge_regions()` coalesces neighbours that touch and
  share flags.
- `memblock_remove_range()` handles the four overlap cases (trim the tail,
  trim the head, split in half, delete) — needed because freeing a range can
  cut one region into two.
- `memblock_phys_alloc(size, align)` is first-fit bottom-up: walk the memory
  regions below `current_limit`, skip the first page (a map whose usable RAM
  starts at 0 would otherwise hand out address 0, which doubles as "nothing
  found"), align up, and reject candidates that overlap anything in the
  reserved list — the same test Linux's `memblock` does. `memblock_phys_free()`
  puts the range back by removing it from `reserved`.

Memblock's job is to describe RAM and hand out the handful of early
allocations you cannot make with a frame allocator (here: the vmemmap array).
It never allocates from the reserved set.

## 13. Page allocator — `mm/pmm.c`

**Files:** `mm/pmm.c`, `include/kernel/pmm.h`, `include/linux/mmzone.h`,
`include/linux/list.h`.

Structures (`include/linux/mmzone.h`, mirroring Linux's `mmzone.h` +
`mm_types.h`): `struct page` (one record per 4 KiB frame), `struct free_area`
(one list per order **and** per migrate type, plus `nr_free`), `struct zone`,
`struct per_cpu_pages`/`per_cpu_pageset`, `struct zonelist`, `pg_data_t`, and
the `pfn_to_page()`/`page_to_pfn()` helpers over the global `vmemmap`.
`include/linux/list.h` provides the intrusive doubly-linked list
(`list_add_tail`, `list_del`, `list_for_each*`, `list_entry`,
`list_first_entry`) that the free areas are built from.

`pmm_init(hhdm)` is the sequencing that matters:

1. size the frame range (`first_pfn .. last_pfn`) from the memblock map;
2. `phys_mem_alias_init()` — switch to the identity tables **before** touching
   any physical memory;
3. `memblock_phys_alloc()` the `struct page` vmemmap and `memset` it;
4. configure the single zone (`zone_start_pfn`, `spanned/present_pages`,
   `free_area_init()`);
5. walk every usable frame of every memory region, skipping the vmemmap's own
   frame, counting free frames into `free_area[0].nr_free`;
6. initialise the per-CPU page caches (present, not yet used on the hot path).

Allocation and free are ported from `mm/page_alloc.c`:
`__rmqueue_smallest()` takes the first page off the requested order's list, and
`__free_one_page()` walks up with `buddy_pfn = pfn ^ (1UL << order)`, checking
that the buddy is free and the same order before merging and raising the order.

**Status:** the structures, the boot carve and the two walks are in place, and
the frame count is stable across timer ticks in QEMU (53051 frames at 256 MiB).
The lists themselves are not linked yet, so `pmm_alloc_pages()` still returns
`NULL` and the idle-loop stress test is a no-op. What remains is mechanical and
listed in [docs/README.md](README.md): link each carved frame onto its order
list, split on allocation, re-insert merged blocks (and decrement/increment
`nr_free` per order rather than only at order 0).

## 14. Base library and the header harness

**Files:** `include/kernel/types.h`, `compiler.h`, `string.h`, `lib/string.c`,
`tests/check_headers.c`.

`types.h` gives `u8..u64`, `s8..s64`, `ulong`, `uint` and `phys_addr_t`;
`compiler.h` gives `__packed`, `__aligned`, `__noreturn`, `__always_inline`,
`__percpu` and `____cacheline_aligned_in_smp`; `lib/string.c` implements
`memcpy`, `memset`, `memmove`, `strlen`, `strcmp` with no libc behind them
(`memmove` handles both overlap directions — memblock relies on it).

`tests/check_headers.c` and `make check` are the regression net: GCC never
compiles a header nothing includes, so this harness includes every public header
and force-calls its API (lists, atomics/spinlocks, per-CPU accessors, the whole
`mmzone` shape, memblock). It is compile-only — `make check` proves the headers
still type-check against real users; the runtime check is the boot log.

---

## 15. Expected boot log (`make run`, 256 MiB)

```
rzOs: Limine handoff OK (higher-half, long mode)
GDT: installed (kernel code/data selectors)
IDT: 256 gates installed
PERCPU: GS base = 0xffffffff8000c200, smp_processor_id() = 0
MEMBLOCK: memory=207 MiB (8 region), reserved=12340 MiB (9 region)
PPM: entering pmm_init
PPM: alias tables starting...
PPM: alias tables done
MEMBLOCK: phys_alloc(64 KiB) -> 0x0000000000001000
PPM: total free frames after init = 53051
MEMBLOCK: re-alloc after free -> 0x0000000000001000 (same block reused)
IRQs: PIC remapped, LAPIC LVT0 unmasked, PIT at 100 Hz
tick: 100  percpu[0]=1
tick: 200  percpu[0]=2
...
tick: 1000  percpu[0]=10  buddy_free=53051
```

Each line is one module reporting in: `physical` addresses are printed by the
serial driver, the frame count comes from the allocator, `percpu[0]=N` is the
per-CPU counter, and the tick lines are the PIT handler driving the idle loop.
(`reserved=12340 MiB` looks larger than installed RAM because the firmware
reports the 4 GiB-and-up MMIO/ACPI holes as reserved regions — expected, not a
leak.)

## 16. Where the project is, and what is next

Working end to end: boot handoff and the higher-half entry, serial console, own
GDT/IDT with exception dumps, PIC remap + IRQ dispatch + EOI, a 100 Hz timer driving an
interrupt-driven idle loop (no scheduler yet), per-CPU storage with a real
`smp_processor_id()`, the memblock map of the machine with a first-fit early
allocator, an identity alias switched in by the kernel itself, and the page
allocator's data structures and boot carve.

Not there yet, in the order they are worth doing:

1. **Finish the buddy allocator** — link the carved frames, split on alloc,
   re-insert merged blocks, per-order `nr_free`, then a test that allocates
   *n* frames and proves they are contiguous.
2. **A real virtual memory manager** — replace the LAPIC stopgap mapping, map
   MMIO explicitly (uncached), separate physical from virtual rather than
   identity-mapping everything, then user address spaces.
3. **ACPI + SMP** — parse RSDP/XSDT/MADT, then INIT/SIPI the APs so the per-CPU
   machinery and spinlocks have more than one CPU to protect.
4. **Slab/kmalloc** — a small-object allocator on top of the page allocator.
5. **ELF segment loading in the loader** (`bootloader/bmain.c`) and
   `ExitBootServices()`, for a boot path that does not depend on Limine.

Beyond that, the roadmap in the [project README](../README.md) covers secure
boot, KASLR and the rest.

## 17. References used while writing this kernel

- Intel SDM / AMD64 APM — long mode, paging, MSRs, interrupts, LAPIC, PIT.
- [Limine boot protocol](https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md)
  — requests, `.limine_requests`, memory map types, HHDM.
- The Linux kernel as the reference implementation for everything here
  (`init/main.c`, `arch/x86/kernel/`, `kernel/irq/`, `mm/memblock.c`,
  `mm/page_alloc.c`, `include/linux/mmzone.h`, `lib/string.c`) — each file names
  its upstream counterpart in a comment.
- System V ABI / ELF64 spec — `kernel/elf.hpp`.
