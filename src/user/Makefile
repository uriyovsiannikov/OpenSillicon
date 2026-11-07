# Internal file rules; included from the root build graph.
$(BUILD_DIR)/user_start.o: src/user/start.S src/user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_os.o: src/user/include/os.c src/user/include/os.h src/include/abi/syscall.h src/include/abi/framebuffer.h src/include/abi/input.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_path.o: src/user/path.c src/user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_init.o: src/user/init/init.c src/user/init/logo_pixels.h \
		src/user/include/os.h other/logo.png | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_shell.o: src/user/shell/shell.c src/user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/%.o: src/user/coreutils/%.c src/user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/fetch.o: src/user/fetch/fetch.c src/user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/%.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/%.o src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/$*.o -o $@

$(BUILD_DIR)/coreutils/netdiag.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/netdiag.o \
		src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/netdiag.o -o $@

$(BUILD_DIR)/coreutils/netdiag.o: src/user/coreutils/netdiag.c \
		src/user/coreutils/netdiag_args.h src/user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -Isrc/user -c $< -o $@

$(BUILD_DIR)/netdiag_args.o: src/user/coreutils/netdiag_args.c \
		src/user/coreutils/netdiag_args.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/host.o: src/user/coreutils/host.c src/user/include/os.h \
		src/user/libos/include/netdb.h src/user/libos/include/netinet/in.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -Isrc/user/libos/include -c $< -o $@

$(BUILD_DIR)/coreutils/host.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/host.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/host.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/coreutils/ping.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/ping.o \
		src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/netdiag_args.o $(BUILD_DIR)/coreutils/ping.o -o $@

$(BUILD_DIR)/coreutils/ping.o: src/user/coreutils/ping.c \
		src/user/coreutils/netdiag_args.h src/user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -Isrc/user -c $< -o $@

$(BUILD_DIR)/init.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_init.o src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_init.o -o $@

$(BUILD_DIR)/shell.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shell.o src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shell.o -o $@

$(BUILD_DIR)/populate_root: src/tools/populate_root.c src/kernel/fs/vfs/vfs.c \
		src/kernel/fs/silliconfs/silliconfs.c src/kernel/proc/fd.c src/include/kernel/vfs.h \
		src/include/kernel/silliconfs.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Isrc/include -Isrc/limine \
		src/tools/populate_root.c src/kernel/fs/vfs/vfs.c src/kernel/fs/silliconfs/silliconfs.c \
		src/kernel/proc/fd.c -o $@

LIBOS_SOURCES := syscall unistd mman string stat allocator dirent stdio socket select uio fcntl pipe pty termios time poll signal ioctl uname env math random stdlib passwd termcap regex path getopt getopt_long
LIBOS_OBJECTS := $(addprefix $(LIBOS_BUILD_DIR)/,$(addsuffix .o,$(LIBOS_SOURCES)))

$(LIBOS_BUILD_DIR)/setjmp.o: src/user/libos/src/setjmp.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_BUILD_DIR):
	mkdir -p $@

