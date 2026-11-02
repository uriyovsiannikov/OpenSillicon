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
	+$(MAKE) -C $(ROOT_DIR) -f Makefile $(if $(strip $(MAKECMDGOALS)),$(MAKECMDGOALS),libos-build)
else
$(BUILD_DIR)/user_start.o: user/start.S user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_os.o: user/os.c user/include/os.h include/abi/syscall.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_path.o: user/path.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_init.o: user/init/init.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_shell.o: user/shell/shell.c user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/%.o: user/coreutils/%.c user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/fetch.o: user/fetch/fetch.c user/include/os.h | $(BUILD_DIR)
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

$(BUILD_DIR)/populate_root: tools/populate_root.c kernel/fs/vfs/vfs.c \
		kernel/fs/silliconfs/silliconfs.c kernel/proc/fd.c include/kernel/vfs.h \
		include/kernel/silliconfs.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Ilimine \
		tools/populate_root.c kernel/fs/vfs/vfs.c kernel/fs/silliconfs/silliconfs.c \
		kernel/proc/fd.c -o $@

LIBOS_SOURCES := syscall unistd mman string stat allocator dirent stdio
LIBOS_OBJECTS := $(addprefix $(LIBOS_BUILD_DIR)/,$(addsuffix .o,$(LIBOS_SOURCES)))

$(LIBOS_BUILD_DIR):
	mkdir -p $@

$(LIBOS_BUILD_DIR)/%.o: user/libos/src/%.c user/libos/src/internal.h \
		include/abi/syscall.h \
		$(wildcard user/libos/include/* user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(LIBOS_BUILD_DIR)/crt0.o: user/libos/src/crt0.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_BUILD_DIR)/syscall_abi.o: user/libos/src/syscall_abi.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_ARCHIVE): $(LIBOS_OBJECTS) $(LIBOS_BUILD_DIR)/syscall_abi.o
	ar rcs $@ $^

$(LIBOS_BUILD_DIR)/libos-test.o: tests/libos_test.c \
		$(wildcard user/libos/include/* user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(LIBOS_TEST): $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_BUILD_DIR)/libos-test.o $(LIBOS_ARCHIVE) user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(LIBOS_BUILD_DIR)/libos-test.o $(LIBOS_ARCHIVE) -o $@

libos-build: $(LIBOS_TEST)

libos-inspect: libos-build
	readelf -h -l -S -r -d $(LIBOS_TEST)
	! readelf -l $(LIBOS_TEST) | grep -E 'INTERP|DYNAMIC'
	! nm -u $(LIBOS_TEST) | grep .

$(BUILD_DIR)/root.img: $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(BUILD_DIR)/populate_root \
		$(COREUTILS_ELFS) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf --coreutils \
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

endif
