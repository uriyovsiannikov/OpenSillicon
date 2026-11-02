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

$(BUILD_DIR)/user_os.o: user/os.c user/include/os.h include/abi/syscall.h include/abi/framebuffer.h include/abi/input.h | $(BUILD_DIR)
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

LIBOS_SOURCES := syscall unistd mman string stat allocator dirent stdio socket select uio fcntl pipe pty termios time poll signal ioctl uname env math random stdlib passwd termcap regex
LIBOS_OBJECTS := $(addprefix $(LIBOS_BUILD_DIR)/,$(addsuffix .o,$(LIBOS_SOURCES)))

$(LIBOS_BUILD_DIR)/setjmp.o: user/libos/src/setjmp.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_BUILD_DIR):
	mkdir -p $@

$(LIBOS_BUILD_DIR)/%.o: user/libos/src/%.c user/libos/src/internal.h \
		include/abi/syscall.h \
		$(wildcard user/libos/include/* user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(LIBOS_BUILD_DIR)/math.o: user/libos/src/math.c \
		$(wildcard user/libos/include/* user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(filter-out -mno-sse -mno-sse2 -msoft-float,$(LIBOS_CFLAGS)) \
		-msse2 -c $< -o $@

$(LIBOS_BUILD_DIR)/stdio.o: user/libos/src/stdio.c \
		$(wildcard user/libos/include/* user/libos/include/sys/*) | $(LIBOS_BUILD_DIR)
	$(CC) $(filter-out -mno-sse -mno-sse2 -msoft-float,$(LIBOS_CFLAGS)) \
		-msse2 -c $< -o $@

$(LIBOS_BUILD_DIR)/crt0.o: user/libos/src/crt0.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_BUILD_DIR)/syscall_abi.o: user/libos/src/syscall_abi.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

LIBZ_SOURCES := adler32 crc32 inflate inffast inftrees zutil
LIBZ_OBJECTS := $(addprefix $(LIBZ_BUILD_DIR)/,$(addsuffix .o,$(LIBZ_SOURCES)))

$(LIBZ_BUILD_DIR):
	mkdir -p $@

$(LIBZ_BUILD_DIR)/%.o: user/xorg/deps/libz/%.c $(wildcard user/xorg/deps/libz/*.h) \
		$(wildcard user/libos/include/* user/libos/include/sys/*) | $(LIBZ_BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -DDYNAMIC_CRC_TABLE -Iuser/xorg/deps/libz -c $< -o $@

$(LIBZ_ARCHIVE): $(LIBZ_OBJECTS)
	ar rcs $@ $^

libz-build: $(LIBZ_ARCHIVE)

LIBFONTENC_CPPFLAGS := -nostdinc -I$(abspath user/libos/include) \
	-I$(abspath user/xorg/tinyx/include) -I$(abspath user/xorg/deps/libfontenc/include) \
	-isystem $(shell $(CC) -print-file-name=include) -idirafter /usr/include \
	-DFONT_ENCODINGS_DIRECTORY=\"/usr/share/fonts/encodings\"
LIBFONTENC_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -Wall -Wextra
LIBFONTENC_OBJECTS := $(addprefix $(LIBFONTENC_BUILD_DIR)/,fontenc.o encparse.o reallocarray.o)

$(LIBFONTENC_BUILD_DIR):
	mkdir -p $@

$(LIBFONTENC_BUILD_DIR)/%.o: user/xorg/deps/libfontenc/src/%.c \
		$(wildcard user/xorg/deps/libfontenc/src/*.h) \
		$(wildcard user/xorg/deps/libfontenc/include/X11/fonts/*.h) | $(LIBFONTENC_BUILD_DIR)
	$(CC) $(LIBFONTENC_CPPFLAGS) $(LIBFONTENC_CFLAGS) -c $< -o $@

$(LIBFONTENC_ARCHIVE): $(LIBFONTENC_OBJECTS)
	ar rcs $@ $^

libfontenc-build: $(LIBFONTENC_ARCHIVE)

LIBXFONT_CPPFLAGS := -nostdinc -I$(abspath user/libos/include) \
	-I$(abspath user/xorg/deps/libz) -I$(abspath user/xorg/tinyx/include) \
	-I$(abspath user/xorg/deps/libxfont/include) \
	-isystem $(shell $(CC) -print-file-name=include) \
	-idirafter /usr/include -DOPEN_SILLICON
LIBXFONT_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mno-mmx \
	-msse2 -mfpmath=sse -Wall -Wextra

$(LIBXFONT_BUILD_DIR):
	mkdir -p $@

$(LIBXFONT_ARCHIVE): $(LIBZ_ARCHIVE) $(LIBFONTENC_ARCHIVE) \
		user/xorg/deps/libxfont/configure user/xorg/deps/libxfont/configure.ac \
		$(wildcard user/xorg/deps/libxfont/src/*.[ch]) \
		$(wildcard user/xorg/deps/libxfont/src/*/*.[ch]) \
		$(wildcard user/xorg/deps/libxfont/include/X11/fonts/*.h) | $(LIBXFONT_BUILD_DIR)
	cd $(LIBXFONT_BUILD_DIR) && \
		ac_cv_func_readlink=no ac_cv_lib_z_inflateInit2_=yes \
		ac_cv_lib_m_hypot=yes \
		CPPFLAGS='$(LIBXFONT_CPPFLAGS)' CFLAGS='$(LIBXFONT_CFLAGS)' \
		PKG_CONFIG_PATH=/usr/lib64/pkgconfig:/usr/share/pkgconfig \
		$(abspath user/xorg/deps/libxfont/configure) --host=x86_64-elf \
		--disable-freetype --disable-fc --disable-bdfformat \
		--disable-shared --enable-static
	$(MAKE) -C $(LIBXFONT_BUILD_DIR) ACLOCAL=true AUTOMAKE=true \
		AUTOCONF=true AUTOHEADER=true
	cp $(LIBXFONT_BUILD_DIR)/src/.libs/libXfont.a $@

libxfont-build: $(LIBXFONT_ARCHIVE) $(LIBFONTENC_ARCHIVE)

TINYX_CPPFLAGS := -nostdinc -I$(abspath include) \
	-I$(abspath user/libos/include) -I$(abspath user/xorg/deps/libz) \
	-I$(abspath user/xorg/deps/libfontenc/include) -I$(abspath user/xorg/deps/libxfont/include) \
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
TINYX_SOURCES := $(shell find user/xorg/tinyx -type f ! -path '*/.git/*' -print)

.PHONY: tinyx-build
tinyx-build: $(TINYX_BINARY)

$(TINYX_BINARY): $(TINYX_SOURCES) user/Makefile $(LIBOS_ARCHIVE) $(LIBZ_ARCHIVE) \
		$(LIBFONTENC_ARCHIVE) $(LIBXFONT_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o
	mkdir -p $(TINYX_SOURCE_DIR) $(TINYX_BUILD_DIR)
	tar --exclude=.git -cf - -C user/xorg/tinyx . | tar -xf - -C $(TINYX_SOURCE_DIR)
	find $(TINYX_SOURCE_DIR) -name Makefile.am -exec touch {} +
	touch $(TINYX_SOURCE_DIR)/configure.ac
	cd $(TINYX_SOURCE_DIR) && autoreconf -fi
	cd $(TINYX_BUILD_DIR) && \
		OPEN_SILLICON_LIBS='$(TINYX_FONT_LIBS)' \
		OPEN_SILLICON_CRT0='$(abspath $(LIBOS_BUILD_DIR)/crt0.o)' \
		OPEN_SILLICON_LDFLAGS='-nostdlib -static -no-pie \
			-Wl,--build-id=none -Wl,-z,noexecstack \
			-Wl,-T,$(abspath user/c_linker.ld)' \
		CPPFLAGS='$(TINYX_CPPFLAGS)' CFLAGS='$(TINYX_CFLAGS)' \
		PKG_CONFIG_PATH='$(abspath $(LIBXFONT_BUILD_DIR))'$${PKG_CONFIG_PATH:+:"$$PKG_CONFIG_PATH"} \
		ac_cv_func_readlink=no ac_cv_func_getifaddrs=no \
		ac_cv_lib_z_inflateInit2_=yes ac_cv_lib_m_hypot=yes \
		$(abspath $(TINYX_SOURCE_DIR))/configure --host=x86_64-elf \
		--enable-kdrive --enable-xopensillicon --disable-xfbdev \
		--disable-xdmcp --disable-xdm-auth-1 --disable-xf86bigfont \
		--disable-dbe --disable-dpms --disable-screensaver \
		--disable-install-setuid --with-fontdir=/usr/share/fonts \
		--with-default-font-path=built-ins
	$(MAKE) -C $(TINYX_BUILD_DIR)
	touch $@

$(TINYX_BINARY): | $(BUILD_DIR)

$(LIBOS_ARCHIVE): $(LIBOS_OBJECTS) $(LIBOS_BUILD_DIR)/syscall_abi.o $(LIBOS_BUILD_DIR)/setjmp.o
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

$(BUILD_DIR)/startx.o: user/xorg/startx/startx.c | $(BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/startx.elf: $(BUILD_DIR)/startx.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/startx.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/root.img: $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(BUILD_DIR)/startx.elf \
		$(BUILD_DIR)/populate_root \
		$(TINYX_BINARY) \
		$(BUILD_DIR)/x11/.apps-built \
		$(COREUTILS_ELFS) \
		tools/mksilliconfs.py | $(BUILD_DIR)
	python3 tools/mksilliconfs.py $@ --blocks 32768 --inodes 1024
	$(BUILD_DIR)/populate_root $@ $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf --tinyx $(TINYX_BINARY) \
		--startx $(BUILD_DIR)/startx.elf \
		--twm $(BUILD_DIR)/x11/twm/src/twm --xterm $(BUILD_DIR)/x11/xterm/xterm --coreutils \
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

X11_APP_SOURCES := $(shell find user/xorg/deps/libXau user/xorg/deps/libxcb user/xorg/deps/libX11 user/xorg/deps/libXext \
	user/xorg/deps/libICE user/xorg/deps/libSM user/xorg/deps/libXt user/xorg/deps/libXmu user/xorg/deps/libXpm user/xorg/deps/libXaw \
	user/xorg/deps/libxtrans user/xorg/deps/xcb-proto user/xorg/twm user/xorg/xterm -type f -print 2>/dev/null)

$(BUILD_DIR)/x11/.apps-built: tools/build_x11_apps.sh $(X11_APP_SOURCES) \
		$(LIBOS_ARCHIVE) $(TINYX_BINARY) | $(BUILD_DIR)
	mkdir -p $(@D)
	sh tools/build_x11_apps.sh
	touch $@

endif
