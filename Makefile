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

.PHONY: all kernel iso run test elf-test process-test fault-test interrupt-test vmm-protection-test \
	timer-test clean

all: kernel

kernel: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR)/main.o: kernel/main.c include/kernel/boot.h include/kernel/log.h \
		include/kernel/pmm.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/timer.h include/kernel/scheduler.h \
		include/arch/x86_64/lapic.h include/arch/x86_64/interrupts.h \
		include/arch/x86_64/user.h include/kernel/fd.h include/kernel/syscall.h \
		include/kernel/elf.h include/kernel/process.h \
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

$(BUILD_DIR)/fd.o: kernel/fd.c include/kernel/fd.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/syscall.c include/kernel/syscall.h \
		include/kernel/fd.h include/kernel/vmm.h include/kernel/log.h \
		include/kernel/process.h \
		include/arch/x86_64/user.h include/arch/x86_64/interrupts.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/elf.o: kernel/elf.c include/kernel/elf.h include/kernel/vmm.h \
		include/kernel/heap.h include/kernel/pmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/process.o: kernel/proc/process.c include/kernel/process.h \
		include/kernel/elf.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/pmm.h include/arch/x86_64/gdt.h | $(BUILD_DIR)
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

KERNEL_OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/boot.o $(BUILD_DIR)/pmm.o \
	$(BUILD_DIR)/vmm.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/log.o \
	$(BUILD_DIR)/gdt.o $(BUILD_DIR)/lapic.o $(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/scheduler.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/gdt_asm.o \
	$(BUILD_DIR)/interrupts_asm.o $(BUILD_DIR)/context.o $(BUILD_DIR)/fd.o \
	$(BUILD_DIR)/syscall.o $(BUILD_DIR)/user_entry.o \
	$(BUILD_DIR)/user_entry_c.o $(BUILD_DIR)/user_test_blob.o \
	$(BUILD_DIR)/elf.o $(BUILD_DIR)/elf_test_blob.o $(BUILD_DIR)/process.o

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) $(KERNEL_LDFLAGS) $(KERNEL_OBJECTS) -o $@

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

test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/boot.c kernel/mm/pmm.c tests/boot_test.c -o $(BUILD_DIR)/boot_test
	$(BUILD_DIR)/boot_test

elf-test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/elf.c tests/elf_test.c -o $(BUILD_DIR)/elf_test
	$(BUILD_DIR)/elf_test

process-test: iso
	@set +e; timeout 10s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/opensillicon.iso \
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
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/fault/opensillicon.iso \
		-serial file:$(BUILD_DIR)/fault/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon exception: vector 14' $(BUILD_DIR)/fault/serial.log

interrupt-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/interrupt \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_INTERRUPT' iso
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/interrupt/opensillicon.iso \
		-serial file:$(BUILD_DIR)/interrupt/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'unexpected interrupt vector 128' $(BUILD_DIR)/interrupt/serial.log

vmm-protection-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/vmm-protection \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_VMM_PROTECTION' iso
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/vmm-protection/opensillicon.iso \
		-serial file:$(BUILD_DIR)/vmm-protection/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon exception: vector 14 error 3' \
		$(BUILD_DIR)/vmm-protection/serial.log

timer-test: iso
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/opensillicon.iso \
		-serial file:$(BUILD_DIR)/timer-serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: Round Robin scheduler passed' \
		$(BUILD_DIR)/timer-serial.log
	@grep -F 'OpenSillicon: timer tick count ' $(BUILD_DIR)/timer-serial.log

clean:
	rm -rf $(BUILD_DIR)
