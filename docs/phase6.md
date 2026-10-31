# Phase 6 — User Mode and System Calls

Phase 6 adds an x86_64 Ring 3 transition and the ABI v1 `int 0x80` syscall
path. The accepted ABI is documented in [ADR-001](../OPEN_ADRS.md): syscall
number and result in `RAX`; arguments in `RDI`, `RSI`, `RDX`, `R10`, `R8`, and
`R9`; success clears CF; errors return positive errno and set CF.

The initial calls are ABI version (0), exit (1), write (2), and getpid (3).
The kernel reports PID 1 for this single smoke process. Descriptors 1 and 2
write to serial; descriptor 0 and all other descriptors are not writable.
Writes are capped at 4096 bytes and user buffers are checked across every
mapped user page before being copied. Kernel and userspace mappings retain
distinct permissions; the smoke program runs from a user, read-only executable
page with a separate user, writable stack page.

## Verification

- `make clean all iso`: PASS, system GCC/binutils with warnings as errors.
- QEMU UEFI boot: PASS; serial output confirms Ring 3 ABI checks and the
  existing scheduler/timer regression.
- The user smoke image checks ABI version, PID, serial write, invalid
  descriptor (`EBADF`), invalid pointer (`EFAULT`), unknown call (`ENOSYS`),
  and exit. A second user entry attempts to read the supervisor-only kernel
  image mapping; the resulting user page fault is reported and returned to
  the kernel test harness.

The descriptor implementation is deliberately temporary and is not VFS-backed.
ELF loading, multiple processes, and general user-fault recovery remain later
milestone work.
