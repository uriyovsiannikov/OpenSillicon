#!/bin/sh
set -eu
r=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
root=$r
. "$root/src/tools/builders/build_helpers.sh"
cc=${CC:-gcc}
cxx_compiler=${CXX:-g++}
p=$r/src/build/x11/prefix
mkdir -p "$r/src/build/dillo"
exec 9>"$r/src/build/dillo/.build.lock"
flock 9
stamp="$r/src/build/dillo/.inputs"
tmp="$stamp.tmp.$$"
trap 'rm -f "$tmp"' EXIT HUP INT TERM
{
    find "$r/src/user/thirdparty/dillo" "$r/src/user/libos/include" -type f -print0 |
        sort -z | xargs -0 sha256sum
    sha256sum "$0" "$r/src/build/libos/liblibos.a" "$r/src/build/libos/crt0.o" \
        "$p/lib/libX11.a" "$p/lib/libxcb.a" "$p/lib/libXau.a" \
        "$p/lib/libpng.a" "$p/lib/libjpeg.a" "$r/src/build/libz/libz.a"
    "$cc" --version
    "$cxx_compiler" --version
} | sha256sum > "$tmp"
if [ -x "$r/src/build/dillo/target/src/dillo" ] &&
    [ -f "$stamp" ] && cmp -s "$tmp" "$stamp"; then
    echo "Dillo inputs unchanged; reusing browser"
    exit 0
fi
f="-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -nostdinc -isystem $("$cc" -print-file-name=include) -I$r/src/user/libos/include -I$p/include -idirafter /usr/include -include $r/src/user/thirdparty/dillo/opensillicon/compat.h -DOPEN_SILLICON"
rm -f "$r/src/build/dillo/fltk/CMakeCache.txt"
build_component "LIB FLTK configure" cmake -S "$r/src/user/thirdparty/dillo/deps/fltk" -B "$r/src/build/dillo/fltk" \
    -DCMAKE_SYSTEM_NAME=Generic -DHAVE_DLFCN_H=0 -DHAVE_DLSYM=0 -DHAVE_STRLCPY=0 -DHAVE_STRLCAT=0 -DUSE_X11=1 -DHAVE_GL=0 -DCMAKE_SYSTEM_PROCESSOR=x86_64 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx_compiler" -DCMAKE_C_FLAGS="$f" -DCMAKE_CXX_FLAGS="$f -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit" \
    -DCMAKE_INSTALL_PREFIX="$r/src/build/dillo/prefix" \
    -DX11_X11_LIB="$p/lib/libX11.a" -DX11_X11_INCLUDE_PATH=/usr/include \
    -DZLIB_LIBRARY="$r/src/build/libz/libz.a" -DZLIB_INCLUDE_DIR="$r/src/user/thirdparty/xorg/deps/libz" \
    -DPNG_LIBRARY="$p/lib/libpng.a" -DPNG_PNG_INCLUDE_DIR="$p/include" \
    -DJPEG_LIBRARY="$p/lib/libjpeg.a" -DJPEG_INCLUDE_DIR="$p/include" \
    -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_EXAMPLES=OFF -DOPTION_USE_GL=OFF -DOPTION_LARGE_FILE=OFF \
    -DOPTION_USE_THREADS=OFF -DOPTION_USE_XINERAMA=OFF -DOPTION_USE_XFIXES=OFF \
    -DOPTION_USE_XCURSOR=OFF -DOPTION_USE_XFT=OFF -DOPTION_USE_XRENDER=OFF -DOPTION_USE_XDBE=OFF \
    -DOPTION_FILESYSTEM_SUPPORT=OFF -DOPTION_BUILD_HTML_DOCUMENTATION=OFF
build_component "LIB FLTK" cmake --build "$r/src/build/dillo/fltk" --target fltk fltk_images -j2
chmod +x "$r/src/build/dillo/fltk/fltk-config"
s=$r/src/user/thirdparty/dillo
b=$r/src/build/dillo
f="-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone"
cpp="-DOPEN_SILLICON -nostdinc -isystem $("$cc" -print-file-name=include) -I$s/opensillicon -I$r/src/user/libos/include -I$p/include -I$r/src/user/thirdparty/xorg/deps/libz -idirafter /usr/include -include $s/opensillicon/compat.h"
cxx="$f -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit"
mkdir -p "$b/target"
"$cc" $f $cpp -c "$s/opensillicon/iconv.c" -o "$b/iconv.o"
ar rcs "$b/libiconv.a" "$b/iconv.o"
"$cxx_compiler" $cxx $cpp -c "$s/opensillicon/runtime.cc" -o "$b/runtime.o"
cd "$b/target"
export PKG_CONFIG_LIBDIR="$p/lib/pkgconfig:$p/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
ld="-nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,$s/opensillicon/linker.ld $r/src/build/libos/crt0.o $b/runtime.o -L$b -L$p/lib -L$r/src/build/libz"
libs="-Wl,--start-group $r/src/build/libos/liblibos.a -Wl,--end-group -lgcc"
PNG_CONFIG="$p/bin/libpng-config" CC="$cc" CXX="$cxx_compiler" \
    CPPFLAGS="$cpp" CFLAGS="$f" CXXFLAGS="$cxx" LDFLAGS="$ld" LIBS="$libs" \
    FLTK_CONFIG="$b/fltk/fltk-config" FLTK_CFLAGS="-I$s/deps/fltk -I$b/fltk" \
    FLTK_CXXFLAGS="$cxx $cpp -I$s/deps/fltk -I$b/fltk" \
    FLTK_LIBS="$b/fltk/lib/libfltk.a -L$p/lib -lX11 -lxcb -lXau $libs" LIBPTHREAD_LIBS=" " \
    build_component "USER Dillo configure" "$s/configure" --build="$(sh "$s/config.guess")" --host=x86_64-elf --target=x86_64-elf --prefix=/usr --sysconfdir=/etc \
    --disable-threaded-dns --disable-ipv6 --disable-tls --disable-webp --disable-brotli --disable-control-socket \
    --disable-xembed --disable-svg --disable-cookies
for component in dlib dpip lout dw src/IO; do
    build_component "LIB Dillo $component" make -j2 -C "$component"
done
# Relink after libc or FLTK changes even when browser objects are unchanged.
rm -f src/dillo
build_component "USER Dillo" make -j2 -C src dillo
mv "$tmp" "$stamp"
