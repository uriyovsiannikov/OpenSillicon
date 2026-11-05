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
.PHONY: x11-build-cache-test
x11-build-cache-test:
	python3 tests/x11_build_cache_test.py

.PHONY: framebuffer-device-test libos-math-test
.PHONY: x86-copy-test
.PHONY: ps2-mouse-test
ps2-mouse-test: $(BUILD_DIR)/ps2_mouse_test
	$<

$(BUILD_DIR)/ps2_mouse_test: tests/ps2_mouse_test.c kernel/drivers/input/ps2_keyboard.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
		-Iinclude $< -Wl,--gc-sections -o $@

x86-copy-test: $(BUILD_DIR)/x86_copy_test
	$<

$(BUILD_DIR)/x86_copy_test: tests/x86_copy_test.c include/arch/x86_64/copy.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude $< -o $@

framebuffer-device-test: $(BUILD_DIR)/framebuffer_device_test
	$<

$(BUILD_DIR)/framebuffer_device_test: tests/framebuffer_device_test.c kernel/tty/framebuffer_device.c include/abi/framebuffer.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Ilimine \
		tests/framebuffer_device_test.c kernel/tty/framebuffer_device.c -o $@

$(BUILD_DIR)/libos_math_host.o: user/libos/src/math.c tests/libos_math_namespace.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -fno-builtin -Wall -Wextra -Werror \
		-include tests/libos_math_namespace.h -c $< -o $@

$(BUILD_DIR)/libos_math_test: tests/libos_math_test.c $(BUILD_DIR)/libos_math_host.o
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror $^ -lm -o $@

libos-math-test: $(BUILD_DIR)/libos_math_test
	$<

.PHONY: libos-uio-test
$(BUILD_DIR)/libos_uio_test: tests/libos_uio_test.c user/libos/src/uio.c include/abi/syscall.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude $(filter %.c,$^) -o $@

libos-uio-test: $(BUILD_DIR)/libos_uio_test
	$<

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

.PHONY: smp-topology-test
smp-topology-test: $(BUILD_DIR)/smp_topology_test
	$(BUILD_DIR)/smp_topology_test

.PHONY: spinlock-test
spinlock-test: $(BUILD_DIR)/spinlock_test
	$(BUILD_DIR)/spinlock_test

$(BUILD_DIR)/spinlock_test: tests/spinlock_test.c include/kernel/spinlock.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread \
		-DKERNEL_SPINLOCK_HOST_TEST -Iinclude $< -o $@

.PHONY: pmm-concurrency-test
pmm-concurrency-test: $(BUILD_DIR)/pmm_concurrency_test
	$(BUILD_DIR)/pmm_concurrency_test

$(BUILD_DIR)/pmm_concurrency_test: tests/pmm_concurrency_test.c \
		kernel/mm/pmm.c include/kernel/pmm.h include/kernel/spinlock.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread \
		-DKERNEL_SPINLOCK_HOST_TEST -Iinclude -Ilimine $< \
		kernel/mm/pmm.c -o $@

.PHONY: heap-concurrency-test
heap-concurrency-test: $(BUILD_DIR)/heap_concurrency_test
	$(BUILD_DIR)/heap_concurrency_test

$(BUILD_DIR)/heap_concurrency_test: tests/heap_concurrency_test.c \
		kernel/mm/heap.c include/kernel/heap.h include/kernel/vmm.h \
		include/kernel/pmm.h include/kernel/spinlock.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread \
		-DKERNEL_SPINLOCK_HOST_TEST -Iinclude -Ilimine $< \
		kernel/mm/heap.c -o $@

$(BUILD_DIR)/smp_topology_test: tests/smp_topology_test.c \
		kernel/arch/x86_64/cpu/smp_topology.c include/kernel/smp.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Ilimine \
		tests/smp_topology_test.c kernel/arch/x86_64/cpu/smp_topology.c \
		-o $@

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
.PHONY: nano-qemu-test
.PHONY: terminal-ansi-test
terminal-ansi-test: $(BUILD_DIR)/terminal_ansi_test
	$<

$(BUILD_DIR)/terminal_ansi_test: tests/terminal_ansi_test.c kernel/tty/terminal.c include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		$(filter %.c,$^) -o $@
.PHONY: nano-libos-test
nano-libos-test: $(BUILD_DIR)/nano_libos_test
	$<

$(BUILD_DIR)/nano_string_host.o: user/libos/src/string.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -fno-builtin -ffunction-sections -fdata-sections \
		-Iuser/libos/include -Iinclude -Derrno=nano_host_errno \
		-Dstrpbrk=os_strpbrk -Dstrcasestr=os_strcasestr -c $< -o $@

