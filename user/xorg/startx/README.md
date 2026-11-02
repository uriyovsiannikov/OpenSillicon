# OpenSillicon graphical session launcher

`make build/startx.elf` builds a static target executable. The root image
installs it as `/bin/startx`.

The launcher checks `/bin/Xopensillicon`, `/bin/twm` and `/bin/xterm`, rejects
an active display :0, starts TinyX, waits for its abstract AF_UNIX socket,
then launches twm and xterm with explicit `-display :0` arguments. It waits
for a session child to exit and terminates/reaps its remaining children.
SIGINT and SIGTERM also trigger cleanup.

`make build/root.img` builds the static twm and xterm executables through
`tools/build_x11_apps.sh` and installs them as `/bin/twm` and `/bin/xterm`.
The X client stack uses the local Unix socket transport and the OS's anonymous
PTY pair; it does not require `/dev/pts`.

The X client dependency sources are in `user/xorg/deps/libXau`, `libXdmcp`, `libxcb`,
`libX11`, `libXext`, `libICE`, `libSM`, `libXt`, `libXmu`, `libXpm` and `libXaw`.
Archive URLs and SHA-256 hashes are recorded in `user/xorg/deps/x11-dependencies.json`;
each source tree retains its upstream license. Builds must use libos headers,
startup and libraries for configure link checks as well as compilation.
The PTY implementation supports the termios subset xterm uses. Job control,
process groups, termcap databases, regex selection, printer subprocesses,
wide-character locale support, X11 authentication, TCP X transports, and
terminal flow control remain unsupported. xterm's normal local display and
interactive shell paths use the supported PTY and Xlib paths.
