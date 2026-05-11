CC = gcc
LD = ld
OBJCOPY = x86_64-linux-gnu-objcopy

EFI_INC = /usr/include/efi
EFI_ARCH_INC = /usr/include/efi/x86_64

EFI_LDS = /usr/lib/x86_64-linux-gnu/elf_x86_64_efi.lds
EFI_CRT = /usr/lib/x86_64-linux-gnu/crt0-efi-x86_64.o

EFI_LIB1 = /usr/lib/x86_64-linux-gnu/libefi.a
EFI_LIB2 = /usr/lib/x86_64-linux-gnu/libgnuefi.a

CFLAGS = \
	-I$(EFI_INC) \
	-I$(EFI_ARCH_INC) \
	-fpic \
	-fshort-wchar \
	-mno-red-zone \
	-ffreestanding \
	-fno-exceptions \
	-fno-stack-protector

LDFLAGS = \
	-nostdlib \
	-znocombreloc \
	-T $(EFI_LDS) \
	-shared \
	-Bsymbolic

all: BOOTX64.EFI

bmain.o: bootloader/bmain.c
	$(CC) $(CFLAGS) -c $< -o $@

bootloader.so: bmain.o
	$(LD) $(LDFLAGS) \
	-o $@ \
	$(EFI_CRT) \
	bmain.o \
	$(EFI_LIB1) \
	$(EFI_LIB2)

BOOTX64.EFI: bootloader.so
	$(OBJCOPY) \
	-j .text \
	-j .sdata \
	-j .data \
	-j .rodata \
	-j .dynamic \
	-j .dynsym \
	-j .rel \
	-j .rela \
	-j .reloc \
	--subsystem=10 \
	-O pei-x86-64 \
	bootloader.so \
	BOOTX64.EFI

run: BOOTX64.EFI kernel.bin
	mkdir -p image/EFI/BOOT
	cp BOOTX64.EFI image/EFI/BOOT/
	cp kernel.bin image/ 
	qemu-system-x86_64 \
	-m 256M \
	-bios /usr/share/qemu/OVMF.fd \
	-hda fat:rw:image -nographic
clean:
	rm -f *.o *.so *.EFI