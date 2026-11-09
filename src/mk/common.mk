SHELL := /bin/sh

CC := gcc
LD := ld
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE.fd
BUILD_DIR := src/build
ISO_ROOT := $(BUILD_DIR)/iso_root
LIBOS_BUILD_DIR ?= src/build/libos
LIBOS_QEMU_BUILD_DIR ?= src/build/libos-qemu
LIBZ_BUILD_DIR ?= src/build/libz
LIBFONTENC_BUILD_DIR ?= src/build/libfontenc
LIBXFONT_BUILD_DIR ?= src/build/libxfont
TINYX_BUILD_DIR ?= src/build/tinyx/target
TINYX_SOURCE_DIR ?= src/build/tinyx/source
TINYX_BINARY := $(TINYX_BUILD_DIR)/kdrive/opensillicon/Xopensillicon
LIBOS_TEST := $(LIBOS_BUILD_DIR)/libos-test.elf
LIBOS_ARCHIVE := $(LIBOS_BUILD_DIR)/liblibos.a
LIBZ_ARCHIVE := $(LIBZ_BUILD_DIR)/libz.a
LIBFONTENC_ARCHIVE := $(LIBFONTENC_BUILD_DIR)/libfontenc.a
LIBXFONT_ARCHIVE := $(LIBXFONT_BUILD_DIR)/libXfont.a
LIBOS_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-Wall -Wextra -Werror -nostdinc -Isrc/include -Isrc/user/libos/include -Isrc/user/libos/src \
	-isystem $(shell $(CC) -print-file-name=include)

KERNEL_CFLAGS := -std=c11 -O2 -g -ffreestanding -fno-stack-protector \
	-fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel \
	-mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-Wall -Wextra -Werror -MMD -MP -Isrc/limine -Isrc/include \
	-Isrc/kernel/net/cyclonetcp/opensillicon \
	-Isrc/kernel/net/cyclonetcp -Isrc/kernel/net/cyclonetcp/common
KERNEL_LDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 \
	-T src/kernel/arch/x86_64/boot/linker.ld
KERNEL_ASFLAGS := -m64 -mno-red-zone
USER_ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone
USER_C_LDFLAGS := -nostdlib -static -no-pie -T src/user/c_linker.ld
USER_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=small \
	-Wall -Wextra -Werror -Isrc/include -Isrc/user

COREUTILS := ls cat echo pwd true false clear uname fetch head tail wc seq mkdir rm touch stat basename dirname kill sleep which ps netdiag ping poweroff reboot host ifconfig kldstat kldload kldunload pkg
COREUTILS_ELFS := $(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .elf,$(COREUTILS)))

ACPICA_ROOT := src/kernel/arch/x86_64/firmware/acpica/source
CYCLONE_TCP_ROOT := src/kernel/net/cyclonetcp
CYCLONE_COMMON_ROOT := src/kernel/net/cyclonetcp/common
CYCLONE_PORT_ROOT := src/kernel/net/cyclonetcp/opensillicon
CYCLONE_CFLAGS := $(KERNEL_CFLAGS) -I$(CYCLONE_PORT_ROOT) \
	-I$(CYCLONE_TCP_ROOT) -I$(CYCLONE_COMMON_ROOT) \
	-Wno-unused-parameter
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

$(BUILD_DIR):
	mkdir -p $@

# Shared variables are exported for nested make invocations and tool scripts.
export CC LD XORRISO QEMU OVMF_CODE BUILD_DIR ISO_ROOT
export LIBOS_BUILD_DIR LIBOS_QEMU_BUILD_DIR LIBOS_TEST LIBOS_ARCHIVE LIBOS_CFLAGS
export LIBZ_BUILD_DIR LIBZ_ARCHIVE LIBFONTENC_BUILD_DIR LIBFONTENC_ARCHIVE
export LIBXFONT_BUILD_DIR LIBXFONT_ARCHIVE
export TINYX_BUILD_DIR TINYX_SOURCE_DIR TINYX_BINARY
export KERNEL_CFLAGS KERNEL_LDFLAGS KERNEL_ASFLAGS USER_ASFLAGS
export USER_C_LDFLAGS USER_CFLAGS COREUTILS COREUTILS_ELFS
export ACPICA_ROOT ACPICA_COMPONENTS ACPICA_DIRS ACPICA_SOURCES ACPICA_OBJECTS ACPICA_CFLAGS
export CYCLONE_TCP_ROOT CYCLONE_COMMON_ROOT CYCLONE_PORT_ROOT CYCLONE_CFLAGS
