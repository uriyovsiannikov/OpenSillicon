# libos — OpenSillicon minimal C library

`libos` is a native, freestanding C11 runtime for static x86_64 ELF programs
loaded by OpenSillicon. It uses the kernel's `int 0x80` ABI directly and is not
binary- or source-compatible with musl or Linux. Dynamic linking, shared
objects, TLS, threads, and general POSIX behavior are out of scope.
Process support is limited to fork, path-based exec, blocking waitpid, and
descriptor duplication. UID/GID getters report zero; setters accept only zero
because the kernel has no credential model. `errno` is a single process-global
integer (the current single-threaded profile does not require TLS).

The CRT consumes the initial `argc`, `argv`, and `envp` words, calls `main`, and
exits through syscall 1 in ABI version 2. The kernel currently supplies argc 0
and null argv and envp entries. Static ELF links use `user/c_linker.ld`; no
interpreter or dynamic
segment is emitted. The library does not invent auxiliary-vector values.

Supported public functions are the declarations in `include/`: `exit`, `_exit`,
`getpid/getppid`, `getuid/geteuid/getgid/getegid`, `setuid/seteuid/setgid/setegid`,
`fork`, blocking `waitpid` (exit status only, options must be zero), `execv`,
`execl` (up to 16 arguments), `sysconf` for descriptor/page/vector limits,
`uname`, limited signal actions/masks, and `kill`; `getenv` returns null
because launched processes currently have an empty environment; the implemented
memory/string primitives, including `strnlen`, `strcat`, `stpcpy`, `strstr`,
ASCII `strcasecmp`/`strncasecmp`, and `strdup`; `qsort` (in-place heapsort);
ASCII `ctype` classification and case conversion; `atoi`/`atol`, `abs`/`labs`, and
`strtol`/`strtoul`, `atof`, and decimal `strtod`; C-locale `localeconv`; deterministic
`rand`/`srand` (not suitable for secrets);
`mmap`-backed `malloc/calloc/realloc/free`; `open/close/read/write/lseek/pipe/dup2`,
`mmap/munmap/mprotect`, `stat/fstat/fstatat`, `clock_gettime(CLOCK_MONOTONIC)`,
`nanosleep`, and `sleep`; minimal `opendir/readdir/closedir`; and unbuffered stdio
(`fopen/fdopen/fileno/fclose/fgetc/getc/ungetc/fgets/fscanf/fread/fwrite/fputs/puts/fflush`,
`setlinebuf`, `printf/fprintf/vfprintf`, and `sprintf/vsprintf/snprintf/vsnprintf`).
It also exposes a small local stream-socket subset: `socket`, `bind`,
`listen`, `connect`, and `accept` for `AF_UNIX`/`SOCK_STREAM`, plus `select`
and bounded `poll` for descriptors in the kernel's 32-entry descriptor table;
`readv`/`writev` support up to 16 vectors per call. Socket `shutdown`,
`SO_PEERCRED`, and send/receive
buffer options are also available. `fcntl(F_GETFL/F_SETFL)` reports and changes
`O_NONBLOCK`; other `fcntl` commands are unsupported. `ioctl(FIONREAD)` reports
queued bytes on an `AF_UNIX` stream socket.
`getaddrinfo` resolves IPv4 A records through the native UDP socket syscalls
using the first `nameserver` entry in `/etc/resolv.conf`; numeric IPv4 nodes
work without DNS. IPv6, TCP sockets, search domains, and DNS-over-TCP fallback
are not implemented.
The x86_64 math subset provides the functions declared in `math.h` for TinyX;
trigonometric arguments with magnitude at least 2^63 return NaN because the
hardware range reducer cannot process them.
The printf formatter supports `%%`, `%s`, `%c`, signed/unsigned decimal,
octal, hexadecimal, pointers, common flags, width and precision, and `h`/`hh`,
`l`/`ll` and `z` integer lengths. Floating point formatting, buffered I/O,
and stream error indicators are not implemented. `fscanf` currently supports
integer, word, and negated single-character scansets needed by legacy X font
directory files. `fopen` and `fdopen` accept read or write modes with an
optional text/binary suffix; append and update modes are unsupported. `snprintf`
returns the full would-have-written length and NUL-terminates when capacity is
nonzero. Unsupported format conversions return -1.

