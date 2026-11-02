# libos — OpenSillicon minimal C library

`libos` is a native, freestanding C11 runtime for static x86_64 ELF programs
loaded by OpenSillicon. It uses the kernel's `int 0x80` ABI directly and is not
binary- or source-compatible with musl or Linux. Dynamic linking, shared
objects, TLS, threads, signals, process creation, and general POSIX behavior
are out of scope. `errno` is a single process-global integer (the current
single-threaded profile does not require TLS).

The CRT consumes the initial `argc`, `argv`, and `envp` words, calls `main`, and
exits through syscall 1. The kernel currently supplies argc 0 and null argv and
envp entries. Static ELF links use `user/c_linker.ld`; no interpreter or dynamic
segment is emitted. The library does not invent auxiliary-vector values.

Supported public functions are the declarations in `include/`: `exit`, `_exit`,
and `getpid`; the implemented memory/string primitives; mmap-backed
`malloc/calloc/realloc/free`; `open/close/read/write/lseek`,
`mmap/munmap/mprotect`, `stat/fstat/fstatat`; minimal
`opendir/readdir/closedir`; and unbuffered stdio
(`fopen/fclose/fread/fwrite/fputs/puts/printf/snprintf`).
`printf` currently supports `%%`, `%s`, `%c`, signed decimal, unsigned decimal,
hexadecimal, pointer, and `l`/`ll` integer lengths. It has no flags, width,
precision, floating point, buffering, or stream error indicators. `fopen`
accepts only the exact modes `"r"` and `"w"`. `snprintf`
returns the full would-have-written length and NUL-terminates when capacity is
nonzero. Unsupported format conversions return -1.

The native syscall ABI is unchanged: arguments in RDI, RSI, RDX, R10, R8, R9;
carry set reports a positive errno; libos converts it to `-1` and sets errno.
Syscall numbers used include exit 1, write 2, getpid 3, open 7, close 8, read 9,
mmap/munmap/mprotect 21–23, lseek 36, fstat 37, getdents64 38, and fstatat 39.
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

Directory enumeration consumes syscall 38's getdents64 records. Public
`struct dirent` is 280 bytes with the name at offset 19, matching that record
header; `readdir` validates each record and EOF returns null. Directory order
is the VFS order and is not promised stable. There is no `rewinddir`,
`scandir`, `chdir`, or raw public `getdents` function.

The library does not expose `clock_gettime`, `nanosleep`, signals, process
creation, vector I/O, ioctl, terminal control, or the broader musl/POSIX
surface even where the kernel has related syscalls. Unsupported features are
not emulated as successful operations.

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
