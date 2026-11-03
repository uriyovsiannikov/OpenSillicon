#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
stamp="$root/build/x11/.apps-input.sha256"
tmp="$stamp.tmp"
mkdir -p "$root/build/x11"

{
	find \
		"$root/user/xorg/deps/libXau" \
		"$root/user/xorg/deps/libxcb" \
		"$root/user/xorg/deps/libX11" \
		"$root/user/xorg/deps/libXext" \
		"$root/user/xorg/deps/libICE" \
		"$root/user/xorg/deps/libSM" \
		"$root/user/xorg/deps/libXt" \
		"$root/user/xorg/deps/libXmu" \
		"$root/user/xorg/deps/libXpm" \
		"$root/user/xorg/deps/libXaw" \
		"$root/user/xorg/deps/libxtrans" \
		"$root/user/xorg/deps/xcb-proto" \
		"$root/user/xorg/deps/libXfixes-6.0.2" \
		"$root/user/xorg/deps/libXi-1.8.4" \
		"$root/user/xorg/deps/libpng" \
		"$root/user/xorg/deps/libz" \
		"$root/user/xorg/deps/freetype" \
		"$root/user/xorg/deps/libjpeg-turbo" \
		"$root/user/xorg/deps/imlib2" \
		"$root/user/xorg/tinyx" \
		"$root/user/xorg/twm" \
		"$root/user/xorg/xterm" \
		"$root/user/xorg/xeyes" \
		"$root/user/xorg/feh" \
		"$root/user/xorg/fonts" \
		"$root/tools/build_x11_apps.sh" \
		"$root/tools/build_feh.sh" \
		"$root/user/c_linker.ld" \
		-type f -print0 2>/dev/null | sort -z | xargs -0 sha256sum
	sha256sum \
		"$root/build/libos/liblibos.a" \
		"$root/build/libz/libz.a" \
		"$root/build/libfontenc/libfontenc.a" \
		"$root/build/libxfont/libXfont.a"
} | sha256sum | cut -d ' ' -f 1 > "$tmp"

if [ -f "$stamp" ] && cmp -s "$tmp" "$stamp" &&
	[ -x "$root/build/tinyx/target/kdrive/opensillicon/Xopensillicon" ] &&
	[ -x "$root/build/x11/twm/src/twm" ] &&
	[ -x "$root/build/x11/xterm/xterm" ] &&
	[ -x "$root/build/x11/xeyes/xeyes" ] &&
	[ -x "$root/build/x11/feh/src/feh" ]; then
	rm -f "$tmp"
	echo "X11 inputs unchanged; reusing existing applications"
	exit 0
fi

echo "X11 inputs changed; rebuilding applications"
if sh "$root/tools/build_x11_apps.sh"; then
	mv "$tmp" "$stamp"
else
	rm -f "$tmp"
	exit 1
fi