$(LIBOS_BUILD_DIR)/%.o: src/user/libos/src/%.c src/user/libos/src/internal.h \
		src/include/abi/syscall.h \
		$(wildcard src/user/libos/include/* src/user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(LIBOS_BUILD_DIR)/math.o: src/user/libos/src/math.c \
		$(wildcard src/user/libos/include/* src/user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(filter-out -mno-sse -mno-sse2 -msoft-float,$(LIBOS_CFLAGS)) \
		-msse2 -c $< -o $@

$(LIBOS_BUILD_DIR)/stdio.o: src/user/libos/src/stdio.c \
		$(wildcard src/user/libos/include/* src/user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(filter-out -mno-sse -mno-sse2 -msoft-float,$(LIBOS_CFLAGS)) \
		-msse2 -c $< -o $@

$(LIBOS_BUILD_DIR)/crt0.o: src/user/libos/src/crt0.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_BUILD_DIR)/syscall_abi.o: src/user/libos/src/syscall_abi.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

LIBZ_SOURCES := adler32 crc32 deflate trees inflate inffast inftrees zutil
LIBZ_OBJECTS := $(addprefix $(LIBZ_BUILD_DIR)/,$(addsuffix .o,$(LIBZ_SOURCES)))

$(LIBZ_BUILD_DIR):
	mkdir -p $@

$(LIBZ_BUILD_DIR)/%.o: src/user/thirdparty/xorg/deps/libz/%.c $(wildcard src/user/thirdparty/xorg/deps/libz/*.h) \
		$(wildcard src/user/libos/include/* src/user/libos/include/sys/*) | $(LIBZ_BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -DDYNAMIC_CRC_TABLE \
		-Isrc/user/thirdparty/xorg/deps/libz -c $< -o $@

$(LIBZ_ARCHIVE): $(LIBZ_OBJECTS)
	ar rcs $@ $^

LIBFONTENC_CPPFLAGS := -nostdinc -I$(abspath src/user/libos/include) \
	-I$(abspath src/user/thirdparty/xorg/tinyx/include) -I$(abspath src/user/thirdparty/xorg/deps/libfontenc/include) \
	-isystem $(shell $(CC) -print-file-name=include) -idirafter /usr/include \
	-DFONT_ENCODINGS_DIRECTORY=\"/usr/share/fonts/encodings\"
LIBFONTENC_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -Wall -Wextra
LIBFONTENC_OBJECTS := $(addprefix $(LIBFONTENC_BUILD_DIR)/,fontenc.o encparse.o reallocarray.o)

$(LIBFONTENC_BUILD_DIR):
	mkdir -p $@

$(LIBFONTENC_BUILD_DIR)/%.o: src/user/thirdparty/xorg/deps/libfontenc/src/%.c \
		$(wildcard src/user/thirdparty/xorg/deps/libfontenc/src/*.h) \
		$(wildcard src/user/thirdparty/xorg/deps/libfontenc/include/X11/fonts/*.h) | $(LIBFONTENC_BUILD_DIR)
	$(CC) $(LIBFONTENC_CPPFLAGS) $(LIBFONTENC_CFLAGS) -c $< -o $@

$(LIBFONTENC_ARCHIVE): $(LIBFONTENC_OBJECTS)
	ar rcs $@ $^

LIBXFONT_CPPFLAGS := -nostdinc -I$(abspath src/user/libos/include) \
	-I$(abspath src/user/thirdparty/xorg/deps/libz) -I$(abspath src/user/thirdparty/xorg/tinyx/include) \
	-I$(abspath src/user/thirdparty/xorg/deps/libxfont/include) \
	-isystem $(shell $(CC) -print-file-name=include) \
	-idirafter /usr/include -DOPEN_SILLICON
LIBXFONT_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mno-mmx \
	-msse2 -mfpmath=sse -Wall -Wextra

$(LIBXFONT_BUILD_DIR):
	mkdir -p $@

$(LIBXFONT_ARCHIVE): $(LIBZ_ARCHIVE) $(LIBFONTENC_ARCHIVE) \
		src/user/thirdparty/xorg/deps/libxfont/configure src/user/thirdparty/xorg/deps/libxfont/configure.ac \
		$(wildcard src/user/thirdparty/xorg/deps/libxfont/src/*.[ch]) \
		$(wildcard src/user/thirdparty/xorg/deps/libxfont/src/*/*.[ch]) \
		$(wildcard src/user/thirdparty/xorg/deps/libxfont/include/X11/fonts/*.h) | $(LIBXFONT_BUILD_DIR)
	cd $(LIBXFONT_BUILD_DIR) && \
		ac_cv_func_readlink=no ac_cv_lib_z_inflateInit2_=yes \
		ac_cv_lib_m_hypot=yes \
		CPPFLAGS='$(LIBXFONT_CPPFLAGS)' CFLAGS='$(LIBXFONT_CFLAGS)' \
		PKG_CONFIG_PATH=/usr/lib64/pkgconfig:/usr/share/pkgconfig \
		$(abspath src/user/thirdparty/xorg/deps/libxfont/configure) --host=x86_64-elf \
		--disable-freetype --disable-fc --enable-bdfformat \
		--disable-shared --enable-static
	$(MAKE) -C $(LIBXFONT_BUILD_DIR) ACLOCAL=true AUTOMAKE=true \
		AUTOCONF=true AUTOHEADER=true
	cp $(LIBXFONT_BUILD_DIR)/src/.libs/libXfont.a $@

TINYX_CPPFLAGS := -nostdinc -I$(abspath src/include) \
	-I$(abspath src/user/libos/include) -I$(abspath src/user/thirdparty/xorg/deps/libz) \
	-I$(abspath src/user/thirdparty/xorg/deps/libfontenc/include) -I$(abspath src/user/thirdparty/xorg/deps/libxfont/include) \
	-I$(abspath $(TINYX_SOURCE_DIR)/include) \
	-I$(abspath $(TINYX_BUILD_DIR)/include) \
	-isystem $(shell $(CC) -print-file-name=include) -idirafter /usr/include \
	-DOPEN_SILLICON
TINYX_CFLAGS := -std=gnu11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone \
	-msse2 -mfpmath=sse -Wno-unused-parameter
TINYX_FONT_LIBS := -Wl,--start-group $(abspath $(LIBXFONT_ARCHIVE)) \
	$(abspath $(LIBFONTENC_ARCHIVE)) $(abspath $(LIBZ_ARCHIVE)) \
	$(abspath $(LIBOS_ARCHIVE)) -Wl,--end-group
TINYX_SOURCES := $(shell find src/user/thirdparty/xorg/tinyx -type f ! -path '*/.git/*' -print)

# Configure only when sources or the build configuration change. Runtime
# archives are link inputs; changing libc must not regenerate the source tree.
TINYX_CONFIG_STAMP := $(TINYX_BUILD_DIR)/.opensillicon-configured
$(TINYX_CONFIG_STAMP): $(TINYX_SOURCES) src/user/Makefile src/mk/common.mk \
        $(wildcard src/user/libos/include/*.h src/user/libos/include/sys/*.h) \
        $(wildcard src/include/abi/*.h) src/user/c_linker.ld | $(LIBXFONT_ARCHIVE) \
        $(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o
	mkdir -p $(TINYX_SOURCE_DIR) $(TINYX_BUILD_DIR)
	tar --exclude=.git -cf - -C src/user/thirdparty/xorg/tinyx . | tar -xf - -C $(TINYX_SOURCE_DIR)
	find $(TINYX_SOURCE_DIR) -name Makefile.am -exec touch {} +
	touch $(TINYX_SOURCE_DIR)/configure.ac
	cd $(TINYX_SOURCE_DIR) && autoreconf -fi
	cd $(TINYX_BUILD_DIR) && \
		OPEN_SILLICON_LIBS='$(TINYX_FONT_LIBS)' \
		OPEN_SILLICON_CRT0='$(abspath $(LIBOS_BUILD_DIR)/crt0.o)' \
		OPEN_SILLICON_LDFLAGS='-nostdlib -static -no-pie \
			-Wl,--build-id=none -Wl,-z,noexecstack \
			-Wl,-T,$(abspath src/user/c_linker.ld)' \
		CPPFLAGS='$(TINYX_CPPFLAGS)' CFLAGS='$(TINYX_CFLAGS)' \
		PKG_CONFIG_PATH='$(abspath $(LIBXFONT_BUILD_DIR))'$${PKG_CONFIG_PATH:+:"$$PKG_CONFIG_PATH"} \
		ac_cv_func_readlink=no \
		ac_cv_lib_z_inflateInit2_=yes ac_cv_lib_m_hypot=yes \
		$(abspath $(TINYX_SOURCE_DIR))/configure --host=x86_64-elf \
		--enable-kdrive --enable-xopensillicon \
				--with-fontdir=/usr/share/fonts \
		--with-default-font-path=built-ins,/usr/share/fonts/misc
	touch $@

$(TINYX_BINARY): $(TINYX_CONFIG_STAMP) $(LIBOS_ARCHIVE) $(LIBZ_ARCHIVE) \
        $(LIBFONTENC_ARCHIVE) $(LIBXFONT_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o
	$(MAKE) -C $(TINYX_BUILD_DIR)
	touch $@

$(TINYX_BINARY): | $(BUILD_DIR)

$(LIBOS_ARCHIVE): $(LIBOS_OBJECTS) $(LIBOS_BUILD_DIR)/syscall_abi.o $(LIBOS_BUILD_DIR)/setjmp.o
	ar rcs $@ $^

$(BUILD_DIR)/startx.o: src/user/thirdparty/xorg/startx/startx.c | $(BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/startx.elf: $(BUILD_DIR)/startx.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/startx.o $(LIBOS_ARCHIVE) -o $@

NANO_SOURCES := $(shell find src/user/thirdparty/nano/src -type f ! -path '*/.git/*'; find src/user/thirdparty/nano -maxdepth 1 -type f ! -path '*/.git/*')

$(BUILD_DIR)/nano/nano: $(NANO_SOURCES) src/tools/build_nano.sh $(LIBOS_ARCHIVE) \
		$(LIBOS_BUILD_DIR)/crt0.o src/user/c_linker.ld
	sh src/tools/build_nano.sh

.PHONY: $(BUILD_DIR)/x11/.apps-ready
$(BUILD_DIR)/x11/.apps-ready: $(TINYX_BINARY) $(LIBOS_ARCHIVE) $(LIBZ_ARCHIVE) \
		$(LIBFONTENC_ARCHIVE) $(LIBXFONT_ARCHIVE)
	sh src/tools/build_x11_if_needed.sh

.PHONY: $(BUILD_DIR)/dillo/.ready
$(BUILD_DIR)/dillo/.ready: $(BUILD_DIR)/x11/.apps-ready $(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o
	sh src/tools/build_dillo.sh

$(BUILD_DIR)/root.img: $(BUILD_DIR)/x11/.apps-ready $(BUILD_DIR)/dillo/.ready $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(MODULE_OBJECTS) \
		$(BUILD_DIR)/nano/nano \
		$(BUILD_DIR)/startx.elf \
		$(BUILD_DIR)/populate_root \
		other/img1.jpg \
		src/user/thirdparty/xorg/fonts/gallant.bdf src/user/thirdparty/xorg/fonts/fonts.dir \
		src/user/thirdparty/xorg/startx/system.twmrc \
		$(TINYX_BINARY) \
		$(COREUTILS_ELFS) \
		src/tools/mksilliconfs.py | $(BUILD_DIR)
	@set -eu; image_tmp=$$(mktemp '$@.tmp.XXXXXX'); \
	trap 'rm -f "$$image_tmp"' EXIT HUP INT TERM; \
	python3 src/tools/mksilliconfs.py "$$image_tmp" --blocks 32768 --inodes 1024; \
	$(BUILD_DIR)/populate_root "$$image_tmp" $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf --tinyx $(TINYX_BINARY) \
		--startx $(BUILD_DIR)/startx.elf \
		--twm $(BUILD_DIR)/x11/twm/src/twm --xterm $(BUILD_DIR)/x11/xterm/xterm \
		--xeyes $(BUILD_DIR)/x11/xeyes/xeyes --feh $(BUILD_DIR)/x11/feh/src/feh \
		--nano $(BUILD_DIR)/nano/nano --dillo $(BUILD_DIR)/dillo/target/src/dillo \
		--module $(BUILD_DIR)/modules/e1000.ko --coreutils \
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
		$(BUILD_DIR)/coreutils/ping.elf \
		$(BUILD_DIR)/coreutils/poweroff.elf $(BUILD_DIR)/coreutils/reboot.elf \
		$(BUILD_DIR)/coreutils/host.elf $(BUILD_DIR)/coreutils/ifconfig.elf \
		$(BUILD_DIR)/coreutils/kldstat.elf $(BUILD_DIR)/coreutils/kldload.elf \
		$(BUILD_DIR)/coreutils/kldunload.elf $(BUILD_DIR)/coreutils/pkg.elf; \
	mv -f "$$image_tmp" '$@'