$(BUILD_DIR)/nano_path_host.o: user/libos/src/path.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -fno-builtin -ffunction-sections -fdata-sections \
		-Iuser/libos/include -Iinclude -Derrno=nano_host_errno \
		-Dbasename=os_basename -Ddirname=os_dirname -c $< -o $@

$(BUILD_DIR)/nano_libos_test: tests/nano_libos_test.c $(BUILD_DIR)/nano_string_host.o $(BUILD_DIR)/nano_path_host.o
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror $^ -Wl,--gc-sections -o $@

nano-qemu-test: iso $(BUILD_DIR)/root.img
	python3 tests/nano_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)'

tinyx-qemu-test: iso $(BUILD_DIR)/root.img
	python3 tests/xorg_session_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)'

.PHONY: smp-qemu-test
.PHONY: user-context-test
user-context-test: $(BUILD_DIR)/user_context_test
	$<

$(BUILD_DIR)/user_context_test: tests/user_context_test.c tests/user_context_test.S \
		kernel/arch/x86_64/context/user_entry.c kernel/arch/x86_64/context/user_entry.S \
		include/arch/x86_64/user.h include/kernel/smp.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -pthread -Iinclude -Ilimine \
		$(filter %.c %.S,$^) -o $@

.PHONY: smp-scheduler-qemu-test
SMP_SCHEDULER_BUILD_DIR ?= $(BUILD_DIR)/smp-scheduler
SMP_SCHEDULER_EXTRA_CFLAGS ?=
SMP_SCHEDULER_QEMU_ARGS ?=
smp-scheduler-qemu-test: $(BUILD_DIR)/root.img
	$(MAKE) BUILD_DIR=$(SMP_SCHEDULER_BUILD_DIR) \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SMP_SCHEDULER $(SMP_SCHEDULER_EXTRA_CFLAGS)' iso
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(SMP_SCHEDULER_BUILD_DIR)' \
		--root-image '$(BUILD_DIR)/root.img' --scheduler-test $(SMP_SCHEDULER_QEMU_ARGS)

.PHONY: smp-scheduler-partial-qemu-test
smp-scheduler-partial-qemu-test: $(BUILD_DIR)/root.img
	$(MAKE) -o $(BUILD_DIR)/root.img smp-scheduler-qemu-test \
		SMP_SCHEDULER_BUILD_DIR=$(BUILD_DIR)/smp-scheduler-partial \
		SMP_SCHEDULER_EXTRA_CFLAGS=-DKERNEL_TEST_SMP_FAIL_INIT_LOGICAL_CPU=1 \
		SMP_SCHEDULER_QEMU_ARGS='--failed-cpu 1 --expected-online 3'

smp-qemu-test: iso $(BUILD_DIR)/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)'

.PHONY: smp-shootdown-qemu-test
smp-shootdown-qemu-test: iso $(BUILD_DIR)/root.img
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/smp-shootdown \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SMP_TLB_ROUNDS=32' \
		kernel
	mkdir -p $(BUILD_DIR)/smp-shootdown/iso_root/EFI/BOOT \
		$(BUILD_DIR)/smp-shootdown/iso_root/boot \
		$(BUILD_DIR)/smp-shootdown/iso_root/limine
	cp limine/BOOTX64.EFI $(BUILD_DIR)/smp-shootdown/iso_root/EFI/BOOT/BOOTX64.EFI
	cp limine/limine-uefi-cd.bin $(BUILD_DIR)/smp-shootdown/iso_root/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/smp-shootdown/kernel.elf $(BUILD_DIR)/smp-shootdown/iso_root/boot/kernel.elf
	cp $(BUILD_DIR)/initramfs.cpio $(BUILD_DIR)/smp-shootdown/iso_root/boot/initramfs.cpio
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n    module_path: boot():/boot/initramfs.cpio\n    module_string: initramfs\n' > $(BUILD_DIR)/smp-shootdown/iso_root/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD_DIR)/smp-shootdown/iso_root \
		-o $(BUILD_DIR)/smp-shootdown/opensillicon.iso
	cp $(BUILD_DIR)/root.img $(BUILD_DIR)/smp-shootdown/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)/smp-shootdown' \
		--cpus 4 --expected-online 4 --tlb-rounds 32

.PHONY: smp-single-qemu-test
smp-single-qemu-test: iso $(BUILD_DIR)/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)' \
		--cpus 1 --expected-online 1

.PHONY: smp-max-qemu-test
smp-max-qemu-test: iso $(BUILD_DIR)/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)' \
		--cpus 8 --expected-online 8

.PHONY: smp-over-limit-qemu-test
smp-over-limit-qemu-test: iso $(BUILD_DIR)/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)' \
		--cpus 9 --expect-unavailable

