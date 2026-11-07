# ET_REL modules use the kernel code model and the explicitly exported API.
MODULE_CFLAGS := $(KERNEL_CFLAGS) -g0 -fno-asynchronous-unwind-tables -fno-unwind-tables
MODULE_OBJECTS := $(BUILD_DIR)/modules/e1000.ko
OBJCOPY ?= objcopy

modules: $(MODULE_OBJECTS)

$(BUILD_DIR)/modules/%.ko: src/kernel/module/examples/%.c src/kernel/module/Makefile src/mk/common.mk
	mkdir -p $(dir $@)
	$(CC) $(MODULE_CFLAGS) -Iinclude -c $< -o $@
	$(OBJCOPY) --remove-section=.note.gnu.property $@

E1000_MODULE_OBJECTS := $(addprefix $(BUILD_DIR)/modules/,e1000.o e1000_profile.o e1000_ring.o e1000_reset.o e1000_entry.o)

$(BUILD_DIR)/modules/e1000_entry.o: src/kernel/drivers/net/e1000/module.c src/kernel/module/Makefile src/mk/common.mk
	mkdir -p $(dir $@)
	$(CC) $(MODULE_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/modules/%.o: src/kernel/drivers/net/e1000/%.c src/kernel/module/Makefile src/mk/common.mk
	mkdir -p $(dir $@)
	$(CC) $(MODULE_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/modules/e1000.ko: $(E1000_MODULE_OBJECTS) src/kernel/module/Makefile
	$(LD) -r $(E1000_MODULE_OBJECTS) -o $@
	$(OBJCOPY) --remove-section=.note.gnu.property $@

-include $(MODULE_OBJECTS:.ko=.d)

-include $(wildcard $(BUILD_DIR)/modules/*.d)
