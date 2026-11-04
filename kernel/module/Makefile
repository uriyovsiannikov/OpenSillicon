# ET_REL modules use the kernel code model and the explicitly exported API.
MODULE_CFLAGS := $(KERNEL_CFLAGS) -g0 -fno-asynchronous-unwind-tables -fno-unwind-tables
MODULE_OBJECTS := $(BUILD_DIR)/modules/hello.ko
OBJCOPY ?= objcopy

.PHONY: modules
modules: $(MODULE_OBJECTS)

$(BUILD_DIR)/modules/%.ko: kernel/module/examples/%.c kernel/module/Makefile mk/common.mk
	mkdir -p $(dir $@)
	$(CC) $(MODULE_CFLAGS) -Iinclude -c $< -o $@
	$(OBJCOPY) --remove-section=.note.gnu.property $@

-include $(MODULE_OBJECTS:.ko=.d)

-include $(wildcard $(BUILD_DIR)/modules/*.d)
