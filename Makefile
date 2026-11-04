# Top-level build graph. Subsystem rules are defined in their owning Makefiles.
.DEFAULT_GOAL := all
OPEN_SILLICON_ROOT := $(CURDIR)
include mk/common.mk
include kernel/Makefile
include kernel/module/Makefile
include user/Makefile
include tests/Makefile

# Keep cleanup serialized with any other explicitly requested goal.
ifneq ($(filter clean,$(MAKECMDGOALS)),)
.NOTPARALLEL:
endif

run: iso $(BUILD_DIR)/root.img
	$(QEMU) -machine q35 -m 256M -bios $(OVMF_CODE) -cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 -serial stdio -display none

run-gui:
	$(QEMU) -machine q35 -m 256M -smp 8 -bios $(OVMF_CODE) -cdrom $(BUILD_DIR)/opensillicon.iso \
		-drive file=$(BUILD_DIR)/root.img,if=none,id=rootdisk,format=raw \
		-device ide-hd,drive=rootdisk,bus=ide.0 -nic none \
		-netdev user,id=net0,net=10.15.3.0/24,host=10.15.3.2,dhcpstart=10.15.3.15 \
		-device e1000,netdev=net0,id=e1000,mac=52:54:00:15:03:01 \
		-serial stdio -display gtk

clean:
	rm -rf $(BUILD_DIR)
