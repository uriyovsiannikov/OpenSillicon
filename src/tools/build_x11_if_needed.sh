#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
stamp="$root/src/build/x11/.apps-input.sha256"
tmp="$stamp.tmp.$$"
link_stamp="$stamp.link"
link_tmp="$link_stamp.tmp.$$"
trap 'rm -f "$tmp" "$link_tmp"' EXIT HUP INT TERM
mkdir -p "$root/src/build/x11"

{
	find \
		"$root/src/user/thirdparty/xorg/deps/libXau" \
		"$root/src/user/thirdparty/xorg/deps/libxcb" \
		"$root/src/user/thirdparty/xorg/deps/libX11" \
		"$root/src/user/thirdparty/xorg/deps/libXext" \
		"$root/src/user/thirdparty/xorg/deps/libICE" \
		"$root/src/user/thirdparty/xorg/deps/libSM" \
		"$root/src/user/thirdparty/xorg/deps/libXt" \
		"$root/src/user/thirdparty/xorg/deps/libXmu" \
		"$root/src/user/thirdparty/xorg/deps/libXpm" \
		"$root/src/user/thirdparty/xorg/deps/libXaw" \
		"$root/src/user/thirdparty/xorg/deps/libxtrans" \
		"$root/src/user/thirdparty/xorg/deps/xcb-proto" \
		"$root/src/user/thirdparty/xorg/deps/libXfixes-6.0.2" \
		"$root/src/user/thirdparty/xorg/deps/libXi-1.8.4" \
		"$root/src/user/thirdparty/xorg/deps/libpng" \
		"$root/src/user/thirdparty/xorg/deps/libz" \
		"$root/src/user/thirdparty/xorg/deps/freetype" \
		"$root/src/user/thirdparty/xorg/deps/libjpeg-turbo" \
		"$root/src/user/thirdparty/xorg/deps/imlib2" \
		"$root/src/user/thirdparty/xorg/twm" \
		"$root/src/user/thirdparty/xorg/xterm" \
		"$root/src/user/thirdparty/xorg/xeyes" \
		"$root/src/user/thirdparty/xorg/feh" \
		"$root/src/user/libos/include" \
		"$root/src/include/abi" \
		"$root/src/tools/build_x11_if_needed.sh" \
		"$root/src/tools/build_x11_apps.sh" \
		"$root/src/tools/build_feh.sh" \
		"$root/src/user/c_linker.ld" \
		-type d \( -name .git -o -name __pycache__ \) -prune -o -type f ! -name '*.pyc' -print0 2>/dev/null | sort -z | xargs -0 sha256sum
} | sha256sum | cut -d ' ' -f 1 > "$tmp"

# Implementation changes in libc/zlib require relinking, not reconfiguring
# every X11 dependency. Public headers remain part of the source fingerprint.
sha256sum "$root/src/build/libos/liblibos.a" "$root/src/build/libos/crt0.o" \
    "$root/src/build/libz/libz.a" "$root/src/user/c_linker.ld" > "$link_tmp"

mode=full
if [ -f "$stamp" ] && cmp -s "$tmp" "$stamp" &&
	[ -x "$root/src/build/tinyx/target/kdrive/opensillicon/Xopensillicon" ] &&
	[ -x "$root/src/build/x11/twm/src/twm" ] &&
	[ -x "$root/src/build/x11/xterm/xterm" ] &&
	[ -x "$root/src/build/x11/xeyes/xeyes" ] &&
	[ -x "$root/src/build/x11/feh/src/feh" ]; then
	if [ -f "$link_stamp" ] && cmp -s "$link_tmp" "$link_stamp"; then
		echo "X11 inputs unchanged; reusing existing applications"
		exit 0
	fi
	mode=relink
fi

echo "X11: $mode build"
if sh "$root/src/tools/build_x11_apps.sh" "$mode"; then
	mv "$tmp" "$stamp"
	mv "$link_tmp" "$link_stamp"
else
	rm -f "$tmp"
	exit 1
fi
