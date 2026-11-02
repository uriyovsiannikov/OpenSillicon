SHELL := /bin/sh

CC := gcc
LD := ld
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE.fd
BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
TIMER_TEST_TIMEOUT ?= 15s
LIBOS_BUILD_DIR ?= build/libos
LIBOS_QEMU_BUILD_DIR ?= build/libos-qemu
LIBOS_TEST := $(LIBOS_BUILD_DIR)/libos-test.elf
LIBOS_ARCHIVE := $(LIBOS_BUILD_DIR)/liblibos.a
LIBOS_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-Wall -Wextra -Werror -nostdinc -Iuser/libos/include -Iuser/libos/src \
	-isystem $(shell $(CC) -print-file-name=include)

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
	fpu-context-qemu-test libos-build libos-inspect libos-qemu-test


UDP_SOCKET_TEST_SOURCE ?= tests/user/udp_socket_test.c
UDP_SOCKET_TEST_CFLAGS ?=
$(BUILD_DIR):
	mkdir -p $@


# Shared variables are exported for nested make invocations and tool scripts.
export CC LD XORRISO QEMU OVMF_CODE BUILD_DIR ISO_ROOT TIMER_TEST_TIMEOUT
export LIBOS_BUILD_DIR LIBOS_QEMU_BUILD_DIR LIBOS_TEST LIBOS_ARCHIVE LIBOS_CFLAGS
export KERNEL_CFLAGS KERNEL_LDFLAGS KERNEL_ASFLAGS USER_ASFLAGS USER_LDFLAGS
export USER_C_LDFLAGS USER_CFLAGS COREUTILS COREUTILS_ELFS
export ACPICA_ROOT ACPICA_COMPONENTS ACPICA_DIRS ACPICA_SOURCES ACPICA_OBJECTS ACPICA_CFLAGS
export PRIVATE_MAPPING_TEST_OBJECTS UDP_SOCKET_TEST_OBJECTS UDP_SOCKET_TEST_SOURCE UDP_SOCKET_TEST_CFLAGS
export LIBOS_SOURCES LIBOS_OBJECTS
