#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$root/build/x11/prefix"
cc=${CC:-gcc}
gcc_include=$($cc -print-file-name=include)
cflags="-std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone"
cppflags="-DOPEN_SILLICON -nostdinc -isystem $gcc_include -I$root/user/libos/include -I$root/user/libos/include/sys -I$prefix/include -I$root/user/xorg/deps/libz -idirafter /usr/include"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"

if [ "${1:-full}" != relink ]; then
feh_force=-B
mkdir -p "$root/build/x11/libpng"
cd "$root/build/x11/libpng"
CPPFLAGS="$cppflags" CFLAGS="$cflags" LDFLAGS="-L$root/build/libz" \
    "$root/user/xorg/deps/libpng/configure" --host=x86_64-elf \
    --prefix="$prefix" --disable-shared --enable-static --disable-tools \
    --disable-tests --without-libpng-compat
make -j2
make install

cmake -S "$root/user/xorg/deps/freetype" -B "$root/build/x11/freetype" \
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=x86_64 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_C_FLAGS="$cflags $cppflags" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON \
    -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
cmake --build "$root/build/x11/freetype" -j2
cmake --install "$root/build/x11/freetype"

cmake -S "$root/user/xorg/deps/libjpeg-turbo" -B "$root/build/x11/jpeg-static" \
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=x86_64 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_C_FLAGS="$cflags $cppflags" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF \
    -DWITH_SIMD=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DHAVE_THREAD_LOCAL=0
cmake --build "$root/build/x11/jpeg-static" --target jpeg-static -j2
install -m644 "$root/build/x11/jpeg-static/libjpeg.a" "$prefix/lib/libjpeg.a"
for header in jpeglib.h jmorecfg.h jerror.h; do
    install -m644 "$root/user/xorg/deps/libjpeg-turbo/src/$header" "$prefix/include/$header"
done
install -m644 "$root/build/x11/jpeg-static/jconfig.h" "$prefix/include/jconfig.h"

mkdir -p "$root/build/x11/imlib2"
cd "$root/build/x11/imlib2"
CPPFLAGS="$cppflags" CFLAGS="$cflags" LDFLAGS="-L$prefix/lib" \
    PNG_CFLAGS="-I$prefix/include" PNG_LIBS="-L$prefix/lib -lpng" \
    JPEG_CFLAGS="-I$prefix/include" JPEG_LIBS="-L$prefix/lib -ljpeg" \
    "$root/user/xorg/deps/imlib2/configure" --host=x86_64-elf \
    --prefix="$prefix" --disable-shared --enable-static --disable-filters \
    --disable-progs --disable-mmx --disable-amd64 --enable-text \
    --x-includes="$prefix/include" --x-libraries="$prefix/lib" \
    --without-x-shm-fd --without-gif --without-heif --without-avif \
    --without-j2k --without-jxl --without-ps --without-raw --without-svg \
    --without-tiff --without-webp --without-y4m --without-bz2 \
    --without-lzma --without-zlib --without-id3
make -j2 -C src/lib
make -C src/lib install
# Give every built-in module its own public descriptor. The codecs otherwise
# keep their upstream private functions and Imlib2 loader ABI.
for codec in png jpeg pnm bmp qoi xpm xbm tga argb; do
    "$cc" $cflags $cppflags -I. -I"$root/user/xorg/deps/imlib2/src/lib" \
        -Dloader=os_loader_$codec -DPACKAGE_DATA_DIR='"/usr/share/feh"' -c \
        "$root/user/xorg/deps/imlib2/src/modules/loaders/loader_$codec.c" \
        -o "loader_$codec.o"
done
"$cc" $cflags $cppflags -I. -I"$root/user/xorg/deps/imlib2/src/lib" \
    -c "$root/user/xorg/deps/imlib2/src/modules/loaders/ldrs_util.c" -o ldrs_util.o
"$cc" $cflags $cppflags -I. -I"$root/user/xorg/deps/imlib2/src/lib" \
    -c "$root/user/xorg/deps/imlib2/src/modules/loaders/exif.c" -o loader_exif.o
ar rcs "$prefix/lib/libImlib2.a" loader_*.o ldrs_util.o

mkdir -p "$root/build/x11/feh"
cd "$root/build/x11/feh"
# Keep all generated objects and dependency files outside the source tree.
tar -cf - -C "$root/user/xorg/feh" . | tar -xf -
cat > opensillicon_config.h <<'EOF'
#define PREFIX "/usr"
#define PACKAGE "feh"
#define VERSION "3.13.1"
EOF
fi
cd "$root/build/x11/feh"
make ${feh_force:-} -C src -j2 curl=0 xinerama=0 exif=0 magic=0 mkstemps=0 verscmp=0 \
    PREFIX=/usr CC="$cc" \
    CFLAGS="$cflags $cppflags -I$prefix/include/freetype2 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -include $root/build/x11/feh/opensillicon_config.h" \
    LDFLAGS="-nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,$root/user/c_linker.ld $root/build/libos/crt0.o -L$prefix/lib -L$root/build/libz" \
    LDLIBS="-Wl,--start-group $root/build/libos/liblibos.a $prefix/lib/libi18n.a -lImlib2 -lfreetype -lpng -ljpeg $root/build/libz/libz.a -lXext -lX11 -lxcb -lXau -Wl,--end-group -lgcc"
