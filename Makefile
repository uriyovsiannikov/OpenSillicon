SHELL := /bin/sh

CC := gcc
LD := ld
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE.fd
BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root

KERNEL_CFLAGS := -std=c11 -O2 -g -ffreestanding -fno-stack-protector \
	-fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel \
	-Wall -Wextra -Werror -Ilimine
KERNEL_LDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 \
	-T kernel/linker.ld

.PHONY: all kernel iso run clean

all: kernel

kernel: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR)/kernel.o: kernel/main.c limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel.o kernel/linker.ld
	$(LD) $(KERNEL_LDFLAGS) $< -o $@

$(BUILD_DIR):
	mkdir -p $@

iso: $(BUILD_DIR)/opensillicon.iso

$(BUILD_DIR)/opensillicon.iso: $(BUILD_DIR)/kernel.elf limine/BOOTX64.EFI limine/limine-uefi-cd.bin
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/boot $(ISO_ROOT)/limine
	cp limine/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	cp limine/limine-uefi-cd.bin $(ISO_ROOT)/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n' > $(ISO_ROOT)/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $@

run: iso
	$(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) -cdrom $(BUILD_DIR)/opensillicon.iso \
		-serial stdio -display none -no-reboot

clean:
	rm -rf $(BUILD_DIR)
