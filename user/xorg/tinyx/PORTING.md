# OpenSillicon port notes

This tree is based on `tinycorelinux/tinyx`, upstream commit
`feab72c` (2024-11-13). The upstream repository is
https://github.com/tinycorelinux/tinyx.

The X11 protocol server and machine-independent DIX, MI, and framebuffer
rasterization code are retained. The VESA/Xvesa KDrive backend was removed:
it depends on BIOS mode switching, VGA access, and `vm86`, none of which fit
OpenSillicon's UEFI x86_64 userspace model.

The upstream Linux fbdev and Linux input/VT backends remain as reference code.
An initial `Xopensillicon` KDrive backend uses the OpenSillicon framebuffer
information and full-frame present syscalls. It currently supports a fixed
32-bit RGB framebuffer without rotation. It polls raw PS/2 keyboard events
through a bounded kernel queue and supplies a basic US keymap, including the
numeric keypad with KDrive's NumLock behavior, PS/2 E0-prefixed navigation
keys, Print Screen, Pause, and right-side modifiers. It also polls three-byte
PS/2 mouse packets and forwards relative motion and three buttons to KDrive.
The OpenSillicon configure profile omits MIT-SHM, XDMCP, and XF86BigFont,
which require System V shared memory or network socket interfaces that the
current target runtime does not provide.
The kernel and libos now provide bounded `AF_UNIX` stream sockets,
`select()`/`poll()`,
`readv()`/`writev()`, `dup2()`, path-based process launch/wait, monotonic time,
and a limited signal interface. The libos also has `pipe()` and `fdopen()` so
TinyX's `Popen()` path can use kernel pipes. Additional libc and filesystem
interfaces are still needed; mouse wheel packets and extended buttons are not
supported.

The server currently configures and links as a host diagnostic build when the
legacy libXfont 1.x dependency is available. This confirms the TinyX server
sources and OpenSillicon KDrive backend link together on Linux; it does not
produce a runnable OpenSillicon binary. The kernel socket subset handles
`AF_UNIX` stream bind/listen, connect/accept, and read/write. Kernel `select()`
now waits on readiness for VFS descriptors, pipes, sockets, and terminal input.
The namespace is bounded and in-kernel; pathname socket
addresses do not create VFS nodes. The TinyX Xtrans profile enables its
abstract `local` socket transport for the display server. `SO_PEERCRED` returns
the peer PID and zero UID/GID because the kernel has no user/group credential
model. Per-socket buffers are capped at 64 KiB. The kernel's vector I/O limit
is 16 vectors and
64 KiB total per call. `fcntl(F_GETFL/F_SETFL)` supports `O_NONBLOCK`; async
`SIGIO` delivery and `F_SETOWN` are unsupported. Process support is limited to
fork, path-based exec, blocking wait, and descriptor duplication. Signal
delivery only handles explicitly queued signals and does not generate
`SIGCHLD` or `SIGPIPE`. The OpenSillicon build skips the legacy lock-file
sequence; the exclusive AF_UNIX display-socket bind prevents two servers from
claiming the same display. `ioctl(FIONREAD)` is implemented for local stream
sockets because Xtrans uses it to query queued connection data. Other generic
ioctls are unsupported. The OpenSillicon server uses the kernel's 32-entry
descriptor limit, skips Unix resource-limit/process-group setup and timer-based
smart scheduling, and accepts numeric user/group access-control IDs only; the
kernel has no passwd/group database. VT switch helper commands and lock files
are also omitted. Broader VFS operations and the full TinyX target link remain
outstanding; the target-profile libXfont static archive now builds. Audit log prefixes use elapsed monotonic uptime
instead of calendar time because the kernel does not provide a wall clock.
OpenSillicon's configure profile selects the existing `CLOCK_MONOTONIC` path
without trying to run a target probe; other platforms keep the runtime probe.
The libos `rand()` used by DIX is deterministic and is not a source of secret
random data. TinyX now carries the Xtrans 1.6.0 source subset it includes
directly, with the OpenSillicon profile limited to abstract AF_UNIX sockets;
IPv4/IPv6 headers and filesystem socket management are excluded from that
profile. Other TinyX platforms keep the upstream transport paths.

## Font dependency

This TinyX revision requires the `xfont` pkg-config module from legacy
libXfont 1.x. It calls the old `FontFileRegisterFpeFunctions` interface, so
libXfont2's `xfont2` module is not a drop-in replacement. The source is
vendored in `user/xorg/deps/libxfont`; `make libxfont-build` produces a target-profile
static archive using `user/xorg/deps/libz` and libos headers. Its encoding dependency is
vendored in `user/xorg/deps/libfontenc`; `make libfontenc-build` builds the static target
archive. That port reads plain encoding files from `/usr/share/fonts/encodings`
or `FONT_ENCODINGS_DIRECTORY`, but does not support gzip-compressed encoding
maps. `make tinyx-build` now compiles and statically links the full
`Xopensillicon` server with libos startup and runtime archives. The output is
`build/tinyx/target/kdrive/opensillicon/Xopensillicon`; it has no dynamic
interpreter or unresolved symbols. The build copies sources to `build/tinyx`
before running autotools, so generated files do not enter this source tree. The
root filesystem build installs this executable as `/bin/Xopensillicon`, so it
can be launched from the system shell. The kernel now provides a 64 KiB initial
user stack; the previous single-page stack faulted while TinyX initialized
Picture formats. The build selects libXfont's built-in `fixed` font and
registers its built-in font path in DIX. The framebuffer backend initializes
shadow/Damage during `finishInitScreen`, before attaching the screen pixmap
in `createRes`. QEMU boots to the X11 root pattern and software cursor; PS/2
mouse motion updates the cursor, and Ctrl+Alt+Backspace returns to the shell
with status zero. connection, window creation, Expose delivery and text rendering with `fixed`.
QEMU verification confirms visible text and closing the window by clicking,
followed by the launcher terminating/reaping the server and returning to the
shell with status zero. A general Xlib port, window manager and terminal
emulator are not yet included.
`make tinyx-qemu-test` reproduces the graphical check, including a second
launch and keyboard-driven closure. Returning to terminal keyboard polling
disables and clears raw event capture so shell input cannot leak into the
next graphical session.
The
upstream
[libXfont 1.5.4 source archive](https://xorg.freedesktop.org/releases/individual/lib/libXfont-1.5.4.tar.bz2)
has SHA-256
`1a7f7490774c87f2052d146d1e0e64518d32e6848184a18654e8d0bb57883242`.
The library includes the protocol's built-in `fixed` and `cursor` fonts;
for a minimal host-side configure check, optional FreeType, PCF/BDF files,
bzip2, and X font-server support can be disabled. Built-in `fixed` and
`cursor` fonts use the target library without a host shared library. External
encoding files require an uncompressed target-side encoding-map directory.

No generated configure output, object files, libraries, or build directories
were present in the imported source tree during this audit.
