#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
prefix="$root/src/build/x11/prefix"
meson_cross="$root/src/build/x11/opensillicon-cross.ini"
host_cc=${CC:-gcc}
build_triplet=$(sh "$root/src/user/thirdparty/xorg/deps/libX11/config.guess")
gcc_include=$($host_cc -print-file-name=include)
mkdir -p "$(dirname -- "$meson_cross")"
cat > "$meson_cross" <<'EOF'
[binaries]
c = 'gcc'
ar = 'ar'
strip = 'strip'
pkgconfig = 'pkg-config'

[host_machine]
system = 'none'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
needs_exe_wrapper = true
EOF
common_c='-std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone'
common_cpp="-DOPEN_SILLICON -nostdinc -I$gcc_include -I$root/src/user/libos/include -I$prefix/include -I$root/src/user/thirdparty/xorg/deps/libX11/include -I$root/src/build/x11/libX11/include -I$root/src/user/thirdparty/xorg/deps/libxtrans -idirafter /usr/include"
pkg_path="$prefix/lib/pkgconfig:$prefix/share/pkgconfig:/usr/share/pkgconfig:/usr/lib64/pkgconfig"
export PKG_CONFIG_PATH=$pkg_path PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig:/usr/share/pkgconfig:/usr/lib64/pkgconfig"

if [ "${1:-full}" = relink ]; then
    rm -f "$root/src/build/x11/twm/src/twm" "$root/src/build/x11/xeyes/xeyes" \
        "$root/src/build/x11/xterm/xterm" "$root/src/build/x11/feh/src/feh"
    meson compile -C "$root/src/build/x11/twm"
    meson compile -C "$root/src/build/x11/xeyes"
    make -C "$root/src/build/x11/xterm" -j2 \
        LIBS="-Wl,--start-group $root/src/build/libos/liblibos.a $prefix/lib/libi18n.a -lXaw7 -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau -lXpm -Wl,--end-group"
    sh "$root/src/tools/build_feh.sh" relink
    exit 0
fi

build_autotools() {
	name=$1
	shift
	source="$root/src/user/thirdparty/xorg/deps/$name"
	dir="$root/src/build/x11/$name"
	mkdir -p "$dir"
	cd "$dir"
	CPPFLAGS="$common_cpp -I$source/include" CFLAGS="$common_c" \
		"$source/configure" --build="$build_triplet" --host=x86_64-elf --prefix="$prefix" \
		--disable-shared --enable-static --disable-docs --disable-specs \
		--disable-tcp-transport "$@"
	make -j2
	make install
}

mkdir -p "$prefix"
mkdir -p "$root/src/build/x11/xcb-proto"
cd "$root/src/build/x11/xcb-proto"
CPPFLAGS="$common_cpp" CFLAGS="$common_c" \
"$root/src/user/thirdparty/xorg/deps/xcb-proto/configure" --build="$build_triplet" --host=x86_64-elf --prefix="$prefix"
make -j2
make install

ac_cv_func_explicit_bzero=no build_autotools libXau --disable-xthreads

# libxcb uses a single-thread compatibility header and local Unix sockets.
mkdir -p "$root/src/build/x11/libxcb"
cd "$root/src/build/x11/libxcb"
xcb_cpp="-DOPEN_SILLICON -nostdinc -I$root/src/user/libos/include -I$root/src/user/thirdparty/xorg/deps/libxcb/include -I$root/src/user/thirdparty/xorg/deps/libxcb/src -I$root/src/user/thirdparty/xorg/deps/libXau/include -I$prefix/include -isystem $gcc_include -idirafter /usr/include"
CPPFLAGS="$xcb_cpp" CFLAGS="$common_c" \
	"$root/src/user/thirdparty/xorg/deps/libxcb/configure" --build="$build_triplet" --host=x86_64-elf --prefix="$prefix" \
	--disable-shared --enable-static --disable-devel-docs
make -j2 ACLOCAL=true AUTOMAKE=true AUTOCONF=true AUTOHEADER=true
make install ACLOCAL=true AUTOMAKE=true AUTOCONF=true AUTOHEADER=true

mkdir -p "$root/src/build/x11/libxtrans"
cd "$root/src/build/x11/libxtrans"
CPPFLAGS="$common_cpp" CFLAGS="$common_c" \
	"$root/src/user/thirdparty/xorg/deps/libxtrans/configure" --build="$build_triplet" --host=x86_64-elf --prefix="$prefix" \
	--disable-tcp-transport
make -j2
make install

mkdir -p "$root/src/build/x11/libX11"
cd "$root/src/build/x11/libX11"
x11_cpp="$common_cpp -I$root/src/user/thirdparty/xorg/deps/libX11/src -I$root/src/user/thirdparty/xorg/deps/libxcb/include -I$root/src/user/thirdparty/xorg/deps/libxcb/src"
ac_cv_func_dlopen=no CPPFLAGS="$x11_cpp" CFLAGS="$common_c" \
	"$root/src/user/thirdparty/xorg/deps/libX11/configure" --build="$build_triplet" --host=x86_64-elf --prefix="$prefix" \
	--disable-shared --enable-static --disable-xthreads \
	--disable-thread-safety-constructor --disable-xf86bigfont
