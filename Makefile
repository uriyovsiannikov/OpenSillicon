SHELL := /bin/sh

CC := gcc
LD := ld
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE.fd
BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
MUSL_BUILD_DIR ?= build/musl-port
MUSL_BOOTSTRAP_DIR ?= build/musl-bootstrap
MUSL_CLANG ?= $(shell python3 -c 'import json; print(json.load(open("$(MUSL_BUILD_DIR)/build-info.json"))["toolchain"]["clang"]["path"])')
MUSL_LLD ?= $(shell python3 -c 'import json; print(json.load(open("$(MUSL_BUILD_DIR)/build-info.json"))["toolchain"]["ld.lld"]["path"])')
MUSL_QEMU_BUILD_DIR ?= build/musl-qemu
MUSL_ALLOC_QEMU_BUILD_DIR ?= build/musl-alloc-qemu
MUSL_STDIO_QEMU_BUILD_DIR ?= build/musl-stdio-qemu
MUSL_CLOCK_QEMU_BUILD_DIR ?= build/musl-clock-qemu
MUSL_NANOSLEEP_QEMU_BUILD_DIR ?= build/musl-nanosleep-qemu
MUSL_FD_QEMU_BUILD_DIR ?= build/musl-fd-qemu
TIMER_TEST_TIMEOUT ?= 15s
MUSL_SYSROOT := $(MUSL_BUILD_DIR)/sysroot
MUSL_HELLO := $(MUSL_BUILD_DIR)/musl_hello.elf
MUSL_ALLOC := $(MUSL_BUILD_DIR)/musl_alloc.elf
MUSL_STDIO := $(MUSL_BUILD_DIR)/musl_stdio.elf
MUSL_CLOCK := $(MUSL_BUILD_DIR)/musl_clock.elf
MUSL_NANOSLEEP := $(MUSL_BUILD_DIR)/musl_nanosleep.elf
MUSL_FD := $(MUSL_BUILD_DIR)/musl_fd_seekstat.elf

KERNEL_CFLAGS := -std=c11 -O2 -g -ffreestanding -fno-stack-protector \
	-fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel \
	-mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-Wall -Wextra -Werror -Ilimine
KERNEL_LDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 \
	-T kernel/arch/x86_64/boot/linker.ld
KERNEL_ASFLAGS := -m64 -mno-red-zone
USER_ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone
USER_LDFLAGS := -nostdlib -static -no-pie -T tests/user/linker.ld
USER_C_LDFLAGS := -nostdlib -static -no-pie -T user/c_linker.ld
USER_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=small \
	-Wall -Wextra -Werror -Iuser

COREUTILS := ls cat echo pwd true false clear uname fetch head wc stat basename dirname kill sleep which ps netdiag ping
COREUTILS_ELFS := $(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .elf,$(COREUTILS)))

ACPICA_ROOT := kernel/arch/x86_64/firmware/acpica/source
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

PRIVATE_MAPPING_TEST_OBJECTS :=
ifneq (,$(findstring KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE,$(KERNEL_CFLAGS)))
PRIVATE_MAPPING_TEST_OBJECTS := $(BUILD_DIR)/private_mapping_lifecycle_blob.o
endif

UDP_SOCKET_TEST_OBJECTS :=
ifneq (,$(findstring KERNEL_TEST_UDP_SOCKET,$(KERNEL_CFLAGS)))
UDP_SOCKET_TEST_OBJECTS := $(BUILD_DIR)/udp_socket_test_blob.o
endif

.PHONY: all kernel iso run run-gui test elf-test process-test vfs-test vfs-smoke-test \
	kernel-random-test boot-entropy-test virtio-rng-test kernel-entropy-test \
	rdseed-test rdseed-qemu-test rdseed-hw-test \
	virtio-rng-qemu-test virtio-rng-absent-qemu-test \
	virtio-rng-cpu-qemu-test \
	silliconfs-test silliconfs-image fault-test \
	interrupt-test vmm-protection-test timer-test clean acpica-check acpi-test \
	tpm2-acpi-test tpm2-crb-test acpi-mmio-range-test \
	pci-test pci-resource-test dma-test pmm-contiguous-test network-foundation-test block-test phase10-test phase11-test terminal-test \
	dma-ring-test e1000-profile-test e1000-ring-test e1000-init-qemu-test \
	e1000-dma-qemu-test e1000-icmp-qemu-test e1000-udp-qemu-test \
	netstack-test udp-socket-test tcp-wire-test tcp-wire-fuzz-test tcp-core-test tcp-runtime-test tcp-runtime-sim-test tcp-linux-interop-test network-service-test \
	netdiag-args-test e1000-netdiag-qemu-test e1000-icmp-ping-qemu-test \
	terminal-qemu-test user-path-test phase12-qemu-test phase12-test \
	pipe-test phase13a-test signal-state-test phase13b-test refresh-test-root \
	shm-test shm-failure-qemu-test shm-lifecycle-qemu-test \
	shm-fork-failure-qemu-test private-map-qemu-test \
	private-map-lifecycle-qemu-test private-map-exhaustion-qemu-test \
	private-map-abi-test private-protection-test private-protection-qemu-test fs-base-qemu-test \
	fpu-context-qemu-test \
	native-musl-verify native-musl-build native-musl-hello \
	native-musl-alloc native-musl-inspect native-musl-qemu-test \
	native-musl-alloc-qemu-test native-musl-stdio native-musl-stdio-qemu-test \
	native-musl-clock native-musl-clock-qemu-test native-musl-nanosleep \
	native-musl-nanosleep-qemu-test native-musl-fd-seekstat \
	native-musl-fd-seekstat-qemu-test

all: kernel

kernel: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR)/main.o: kernel/main.c include/kernel/boot.h include/kernel/log.h \
		include/kernel/pmm.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/errno.h \
		include/kernel/timer.h include/kernel/scheduler.h \
		include/arch/x86_64/lapic.h include/arch/x86_64/interrupts.h \
		include/arch/x86_64/user.h include/kernel/fd.h include/kernel/syscall.h \
		include/kernel/elf.h include/kernel/process.h include/kernel/vfs.h \
		include/arch/x86_64/acpi.h include/arch/x86_64/ioapic.h \
		include/arch/x86_64/pci.h \
		include/kernel/dma.h \
		include/kernel/e1000.h include/kernel/e1000_profile.h \
		include/kernel/netstack.h include/kernel/udp_socket.h \
		include/kernel/network_service.h \
		include/kernel/tcp_runtime.h \
	include/kernel/random.h include/kernel/boot_entropy.h \
	include/kernel/entropy.h \
	include/kernel/rdseed.h \
	include/kernel/virtio_rng.h \
		include/arch/x86_64/ahci.h include/kernel/block.h \
		include/kernel/block_silliconfs.h include/kernel/silliconfs.h \
		include/arch/x86_64/fpu.h \
		include/kernel/tty.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/boot.o: kernel/core/boot/boot.c include/kernel/boot.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/boot_entropy.o: kernel/core/boot/boot_entropy.c \
		include/kernel/boot_entropy.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/entropy.o: kernel/random/entropy.c include/kernel/entropy.h \
		include/kernel/random.h include/kernel/rdseed.h \
		include/kernel/virtio_rng.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/rdseed.o: kernel/drivers/entropy/rdseed.c include/kernel/rdseed.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/virtio_rng.o: kernel/drivers/entropy/virtio_rng.c \
		include/kernel/virtio_rng.h include/kernel/dma.h \
		include/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/pmm.o: kernel/mm/pmm.c include/kernel/pmm.h \
		include/kernel/boot.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vmm.o: kernel/mm/vmm.c include/kernel/vmm.h \
		include/kernel/pmm.h include/kernel/boot.h include/kernel/log.h \
		include/kernel/shm.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/dma.o: kernel/mm/dma.c include/kernel/dma.h \
		include/kernel/pmm.h include/kernel/vmm.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/dma_ring.o: kernel/mm/dma_ring.c include/kernel/dma_ring.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/shm.o: kernel/mm/shm.c include/kernel/shm.h \
		include/kernel/vmm.h include/kernel/pmm.h include/kernel/heap.h \
		include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/heap.o: kernel/mm/heap.c include/kernel/heap.h \
		include/kernel/vmm.h include/kernel/pmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/log.o: kernel/core/log/log.c include/kernel/log.h include/kernel/tty.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/fd.o: kernel/proc/fd.c include/kernel/fd.h include/kernel/log.h \
		include/kernel/heap.h include/kernel/vfs.h include/kernel/tty.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pipe.o: kernel/ipc/pipe.c include/kernel/pipe.h \
		include/kernel/process.h include/kernel/fd.h include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c kernel/ipc/pipe.c -o $@

