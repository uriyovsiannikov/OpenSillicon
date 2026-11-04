# GNU nano on OpenSillicon

`make nano-build` compiles the sources in `user/nano/src` with the native
libos runtime. `make build/root.img` and `make run-gui` install the resulting
static ELF as `/bin/nano`. Run `nano /tmp/example.txt`; Ctrl-O writes the file,
Enter confirms its name, and Ctrl-X exits.

The port uses ncurses 6.5, bundled as its original source archive under `deps/`.
The build verifies SHA-256
`136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6`.
Source: https://ftp.gnu.org/gnu/ncurses/ncurses-6.5.tar.gz.
Its license is included in the archive (`ncurses-6.5/COPYING`). No network access
is needed to build. Host tools: GCC, binutils, make, tar, gzip, awk, tic and
infocmp. Generator programs use host libc; the editor and ncurses use libos.
Generated files remain under `build/nano/`.

The fixed port configuration avoids gnulib/autotools bootstrapping of the nano
Git checkout. Embedded xterm, vt100 and linux terminfo entries avoid requiring
an on-disk terminfo database. TERM defaults to xterm because native exec
currently supplies an empty environment. Both the native console and xterm
PTYs expose the termios operations needed by ncurses.

This is a byte-oriented build: UTF-8, localization, syntax highlighting,
nanorc parsing, file browser and external spell checking are disabled.
Literal search works; libos does not implement regular expressions and nano
reports an error when regex search is requested. File timestamp restoration
is unsupported. Native console termios supports canonical/echo versus raw
input; PTYs have their existing, more complete behavior.

`make nano-qemu-test` boots a private copy of the root image, edits and saves a
file, reopens and changes it, then verifies its contents with cat. The normal
root image is not modified by the test.