The native syscall ABI uses arguments in RDI, RSI, RDX, R10, R8, R9; carry set
reports a positive errno; libos converts it to `-1` and sets errno. ABI version
2 groups calls by subsystem and is defined once in `include/abi/syscall.h`.
Numbers used here include exit 1, getpid 2, open 10, close 11, read 12,
write 13, stat 14, fstat 15, fstatat 16, readdir 17, getdents64 18, lseek 19,
mmap/munmap/mprotect 30–32, local sockets 43–47, and signal masks 54.
Memory mapping supports only private anonymous, non-executable mappings;
`PROT_EXEC` is rejected by the kernel. `struct stat` is the kernel's existing
144-byte x86_64 record (`st_mode` offset 24, `st_size` offset 48). The VFS
currently supplies inode, type/mode, and size; device IDs, link count,
ownership, block counts, and timestamps are zero because the kernel does not
track them. Path queries use the kernel resolver: absolute paths work, and
relative `AT_FDCWD` paths are rooted at `/` because there is no process cwd
interface in this profile. Relative paths from directory descriptors use the
kernel's existing descriptor path context. Only VFS regular files and
directories are represented. `AT_SYMLINK_NOFOLLOW` returns
`EOPNOTSUPP` (symlinks are not represented); other unsupported `fstatat`
flags return `EINVAL`. `stat` is a wrapper around `fstatat(AT_FDCWD, path,
buf, 0)`.

Directory enumeration consumes syscall 18's getdents64 records. Public
`struct dirent` is 280 bytes with the name at offset 19, matching that record
header; `readdir` validates each record and EOF returns null. Directory order
is the VFS order and is not promised stable. There is no `rewinddir`,
`scandir`, `chdir`, or raw public `getdents` function.

The socket implementation is a bounded, in-kernel local stream transport. It
does not provide datagrams, ancillary data beyond `SO_PEERCRED`, filesystem
socket nodes, or nonblocking flags. `SO_PEERCRED` reports the peer PID and
zero UID/GID because the kernel has no user/group credential model. `select`
and `poll` support only descriptors below 32 and report read/write/error
readiness for VFS files, pipes, local sockets, and terminal input, using the
kernel's 100 Hz timer. The select timeout argument is not updated. `poll` maps
onto `select`, so it cannot distinguish hangup from readable data or report
priority data. Signal delivery supports `SIGHUP`, `SIGINT`, `SIGQUIT`,
`SIGABRT`, `SIGFPE`, `SIGKILL`, `SIGUSR1`, `SIGSEGV`, `SIGUSR2`, `SIGPIPE`,
`SIGALRM`, `SIGTERM`, `SIGCHLD`, and `SIGIO`; `SIGKILL` cannot be caught or
masked. `kill(pid, 0)` checks whether a process exists. Signals are delivered
only when explicitly queued with `kill()`; the kernel does not synthesize
`SIGCHLD` or `SIGPIPE`. `sigprocmask` manages process masks, but signal action
flags and per-action masks are not implemented. Other POSIX signals and job
control are unsupported. PTYs provide anonymous master/slave pairs via
`openpty` (the `name` and initial `termios` arguments must be null), bounded
queues, canonical line input, echo, raw mode, LF-to-CRLF output,
`FIONREAD`, `TIOCGWINSZ`/`TIOCSWINSZ`, and the `TCGETS`/`TCSETS` subset used by
xterm. The slave descriptor must be inherited or duplicated. `/dev/pts`,
`posix_openpt`, controlling terminals, process groups, terminal-generated
signals, flow control, and the rest of POSIX termios are not implemented.
Unsupported features are not emulated as successful operations.

The static X client set is assembled by `tools/build_x11_apps.sh` when building
`build/root.img`. It installs twm and xterm in `/bin`; xterm uses the anonymous
PTY slave inherited across fork/exec and connects to TinyX through the local
Unix socket. The X port keeps networking, auth-file locking, regex selection,
printer commands, and process-group/job-control features disabled or failing
explicitly where libos has no matching kernel operation.

The allocator maps one or more pages per allocation and has no reuse/free-list
optimization. Allocation alignment is at least 16 bytes. The initial QEMU
fixture covers startup, errors, memory/string operations, allocation, static
stdio formatting, file I/O, metadata, and directory enumeration.

Build and verify in an isolated output directory:

```sh
make LIBOS_BUILD_DIR=/tmp/libos-build libos-build libos-inspect
make LIBOS_BUILD_DIR=/tmp/libos-build \
  LIBOS_QEMU_BUILD_DIR=/tmp/libos-qemu libos-qemu-test
```