make -j2
make install
install -D -m 644 src/xlibi18n/.libs/libi18n.a "$prefix/lib/libi18n.a"

build_autotools libXext
ac_cv_func_arc4random_buf=no ac_cv_func_getentropy=no \
	build_autotools libICE
ac_cv_func_uuid_create=no build_autotools libSM
build_autotools libXt
build_autotools libXmu
build_autotools libXfixes-6.0.2
build_autotools libXi-1.8.4

mkdir -p "$root/src/build/x11/libXpm"
cd "$root/src/build/x11/libXpm"
CPPFLAGS="$common_cpp -I$root/src/user/thirdparty/xorg/deps/libXpm/include" CFLAGS="$common_c" \
	ac_cv_func_closefrom=no ac_cv_func_close_range=no \
	"$root/src/user/thirdparty/xorg/deps/libXpm/configure" --build="$build_triplet" --host=x86_64-elf --prefix="$prefix" \
	--disable-shared --enable-static --disable-docs --disable-specs
make -j2 -C src
make -C src install
install -D -m 644 xpm.pc "$prefix/lib/pkgconfig/xpm.pc"
install -D -m 644 "$root/src/user/thirdparty/xorg/deps/libXpm/include/X11/xpm.h" "$prefix/include/X11/xpm.h"

build_autotools libXaw
ln -sf libXaw7.a "$prefix/lib/libXaw.a"

# twm's Meson port links the same static libraries and the freestanding CRT.
if [ -f "$root/src/build/x11/twm/build.ninja" ]; then
	meson setup --reconfigure "$root/src/build/x11/twm" "$root/src/user/thirdparty/xorg/twm" \
		--prefix="$prefix" --buildtype=release
else
	meson setup "$root/src/build/x11/twm" "$root/src/user/thirdparty/xorg/twm" \
		--prefix="$prefix" --buildtype=release
fi
meson compile -C "$root/src/build/x11/twm"

# xterm keeps its POSIX termios/openpty path; networking and termcap selection
# are limited to the implemented local xterm subset.
mkdir -p "$root/src/build/x11/xterm"
cd "$root/src/build/x11/xterm"
xterm_cpp="-DOPEN_SILLICON -D_POSIX_C_SOURCE=200809L -DUSE_TERMIOS -DUSE_POSIX_TERMIOS -DUSE_POSIX_WAIT -DDISABLE_SETUID -nostdinc -I$gcc_include -I$root/src/user/libos/include -I$root/src/user/thirdparty/xorg/xterm -I$prefix/include -I$root/src/user/thirdparty/xorg/deps/libX11/include -I$root/src/build/x11/libX11/include -I$root/src/user/thirdparty/xorg/deps/libxtrans -idirafter /usr/include"
xterm_ld="-nostdlib -static -no-pie -L$prefix/lib -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,$root/src/user/c_linker.ld $root/src/build/libos/crt0.o $root/src/build/libos/liblibos.a $prefix/lib/libi18n.a"
CPPFLAGS="$xterm_cpp" CFLAGS="$common_c" LDFLAGS="$xterm_ld" \
	LIBS="-lXaw -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau -lXpm" \
	ac_cv_header_curses_h=no ac_cv_header_ncurses_h=no ac_cv_header_termcap_h=no \
	"$root/src/user/thirdparty/xorg/xterm/configure" --build="$build_triplet" --host=x86_64-elf --prefix="$prefix" \
	--disable-imake --disable-man-pages --disable-setuid --disable-luit \
	--disable-tek4014 --disable-wide-chars --disable-openpty --disable-regex \
	--disable-tcap-query --disable-tcap-fkeys --disable-pty-handshake \
	--with-app-defaults=/etc/X11/app-defaults
rm -f xterm
make -j2 LIBS="-Wl,--start-group $root/src/build/libos/liblibos.a $prefix/lib/libi18n.a -lXaw7 -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau -lXpm -Wl,--end-group"

# xeyes uses Xt and the X Input extension library. Keep optional Xrender and
# Present paths disabled because this server does not implement them.
mkdir -p "$root/src/build/x11/xeyes"
cd "$root/src/build/x11/xeyes"
export CFLAGS="$common_c -DOPEN_SILLICON -nostdinc -I$gcc_include -I$root/src/user/libos/include -I$prefix/include -I$root/src/user/thirdparty/xorg/deps/libX11/include -I$root/src/build/x11/libX11/include -I$root/src/user/thirdparty/xorg/deps/libxtrans -idirafter /usr/include"
export LDFLAGS="-L$prefix/lib -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,$root/src/user/c_linker.ld $root/src/build/libos/crt0.o $root/src/build/libos/liblibos.a $prefix/lib/libi18n.a"
if [ -f build.ninja ]; then
	meson setup --reconfigure . "$root/src/user/thirdparty/xorg/xeyes" \
		--cross-file "$meson_cross" \
		--prefix="$prefix" --buildtype=release -Dxrender=disabled -Dpresent=disabled
else
	meson setup . "$root/src/user/thirdparty/xorg/xeyes" \
		--cross-file "$meson_cross" --prefix="$prefix" \
		--buildtype=release -Dxrender=disabled -Dpresent=disabled
fi
meson compile -C .

sh "$root/src/tools/build_feh.sh"
