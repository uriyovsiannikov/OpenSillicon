# Direct subsystem invocation re-enters the root graph so all source paths and
# cross-subsystem artifact dependencies have one canonical interpretation.
ifndef OPEN_SILLICON_ROOT
ROOT_DIR := $(abspath $(CURDIR)/..)
.DEFAULT_GOAL := __dispatch
.PHONY: __dispatch $(MAKECMDGOALS)
ifneq ($(strip $(MAKECMDGOALS)),)
$(MAKECMDGOALS): __dispatch
endif
__dispatch:
	+$(MAKE) -C $(ROOT_DIR) -f Makefile $(if $(strip $(MAKECMDGOALS)),$(MAKECMDGOALS),libos-qemu-test)
else
$(BUILD_DIR)/devfs_procfs_test: tests/devfs_procfs_test.c \
		kernel/fs/vfs/vfs.c kernel/fs/ramfs/ramfs.c \
		kernel/fs/devfs/devfs.c kernel/fs/procfs/procfs.c \
		kernel/proc/fd.c include/kernel/vfs.h include/kernel/devfs.h \
		include/kernel/procfs.h include/kernel/process.h include/kernel/pmm.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Ilimine \
		tests/devfs_procfs_test.c kernel/fs/vfs/vfs.c \
		kernel/fs/ramfs/ramfs.c kernel/fs/devfs/devfs.c \
		kernel/fs/procfs/procfs.c kernel/proc/fd.c -o $@

.PHONY: devfs-procfs-test
.PHONY: pty-test
pty-test: $(BUILD_DIR)/pty_test
	$(BUILD_DIR)/pty_test

$(BUILD_DIR)/pty_test: tests/pty_test.c kernel/tty/pty.c kernel/proc/fd.c kernel/ipc/pipe_buffer.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
		-Iinclude -Ilimine $^ -Wl,--gc-sections -o $@

devfs-procfs-test: $(BUILD_DIR)/devfs_procfs_test
	$(BUILD_DIR)/devfs_procfs_test

$(BUILD_DIR)/initramfs.cpio: $(shell find initramfs -type f -o -type d) \
		tools/mkinitramfs.py | $(BUILD_DIR)
	python3 tools/mkinitramfs.py initramfs $@

$(LIBOS_QEMU_BUILD_DIR)/libos-root.img: $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/populate_root $(LIBOS_TEST) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf --libos $(LIBOS_TEST)

.PHONY: tinyx-qemu-test
tinyx-qemu-test: iso $(BUILD_DIR)/root.img
	python3 tests/tinyx_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)'

libos-qemu-test: libos-build
	CCACHE_DISABLE=1 $(MAKE) BUILD_DIR=$(LIBOS_QEMU_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_LIBOS' \
		iso $(LIBOS_QEMU_BUILD_DIR)/libos-root.img
	@set +e; timeout 45s $(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) \
		-cdrom $(LIBOS_QEMU_BUILD_DIR)/opensillicon.iso \
		-drive file=$(LIBOS_QEMU_BUILD_DIR)/libos-root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 \
		-serial file:$(LIBOS_QEMU_BUILD_DIR)/serial.log -display none \
		-no-reboot -no-shutdown; status=$$?; test $$status -eq 124
	grep -F 'OpenSillicon: libos fixture exited with status zero' \
		$(LIBOS_QEMU_BUILD_DIR)/serial.log
endif
