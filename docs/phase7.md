# Phase 7 — ELF64 and Processes

Phase 7 adds a static x86_64 ELF64 loader and a single-CPU process manager.
The accepted process policy is recorded in [ADR-006](../OPEN_ADRS.md).

The loader accepts little-endian ELF64 `ET_EXEC` images for x86_64 with at
most 16 `PT_LOAD` segments. It checks all file and address ranges, requires
readable segments, enforces W^X, rejects page-overlapping segments and dynamic
linking, and requires the entry point to lie in an executable segment. The
image is loaded into isolated user mappings. Its image range ends below the
stack guard pages. The process stack has one mapped page and unmapped guard
pages above and below it.

Processes are single-threaded. `fork` eagerly copies the complete user address
space. `exec` accepts an ELF byte buffer and length from the caller, validates
and loads a replacement image before switching away from the old image. The
initial VFS-less interface does not accept paths. `waitpid` waits for direct
children; `wait` is `waitpid(-1, status)`. Exit status is one byte. Children
remain zombies until their parent reaps them. File descriptors are inherited
by fork; descriptors 1 and 2 continue to use serial output.

The LAPIC timer preempts Ring 3 by saving each process's interrupt frame on its
kernel stack and restoring another runnable process frame. Kernel threads keep
their cooperative Round Robin behavior. The implementation remains
single-CPU.

## Verification

- `make clean all iso test elf-test process-test fault-test interrupt-test vmm-protection-test timer-test`:
  PASS.
- Host ELF tests cover valid and malformed images, W^X, unsupported dynamic
  segments, invalid entry points, allocation/mapping failure, segment copying,
  BSS zero-fill, and unload cleanup.
- QEMU UEFI serial test covers user isolation, invalid `exec` rollback, fork,
  an observed timer-driven user preemption, successful ELF `exec`, inherited
  serial descriptor use, invalid wait-status pointer handling, blocking
  `waitpid`, any-child wait, exit status collection, and process cleanup.

Signals, pipes, shared memory, multithreaded processes, dynamic ELF, and VFS
path-based `exec` remain unsupported.
