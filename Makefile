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
	-I$(LIMINE_DIR) \
	-c

KERNEL_LDFLAGS = \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	--gc-sections \
	-T kernel/linker.ld

# =========================
# Targets
# =========================

# 'image' collides with the image/ directory, so it must be phony
# or make will consider it up-to-date and skip the copy steps.
.PHONY: all image run debug clean

all: image

# =========================================================
# KERNEL
# =========================================================

kernel.o: kernel/kmain.c | $(LIMINE_DIR)
	$(CC) $(KERNEL_CFLAGS) $< -o $@

kernel.elf: kernel.o
	$(LD) $(KERNEL_LDFLAGS) -o $@ kernel.o

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
# CLEAN
# =========================================================

clean:
	rm -f *.o *.elf
	rm -rf image/EFI

# Remove everything built/generated, including the vendored Limine clone.
distclean: clean
	rm -rf $(LIMINE_DIR)
	rm -rf image
