# Phase 8 — initramfs, RAMFS, and VFS

Phase 8 implements the contracts accepted in [ADR-004 and ADR-005](../OPEN_ADRS.md).
Limine supplies one module named `initramfs`; the ISO build generates it as an
uncompressed `newc` CPIO archive. The parser validates all hexadecimal header
fields, alignment, names, supported file types, file ranges, entry limits, and
the trailer before mounting. It supports regular files and directories in
`070701` archives. CRC archives, symbolic links, hard links, device nodes, and
other record types are rejected. Archive contents are read-only and remain in
the Limine module memory.

The VFS has reference-counted vnodes and separate reference-counted open-file
descriptions. A file description holds a vnode reference, access flags, and a
shared offset; `fork` retains references so parent and child share offsets.
The mount table performs normalized, absolute, longest-prefix path lookup.
RAMFS is mounted at `/`; the initramfs archive is mounted read-only at `/init`.
The kernel creates `/tmp` in RAMFS. Switch-root, unmounting, and persistent
storage remain for Phase 10.

Syscall ABI v1 appends `open` (7), `close` (8), `read` (9), `stat` (10),
`readdir` (11), and `exec_path` (12); the existing `write` call also writes to opened files. Open
uses `RDI=path`, `RSI=flags`, `RDX=mode`; close uses `RDI=fd`; read uses
`RDI=fd`, `RSI=buffer`, `RDX=length`; stat uses `RDI=path`, `RSI` pointing
to `struct vfs_stat`; readdir uses `RDI=fd`, `RSI` pointing to
`struct vfs_dirent`. Read and write
are limited to 4096 bytes per call. `readdir` returns 1 for one entry and 0
at end. `exec_path` takes a NUL-terminated path in `RDI`, supports static
ELF64 only, and requires a regular file with an execute bit. Failure leaves
the current image running; success replaces it after image validation.
Syscall 5's memory-buffer ABI remains unchanged. Dynamic linking and shared
libraries remain deferred. Errors follow ABI v1's positive errno and
carry-flag convention. General user permission enforcement, `mkdir`, `lseek`,
symlinks, and unmount support are not implemented.

## Verification

- `make clean all iso test elf-test vfs-test process-test vfs-smoke-test fault-test interrupt-test vmm-protection-test timer-test`:
  PASS.
- `vfs-test` checks malformed/truncated `newc`, module-backed reads, path
  normalization, read-only archive behavior, RAMFS create/read/write, stat,
  readdir, descriptor errors, and shared offsets across cloned tables.
- `vfs-smoke-test` boots the generated ISO under QEMU UEFI and runs Ring 3
  open/read/write/close/stat/readdir calls, including invalid user pointers,
  missing paths, relative-path rejection, read-only writes, and RAMFS
  persistence within the boot.
- The same QEMU test checks syscall 12 with a missing path (`ENOENT`), an
  executable non-ELF (`ENOEXEC`), a non-executable file (`EACCES`), and a bad
  user pointer (`EFAULT`), then successfully replaces itself with a static
  ELF64 from the initramfs. The child marker and kernel completion log verify
  image replacement and successful exit.
- GNU `cpio -H newc` output including the conventional `.` root entry was also
  mounted and passed the host VFS test.