.PHONY: smp-failure-qemu-test
smp-failure-qemu-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/smp-failure \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SMP_FAIL_INIT_LOGICAL_CPU=1' \
		kernel
	mkdir -p $(BUILD_DIR)/smp-failure/iso_root/EFI/BOOT \
		$(BUILD_DIR)/smp-failure/iso_root/boot \
		$(BUILD_DIR)/smp-failure/iso_root/limine
	cp limine/BOOTX64.EFI $(BUILD_DIR)/smp-failure/iso_root/EFI/BOOT/BOOTX64.EFI
	cp limine/limine-uefi-cd.bin $(BUILD_DIR)/smp-failure/iso_root/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/smp-failure/kernel.elf $(BUILD_DIR)/smp-failure/iso_root/boot/kernel.elf
	cp $(BUILD_DIR)/initramfs.cpio $(BUILD_DIR)/smp-failure/iso_root/boot/initramfs.cpio
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n    module_path: boot():/boot/initramfs.cpio\n    module_string: initramfs\n' > $(BUILD_DIR)/smp-failure/iso_root/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD_DIR)/smp-failure/iso_root \
		-o $(BUILD_DIR)/smp-failure/opensillicon.iso
	cp $(BUILD_DIR)/root.img $(BUILD_DIR)/smp-failure/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)/smp-failure' \
		--cpus 4 --expected-online 3 --failed-cpu 1

.PHONY: smp-timeout-qemu-test
smp-timeout-qemu-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/smp-timeout \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SMP_TIMEOUT_LOGICAL_CPU=1' \
		kernel
	mkdir -p $(BUILD_DIR)/smp-timeout/iso_root/EFI/BOOT \
		$(BUILD_DIR)/smp-timeout/iso_root/boot \
		$(BUILD_DIR)/smp-timeout/iso_root/limine
	cp limine/BOOTX64.EFI $(BUILD_DIR)/smp-timeout/iso_root/EFI/BOOT/BOOTX64.EFI
	cp limine/limine-uefi-cd.bin $(BUILD_DIR)/smp-timeout/iso_root/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/smp-timeout/kernel.elf $(BUILD_DIR)/smp-timeout/iso_root/boot/kernel.elf
	cp $(BUILD_DIR)/initramfs.cpio $(BUILD_DIR)/smp-timeout/iso_root/boot/initramfs.cpio
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n    module_path: boot():/boot/initramfs.cpio\n    module_string: initramfs\n' > $(BUILD_DIR)/smp-timeout/iso_root/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD_DIR)/smp-timeout/iso_root \
		-o $(BUILD_DIR)/smp-timeout/opensillicon.iso
	cp $(BUILD_DIR)/root.img $(BUILD_DIR)/smp-timeout/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)/smp-timeout' \
		--cpus 4 --expected-online 3 --failed-cpu 1 --failed-stage 0

.PHONY: smp-exception-qemu-test
smp-exception-qemu-test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/smp-exception \
		KERNEL_CFLAGS='$(KERNEL_CFLAGS) -DKERNEL_TEST_SMP_AP_EXCEPTION_LOGICAL_CPU=1' \
		kernel
	mkdir -p $(BUILD_DIR)/smp-exception/iso_root/EFI/BOOT \
		$(BUILD_DIR)/smp-exception/iso_root/boot \
		$(BUILD_DIR)/smp-exception/iso_root/limine
	cp limine/BOOTX64.EFI $(BUILD_DIR)/smp-exception/iso_root/EFI/BOOT/BOOTX64.EFI
	cp limine/limine-uefi-cd.bin $(BUILD_DIR)/smp-exception/iso_root/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/smp-exception/kernel.elf $(BUILD_DIR)/smp-exception/iso_root/boot/kernel.elf
	cp $(BUILD_DIR)/initramfs.cpio $(BUILD_DIR)/smp-exception/iso_root/boot/initramfs.cpio
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n    module_path: boot():/boot/initramfs.cpio\n    module_string: initramfs\n' > $(BUILD_DIR)/smp-exception/iso_root/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD_DIR)/smp-exception/iso_root \
		-o $(BUILD_DIR)/smp-exception/opensillicon.iso
	cp $(BUILD_DIR)/root.img $(BUILD_DIR)/smp-exception/root.img
	python3 tests/smp_qemu_test.py --qemu '$(QEMU)' \
		--bios '$(OVMF_CODE)' --build-dir '$(BUILD_DIR)/smp-exception' \
		--cpus 4 --expected-online 3 --failed-cpu 1 --failed-stage 1

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

.PHONY: netstack-security-test
NETSTACK_TEST_CC ?= clang
netstack-security-test: $(BUILD_DIR)/netstack_security_test
	$<

$(BUILD_DIR)/netstack_security_test: tests/netstack_security_test.c kernel/net/core/netstack.c kernel/net/tcp/tcp_wire.c include/kernel/netstack.h include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(NETSTACK_TEST_CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude tests/netstack_security_test.c kernel/net/core/netstack.c kernel/net/tcp/tcp_wire.c -o $@

endif
