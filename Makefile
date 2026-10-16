CC              = gcc
LD              = ld

# =========================
# Limine (standard bootloader)
# =========================

LIMINE_BRANCH   = v11.x-binary
LIMINE_DIR      = limine
LIMINE_REPO     = https://github.com/limine-bootloader/limine.git
LIMINE_HDR_URL  = https://raw.githubusercontent.com/limine-bootloader/limine-protocol/trunk/include/limine.h

# =========================
# Kernel Flags
# =========================

KERNEL_CFLAGS = \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-PIC \
	-ffunction-sections \
	-fdata-sections \
	-m64 \
	-march=x86-64 \
	-mabi=sysv \
	-mno-80387 \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-mno-red-zone \
	-mcmodel=kernel \
	-Wall \
	-Wextra \
	-Iinclude \
	-Iarch \
	-I$(LIMINE_DIR) \
	-c

KERNEL_LDFLAGS = \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	--gc-sections \
	-T arch/x86_64/boot/linker.ld

# Source discovery: every .c/.S under the Linux-style tree layout.
# (mm/ added for the memory-management milestone.)
C_SRCS := $(shell find kernel lib drivers mm arch -type f -name '*.c' | sort)
S_SRCS := $(shell find arch -type f -name '*.S' | sort)
OBJS   := $(C_SRCS:.c=.o) $(S_SRCS:.S=.o)

# =========================
# Targets
# =========================

# 'image' collides with the image/ directory, so it must be phony
# or make will consider it up-to-date and skip the copy steps.
.PHONY: all image run debug clean distclean check

all: image

# =========================================================
# KERNEL
# =========================================================

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) $< -o $@

%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) $< -o $@

kernel.elf: $(OBJS)
	$(LD) $(KERNEL_LDFLAGS) -o $@ $(OBJS)

# =========================================================
# LIMINE
# =========================================================

$(LIMINE_DIR):
	@if [ ! -f $(LIMINE_DIR)/BOOTX64.EFI ] || [ ! -f $(LIMINE_DIR)/limine.h ]; then \
		rm -rf $(LIMINE_DIR); \
		git clone $(LIMINE_REPO) --branch=$(LIMINE_BRANCH) --depth=1 $(LIMINE_DIR) && \
		make -C $(LIMINE_DIR) && \
		curl -fsSL $(LIMINE_HDR_URL) -o $(LIMINE_DIR)/limine.h; \
	else \
		echo "limine: already present, skipping download"; \
	fi

# limine.h and the EFI binaries must exist before any kernel source is
# compiled: on a fresh checkout the clone has to happen first, otherwise
# the first build races it (and fails) - especially with -j.
# Order-only (|) so touching the vendored tree does not rebuild objects.
$(OBJS) check_headers.o: | $(LIMINE_DIR)

# =========================================================
# IMAGE SETUP
# =========================================================

image: kernel.elf $(LIMINE_DIR)
	mkdir -p image/EFI/BOOT
	cp $(LIMINE_DIR)/BOOTX64.EFI image/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTIA32.EFI image/EFI/BOOT/
	cp limine.conf image/
	cp kernel.elf image/

# =========================================================
# RUN
# =========================================================

run: image
	qemu-system-x86_64 \
	-m 256M \
	-bios /usr/share/qemu/OVMF.fd \
	-drive format=raw,file=fat:rw:image \
	-serial stdio \
	-no-reboot

# =========================================================
# DEBUG
# =========================================================

debug: image
	qemu-system-x86_64 \
	-m 256M \
	-bios /usr/share/qemu/OVMF.fd \
	-drive format=raw,file=fat:rw:image \
	-s \
	-S \
	-serial stdio

# =========================================================
# HEADER CHECK - force-use compile of every public header.
# GCC never compiles an unused header; this harness calls
# every function in the public headers so a broken header
# cannot hide from the build. (Linux: make headers_check.)
# =========================================================

check: check_headers.o
	@echo "headers clean"

check_headers.o: tests/check_headers.c
	$(CC) $(KERNEL_CFLAGS) $< -o $@

# =========================================================
# CLEAN
# =========================================================

clean:
	find kernel lib drivers mm arch tests -name '*.o' -delete 2>/dev/null || true
	find kernel lib drivers mm arch tests -name '*.d' -delete 2>/dev/null || true
	rm -f *.o *.d *.elf
	rm -rf image/EFI

# Remove everything built/generated, including the vendored Limine clone.
distclean: clean
	rm -rf $(LIMINE_DIR)
	rm -rf image