$(BUILD_DIR)/pipe_buffer.o: kernel/ipc/pipe_buffer.c include/kernel/pipe.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/terminal.o: kernel/tty/terminal.c include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/keyboard.o: kernel/tty/keyboard.c \
		include/arch/x86_64/ps2_keyboard.h include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/framebuffer.o: kernel/tty/framebuffer.c \
		include/kernel/framebuffer.h include/kernel/terminal.h \
		kernel/tty/font.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tty.o: kernel/tty/tty.c include/kernel/tty.h \
		include/kernel/terminal.h include/kernel/framebuffer.h \
		include/arch/x86_64/ps2_keyboard.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ps2_keyboard.o: kernel/drivers/input/ps2_keyboard.c \
		include/arch/x86_64/ps2_keyboard.h include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/string.o: kernel/core/lib/string.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vfs.o: kernel/fs/vfs/vfs.c include/kernel/vfs.h \
		include/kernel/fd.h include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ramfs.o: kernel/fs/ramfs/ramfs.c include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/initramfs.o: kernel/fs/initramfs/initramfs.c include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/silliconfs.o: kernel/fs/silliconfs/silliconfs.c \
		include/kernel/silliconfs.h include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/core/syscall/syscall.c include/kernel/syscall.h \
		include/kernel/fd.h include/kernel/vmm.h include/kernel/log.h \
		include/kernel/process.h include/kernel/vfs.h include/kernel/udp_socket.h \
		include/kernel/icmp_echo.h \
		include/kernel/network_service.h \
		include/arch/x86_64/user.h include/arch/x86_64/interrupts.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/elf.o: kernel/proc/elf.c include/kernel/elf.h include/kernel/vmm.h \
		include/kernel/heap.h include/kernel/pmm.h include/kernel/shm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/process.o: kernel/proc/process.c include/kernel/process.h \
		include/kernel/signal_state.h include/kernel/elf.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/pmm.h include/kernel/fd.h include/kernel/timer.h \
		include/kernel/network_service.h \
		include/arch/x86_64/gdt.h include/arch/x86_64/fpu.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/signal_state.o: kernel/proc/signal_state.c \
		include/kernel/signal_state.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/arch/x86_64/interrupts/interrupts.c \
		include/arch/x86_64/gdt.h include/arch/x86_64/interrupts.h \
		include/kernel/log.h include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt.o: kernel/arch/x86_64/cpu/gdt.c include/arch/x86_64/gdt.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/fpu.o: kernel/arch/x86_64/cpu/fpu.c \
		include/arch/x86_64/fpu.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/random.o: kernel/random/random.c include/kernel/random.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/lapic.o: kernel/arch/x86_64/controllers/lapic.c \
		include/arch/x86_64/lapic.h include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ioapic.o: kernel/arch/x86_64/controllers/ioapic.c \
		include/arch/x86_64/ioapic.h include/arch/x86_64/acpi.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pci.o: kernel/drivers/bus/pci/pci.c \
		include/arch/x86_64/pci.h include/arch/x86_64/acpi.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pci_resource.o: kernel/drivers/bus/pci/pci_resource.c \
		include/arch/x86_64/pci_resource.h include/arch/x86_64/pci.h \
		include/kernel/vmm.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netbuf.o: kernel/net/core/netbuf.c include/kernel/netbuf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netdev.o: kernel/net/core/netdev.c include/kernel/netdev.h \
		include/kernel/netbuf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netstack.o: kernel/net/core/netstack.c include/kernel/netstack.h \
		include/kernel/tcp_wire.h include/kernel/tcp_runtime.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/udp_socket.o: kernel/net/udp/udp_socket.c \
		include/kernel/udp_socket.h include/kernel/netstack.h \
		include/kernel/netdev.h include/kernel/netbuf.h \
		include/kernel/fd.h include/kernel/timer.h include/kernel/errno.h \
		include/kernel/network_service.h include/kernel/icmp_echo.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/network_service.o: kernel/net/service/network_service.c \
		include/kernel/network_service.h include/kernel/udp_socket.h \
		include/kernel/errno.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/tcp_core.o: kernel/net/tcp/tcp_core.c include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tcp_wire.o: kernel/net/tcp/tcp_wire.c include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tcp_runtime.o: kernel/net/tcp/tcp_runtime.c \
		include/kernel/tcp_runtime.h include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_profile.o: kernel/drivers/net/e1000/e1000_profile.c \
		include/kernel/e1000_profile.h include/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_ring.o: kernel/drivers/net/e1000/e1000_ring.c \
		include/kernel/e1000_ring.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000.o: kernel/drivers/net/e1000/e1000.c include/kernel/e1000.h \
		include/kernel/e1000_profile.h include/arch/x86_64/pci_resource.h \
		include/kernel/e1000_ring.h include/kernel/dma.h \
		include/kernel/dma_ring.h include/kernel/netdev.h \
		include/kernel/netbuf.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_reset.o: kernel/drivers/net/e1000/e1000_reset.c include/kernel/e1000.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ahci.o: kernel/drivers/storage/ahci.c \
		include/arch/x86_64/ahci.h include/arch/x86_64/pci.h \
		include/kernel/block.h include/kernel/errno.h include/kernel/dma.h include/kernel/pmm.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block.o: kernel/fs/block/block.c include/kernel/block.h \
		include/kernel/errno.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block_silliconfs.o: kernel/fs/block/block_silliconfs.c \
		include/kernel/block_silliconfs.h include/kernel/block.h \
		include/kernel/errno.h include/kernel/silliconfs.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/acpi.o: kernel/arch/x86_64/firmware/acpi.c \
		include/arch/x86_64/acpi.h include/kernel/heap.h \
		include/kernel/log.h include/kernel/timer.h include/kernel/vmm.h \
		kernel/arch/x86_64/firmware/acpica/source/include/acpi.h \
		kernel/arch/x86_64/firmware/acpica/source/include/acpiosxf.h \
		kernel/arch/x86_64/firmware/acpica/source/include/acpixf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL \
		-isystem kernel/arch/x86_64/firmware/acpica/source/include -Iinclude -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/arch/x86_64/timer/timer.c \
		include/arch/x86_64/lapic.h include/arch/x86_64/interrupts.h \
		include/kernel/timer.h include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/sched/scheduler.c \
		include/kernel/scheduler.h include/kernel/heap.h include/kernel/vmm.h \
		include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt_asm.o: kernel/arch/x86_64/cpu/gdt.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts_asm.o: kernel/arch/x86_64/interrupts/interrupts.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/context.o: kernel/arch/x86_64/context/context.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry.o: kernel/arch/x86_64/context/user_entry.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry_c.o: kernel/arch/x86_64/context/user_entry.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/user_start.o: user/start.S user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_os.o: user/os.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

UDP_SOCKET_TEST_SOURCE ?= tests/user/udp_socket_test.c
UDP_SOCKET_TEST_CFLAGS ?=
$(BUILD_DIR)/udp_socket_test.o: $(UDP_SOCKET_TEST_SOURCE) user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) $(UDP_SOCKET_TEST_CFLAGS) -c $< -o $@

$(BUILD_DIR)/udp_socket_start.o: tests/user/udp_socket_start.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/udp_socket_test.elf: $(BUILD_DIR)/udp_socket_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/udp_socket_test.o \
		user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/udp_socket_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/udp_socket_test.o -o $@

$(BUILD_DIR)/udp_socket_test_blob.o: $(BUILD_DIR)/udp_socket_test.elf
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_udp_socket_test_elf_start=_binary_udp_socket_test_bin_start \
		--redefine-sym _binary_udp_socket_test_elf_end=_binary_udp_socket_test_bin_end \
		udp_socket_test.elf udp_socket_test_blob.o

$(BUILD_DIR)/user_path.o: user/path.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_init.o: user/init/init.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_shell.o: user/shell/shell.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/%.o: user/coreutils/%.c user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/fetch.o: user/fetch.c user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/%.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/%.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/$*.o -o $@

$(BUILD_DIR)/coreutils/netdiag.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/netdiag.o \
		user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/netdiag.o -o $@

$(BUILD_DIR)/coreutils/netdiag.o: user/coreutils/netdiag.c \
		user/coreutils/netdiag_args.h user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/coreutils/ping.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/ping.o \
		user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/ping.o -o $@

$(BUILD_DIR)/coreutils/ping.o: user/coreutils/ping.c \
		user/coreutils/netdiag_args.h user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/netdiag_args.o: user/netdiag_args.c \
		user/coreutils/netdiag_args.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/netdiag_args_test: user/netdiag_args.c \
		tests/netdiag_args_test.c user/coreutils/netdiag_args.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iuser \
		user/netdiag_args.c tests/netdiag_args_test.c -o $@

netdiag-args-test: $(BUILD_DIR)/netdiag_args_test
	$(BUILD_DIR)/netdiag_args_test

$(BUILD_DIR)/user_probe.o: tests/user/probe.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_argv_probe.o: tests/user/argv_probe.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/argv_probe.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o $(BUILD_DIR)/user_argv_probe.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_argv_probe.o -o $@

$(BUILD_DIR)/user_argv_limit_test.o: tests/user/argv_limit_test.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/argv_limit_test.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o $(BUILD_DIR)/user_argv_limit_test.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_argv_limit_test.o -o $@

$(BUILD_DIR)/user_pipe_test.o: tests/user/pipe_test.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/user_pipe_exec_child.o: tests/user/pipe_exec_child.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/user_signal_test.o: tests/user/signal_test.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -mno-sse -mno-sse2 -msoft-float \
		-Iuser -c $< -o $@

$(BUILD_DIR)/user_fpu_context_test.o: tests/user/fpu_context_test.c \
		user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/user_fpu_exec_child.o: tests/user/fpu_exec_child.c \
		user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/fpu_context_test.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_fpu_context_test.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_fpu_context_test.o -o $@

$(BUILD_DIR)/fpu_exec_child.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_fpu_exec_child.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_fpu_exec_child.o -o $@

$(BUILD_DIR)/user_shm_lifecycle_test.o: tests/user/shm_lifecycle_test.c \
		user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/user_shm_fork_failure_test.o: tests/user/shm_fork_failure_test.c \
		user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -Iuser -c $< -o $@

$(BUILD_DIR)/shm_lifecycle_test.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shm_lifecycle_test.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shm_lifecycle_test.o -o $@

$(BUILD_DIR)/shm_fork_failure_test.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shm_fork_failure_test.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shm_fork_failure_test.o -o $@

$(BUILD_DIR)/signal_test.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_signal_test.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_signal_test.o -o $@

$(BUILD_DIR)/user_signal_exec_child.o: tests/user/signal_exec_child.c \
		user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -mno-sse -mno-sse2 -msoft-float \
		-Iuser -c $< -o $@

$(BUILD_DIR)/signal_exec_child.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_signal_exec_child.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_signal_exec_child.o -o $@

$(BUILD_DIR)/pipe_test.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o $(BUILD_DIR)/user_pipe_test.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_pipe_test.o -o $@

$(BUILD_DIR)/pipe_exec_child.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_pipe_exec_child.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_pipe_exec_child.o -o $@

$(BUILD_DIR)/user_crash.o: tests/user/crash.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/init.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_init.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_init.o -o $@

$(BUILD_DIR)/shell.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shell.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shell.o -o $@

$(BUILD_DIR)/probe.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_probe.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_probe.o -o $@

$(BUILD_DIR)/crash.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o $(BUILD_DIR)/user_crash.o user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_crash.o -o $@

$(BUILD_DIR)/populate_root: tools/populate_root.c kernel/fs/vfs/vfs.c \
		kernel/fs/silliconfs/silliconfs.c kernel/proc/fd.c include/kernel/vfs.h \
		include/kernel/silliconfs.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		tools/populate_root.c kernel/fs/vfs/vfs.c kernel/fs/silliconfs/silliconfs.c \
		kernel/proc/fd.c -o $@

native-musl-verify:
	python3 user/musl/scripts/verify.py --output $(MUSL_BOOTSTRAP_DIR)

