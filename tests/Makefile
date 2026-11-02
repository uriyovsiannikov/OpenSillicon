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
$(BUILD_DIR)/initramfs.cpio: $(shell find initramfs -type f -o -type d) \
		tools/mkinitramfs.py | $(BUILD_DIR)
	python3 tools/mkinitramfs.py initramfs $@

$(LIBOS_QEMU_BUILD_DIR)/libos-root.img: $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf $(BUILD_DIR)/populate_root $(LIBOS_TEST) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf --libos $(LIBOS_TEST)

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
