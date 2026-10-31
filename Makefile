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
	-mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-Wall -Wextra -Werror -Ilimine
KERNEL_LDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 \
	-T kernel/linker.ld
KERNEL_ASFLAGS := -m64 -mno-red-zone
USER_ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone
USER_LDFLAGS := -nostdlib -static -no-pie -T user/linker.ld

ACPICA_ROOT := third_party/acpica/source
ACPICA_COMPONENTS := dispatcher events executer hardware namespace parser \
	resources tables utilities
ACPICA_DIRS := $(foreach component,$(ACPICA_COMPONENTS), \
	$(ACPICA_ROOT)/components/$(component))
ACPICA_SOURCES := $(foreach directory,$(ACPICA_DIRS), \
	$(wildcard $(directory)/*.c))
ACPICA_SOURCES := $(filter-out $(ACPICA_ROOT)/components/resources/rsdump.c, \
	$(ACPICA_SOURCES))
ACPICA_OBJECTS := $(patsubst $(ACPICA_ROOT)/%.c, \
	$(BUILD_DIR)/acpica/%.o,$(ACPICA_SOURCES))
ACPICA_CFLAGS := $(KERNEL_CFLAGS) -Wno-unused-parameter -fno-builtin \
	-DBUILDING_ACPICA -DOPEN_SILLICON_KERNEL \
	-I$(ACPICA_ROOT)/include -I$(ACPICA_ROOT)/include/platform

.PHONY: all kernel iso run test elf-test process-test vfs-test vfs-smoke-test \
	silliconfs-test silliconfs-image fault-test \
	interrupt-test vmm-protection-test timer-test clean acpica-check acpi-test \
	pci-test block-test phase10-test

all: kernel

kernel: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR)/main.o: kernel/main.c include/kernel/boot.h include/kernel/log.h \
		include/kernel/pmm.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/timer.h include/kernel/scheduler.h \
		include/arch/x86_64/lapic.h include/arch/x86_64/interrupts.h \
		include/arch/x86_64/user.h include/kernel/fd.h include/kernel/syscall.h \
		include/kernel/elf.h include/kernel/process.h include/kernel/vfs.h \
		include/arch/x86_64/acpi.h include/arch/x86_64/ioapic.h \
		include/arch/x86_64/pci.h \
		include/arch/x86_64/ahci.h include/kernel/block.h \
		include/kernel/block_silliconfs.h include/kernel/silliconfs.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/boot.o: kernel/boot.c include/kernel/boot.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pmm.o: kernel/mm/pmm.c include/kernel/pmm.h \
		include/kernel/boot.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vmm.o: kernel/mm/vmm.c include/kernel/vmm.h \
		include/kernel/pmm.h include/kernel/boot.h include/kernel/log.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/heap.o: kernel/mm/heap.c include/kernel/heap.h \
		include/kernel/vmm.h include/kernel/pmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/log.o: kernel/log.c include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/fd.o: kernel/fd.c include/kernel/fd.h include/kernel/log.h \
		include/kernel/heap.h include/kernel/vfs.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/string.o: kernel/lib/string.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vfs.o: kernel/vfs/vfs.c include/kernel/vfs.h \
		include/kernel/fd.h include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ramfs.o: kernel/vfs/ramfs.c include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/initramfs.o: kernel/vfs/initramfs.c include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/silliconfs.o: kernel/vfs/silliconfs.c \
		include/kernel/silliconfs.h include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/syscall.c include/kernel/syscall.h \
		include/kernel/fd.h include/kernel/vmm.h include/kernel/log.h \
		include/kernel/process.h include/kernel/vfs.h \
		include/arch/x86_64/user.h include/arch/x86_64/interrupts.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/elf.o: kernel/elf.c include/kernel/elf.h include/kernel/vmm.h \
		include/kernel/heap.h include/kernel/pmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/process.o: kernel/proc/process.c include/kernel/process.h \
		include/kernel/elf.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/pmm.h include/kernel/fd.h include/arch/x86_64/gdt.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/arch/x86_64/interrupts.c \
		include/arch/x86_64/gdt.h include/arch/x86_64/interrupts.h \
		include/kernel/log.h include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt.o: kernel/arch/x86_64/gdt.c include/arch/x86_64/gdt.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/lapic.o: kernel/arch/x86_64/lapic.c \
		include/arch/x86_64/lapic.h include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ioapic.o: kernel/arch/x86_64/ioapic.c \
		include/arch/x86_64/ioapic.h include/arch/x86_64/acpi.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pci.o: kernel/arch/x86_64/pci.c \
		include/arch/x86_64/pci.h include/arch/x86_64/acpi.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ahci.o: kernel/arch/x86_64/ahci.c \
		include/arch/x86_64/ahci.h include/arch/x86_64/pci.h \
		include/kernel/block.h include/kernel/errno.h include/kernel/pmm.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block.o: kernel/block.c include/kernel/block.h \
		include/kernel/errno.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block_silliconfs.o: kernel/block_silliconfs.c \
		include/kernel/block_silliconfs.h include/kernel/block.h \
		include/kernel/errno.h include/kernel/silliconfs.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/acpi.o: kernel/arch/x86_64/acpi.c \
		include/arch/x86_64/acpi.h include/kernel/heap.h \
		include/kernel/log.h include/kernel/timer.h include/kernel/vmm.h \
		third_party/acpica/source/include/acpi.h \
		third_party/acpica/source/include/acpiosxf.h \
		third_party/acpica/source/include/acpixf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL \
		-isystem third_party/acpica/source/include -Iinclude -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/arch/x86_64/timer.c \
		include/arch/x86_64/lapic.h include/arch/x86_64/interrupts.h \
		include/kernel/timer.h include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/sched/scheduler.c \
		include/kernel/scheduler.h include/kernel/heap.h include/kernel/vmm.h \
		include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt_asm.o: kernel/arch/x86_64/gdt.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts_asm.o: kernel/arch/x86_64/interrupts.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/context.o: kernel/arch/x86_64/context.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry.o: kernel/arch/x86_64/user_entry.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry_c.o: kernel/arch/x86_64/user_entry.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/acpica/%.o: $(ACPICA_ROOT)/%.c \
		$(ACPICA_ROOT)/include/platform/acenv.h \
		$(ACPICA_ROOT)/include/platform/acopensillicon.h \
		$(ACPICA_ROOT)/include/ctype.h
	mkdir -p $(dir $@)
	$(CC) $(ACPICA_CFLAGS) -c $< -o $@

acpica-check: $(ACPICA_OBJECTS)

$(BUILD_DIR)/acpi_test: kernel/arch/x86_64/acpi.c tests/acpi_test.c \
		include/arch/x86_64/acpi.h third_party/acpica/source/include/acpi.h
	$(CC) -std=c11 -Wall -Wextra -Werror \
		-DOPEN_SILLICON_KERNEL -isystem third_party/acpica/source/include \
		-Iinclude -Ilimine kernel/arch/x86_64/acpi.c tests/acpi_test.c \
		-o $@

acpi-test: $(BUILD_DIR)/acpi_test
	$(BUILD_DIR)/acpi_test

$(BUILD_DIR)/pci_test: kernel/arch/x86_64/pci.c tests/pci_test.c \
		include/arch/x86_64/pci.h include/arch/x86_64/acpi.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/arch/x86_64/pci.c tests/pci_test.c -o $@

pci-test: $(BUILD_DIR)/pci_test
	$(BUILD_DIR)/pci_test

$(BUILD_DIR)/user_test.o: user/test.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_test.elf: $(BUILD_DIR)/user_test.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $< -o $@

$(BUILD_DIR)/user_test.bin: $(BUILD_DIR)/user_test.elf
	objcopy -O binary $< $@

$(BUILD_DIR)/user_test_blob.o: $(BUILD_DIR)/user_test.bin
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		user_test.bin user_test_blob.o

$(BUILD_DIR)/exec_child.o: user/exec_child.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/exec_child.elf: $(BUILD_DIR)/exec_child.o user/exec_child.ld
	$(LD) -nostdlib -static -no-pie -T user/exec_child.ld $< -o $@

$(BUILD_DIR)/elf_test_user.o: user/elf_test.S $(BUILD_DIR)/exec_child.elf | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/elf_test_user.elf: $(BUILD_DIR)/elf_test_user.o user/elf_test.ld
	$(LD) -nostdlib -static -no-pie -T user/elf_test.ld $< -o $@

$(BUILD_DIR)/elf_test_user.bin: $(BUILD_DIR)/elf_test_user.elf
	objcopy -O binary $< $@

$(BUILD_DIR)/elf_test_blob.o: $(BUILD_DIR)/elf_test_user.elf
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_elf_test_user_elf_start=_binary_elf_test_bin_start \
		--redefine-sym _binary_elf_test_user_elf_end=_binary_elf_test_bin_end \
		elf_test_user.elf elf_test_blob.o

$(BUILD_DIR)/vfs_test_user.o: user/vfs_test.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs_test_user.elf: $(BUILD_DIR)/vfs_test_user.o user/vfs_test.ld
	$(LD) -nostdlib -static -no-pie -T user/vfs_test.ld $< -o $@

$(BUILD_DIR)/vfs_test_blob.o: $(BUILD_DIR)/vfs_test_user.elf
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_vfs_test_user_elf_start=_binary_vfs_test_bin_start \
		--redefine-sym _binary_vfs_test_user_elf_end=_binary_vfs_test_bin_end \
		vfs_test_user.elf vfs_test_blob.o

$(BUILD_DIR)/exec_path_child.o: user/exec_path_child.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/exec_path_child.elf: $(BUILD_DIR)/exec_path_child.o user/exec_child.ld
	$(LD) -nostdlib -static -no-pie -T user/exec_child.ld $< -o $@

$(BUILD_DIR)/initramfs.cpio: $(shell find initramfs -type f -o -type d) \
		$(BUILD_DIR)/exec_path_child.elf tools/mkinitramfs.py
	python3 tools/mkinitramfs.py initramfs $@ \
		--file bin/exec_path_child=$(BUILD_DIR)/exec_path_child.elf

KERNEL_OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/boot.o $(BUILD_DIR)/pmm.o \
	$(BUILD_DIR)/vmm.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/log.o \
	$(BUILD_DIR)/gdt.o $(BUILD_DIR)/lapic.o $(BUILD_DIR)/acpi.o \
	$(BUILD_DIR)/ioapic.o $(BUILD_DIR)/ahci.o $(BUILD_DIR)/block.o \
	$(BUILD_DIR)/block_silliconfs.o \
	$(BUILD_DIR)/pci.o \
	$(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/scheduler.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/gdt_asm.o \
	$(BUILD_DIR)/interrupts_asm.o $(BUILD_DIR)/context.o $(BUILD_DIR)/fd.o \
	$(BUILD_DIR)/syscall.o $(BUILD_DIR)/user_entry.o \
	$(BUILD_DIR)/user_entry_c.o $(BUILD_DIR)/user_test_blob.o \
	$(BUILD_DIR)/elf.o $(BUILD_DIR)/elf_test_blob.o $(BUILD_DIR)/process.o \
	$(BUILD_DIR)/string.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ramfs.o \
	$(BUILD_DIR)/initramfs.o $(BUILD_DIR)/silliconfs.o \
	$(BUILD_DIR)/vfs_test_blob.o $(ACPICA_OBJECTS)

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) $(KERNEL_LDFLAGS) $(KERNEL_OBJECTS) -o $@

$(BUILD_DIR):
	mkdir -p $@

iso: $(BUILD_DIR)/opensillicon.iso

$(BUILD_DIR)/root.img: | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024

$(BUILD_DIR)/test-root.img: | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024

$(BUILD_DIR)/opensillicon.iso: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/initramfs.cpio \
		limine/BOOTX64.EFI limine/limine-uefi-cd.bin
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/boot $(ISO_ROOT)/limine
	cp limine/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	cp limine/limine-uefi-cd.bin $(ISO_ROOT)/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
	cp $(BUILD_DIR)/initramfs.cpio $(ISO_ROOT)/boot/initramfs.cpio
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n    module_path: boot():/boot/initramfs.cpio\n    module_string: initramfs\n' > $(ISO_ROOT)/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $@

run: iso $(BUILD_DIR)/root.img
	$(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) -cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 -serial stdio -display none -no-reboot

$(BUILD_DIR)/block_test: kernel/block.c kernel/block_silliconfs.c \
		tests/block_test.c include/kernel/block.h include/kernel/errno.h \
		include/kernel/block_silliconfs.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude kernel/block.c \
		kernel/block_silliconfs.c tests/block_test.c -o $@

block-test: $(BUILD_DIR)/block_test
	$(BUILD_DIR)/block_test

phase10-test: block-test acpi-test pci-test silliconfs-test

test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/boot.c kernel/mm/pmm.c tests/boot_test.c -o $(BUILD_DIR)/boot_test
	$(BUILD_DIR)/boot_test

elf-test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/elf.c tests/elf_test.c -o $(BUILD_DIR)/elf_test
	$(BUILD_DIR)/elf_test

vfs-test: $(BUILD_DIR)/initramfs.cpio | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/vfs/vfs.c kernel/vfs/ramfs.c kernel/vfs/initramfs.c \
		kernel/fd.c tests/vfs_test.c -o $(BUILD_DIR)/vfs_test
	$(BUILD_DIR)/vfs_test $(BUILD_DIR)/initramfs.cpio

silliconfs-image: | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $(BUILD_DIR)/silliconfs.img \
		--blocks 512 --inodes 128

silliconfs-test: silliconfs-image | $(BUILD_DIR)
	python3 tests/mksilliconfs_test.py
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/vfs/vfs.c kernel/vfs/silliconfs.c kernel/fd.c \
		kernel/block.c kernel/block_silliconfs.c \
		tests/silliconfs_test.c -o $(BUILD_DIR)/silliconfs_test
	$(BUILD_DIR)/silliconfs_test $(BUILD_DIR)/silliconfs.img

vfs-smoke-test: iso $(BUILD_DIR)/test-root.img
	python3 tools/mksilliconfs.py $(BUILD_DIR)/test-root.img --blocks 32768 --inodes 1024
	@set +e; timeout 10s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/vfs-serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: initramfs and RAMFS mounted' \
		$(BUILD_DIR)/vfs-serial.log
	@grep -F 'OpenSillicon: syscall 12 exec_path passed' \
		$(BUILD_DIR)/vfs-serial.log
	@grep -F 'OpenSillicon: VFS userspace operations passed' \
		$(BUILD_DIR)/vfs-serial.log

process-test: iso $(BUILD_DIR)/test-root.img
	python3 tools/mksilliconfs.py $(BUILD_DIR)/test-root.img --blocks 32768 --inodes 1024
	@set +e; timeout 10s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/process-serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: fork inherited serial descriptor' \
		$(BUILD_DIR)/process-serial.log
	@grep -F 'OpenSillicon: static ELF64 fork, exec, and wait passed' \
		$(BUILD_DIR)/process-serial.log
	@grep -E 'OpenSillicon: user timer preemptions [1-9][0-9]*' \
		$(BUILD_DIR)/process-serial.log
	@grep -F 'OpenSillicon: ELF64 process, fork, wait, and cleanup passed' \
		$(BUILD_DIR)/process-serial.log

fault-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/fault \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_PAGE_FAULT' iso
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/fault $(BUILD_DIR)/fault/test-root.img
	python3 tools/mksilliconfs.py $(BUILD_DIR)/fault/test-root.img --blocks 32768 --inodes 1024
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/fault/opensillicon.iso \
		-drive file=$(BUILD_DIR)/fault/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/fault/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon exception: vector 14' $(BUILD_DIR)/fault/serial.log

interrupt-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/interrupt \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_INTERRUPT' iso
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/interrupt $(BUILD_DIR)/interrupt/test-root.img
	python3 tools/mksilliconfs.py $(BUILD_DIR)/interrupt/test-root.img --blocks 32768 --inodes 1024
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/interrupt/opensillicon.iso \
		-drive file=$(BUILD_DIR)/interrupt/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/interrupt/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'unexpected interrupt vector 128' $(BUILD_DIR)/interrupt/serial.log

vmm-protection-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/vmm-protection \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_VMM_PROTECTION' iso
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/vmm-protection $(BUILD_DIR)/vmm-protection/test-root.img
	python3 tools/mksilliconfs.py $(BUILD_DIR)/vmm-protection/test-root.img --blocks 32768 --inodes 1024
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/vmm-protection/opensillicon.iso \
		-drive file=$(BUILD_DIR)/vmm-protection/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/vmm-protection/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon exception: vector 14 error 3' \
		$(BUILD_DIR)/vmm-protection/serial.log

timer-test: iso $(BUILD_DIR)/test-root.img
	python3 tools/mksilliconfs.py $(BUILD_DIR)/test-root.img --blocks 32768 --inodes 1024
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/timer-serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: Round Robin scheduler passed' \
		$(BUILD_DIR)/timer-serial.log
	@grep -F 'OpenSillicon: timer tick count ' $(BUILD_DIR)/timer-serial.log

clean:
	rm -rf $(BUILD_DIR)