$(MUSL_BUILD_DIR)/.native-musl-ready: user/musl/scripts/build_native.py \
		user/musl/scripts/common.py \
		user/musl/manifest/source.json \
		user/musl/manifest/patches.json \
		$(wildcard user/musl/opensillicon/*)
	python3 user/musl/scripts/build_native.py \
		--output $(MUSL_BUILD_DIR) --bootstrap $(MUSL_BOOTSTRAP_DIR) --jobs 4
	touch $@

native-musl-build: $(MUSL_BUILD_DIR)/.native-musl-ready

$(MUSL_SYSROOT)/usr/lib/libc.a: $(MUSL_BUILD_DIR)/.native-musl-ready

$(MUSL_HELLO): tests/musl_hello.c user/c_linker.ld Makefile \
		$(MUSL_BUILD_DIR)/build-info.json $(MUSL_SYSROOT)/usr/lib/libc.a
	$(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
		-ftls-model=local-exec -nostdinc \
		-isystem $(MUSL_SYSROOT)/usr/include -c tests/musl_hello.c \
		-o $(MUSL_BUILD_DIR)/musl_hello.o
	PATH="$(dir $(MUSL_LLD)):$$PATH" $(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-nostdlib -static -fuse-ld=lld -B$(dir $(MUSL_LLD)) \
		-no-pie -Wl,-z,noexecstack -Wl,--build-id=none \
		-Wl,-T,user/c_linker.ld $(MUSL_SYSROOT)/usr/lib/crt1.o \
		$(MUSL_SYSROOT)/usr/lib/crti.o $(MUSL_BUILD_DIR)/musl_hello.o \
		-L$(MUSL_SYSROOT)/usr/lib -lc $(MUSL_SYSROOT)/usr/lib/crtn.o \
		-o $(MUSL_HELLO)

$(MUSL_ALLOC): tests/musl_alloc.c user/c_linker.ld Makefile \
		$(MUSL_BUILD_DIR)/build-info.json $(MUSL_SYSROOT)/usr/lib/libc.a
	$(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
		-ftls-model=local-exec -nostdinc \
		-isystem $(MUSL_SYSROOT)/usr/include -c tests/musl_alloc.c \
		-o $(MUSL_BUILD_DIR)/musl_alloc.o
	PATH="$(dir $(MUSL_LLD)):$$PATH" $(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-nostdlib -static -fuse-ld=lld -B$(dir $(MUSL_LLD)) \
		-no-pie -Wl,-z,noexecstack -Wl,--build-id=none \
		-Wl,-T,user/c_linker.ld $(MUSL_SYSROOT)/usr/lib/crt1.o \
		$(MUSL_SYSROOT)/usr/lib/crti.o $(MUSL_BUILD_DIR)/musl_alloc.o \
		-L$(MUSL_SYSROOT)/usr/lib -lc $(MUSL_SYSROOT)/usr/lib/crtn.o \
		-o $(MUSL_ALLOC)

$(MUSL_STDIO): tests/musl_stdio.c user/c_linker.ld Makefile \
		$(MUSL_BUILD_DIR)/build-info.json $(MUSL_SYSROOT)/usr/lib/libc.a
	$(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
		-ftls-model=local-exec -nostdinc \
		-isystem $(MUSL_SYSROOT)/usr/include -c tests/musl_stdio.c \
		-o $(MUSL_BUILD_DIR)/musl_stdio.o
	PATH="$(dir $(MUSL_LLD)):$$PATH" $(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-nostdlib -static -fuse-ld=lld -B$(dir $(MUSL_LLD)) \
		-no-pie -Wl,-z,noexecstack -Wl,--build-id=none \
		-Wl,-T,user/c_linker.ld $(MUSL_SYSROOT)/usr/lib/crt1.o \
		$(MUSL_SYSROOT)/usr/lib/crti.o $(MUSL_BUILD_DIR)/musl_stdio.o \
		-L$(MUSL_SYSROOT)/usr/lib -lc $(MUSL_SYSROOT)/usr/lib/crtn.o \
		-o $(MUSL_STDIO)

native-musl-hello:
	$(MAKE) native-musl-build
	$(MAKE) $(MUSL_HELLO)

native-musl-alloc:
	$(MAKE) native-musl-build
	$(MAKE) $(MUSL_HELLO) $(MUSL_ALLOC)

native-musl-stdio:
	$(MAKE) native-musl-build
	$(MAKE) $(MUSL_STDIO)

$(MUSL_CLOCK): tests/musl_clock.c user/c_linker.ld Makefile \
		$(MUSL_BUILD_DIR)/build-info.json $(MUSL_SYSROOT)/usr/lib/libc.a
	$(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
		-ftls-model=local-exec -nostdinc \
		-isystem $(MUSL_SYSROOT)/usr/include -c tests/musl_clock.c \
		-o $(MUSL_BUILD_DIR)/musl_clock.o
	PATH="$(dir $(MUSL_LLD)):$$PATH" $(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-nostdlib -static -fuse-ld=lld -B$(dir $(MUSL_LLD)) \
		-no-pie -Wl,-z,noexecstack -Wl,--build-id=none \
		-Wl,-T,user/c_linker.ld $(MUSL_SYSROOT)/usr/lib/crt1.o \
		$(MUSL_SYSROOT)/usr/lib/crti.o $(MUSL_BUILD_DIR)/musl_clock.o \
		-L$(MUSL_SYSROOT)/usr/lib -lc $(MUSL_SYSROOT)/usr/lib/crtn.o \
		-o $(MUSL_CLOCK)

native-musl-clock: native-musl-build
	$(MAKE) $(MUSL_CLOCK)

$(MUSL_NANOSLEEP): tests/musl_nanosleep.c user/c_linker.ld Makefile \
		$(MUSL_BUILD_DIR)/build-info.json $(MUSL_SYSROOT)/usr/lib/libc.a
	$(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
		-ftls-model=local-exec -nostdinc \
		-isystem $(MUSL_SYSROOT)/usr/include -c tests/musl_nanosleep.c \
		-o $(MUSL_BUILD_DIR)/musl_nanosleep.o
	PATH="$(dir $(MUSL_LLD)):$$PATH" $(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-nostdlib -static -fuse-ld=lld -B$(dir $(MUSL_LLD)) \
		-no-pie -Wl,-z,noexecstack -Wl,--build-id=none \
		-Wl,-T,user/c_linker.ld $(MUSL_SYSROOT)/usr/lib/crt1.o \
		$(MUSL_SYSROOT)/usr/lib/crti.o $(MUSL_BUILD_DIR)/musl_nanosleep.o \
		-L$(MUSL_SYSROOT)/usr/lib -lc $(MUSL_SYSROOT)/usr/lib/crtn.o \
		-o $(MUSL_NANOSLEEP)

native-musl-nanosleep: native-musl-build
	$(MAKE) $(MUSL_NANOSLEEP)

$(MUSL_FD): tests/musl_fd_seekstat.c user/c_linker.ld Makefile \
		$(MUSL_BUILD_DIR)/build-info.json $(MUSL_SYSROOT)/usr/lib/libc.a
	$(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
		-ftls-model=local-exec -nostdinc \
		-isystem $(MUSL_SYSROOT)/usr/include -c tests/musl_fd_seekstat.c \
		-o $(MUSL_BUILD_DIR)/musl_fd_seekstat.o
	PATH="$(dir $(MUSL_LLD)):$$PATH" $(MUSL_CLANG) --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
		-nostdlib -static -fuse-ld=lld -B$(dir $(MUSL_LLD)) \
		-no-pie -Wl,-z,noexecstack -Wl,--build-id=none \
		-Wl,-T,user/c_linker.ld $(MUSL_SYSROOT)/usr/lib/crt1.o \
		$(MUSL_SYSROOT)/usr/lib/crti.o $(MUSL_BUILD_DIR)/musl_fd_seekstat.o \
		-L$(MUSL_SYSROOT)/usr/lib -lc $(MUSL_SYSROOT)/usr/lib/crtn.o \
		-o $(MUSL_FD)

native-musl-fd-seekstat: native-musl-build
	$(MAKE) $(MUSL_FD)

native-musl-inspect: native-musl-hello native-musl-alloc native-musl-stdio native-musl-clock native-musl-nanosleep native-musl-fd-seekstat
	@set -e; for elf in $(MUSL_HELLO) $(MUSL_ALLOC) $(MUSL_STDIO) $(MUSL_CLOCK) $(MUSL_NANOSLEEP) $(MUSL_FD); do \
		echo "Inspecting $$elf"; \
		readelf -h -l -S -r -d $$elf; \
		! readelf -l $$elf | grep -E 'INTERP|DYNAMIC'; \
		! nm -u $$elf | grep -v '_DYNAMIC'; \
	done

$(BUILD_DIR)/musl-root.img: $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(BUILD_DIR)/probe.elf $(BUILD_DIR)/populate_root $(MUSL_HELLO) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(MUSL_HELLO)

native-musl-qemu-test:
	$(MAKE) native-musl-hello
	CCACHE_DISABLE=1 $(MAKE) BUILD_DIR=$(MUSL_QEMU_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_NATIVE_MUSL' \
		iso $(MUSL_QEMU_BUILD_DIR)/musl-root.img
	@set +e; timeout 35s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(MUSL_QEMU_BUILD_DIR)/opensillicon.iso \
		-drive file=$(MUSL_QEMU_BUILD_DIR)/musl-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(MUSL_QEMU_BUILD_DIR)/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	grep -F 'OpenSillicon: native musl write, errno, and TLS passed' \
		$(MUSL_QEMU_BUILD_DIR)/serial.log
	grep -F 'OpenSillicon: native musl 1.2.6 CRT, TLS, errno, write, and exit passed' \
		$(MUSL_QEMU_BUILD_DIR)/serial.log

$(BUILD_DIR)/musl-alloc-root.img: $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf \
		$(BUILD_DIR)/populate_root $(MUSL_HELLO) $(MUSL_ALLOC) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(MUSL_HELLO) $(MUSL_ALLOC)

native-musl-alloc-qemu-test:
	$(MAKE) native-musl-alloc
	CCACHE_DISABLE=1 $(MAKE) BUILD_DIR=$(MUSL_ALLOC_QEMU_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_NATIVE_MUSL_ALLOC -DKERNEL_TEST_PRIVATE_MAPPING_FAILURE' \
		iso $(MUSL_ALLOC_QEMU_BUILD_DIR)/musl-alloc-root.img
	@set +e; timeout 45s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(MUSL_ALLOC_QEMU_BUILD_DIR)/opensillicon.iso \
		-drive file=$(MUSL_ALLOC_QEMU_BUILD_DIR)/musl-alloc-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(MUSL_ALLOC_QEMU_BUILD_DIR)/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	grep -F 'OpenSillicon: musl mallocng small/large allocations and mmap passed' \
		$(MUSL_ALLOC_QEMU_BUILD_DIR)/serial.log
	grep -F 'OpenSillicon: native musl malloc, mmap, mprotect, munmap, TLS, and exit passed' \
		$(MUSL_ALLOC_QEMU_BUILD_DIR)/serial.log

$(BUILD_DIR)/musl-stdio-root.img: $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf \
		$(BUILD_DIR)/populate_root $(MUSL_STDIO) tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(MUSL_HELLO) \
		$(MUSL_ALLOC) $(MUSL_STDIO)

native-musl-stdio-qemu-test:
	$(MAKE) native-musl-build
	$(MAKE) $(MUSL_HELLO) $(MUSL_ALLOC) $(MUSL_STDIO)
	CCACHE_DISABLE=1 $(MAKE) BUILD_DIR=$(MUSL_STDIO_QEMU_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_NATIVE_MUSL_ALLOC -DKERNEL_TEST_NATIVE_MUSL_STDIO' \
		iso $(MUSL_STDIO_QEMU_BUILD_DIR)/musl-stdio-root.img
	@set +e; timeout 45s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(MUSL_STDIO_QEMU_BUILD_DIR)/opensillicon.iso \
		-drive file=$(MUSL_STDIO_QEMU_BUILD_DIR)/musl-stdio-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	grep -F 'stdio printf: ok -7 42 0x2a' $(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log
	grep -F 'stdio stdout: fprintf' $(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log
	grep -F 'stdio stderr: fprintf' $(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log
	grep -F 'stdio puts' $(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log
	grep -F 'stdio fwrite' $(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log
	grep -F 'stdio writev: ok' $(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log
	grep -F 'OpenSillicon: musl stdio, TTY query, VFS readv, allocator, and TLS passed' \
		$(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log
	grep -F 'OpenSillicon: native musl stdio QEMU fixture exited with status zero' \
		$(MUSL_STDIO_QEMU_BUILD_DIR)/serial.log

$(MUSL_CLOCK_QEMU_BUILD_DIR)/musl-clock-root.img: $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf \
		$(BUILD_DIR)/populate_root $(MUSL_HELLO) $(MUSL_ALLOC) $(MUSL_STDIO) \
		$(MUSL_CLOCK) tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(MUSL_HELLO) \
		$(MUSL_ALLOC) $(MUSL_STDIO) $(MUSL_CLOCK)

$(MUSL_NANOSLEEP_QEMU_BUILD_DIR)/musl-nanosleep-root.img: $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf \
		$(BUILD_DIR)/populate_root $(MUSL_HELLO) $(MUSL_ALLOC) $(MUSL_STDIO) \
		$(MUSL_CLOCK) $(MUSL_NANOSLEEP) tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(MUSL_HELLO) \
		$(MUSL_ALLOC) $(MUSL_STDIO) $(MUSL_CLOCK) $(MUSL_NANOSLEEP)

native-musl-nanosleep-qemu-test: native-musl-nanosleep
	CCACHE_DISABLE=1 $(MAKE) BUILD_DIR=$(MUSL_NANOSLEEP_QEMU_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_NATIVE_MUSL_NANOSLEEP' \
		iso $(MUSL_NANOSLEEP_QEMU_BUILD_DIR)/musl-nanosleep-root.img
	@set +e; timeout 45s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(MUSL_NANOSLEEP_QEMU_BUILD_DIR)/opensillicon.iso \
		-drive file=$(MUSL_NANOSLEEP_QEMU_BUILD_DIR)/musl-nanosleep-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(MUSL_NANOSLEEP_QEMU_BUILD_DIR)/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	grep -F 'OpenSillicon: native musl nanosleep and error paths passed' \
		$(MUSL_NANOSLEEP_QEMU_BUILD_DIR)/serial.log

$(MUSL_FD_QEMU_BUILD_DIR)/musl-fd-root.img: $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(BUILD_DIR)/populate_root \
		$(MUSL_HELLO) $(MUSL_ALLOC) $(MUSL_STDIO) $(MUSL_CLOCK) \
		$(MUSL_NANOSLEEP) $(MUSL_FD) tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(MUSL_HELLO) \
		$(MUSL_ALLOC) $(MUSL_STDIO) $(MUSL_CLOCK) $(MUSL_NANOSLEEP) $(MUSL_FD)

native-musl-fd-seekstat-qemu-test: native-musl-fd-seekstat
	CCACHE_DISABLE=1 $(MAKE) BUILD_DIR=$(MUSL_FD_QEMU_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_NATIVE_MUSL_FD' \
		iso $(MUSL_FD_QEMU_BUILD_DIR)/musl-fd-root.img
	@set +e; timeout 45s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(MUSL_FD_QEMU_BUILD_DIR)/opensillicon.iso \
		-drive file=$(MUSL_FD_QEMU_BUILD_DIR)/musl-fd-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(MUSL_FD_QEMU_BUILD_DIR)/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	grep -F 'OpenSillicon: native musl lseek and fstat passed' \
		$(MUSL_FD_QEMU_BUILD_DIR)/serial.log

native-musl-clock-qemu-test: native-musl-clock
	CCACHE_DISABLE=1 $(MAKE) BUILD_DIR=$(MUSL_CLOCK_QEMU_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_NATIVE_MUSL_CLOCK' \
		iso $(MUSL_CLOCK_QEMU_BUILD_DIR)/musl-clock-root.img
	@set +e; timeout 45s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(MUSL_CLOCK_QEMU_BUILD_DIR)/opensillicon.iso \
		-drive file=$(MUSL_CLOCK_QEMU_BUILD_DIR)/musl-clock-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(MUSL_CLOCK_QEMU_BUILD_DIR)/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	grep -F 'OpenSillicon: native musl CLOCK_MONOTONIC and error paths passed' \
		$(MUSL_CLOCK_QEMU_BUILD_DIR)/serial.log

$(BUILD_DIR)/acpica/%.o: $(ACPICA_ROOT)/%.c \
		$(ACPICA_ROOT)/include/platform/acenv.h \
		$(ACPICA_ROOT)/include/platform/acopensillicon.h \
		$(ACPICA_ROOT)/include/ctype.h
	mkdir -p $(dir $@)
	$(CC) $(ACPICA_CFLAGS) -c $< -o $@

acpica-check: $(ACPICA_OBJECTS)

$(BUILD_DIR)/acpi_test: kernel/arch/x86_64/firmware/acpi.c tests/acpi_test.c \
		include/arch/x86_64/acpi.h kernel/arch/x86_64/firmware/acpica/source/include/acpi.h
	$(CC) -std=c11 -Wall -Wextra -Werror \
		-DOPEN_SILLICON_KERNEL -isystem kernel/arch/x86_64/firmware/acpica/source/include \
		-Iinclude -Ilimine kernel/arch/x86_64/firmware/acpi.c tests/acpi_test.c \
		-o $@

acpi-test: $(BUILD_DIR)/acpi_test
	$(BUILD_DIR)/acpi_test

$(BUILD_DIR)/tpm2_acpi_test: kernel/drivers/tpm/tpm2_acpi.c tests/tpm2_acpi_test.c \
		include/kernel/tpm2_acpi.h | $(BUILD_DIR)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude \
		kernel/drivers/tpm/tpm2_acpi.c tests/tpm2_acpi_test.c -o $@

tpm2-acpi-test: $(BUILD_DIR)/tpm2_acpi_test
	$(BUILD_DIR)/tpm2_acpi_test

$(BUILD_DIR)/tpm2_crb_test: kernel/drivers/tpm/tpm2_crb.c \
		tests/tpm2_crb_fake.c tests/tpm2_crb_test.c \
		include/kernel/tpm2_crb.h tests/tpm2_crb_fake.h | $(BUILD_DIR)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude -Itests \
		kernel/drivers/tpm/tpm2_crb.c tests/tpm2_crb_fake.c \
		tests/tpm2_crb_test.c -o $@

tpm2-crb-test: $(BUILD_DIR)/tpm2_crb_test
	$(BUILD_DIR)/tpm2_crb_test

$(BUILD_DIR)/acpi_mmio_range_test: \
		kernel/arch/x86_64/firmware/acpi_mmio_range.c \
		tests/acpi_mmio_range_test.c include/kernel/acpi_mmio_range.h | $(BUILD_DIR)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude \
		kernel/arch/x86_64/firmware/acpi_mmio_range.c \
		tests/acpi_mmio_range_test.c -o $@

acpi-mmio-range-test: $(BUILD_DIR)/acpi_mmio_range_test
	$(BUILD_DIR)/acpi_mmio_range_test

$(BUILD_DIR)/pci_test: kernel/drivers/bus/pci/pci.c tests/pci_test.c \
		include/arch/x86_64/pci.h include/arch/x86_64/acpi.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/drivers/bus/pci/pci.c tests/pci_test.c -o $@

pci-test: $(BUILD_DIR)/pci_test
	$(BUILD_DIR)/pci_test

$(BUILD_DIR)/pci_resource_test: kernel/drivers/bus/pci/pci_resource.c \
		tests/pci_resource_test.c include/arch/x86_64/pci_resource.h \
		include/kernel/vmm.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/drivers/bus/pci/pci_resource.c tests/pci_resource_test.c -o $@

pci-resource-test: $(BUILD_DIR)/pci_resource_test
	$(BUILD_DIR)/pci_resource_test

$(BUILD_DIR)/dma_test: kernel/mm/dma.c tests/dma_test.c \
		include/kernel/dma.h include/kernel/pmm.h include/kernel/vmm.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/mm/dma.c tests/dma_test.c -o $@

dma-test: $(BUILD_DIR)/dma_test
	$(BUILD_DIR)/dma_test
	$(BUILD_DIR)/dma_test reject

$(BUILD_DIR)/dma_ring_test: kernel/mm/dma_ring.c tests/dma_ring_test.c \
		kernel/mm/dma.c include/kernel/dma_ring.h include/kernel/dma.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/mm/dma.c kernel/mm/dma_ring.c tests/dma_ring_test.c -o $@

dma-ring-test: $(BUILD_DIR)/dma_ring_test
	$(BUILD_DIR)/dma_ring_test

$(BUILD_DIR)/e1000_profile_test: kernel/drivers/net/e1000/e1000_profile.c \
		tests/e1000_profile_test.c include/kernel/e1000_profile.h \
		include/arch/x86_64/pci.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/drivers/net/e1000/e1000_profile.c tests/e1000_profile_test.c -o $@

e1000-profile-test: $(BUILD_DIR)/e1000_profile_test $(BUILD_DIR)/e1000_reset_test
	$(BUILD_DIR)/e1000_profile_test
	$(BUILD_DIR)/e1000_reset_test

$(BUILD_DIR)/e1000_ring_test: kernel/drivers/net/e1000/e1000_ring.c \
		tests/e1000_ring_test.c include/kernel/e1000_ring.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/drivers/net/e1000/e1000_ring.c tests/e1000_ring_test.c -o $@

e1000-ring-test: $(BUILD_DIR)/e1000_ring_test
	$(BUILD_DIR)/e1000_ring_test

$(BUILD_DIR)/random_test: kernel/random/random.c tests/random_test.c \
		include/kernel/random.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -DKERNEL_RANDOM_TEST \
		-Iinclude kernel/random/random.c tests/random_test.c -o $@

kernel-random-test: $(BUILD_DIR)/random_test
	$(BUILD_DIR)/random_test

$(BUILD_DIR)/boot_entropy_test: kernel/core/boot/boot_entropy.c \
		tests/boot_entropy_test.c include/kernel/boot_entropy.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/core/boot/boot_entropy.c tests/boot_entropy_test.c -o $@

boot-entropy-test: $(BUILD_DIR)/boot_entropy_test
	$(BUILD_DIR)/boot_entropy_test

$(BUILD_DIR)/entropy_test: kernel/random/entropy.c tests/entropy_test.c \
		include/kernel/entropy.h include/kernel/random.h \
		include/kernel/rdseed.h include/kernel/virtio_rng.h | $(BUILD_DIR)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-DKERNEL_ENTROPY_TESTING -Iinclude -Ilimine kernel/random/entropy.c \
		tests/entropy_test.c -o $@

kernel-entropy-test: $(BUILD_DIR)/entropy_test
	$(BUILD_DIR)/entropy_test

$(BUILD_DIR)/rdseed_test: kernel/drivers/entropy/rdseed.c tests/rdseed_test.c \
		include/kernel/rdseed.h | $(BUILD_DIR)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-DRDSEED_TESTING -Iinclude kernel/drivers/entropy/rdseed.c tests/rdseed_test.c -o $@

rdseed-test: $(BUILD_DIR)/rdseed_test
	$(BUILD_DIR)/rdseed_test

$(BUILD_DIR)/rdseed_hw_test: tools/rdseed_hw_test.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror $< -o $@

rdseed-hw-test: $(BUILD_DIR)/rdseed_hw_test
	$(BUILD_DIR)/rdseed_hw_test

rdseed-qemu-test:
	$(MAKE) BUILD_DIR=build/rdseed-qemu \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DOPEN_SILLICON_RDSEED_QEMU_TEST' iso \
		build/rdseed-qemu/phase12-root.img
	for profile in rdseed rdrand-only rdseed-no-rdrand neither; do \
		python3 tests/rdseed_qemu_test.py $(QEMU) $(OVMF_CODE) \
		build/rdseed-qemu/opensillicon.iso \
		build/rdseed-qemu/phase12-root.img $$profile \
		build/rdseed-qemu/rdseed-$$profile.log || exit $$?; \
	done

$(BUILD_DIR)/virtio_rng_test: kernel/drivers/entropy/virtio_rng.c tests/virtio_rng_test.c \
		include/kernel/virtio_rng.h include/kernel/dma.h \
		include/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-DVIRTIO_RNG_TESTING -Iinclude kernel/drivers/entropy/virtio_rng.c \
		tests/virtio_rng_test.c -o $@

virtio-rng-test: $(BUILD_DIR)/virtio_rng_test
	$(BUILD_DIR)/virtio_rng_test

virtio-rng-qemu-test:
	$(MAKE) BUILD_DIR=build/virtio-rng-present \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DOPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG' iso \
		build/virtio-rng-present/phase12-root.img
	python3 tests/virtio_rng_qemu_test.py $(QEMU) $(OVMF_CODE) present \
		build/virtio-rng-present/opensillicon.iso \
		build/virtio-rng-present/phase12-root.img \
		build/virtio-rng-present/virtio-rng.log

virtio-rng-absent-qemu-test:
	$(MAKE) BUILD_DIR=build/virtio-rng-absent \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DOPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG' iso \
		build/virtio-rng-absent/phase12-root.img
	python3 tests/virtio_rng_qemu_test.py $(QEMU) $(OVMF_CODE) absent \
		build/virtio-rng-absent/opensillicon.iso \
		build/virtio-rng-absent/phase12-root.img \
		build/virtio-rng-absent/virtio-rng.log

virtio-rng-cpu-qemu-test:
	$(MAKE) BUILD_DIR=build/virtio-rng-cpu \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DOPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG' iso \
		build/virtio-rng-cpu/phase12-root.img
	for profile in max-rdrand-rdseed max-rdrand max-neither; do \
		python3 tests/virtio_rng_qemu_test.py $(QEMU) $(OVMF_CODE) present \
		build/virtio-rng-cpu/opensillicon.iso \
		build/virtio-rng-cpu/phase12-root.img \
		build/virtio-rng-cpu/virtio-rng-$$profile.log $$profile || exit $$?; \
	done

e1000-init-qemu-test: iso $(BUILD_DIR)/phase12-root.img
	@set +e; timeout 18s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/phase12-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 -nodefaults -nic none \
		-netdev user,id=net0 -device e1000,netdev=net0 \
		-serial file:$(BUILD_DIR)/e1000-init.log -display none \
		-no-reboot -no-shutdown; status=$$?; set -e; \
	test $$status -eq 124; \
	grep -F 'e1000 DMA rings ready TX/RX 8 descriptors each' \
		$(BUILD_DIR)/e1000-init.log; \
	grep -F 'Initializing QEMU e1000 82540EM ...' \
		$(BUILD_DIR)/e1000-init.log

e1000-dma-qemu-test:
	$(MAKE) BUILD_DIR=build/e1000-smoke \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_E1000_SMOKE' \
		iso build/e1000-smoke/test-root.img
	python3 tests/e1000_qemu_smoke.py $(QEMU) $(OVMF_CODE) \
		build/e1000-smoke/opensillicon.iso \
		build/e1000-smoke/test-root.img \
		build/e1000-smoke/e1000-smoke.log


$(BUILD_DIR)/netstack_test: kernel/net/core/netstack.c kernel/net/tcp/tcp_wire.c \
		tests/netstack_test.c include/kernel/netstack.h \
		include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/net/core/netstack.c kernel/net/tcp/tcp_wire.c \
		tests/netstack_test.c -Wl,--wrap=tcp_wire_parse -o $@

$(BUILD_DIR)/netstack_runtime_gate_test: kernel/net/core/netstack.c \
		kernel/net/tcp/tcp_wire.c kernel/net/tcp/tcp_core.c \
		kernel/net/tcp/tcp_runtime.c tests/netstack_runtime_gate_test.c \
		include/kernel/netstack.h include/kernel/tcp_runtime.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -DOPEN_SILLICON_ENABLE_TCP_ROUTING \
		-Iinclude kernel/net/core/netstack.c kernel/net/tcp/tcp_wire.c \
		kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_runtime.c \
		tests/netstack_runtime_gate_test.c -o $@

netstack-test: $(BUILD_DIR)/netstack_test \
		$(BUILD_DIR)/netstack_runtime_gate_test
	$(BUILD_DIR)/netstack_test
	$(BUILD_DIR)/netstack_runtime_gate_test

$(BUILD_DIR)/udp_socket_test: kernel/net/udp/udp_socket.c \
		kernel/net/core/netstack.c kernel/net/tcp/tcp_wire.c \
		kernel/net/core/netdev.c kernel/net/core/netbuf.c \
		kernel/net/service/network_service.c kernel/proc/fd.c tests/udp_socket_test.c
	$(CC) -std=c11 -Wall -Wextra -Werror -ffunction-sections \
		-fdata-sections -Iinclude -Ilimine kernel/net/udp/udp_socket.c \
		kernel/net/core/netstack.c kernel/net/tcp/tcp_wire.c \
		kernel/net/core/netdev.c kernel/net/core/netbuf.c \
		kernel/net/service/network_service.c kernel/proc/fd.c tests/udp_socket_test.c \
		-Wl,--gc-sections -o $@

udp-socket-test: $(BUILD_DIR)/udp_socket_test
	$(BUILD_DIR)/udp_socket_test

$(BUILD_DIR)/network_service_test: kernel/net/service/network_service.c \
		tests/network_service_test.c include/kernel/network_service.h \
		include/kernel/udp_socket.h include/kernel/errno.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/net/service/network_service.c tests/network_service_test.c -o $@

network-service-test: $(BUILD_DIR)/network_service_test
	$(BUILD_DIR)/network_service_test

$(BUILD_DIR)/tcp_core_test: kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_wire.c \
		tests/tcp_core_test.c include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		-DTCP_CORE_TESTING kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_wire.c \
		tests/tcp_core_test.c -o $@

$(BUILD_DIR)/tcp_wire_test: kernel/net/tcp/tcp_wire.c tests/tcp_wire_test.c \
		include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/net/tcp/tcp_wire.c tests/tcp_wire_test.c -o $@

tcp-wire-test: $(BUILD_DIR)/tcp_wire_test
	$(BUILD_DIR)/tcp_wire_test

$(BUILD_DIR)/tcp_wire_fuzz_test: kernel/net/tcp/tcp_wire.c \
		tests/tcp_wire_fuzz_test.c include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		kernel/net/tcp/tcp_wire.c tests/tcp_wire_fuzz_test.c -o $@

tcp-wire-fuzz-test: $(BUILD_DIR)/tcp_wire_fuzz_test
	$(BUILD_DIR)/tcp_wire_fuzz_test

tcp-core-test: $(BUILD_DIR)/tcp_core_test
	$(BUILD_DIR)/tcp_core_test

$(BUILD_DIR)/tcp_runtime_test: kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_runtime.c \
		kernel/net/tcp/tcp_wire.c tests/tcp_runtime_test.c include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h \
		include/kernel/tcp_runtime.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -DOPEN_SILLICON_ENABLE_TCP_ROUTING \
		-DTCP_CORE_TESTING -DTCP_RUNTIME_TESTING \
		-Iinclude kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_wire.c \
		kernel/net/tcp/tcp_runtime.c \
		tests/tcp_runtime_test.c -o $@

$(BUILD_DIR)/tcp_runtime_disabled_test: kernel/net/tcp/tcp_core.c \
		kernel/net/tcp/tcp_runtime.c kernel/net/tcp/tcp_wire.c \
		tests/tcp_runtime_disabled_test.c include/kernel/tcp_runtime.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_wire.c \
		kernel/net/tcp/tcp_runtime.c tests/tcp_runtime_disabled_test.c -o $@

tcp-runtime-test: $(BUILD_DIR)/tcp_runtime_test \
		$(BUILD_DIR)/tcp_runtime_disabled_test \
		$(BUILD_DIR)/tcp_runtime_sim_test
	$(BUILD_DIR)/tcp_runtime_test
	$(BUILD_DIR)/tcp_runtime_disabled_test
	$(BUILD_DIR)/tcp_runtime_sim_test

$(BUILD_DIR)/tcp_runtime_sim_test: kernel/net/tcp/tcp_core.c \
		kernel/net/tcp/tcp_runtime.c kernel/net/tcp/tcp_wire.c \
		tests/tcp_runtime_sim_test.c include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h include/kernel/tcp_runtime.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -DOPEN_SILLICON_ENABLE_TCP_ROUTING \
		-DTCP_CORE_TESTING -DTCP_RUNTIME_TESTING -Iinclude \
		kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_wire.c kernel/net/tcp/tcp_runtime.c \
		tests/tcp_runtime_sim_test.c -o $@

tcp-runtime-sim-test: $(BUILD_DIR)/tcp_runtime_sim_test
	$(BUILD_DIR)/tcp_runtime_sim_test

$(BUILD_DIR)/tcp_linux_interop: kernel/net/tcp/tcp_core.c \
		kernel/net/tcp/tcp_wire.c kernel/net/tcp/tcp_runtime.c \
		tests/tcp_linux_interop.c include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h include/kernel/tcp_runtime.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -DOPEN_SILLICON_ENABLE_TCP_ROUTING \
		-DTCP_CORE_TESTING -DTCP_RUNTIME_TESTING -Iinclude \
		kernel/net/tcp/tcp_core.c kernel/net/tcp/tcp_wire.c kernel/net/tcp/tcp_runtime.c \
		tests/tcp_linux_interop.c -o $@

tcp-linux-interop-test: $(BUILD_DIR)/tcp_linux_interop
	unshare -Urn python3 tests/tcp_linux_interop.py $(BUILD_DIR)/tcp_linux_interop

e1000-icmp-qemu-test:
	$(MAKE) BUILD_DIR=build/e1000-icmp \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_E1000_ICMP_SMOKE' \
		iso build/e1000-icmp/test-root.img
	python3 tests/e1000_icmp_qemu_test.py $(QEMU) $(OVMF_CODE) \
		build/e1000-icmp/opensillicon.iso \
		build/e1000-icmp/test-root.img build/e1000-icmp/e1000-icmp.log

e1000-udp-qemu-test:
	$(MAKE) BUILD_DIR=build/e1000-udp \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_UDP_SOCKET' \
		iso build/e1000-udp/phase12-root.img
	python3 tests/e1000_udp_qemu_test.py $(QEMU) $(OVMF_CODE) \
		build/e1000-udp/opensillicon.iso \
		build/e1000-udp/phase12-root.img build/e1000-udp/e1000-udp.log

e1000-netdiag-qemu-test:
	$(MAKE) BUILD_DIR=build/e1000-netdiag \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_UDP_SOCKET' \
		UDP_SOCKET_TEST_SOURCE=user/coreutils/netdiag.c \
		UDP_SOCKET_TEST_CFLAGS=-DNETDIAG_QEMU_TEST \
		iso build/e1000-netdiag/phase12-root.img
	python3 tests/e1000_udp_qemu_test.py $(QEMU) $(OVMF_CODE) \
		build/e1000-netdiag/opensillicon.iso \
		build/e1000-netdiag/phase12-root.img \
		build/e1000-netdiag/e1000-netdiag.log --netdiag

e1000-icmp-ping-qemu-test:
	$(MAKE) BUILD_DIR=build/e1000-icmp-ping \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_UDP_SOCKET -DKERNEL_TEST_ICMP_PING' \
		UDP_SOCKET_TEST_SOURCE=user/coreutils/ping.c \
		UDP_SOCKET_TEST_CFLAGS=-DPING_QEMU_TEST \
		iso build/e1000-icmp-ping/phase12-root.img
	python3 tests/e1000_icmp_qemu_test.py $(QEMU) $(OVMF_CODE) \
		build/e1000-icmp-ping/opensillicon.iso \
		build/e1000-icmp-ping/phase12-root.img \
		build/e1000-icmp-ping/e1000-icmp-ping.log --userspace-ping

$(BUILD_DIR)/e1000_reset_test: kernel/drivers/net/e1000/e1000_reset.c \
		tests/e1000_reset_test.c include/kernel/e1000.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/drivers/net/e1000/e1000_reset.c tests/e1000_reset_test.c -o $@

$(BUILD_DIR)/pmm_contiguous_test: kernel/mm/pmm.c tests/pmm_contiguous_test.c \
		include/kernel/pmm.h include/kernel/boot.h limine/limine.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/mm/pmm.c tests/pmm_contiguous_test.c -o $@

pmm-contiguous-test: $(BUILD_DIR)/pmm_contiguous_test
	$(BUILD_DIR)/pmm_contiguous_test

$(BUILD_DIR)/network_foundation_test: kernel/net/core/netbuf.c \
		kernel/net/core/netdev.c tests/network_foundation_test.c \
		include/kernel/netbuf.h include/kernel/netdev.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/net/core/netbuf.c kernel/net/core/netdev.c \
		tests/network_foundation_test.c -o $@

network-foundation-test: $(BUILD_DIR)/network_foundation_test pci-test
	$(BUILD_DIR)/network_foundation_test

$(BUILD_DIR)/user_test.o: tests/user/test.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_test.elf: $(BUILD_DIR)/user_test.o tests/user/linker.ld
	$(LD) $(USER_LDFLAGS) $< -o $@

$(BUILD_DIR)/user_test.bin: $(BUILD_DIR)/user_test.elf
	objcopy -O binary $< $@

$(BUILD_DIR)/user_test_blob.o: $(BUILD_DIR)/user_test.bin
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		user_test.bin user_test_blob.o

$(BUILD_DIR)/exec_child.o: tests/user/exec_child.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/exec_child.elf: $(BUILD_DIR)/exec_child.o tests/user/exec_child.ld
	$(LD) -nostdlib -static -no-pie -T tests/user/exec_child.ld $< -o $@

$(BUILD_DIR)/elf_test_user.o: tests/user/elf_test.S $(BUILD_DIR)/exec_child.elf | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/elf_test_user.elf: $(BUILD_DIR)/elf_test_user.o tests/user/elf_test.ld
	$(LD) -nostdlib -static -no-pie -T tests/user/elf_test.ld $< -o $@

$(BUILD_DIR)/elf_test_user.bin: $(BUILD_DIR)/elf_test_user.elf
	objcopy -O binary $< $@

$(BUILD_DIR)/elf_test_blob.o: $(BUILD_DIR)/elf_test_user.elf
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_elf_test_user_elf_start=_binary_elf_test_bin_start \
		--redefine-sym _binary_elf_test_user_elf_end=_binary_elf_test_bin_end \
		elf_test_user.elf elf_test_blob.o

$(BUILD_DIR)/private_mapping_lifecycle_test.o: \
		tests/user/private_mapping_lifecycle_test.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/private_mapping_lifecycle_test.elf: \
		$(BUILD_DIR)/private_mapping_lifecycle_test.o tests/user/elf_test.ld
	$(LD) -nostdlib -static -no-pie -T tests/user/elf_test.ld $< -o $@

$(BUILD_DIR)/private_mapping_lifecycle_blob.o: \
		$(BUILD_DIR)/private_mapping_lifecycle_test.elf
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		private_mapping_lifecycle_test.elf private_mapping_lifecycle_blob.o

$(BUILD_DIR)/vfs_test_user.o: tests/user/vfs_test.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs_test_user.elf: $(BUILD_DIR)/vfs_test_user.o tests/user/vfs_test.ld
	$(LD) -nostdlib -static -no-pie -T tests/user/vfs_test.ld $< -o $@

$(BUILD_DIR)/vfs_test_blob.o: $(BUILD_DIR)/vfs_test_user.elf
	cd $(BUILD_DIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_vfs_test_user_elf_start=_binary_vfs_test_bin_start \
		--redefine-sym _binary_vfs_test_user_elf_end=_binary_vfs_test_bin_end \
		vfs_test_user.elf vfs_test_blob.o

$(BUILD_DIR)/exec_path_child.o: tests/user/exec_path_child.S | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/exec_path_child.elf: $(BUILD_DIR)/exec_path_child.o tests/user/exec_child.ld
	$(LD) -nostdlib -static -no-pie -T tests/user/exec_child.ld $< -o $@

$(BUILD_DIR)/initramfs.cpio: $(shell find initramfs -type f -o -type d) \
		$(BUILD_DIR)/exec_path_child.elf $(BUILD_DIR)/pipe_test.elf \
		$(BUILD_DIR)/pipe_exec_child.elf $(BUILD_DIR)/signal_test.elf \
		$(BUILD_DIR)/signal_exec_child.elf \
		$(BUILD_DIR)/shm_lifecycle_test.elf \
		$(BUILD_DIR)/shm_fork_failure_test.elf \
		$(BUILD_DIR)/fpu_context_test.elf $(BUILD_DIR)/fpu_exec_child.elf \
		$(BUILD_DIR)/argv_probe.elf $(BUILD_DIR)/argv_limit_test.elf \
		tools/mkinitramfs.py
	python3 tools/mkinitramfs.py initramfs $@ \
		--file bin/exec_path_child=$(BUILD_DIR)/exec_path_child.elf \
		--file bin/pipe_test=$(BUILD_DIR)/pipe_test.elf \
		--file bin/pipe_exec_child=$(BUILD_DIR)/pipe_exec_child.elf \
		--file bin/signal_test=$(BUILD_DIR)/signal_test.elf \
		--file bin/signal_exec_child=$(BUILD_DIR)/signal_exec_child.elf \
		--file bin/shm_lifecycle_test=$(BUILD_DIR)/shm_lifecycle_test.elf \
		--file bin/shm_fork_failure_test=$(BUILD_DIR)/shm_fork_failure_test.elf \
		--file bin/fpu_context_test=$(BUILD_DIR)/fpu_context_test.elf \
		--file bin/fpu_exec_child=$(BUILD_DIR)/fpu_exec_child.elf \
		--file bin/argv_probe=$(BUILD_DIR)/argv_probe.elf \
		--file bin/argv_limit_test=$(BUILD_DIR)/argv_limit_test.elf

KERNEL_OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/boot.o $(BUILD_DIR)/boot_entropy.o $(BUILD_DIR)/entropy.o $(BUILD_DIR)/random.o $(BUILD_DIR)/rdseed.o $(BUILD_DIR)/pmm.o \
	$(BUILD_DIR)/virtio_rng.o \
	$(BUILD_DIR)/vmm.o $(BUILD_DIR)/shm.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/log.o \
	$(BUILD_DIR)/gdt.o $(BUILD_DIR)/fpu.o $(BUILD_DIR)/lapic.o $(BUILD_DIR)/acpi.o \
	$(BUILD_DIR)/ioapic.o $(BUILD_DIR)/ahci.o $(BUILD_DIR)/block.o \
	$(BUILD_DIR)/block_silliconfs.o \
	$(BUILD_DIR)/pci.o $(BUILD_DIR)/pci_resource.o $(BUILD_DIR)/dma.o \
	$(BUILD_DIR)/dma_ring.o $(BUILD_DIR)/netbuf.o $(BUILD_DIR)/netdev.o \
	$(BUILD_DIR)/netstack.o \
	$(BUILD_DIR)/tcp_core.o $(BUILD_DIR)/tcp_wire.o $(BUILD_DIR)/tcp_runtime.o \
	$(BUILD_DIR)/udp_socket.o $(BUILD_DIR)/network_service.o \
	$(BUILD_DIR)/e1000_profile.o $(BUILD_DIR)/e1000.o \
	$(BUILD_DIR)/e1000_ring.o \
	$(BUILD_DIR)/e1000_reset.o \
	$(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/scheduler.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/gdt_asm.o \
	$(BUILD_DIR)/interrupts_asm.o $(BUILD_DIR)/context.o $(BUILD_DIR)/fd.o \
	$(BUILD_DIR)/terminal.o $(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/tty.o \
	$(BUILD_DIR)/ps2_keyboard.o \
	$(BUILD_DIR)/syscall.o $(BUILD_DIR)/pipe.o $(BUILD_DIR)/pipe_buffer.o \
	$(BUILD_DIR)/user_entry.o \
	$(BUILD_DIR)/user_entry_c.o $(BUILD_DIR)/user_test_blob.o \
	$(BUILD_DIR)/elf.o $(BUILD_DIR)/elf_test_blob.o \
	$(PRIVATE_MAPPING_TEST_OBJECTS) $(BUILD_DIR)/process.o \
	$(BUILD_DIR)/signal_state.o \
	$(BUILD_DIR)/string.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ramfs.o \
	$(BUILD_DIR)/initramfs.o $(BUILD_DIR)/silliconfs.o \
	$(BUILD_DIR)/vfs_test_blob.o $(UDP_SOCKET_TEST_OBJECTS) $(ACPICA_OBJECTS)

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) kernel/arch/x86_64/boot/linker.ld
	$(LD) $(KERNEL_LDFLAGS) $(KERNEL_OBJECTS) -o $@

$(BUILD_DIR):
	mkdir -p $@

iso: $(BUILD_DIR)/opensillicon.iso

$(BUILD_DIR)/root.img: $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(BUILD_DIR)/probe.elf $(BUILD_DIR)/populate_root \
		$(COREUTILS_ELFS) $(MUSL_HELLO) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf $(MUSL_HELLO) --coreutils \
		$(BUILD_DIR)/coreutils/ls.elf $(BUILD_DIR)/coreutils/cat.elf \
		$(BUILD_DIR)/coreutils/echo.elf $(BUILD_DIR)/coreutils/pwd.elf \
		$(BUILD_DIR)/coreutils/true.elf $(BUILD_DIR)/coreutils/false.elf \
		$(BUILD_DIR)/coreutils/clear.elf $(BUILD_DIR)/coreutils/uname.elf \
		$(BUILD_DIR)/coreutils/fetch.elf $(BUILD_DIR)/coreutils/head.elf \
		$(BUILD_DIR)/coreutils/wc.elf $(BUILD_DIR)/coreutils/stat.elf \
		$(BUILD_DIR)/coreutils/basename.elf \
		$(BUILD_DIR)/coreutils/dirname.elf $(BUILD_DIR)/coreutils/kill.elf \
		$(BUILD_DIR)/coreutils/sleep.elf $(BUILD_DIR)/coreutils/which.elf \
		$(BUILD_DIR)/coreutils/ps.elf $(BUILD_DIR)/coreutils/netdiag.elf \
		$(BUILD_DIR)/coreutils/ping.elf

refresh-test-root: $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(BUILD_DIR)/probe.elf $(BUILD_DIR)/populate_root \
		$(COREUTILS_ELFS) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $(BUILD_DIR)/test-root.img \
		--blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $(BUILD_DIR)/test-root.img $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf --coreutils \
		$(BUILD_DIR)/coreutils/ls.elf $(BUILD_DIR)/coreutils/cat.elf \
		$(BUILD_DIR)/coreutils/echo.elf $(BUILD_DIR)/coreutils/pwd.elf \
		$(BUILD_DIR)/coreutils/true.elf $(BUILD_DIR)/coreutils/false.elf \
		$(BUILD_DIR)/coreutils/clear.elf $(BUILD_DIR)/coreutils/uname.elf \
		$(BUILD_DIR)/coreutils/fetch.elf $(BUILD_DIR)/coreutils/head.elf \
		$(BUILD_DIR)/coreutils/wc.elf $(BUILD_DIR)/coreutils/stat.elf \
		$(BUILD_DIR)/coreutils/basename.elf \
		$(BUILD_DIR)/coreutils/dirname.elf $(BUILD_DIR)/coreutils/kill.elf \
		$(BUILD_DIR)/coreutils/sleep.elf $(BUILD_DIR)/coreutils/which.elf \
		$(BUILD_DIR)/coreutils/ps.elf $(BUILD_DIR)/coreutils/netdiag.elf \
		$(BUILD_DIR)/coreutils/ping.elf

$(BUILD_DIR)/test-root.img: refresh-test-root
	@test -f $@

$(BUILD_DIR)/phase12-root.img: $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(BUILD_DIR)/probe.elf $(BUILD_DIR)/populate_root \
		$(COREUTILS_ELFS) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf --coreutils \
		$(BUILD_DIR)/coreutils/ls.elf $(BUILD_DIR)/coreutils/cat.elf \
		$(BUILD_DIR)/coreutils/echo.elf $(BUILD_DIR)/coreutils/pwd.elf \
		$(BUILD_DIR)/coreutils/true.elf $(BUILD_DIR)/coreutils/false.elf \
		$(BUILD_DIR)/coreutils/clear.elf $(BUILD_DIR)/coreutils/uname.elf \
		$(BUILD_DIR)/coreutils/fetch.elf $(BUILD_DIR)/coreutils/head.elf \
		$(BUILD_DIR)/coreutils/wc.elf $(BUILD_DIR)/coreutils/stat.elf \
		$(BUILD_DIR)/coreutils/basename.elf \
		$(BUILD_DIR)/coreutils/dirname.elf $(BUILD_DIR)/coreutils/kill.elf \
		$(BUILD_DIR)/coreutils/sleep.elf $(BUILD_DIR)/coreutils/which.elf \
		$(BUILD_DIR)/coreutils/ps.elf $(BUILD_DIR)/coreutils/netdiag.elf \
		$(BUILD_DIR)/coreutils/ping.elf

$(BUILD_DIR)/blank-root.img: tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024

$(BUILD_DIR)/bad-init-root.img: $(BUILD_DIR)/shell.elf $(BUILD_DIR)/probe.elf \
		$(BUILD_DIR)/populate_root tests/not_elf.txt tools/mksilliconfs.py
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ tests/not_elf.txt $(BUILD_DIR)/shell.elf \
		$(BUILD_DIR)/probe.elf

$(BUILD_DIR)/crash-root.img: $(BUILD_DIR)/init.elf $(BUILD_DIR)/crash.elf \
		$(BUILD_DIR)/probe.elf $(BUILD_DIR)/populate_root tools/mksilliconfs.py
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/crash.elf $(BUILD_DIR)/probe.elf

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

run-gui: iso $(BUILD_DIR)/root.img
	$(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) -cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 -nic none \
		-netdev user,id=net0 -device e1000,netdev=net0,id=e1000,mac=52:54:00:15:03:01 \
		-serial stdio -display gtk -no-reboot

$(BUILD_DIR)/block_test: kernel/fs/block/block.c kernel/fs/block/block_silliconfs.c \
		tests/block_test.c include/kernel/block.h include/kernel/errno.h \
		include/kernel/block_silliconfs.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude kernel/fs/block/block.c \
		kernel/fs/block/block_silliconfs.c tests/block_test.c -o $@

block-test: $(BUILD_DIR)/block_test
	$(BUILD_DIR)/block_test

phase10-test: block-test acpi-test pci-test silliconfs-test

test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/core/boot/boot.c kernel/mm/pmm.c tests/boot_test.c -o $(BUILD_DIR)/boot_test
	$(BUILD_DIR)/boot_test

elf-test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/proc/elf.c tests/elf_test.c -o $(BUILD_DIR)/elf_test
	$(BUILD_DIR)/elf_test

$(BUILD_DIR)/vmm_protection_test: tests/vmm_protection_test.c \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine $< -o $@

private-protection-test: $(BUILD_DIR)/vmm_protection_test
	$(BUILD_DIR)/vmm_protection_test

$(BUILD_DIR)/private_mapping_abi_test: tests/private_mapping_abi_test.c \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine $< -o $@

private-map-abi-test: $(BUILD_DIR)/private_mapping_abi_test
	$(BUILD_DIR)/private_mapping_abi_test

vfs-test: $(BUILD_DIR)/initramfs.cpio | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/fs/vfs/vfs.c kernel/fs/ramfs/ramfs.c kernel/fs/initramfs/initramfs.c \
		kernel/proc/fd.c kernel/tty/tty.c kernel/tty/terminal.c \
		kernel/tty/keyboard.c kernel/tty/framebuffer.c \
		kernel/drivers/input/ps2_keyboard.c tests/timer_stub.c \
		-Ithird_party tests/vfs_test.c -o $(BUILD_DIR)/vfs_test
	$(BUILD_DIR)/vfs_test $(BUILD_DIR)/initramfs.cpio

silliconfs-image: | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $(BUILD_DIR)/silliconfs.img \
		--blocks 512 --inodes 128

silliconfs-test: silliconfs-image | $(BUILD_DIR)
	python3 tests/mksilliconfs_test.py
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/fs/vfs/vfs.c kernel/fs/silliconfs/silliconfs.c kernel/proc/fd.c \
		kernel/fs/block/block.c kernel/fs/block/block_silliconfs.c \
		kernel/tty/tty.c kernel/tty/terminal.c kernel/tty/framebuffer.c \
		kernel/tty/keyboard.c kernel/drivers/input/ps2_keyboard.c \
		tests/timer_stub.c -Ithird_party \
		tests/silliconfs_test.c -o $(BUILD_DIR)/silliconfs_test
	$(BUILD_DIR)/silliconfs_test $(BUILD_DIR)/silliconfs.img

$(BUILD_DIR)/terminal_test: kernel/tty/terminal.c kernel/tty/tty.c \
		kernel/tty/keyboard.c kernel/tty/framebuffer.c \
		kernel/drivers/input/ps2_keyboard.c tests/timer_stub.c \
		tests/terminal_test.c include/kernel/terminal.h \
		kernel/tty/font.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/tty/terminal.c kernel/tty/tty.c kernel/tty/keyboard.c \
		kernel/tty/framebuffer.c kernel/drivers/input/ps2_keyboard.c \
		tests/timer_stub.c tests/terminal_test.c -o $@

terminal-test: $(BUILD_DIR)/terminal_test
	$(BUILD_DIR)/terminal_test

terminal-qemu-test: iso $(BUILD_DIR)/test-root.img
	python3 tests/terminal_qemu_test.py $(QEMU) $(OVMF_CODE) \
		$(BUILD_DIR)/opensillicon.iso $(BUILD_DIR)/test-root.img

phase11-test: terminal-test phase10-test terminal-qemu-test

user-path-test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iuser user/path.c \
		tests/user_path_test.c -o $(BUILD_DIR)/user_path_test
	$(BUILD_DIR)/user_path_test

phase12-qemu-test: iso $(BUILD_DIR)/phase12-root.img $(BUILD_DIR)/blank-root.img \
		$(BUILD_DIR)/bad-init-root.img $(BUILD_DIR)/crash-root.img
	python3 tests/phase12_qemu_test.py $(QEMU) $(OVMF_CODE) \
		$(BUILD_DIR)/opensillicon.iso $(BUILD_DIR)/phase12-root.img \
		$(BUILD_DIR)/blank-root.img $(BUILD_DIR)/bad-init-root.img \
		$(BUILD_DIR)/crash-root.img

phase12-test: user-path-test phase11-test phase12-qemu-test

$(BUILD_DIR)/pipe_test: kernel/ipc/pipe.c kernel/ipc/pipe_buffer.c kernel/proc/fd.c \
		tests/pipe_test.c include/kernel/pipe.h include/kernel/fd.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/ipc/pipe.c kernel/ipc/pipe_buffer.c kernel/proc/fd.c \
		tests/pipe_test.c -o $@

pipe-test: $(BUILD_DIR)/pipe_test
	$(BUILD_DIR)/pipe_test

$(BUILD_DIR)/signal_state_test: kernel/proc/signal_state.c \
		tests/signal_state_test.c include/kernel/signal_state.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		kernel/proc/signal_state.c tests/signal_state_test.c -o $@

signal-state-test: $(BUILD_DIR)/signal_state_test
	$(BUILD_DIR)/signal_state_test

phase13a-test: pipe-test phase12-test process-test vfs-smoke-test \
		timer-test fault-test interrupt-test vmm-protection-test acpica-check

phase13b-test: signal-state-test phase13a-test

$(BUILD_DIR)/shm_test: kernel/mm/shm.c tests/shm_test.c \
		include/kernel/shm.h include/kernel/vmm.h include/kernel/pmm.h \
		include/kernel/heap.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		kernel/mm/shm.c tests/shm_test.c -o $@

shm-test: $(BUILD_DIR)/shm_test
	$(BUILD_DIR)/shm_test

shm-failure-qemu-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/shm-failure \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SHM_ALLOC_FAILURE' iso
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/shm-failure \
		$(BUILD_DIR)/shm-failure/test-root.img
	@set +e; timeout 12s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/shm-failure/opensillicon.iso \
		-drive file=$(BUILD_DIR)/shm-failure/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/shm-failure/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon test: injected shared backing allocation failure' \
		$(BUILD_DIR)/shm-failure/serial.log
	@grep -F 'OpenSillicon: Ring 3 and shared-memory syscall tests passed' \
		$(BUILD_DIR)/shm-failure/serial.log

shm-lifecycle-qemu-test:
	$(MAKE) BUILD_DIR=build/shm-lifecycle \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SHM_LIFECYCLE' \
		iso build/shm-lifecycle/test-root.img
	@set +e; timeout 20s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom build/shm-lifecycle/opensillicon.iso \
		-drive file=build/shm-lifecycle/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/shm-lifecycle/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: shared-memory fork generations, detach, zombie reap, exec, and failed exec passed' \
		build/shm-lifecycle/serial.log
	@grep -F 'OpenSillicon: shared-memory process lifecycle passed' \
		build/shm-lifecycle/serial.log

shm-fork-failure-qemu-test:
	$(MAKE) BUILD_DIR=build/shm-fork-failure \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SHM_LIFECYCLE -DKERNEL_TEST_SHM_FORK_FAILURE' \
		iso build/shm-fork-failure/test-root.img
	@set +e; timeout 20s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom build/shm-fork-failure/opensillicon.iso \
		-drive file=build/shm-fork-failure/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/shm-fork-failure/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon test: injected shared fork mapping failure' \
		build/shm-fork-failure/serial.log
	@grep -F 'OpenSillicon: partial shared fork mapping rollback passed' \
		build/shm-fork-failure/serial.log
	@grep -F 'OpenSillicon: shared fork mapping rollback and reference cleanup passed' \
		build/shm-fork-failure/serial.log

private-map-qemu-test:
	$(MAKE) BUILD_DIR=build/private-map \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_PRIVATE_MAPPING_FAILURE' \
		iso build/private-map/test-root.img
	@set +e; timeout 20s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom build/private-map/opensillicon.iso \
		-drive file=build/private-map/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/private-map/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: private mapping zero-fill, RW/NX, exact unmap, fork isolation, exec replacement, and teardown passed' \
		build/private-map/serial.log
	@grep -F 'OpenSillicon: private mapping metadata, page, and fork rollback injection passed' \
		build/private-map/serial.log

private-map-lifecycle-qemu-test:
	$(MAKE) BUILD_DIR=build/private-map-lifecycle \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE' \
		iso build/private-map-lifecycle/test-root.img
	@set +e; timeout 30s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom build/private-map-lifecycle/opensillicon.iso \
		-drive file=build/private-map-lifecycle/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/private-map-lifecycle/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: private mapping NX instruction fault, zombie retention, and reap cleanup passed' \
		build/private-map-lifecycle/serial.log

private-map-exhaustion-qemu-test:
	$(MAKE) BUILD_DIR=build/private-map-exhaustion \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_PRIVATE_MAPPING_EXHAUSTION' \
		iso build/private-map-exhaustion/test-root.img
	@set +e; timeout 30s $(QEMU) -machine q35 -m 128M -bios $(OVMF_CODE) \
		-cdrom build/private-map-exhaustion/opensillicon.iso \
		-drive file=build/private-map-exhaustion/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/private-map-exhaustion/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: actual PMM exhaustion rollback and retry passed' \
		build/private-map-exhaustion/serial.log

private-protection-qemu-test:
	$(MAKE) BUILD_DIR=build/private-protection \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_PRIVATE_MAPPING_PROTECTION -DKERNEL_TEST_PRIVATE_MAPPING_FAILURE' \
		iso build/private-protection/test-root.img
	@set +e; timeout 15s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom build/private-protection/opensillicon.iso \
		-drive file=build/private-protection/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/private-protection/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: private protection modes, range checks, RW/NX faults, fork isolation, and teardown passed' \
		build/private-protection/serial.log

fs-base-qemu-test:
	$(MAKE) BUILD_DIR=build/fs-base \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_FS_BASE' \
		iso build/fs-base/test-root.img
	@set +e; timeout 30s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom build/fs-base/opensillicon.iso \
		-drive file=build/fs-base/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/fs-base/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon test: successful exec reset FS base' \
		build/fs-base/serial.log
	@grep -F 'OpenSillicon shell ready' build/fs-base/serial.log

fpu-context-qemu-test:
	$(MAKE) -B BUILD_DIR=build/fpu-context \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_FPU_CONTEXT' iso \
		build/fpu-context/test-root.img
	@set +e; timeout 30s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom build/fpu-context/opensillicon.iso \
	-drive file=build/fpu-context/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:build/fpu-context/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon: Ring3 FPU/SSE context, fork, preemption, syscall, and exec tests passed' \
		build/fpu-context/serial.log
	@grep -F 'OpenSillicon: successful exec restored clean FPU state' \
		build/fpu-context/serial.log

vfs-smoke-test: iso $(BUILD_DIR)/test-root.img
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
	@grep -F 'OpenSillicon: anonymous pipe fork, exec, and blocking tests passed' \
		$(BUILD_DIR)/process-serial.log
	@grep -F 'OpenSillicon: signal delivery, sigreturn, and blocked syscall ' \
		$(BUILD_DIR)/process-serial.log
	@grep -F 'OpenSillicon: shared VMM marking, clone, detach, and teardown passed' \
		$(BUILD_DIR)/process-serial.log
	@grep -F 'OpenSillicon: Ring 3 and shared-memory syscall tests passed' \
		$(BUILD_DIR)/process-serial.log
	@grep -F 'OpenSillicon: shared-memory process lifecycle passed' \
		$(BUILD_DIR)/process-serial.log
	@grep -E 'OpenSillicon: pipe blocked timer ticks [1-9][0-9]*' \
		$(BUILD_DIR)/process-serial.log

fault-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/fault \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_PAGE_FAULT' iso
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/fault $(BUILD_DIR)/fault/test-root.img
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
	@set +e; timeout 8s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(BUILD_DIR)/vmm-protection/opensillicon.iso \
		-drive file=$(BUILD_DIR)/vmm-protection/test-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(BUILD_DIR)/vmm-protection/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	@grep -F 'OpenSillicon exception: vector 14 error 3' \
		$(BUILD_DIR)/vmm-protection/serial.log

timer-test: iso $(BUILD_DIR)/test-root.img
	@set +e; timeout $(TIMER_TEST_TIMEOUT) $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
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
