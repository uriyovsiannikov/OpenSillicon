#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$root/src/tools/builders/build_helpers.sh"
cc=${CC:-gcc}
out="$root/src/build/nano"
src="$out/deps/ncurses-6.5"
mkdir -p "$out/deps" "$out/ncurses" "$out/app"
exec 9>"$out/.build.lock"
flock 9
printf '%s  %s\n' 136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6 "$root/src/user/thirdparty/nano/deps/ncurses-6.5.tar.gz" | sha256sum -c -
if [ ! -f "$src/configure" ]; then
    tar -xzf "$root/src/user/thirdparty/nano/deps/ncurses-6.5.tar.gz" -C "$out/deps"
fi
build_triplet=$(sh "$src/config.guess")
cflags='-O2 -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone'
cppflags="-nostdinc -I$root/src/user/libos/include -isystem $($cc -print-file-name=include)"
cd "$out/ncurses"
if [ ! -f Makefile ]; then
    CPPFLAGS="$cppflags" CFLAGS="$cflags" \
    LDFLAGS="-nostdlib -static -no-pie -Wl,-T,$root/src/user/c_linker.ld $root/src/build/libos/crt0.o" \
    LIBS="$root/src/build/libos/liblibos.a -lgcc" \
    build_component "LIB ncurses configure" "$src/configure" --build="$build_triplet" --host=x86_64-elf --prefix=/usr \
        --without-progs --without-tests --without-manpages --without-cxx \
        --without-cxx-binding --without-ada --without-debug --without-shared \
        --disable-widec --disable-database --with-fallbacks=xterm,vt100,linux
fi
# Generator programs run on the build host, with its own libc and headers.
build_component "LIB ncurses headers" make -C include
build_component "LIB ncurses" make -C ncurses -j"${NANO_JOBS:-4}" \
    BUILD_CPPFLAGS="-DHAVE_CONFIG_H -DUSE_BUILD_CC -I. -I../include -I$src/ncurses -I$src/include" \
    BUILD_LDFLAGS= BUILD_LIBS=
cp "$root/src/user/thirdparty/nano/config.h" "$out/app/config.h"
printf '#define REVISION "9.3 (OpenSillicon)"\n' > "$out/app/revision.h"
for source in "$root"/src/user/thirdparty/nano/src/*.c; do
    name=$(basename "$source" .c)
    extra=
    if [ "$name" = nano ]; then extra=-Dmain=nano_main; fi
    "$cc" $cflags $cppflags -U__linux__ -DOPEN_SILLICON -DHAVE_CONFIG_H $extra \
        -I"$out/app" -I"$out/ncurses/include" -c "$source" -o "$out/app/$name.o"
done
"$cc" $cflags $cppflags -c "$root/src/user/thirdparty/nano/main.c" -o "$out/app/port_main.o"
"$cc" -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
    -Wl,-T,"$root/src/user/c_linker.ld" "$root/src/build/libos/crt0.o" "$out"/app/*.o \
    -Wl,--start-group "$out/ncurses/lib/libncurses.a" "$root/src/build/libos/liblibos.a" \
    -Wl,--end-group -lgcc -o "$out/nano"
