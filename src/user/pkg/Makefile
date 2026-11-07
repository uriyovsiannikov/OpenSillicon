# Included from the root graph. The host tools and native client share the
# archive parser and SHA-256 implementation; the server has no guest runtime.
PKG_DIR := src/user/pkg
PKG_COMMON := $(PKG_DIR)/format.c $(PKG_DIR)/sha256.c $(PKG_DIR)/io.c
PKG_CLIENT := $(PKG_DIR)/pkg.c $(PKG_DIR)/http.c $(PKG_COMMON)
PKG_HEADERS := $(wildcard $(PKG_DIR)/*.h)
PKG_OBJECTS := $(patsubst $(PKG_DIR)/%.c,$(BUILD_DIR)/pkg/%.o,$(PKG_CLIENT))
HOST_CC ?= cc
PKG_HOST_FLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror \
	-D_POSIX_C_SOURCE=200809L -DPKG_HOST -I$(PKG_DIR)

$(BUILD_DIR)/pkg/%.o: $(PKG_DIR)/%.c $(PKG_HEADERS) src/mk/common.mk
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -MMD -MP -I$(PKG_DIR) -c $< -o $@

$(BUILD_DIR)/coreutils/pkg.elf: $(PKG_OBJECTS) $(LIBOS_BUILD_DIR)/crt0.o \
		$(LIBOS_ARCHIVE) src/user/c_linker.ld
	mkdir -p $(@D)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(PKG_OBJECTS) $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/pkg/pkg-host: $(PKG_CLIENT) $(PKG_HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(PKG_HOST_FLAGS) $(PKG_CLIENT) -o $@

$(BUILD_DIR)/pkg/pkg-build: src/tools/pkg/build.c $(PKG_COMMON) $(PKG_HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(PKG_HOST_FLAGS) $< $(PKG_COMMON) -o $@

$(BUILD_DIR)/pkg/pkg-server: src/tools/pkg/server/server.c $(PKG_COMMON) $(PKG_HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(PKG_HOST_FLAGS) $< $(PKG_COMMON) -o $@

$(BUILD_DIR)/pkg/hello-stage/bin/hello: src/tools/pkg/repo/_src/hello/hello.c \
		$(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $(BUILD_DIR)/pkg/hello.o
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/pkg/hello.o $(LIBOS_ARCHIVE) -o $@

src/tools/pkg/repo/_pkges/hello-1.0.0.spkg: $(BUILD_DIR)/pkg/hello-stage/bin/hello \
		$(BUILD_DIR)/pkg/pkg-build
	$(BUILD_DIR)/pkg/pkg-build hello 1.0.0 $(BUILD_DIR)/pkg/hello-stage $@ \
		'Hello program for OpenSillicon'

$(BUILD_DIR)/pkg/pkg-vfs-test: src/tests/pkg_vfs_test.c src/kernel/fs/vfs/vfs.c \
		src/kernel/fs/ramfs/ramfs.c src/kernel/proc/fd.c \
		src/include/kernel/vfs.h src/include/kernel/fd.h
	mkdir -p $(@D)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections \
		-fdata-sections -Isrc/include -Isrc/limine $(filter %.c,$^) \
		-Wl,--gc-sections -o $@

-include $(PKG_OBJECTS:.o=.d)
