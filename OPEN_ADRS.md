OpenSillicon — Open Architecture Decision Records

Version: 1.0
Status: Open decisions

This document lists architectural decisions that have not yet been finalized.

The coding agent must not silently make these decisions.

When implementation depends on one of them, stop and request clarification or propose an ADR.

---

ADR-001 — System Call ABI

Status: Accepted

Decision (Phase 6): use `int 0x80` for syscall entry. `RAX` carries the
syscall number and return value; arguments use `RDI`, `RSI`, `RDX`, `R10`,
`R8`, and `R9` in order. Success clears CF. Errors return a positive errno
value in `RAX` and set CF. Initial append-only numbers are 0 (`get ABI
version`), 1 (`exit`), 2 (`write`), and 3 (`getpid`). The ABI version returned
by syscall 0 is 1. Phase 7 appends syscall 4 (`fork`), 5 (`exec`), and 6
(`waitpid`); `wait()` uses `waitpid(-1, status)`. New calls append numbers;
assigned numbers and semantics are not reused or changed. This initial kernel supports a 4096-byte maximum
single `write`, standard descriptors 1 and 2 for serial output, and the
minimal process identity value 1.

Rationale: the interrupt gate offers a simple, debuggable entry path while
Ring 3 support is new. The explicit CF error convention distinguishes errors
from successful values without reserving a range of successful returns.
Fast `syscall`/`sysret` entry can be considered later without changing this
userspace calling convention.

Define the exact syscall ABI.

Questions:

- Which registers carry syscall numbers and arguments?
- Which instruction is used for syscall entry?
- How are return values represented?
- How are errors represented?
- Which initial syscall numbers are required?
- How should the ABI evolve without breaking userspace?

These questions are resolved by this decision for ABI version 1. Later calls
require a compatible append-only extension.

Reference: BSD and FreeBSD syscall conventions.

Binary compatibility with FreeBSD is not required.

---

ADR-002 — Virtual Memory Layout

Status: Accepted

Decision (Phase 4): use x86_64 four-level canonical address space. Keep the
existing kernel image base at `0xffffffff80000000`. Retain the HHDM offset
reported by Limine as the direct-map base; do not assume it is a fixed address.
The lower canonical half is reserved for userspace, with `[0, 0x10000)` kept
unmapped and the top canonical page reserved as a guard. Kernel vmalloc uses
`0xffffc00000000000` through `0xffffe00000000000`; the kernel heap uses
`0xffffe00000000000` through `0xfffff00000000000`. The implementation must
validate these regions against the actual HHDM span and loaded kernel image
before using them. The kernel image and boot HHDM remain mapped in every
address space; user mappings are isolated to the lower half. Stack and
memory-mapped-file regions are allocated within the userspace range and use
unmapped guard pages.

Alternatives considered:

- Fixed HHDM base: simpler address arithmetic, but conflicts with Limine's
  runtime-provided offset and the approved boot interface.
- One dynamic high-half allocator for all kernel virtual mappings: more
  flexible, but needlessly combines heap and general mapping lifetimes.

The accepted layout preserves the existing linked kernel address and HHDM
contract while giving vmalloc and heap distinct, checkable ranges. Four-level
paging is the compatibility constraint; five-level paging is deferred.

---

ADR-003 — SilliconFS On-Disk Format

Status: Open

Define the complete filesystem format.

Questions:

- Superblock layout.
- Superblock location.
- Inode table layout.
- Inode structure.
- Directory entry structure.
- Free block bitmap.
- Free inode bitmap.
- Block mapping strategy.
- Maximum file size.
- Maximum filesystem size.
- Filename limits.
- Metadata timestamps.
- Feature flags.
- Version compatibility.
- Filesystem corruption detection.

Initial requirements:

- Inode-based.
- 4 KiB logical filesystem blocks.
- Little-endian.
- Versioned format.
- Feature flags.
- No journaling initially.
- Unix-style permissions.

A formatter and validation tool should be developed alongside the filesystem.

---

ADR-004 — initramfs and switch-root

Status: Open

Define the initial ramdisk format and root transition.

Questions:

- Which archive format should initramfs use?
- How is the archive passed by Limine?
- How are files extracted or accessed?
- How is the real root filesystem located?
- Which process performs switch-root?
- What happens to the old initramfs?
- How are mount points transferred?

The system should boot using initramfs and later mount SilliconFS as the persistent root filesystem.

---

ADR-005 — VFS Contracts

Status: Open

Define exact VFS interfaces.

Questions:

- vnode structure.
- Inode association.
- File object structure.
- File operations.
- Directory operations.
- Mount operations.
- Path lookup.
- Reference counting.
- Caching.
- Locking.
- Error conventions.
- Filesystem registration.

Keep the VFS independent from SilliconFS-specific implementation details.

---

ADR-006 — Process, Signals and IPC Semantics

Status: Accepted

Decision (Phase 7): a process is single-threaded and uses one isolated user
address space. `fork()` eagerly copies the parent's user pages; it returns the
child PID to the parent and zero to the child. `exec()` takes a pointer and
length for a static x86_64 ELF64 image in the calling process and replaces
that process image only after the replacement image has loaded successfully.
This memory-buffer interface is temporary until VFS path lookup exists.
`waitpid()` blocks for a direct child PID; `wait()` waits for any direct child.
Exit status is the low 8 bits of the supplied status; a non-null wait status
pointer receives that single byte. Exited children remain as zombies until
reaped; reaping releases process memory and its descriptor table. Fork inherits
the descriptor table. The initially supported standard
descriptors retain their existing serial behavior.

The scheduler remains single-CPU. LAPIC timer interrupts preempt userspace by
saving and restoring the complete user interrupt frame on each process's
kernel stack. Kernel threads continue to use cooperative Round Robin. Signals,
pipes, shared memory, and multithreaded processes remain deferred.

The process syscall numbers append to ABI v1: 4 (`fork`), 5 (`exec`), and 6
(`waitpid`). `wait()` uses `waitpid(-1, status)`. PID allocation is monotonic
for the lifetime of the kernel and does not reuse identifiers.

Rationale: eager copying keeps address-space ownership explicit before
copy-on-write exists. Direct-child waits and a memory-buffer `exec` avoid
requiring VFS and path resolution ahead of Phase 8. Per-process kernel stacks
make timer-driven user preemption compatible with the existing interrupt
frame and TSS entry model.

Define process and IPC behavior.

Questions:

- Exact "fork()" semantics.
- Exact "exec()" behavior.
- "wait()" and "waitpid()" behavior.
- Process exit status.
- Zombie process handling.
- Signal set.
- Signal delivery rules.
- Signal masks.
- Signal handlers.
- Pipe semantics.
- Shared memory lifecycle.
- File descriptor inheritance.

Use practical Unix/POSIX behavior as a reference without promising full POSIX compliance.

These decisions resolve process creation, replacement, exit, wait, and
descriptor inheritance for Phase 7. The remaining IPC and signal questions
stay open for later milestones.

---

ADR-007 — Synchronization and Interrupt Context

Status: Open

Define synchronization primitives.

Questions:

- Spinlocks.
- Mutexes.
- Semaphores.
- Condition variables.
- Interrupt-safe locks.
- Lock ordering.
- Preemption control.
- Interrupt context restrictions.
- Scheduler interaction.
- Future SMP requirements.

The design must avoid deadlocks and unsafe sleeping in interrupt context.

---

ADR-008 — ACPICA Integration Profile

Status: Open

Define the ACPICA integration strategy.

Questions:

- Which ACPICA version?
- Which source modules are required?
- Which OS services must be implemented?
- Which ACPICA features can be excluded?
- How are ACPI tables discovered?
- How are AML methods executed?
- How are ACPI events handled?

Check the license of the exact version and source subset before integration.

OpenSillicon must provide its own ACPICA glue layer.

---

ADR-009 — POSIX.1-2008 Compatibility Profile

Status: Open

Define the practical POSIX subset.

Questions:

- Which system calls are required?
- Which libc interfaces are required?
- Which file operations are required?
- Which process semantics are supported?
- Which signals are supported?
- Which terminal interfaces are supported?
- Which unsupported features return errors?

The goal is practical compatibility, not complete POSIX compliance.

---

ADR-010 — Filesystem Hierarchy and Account Policy

Status: Open

Define the exact filesystem hierarchy and account behavior.

Proposed hierarchy:

- "/bin"
- "/sbin"
- "/usr"
- "/etc"
- "/var"
- "/dev"
- "/tmp"
- "/boot"
- "/proc"

Questions:

- Which directories are symbolic links?
- Which directories are writable during normal operation?
- How are users created?
- How are passwords stored?
- Which shell is assigned by default?
- How are system accounts represented?
- How are service accounts handled?

Use BSD-like conventions.

---

ADR-011 — Xorg Graphics Backend

Status: Open

Define the graphics interfaces required for a real Xorg server.

Questions:

- Required framebuffer interfaces.
- Display device abstraction.
- Modesetting.
- Memory mapping.
- Input device interfaces.
- DRM/KMS-like API scope.
- Required Xorg drivers.
- Whether a custom DDX driver is necessary.

Do not claim Xorg support until an actual Xorg server runs.

---

ADR-012 — Network Stack and Socket API

Status: Open

Define the networking architecture.

Questions:

- Native stack or third-party stack?
- Network device interface.
- Ethernet implementation.
- IPv4 scope.
- IPv6 scope.
- ARP.
- ICMP.
- UDP.
- TCP.
- Routing.
- Socket ABI.
- DNS resolver.
- DHCP.

The network stack should follow BSD-inspired conventions where practical.

---

ADR-013 — Compiler, Linker and Binary Flags

Status: Accepted

Decision (Phase 0): Use the installed system GCC and GNU binutils for x86_64
freestanding kernel builds. GCC compiles with `-ffreestanding`,
`-fno-stack-protector`, and `-mno-red-zone`; GNU ld links with `-nostdlib` and
`-static`, using `kernel/linker.ld`. Kernel and userspace flags remain
separate. The measured development baseline is GCC 16.2.1 and binutils 2.46.1;
these are environment versions, not hard minimums.

Rationale: the host compiler targets x86_64 ELF and can produce a freestanding
kernel without Linux runtime components. This was explicitly approved for
Phase 0. An `x86_64-elf` cross toolchain is not required.

Initial kernel flags use `-O2 -g`, disable stack protection and PIC/PIE, disable
MMX/SSE and use soft-float until the kernel configures CPU state, and use the
x86_64 kernel code model. Future release and userspace flags remain to be
specified when those targets are introduced.

Requirements:

- C11.
- System GCC and GNU binutils targeting x86_64 ELF.
- Freestanding kernel.
- No red zone in kernel.
- Separate kernel and userspace configurations.
- Warnings treated as errors.

---

ADR-014 — Block Layer and Sector Semantics

Status: Open

Define the block device interface.

Questions:

- Logical sector size representation.
- Physical sector size representation.
- Alignment requirements.
- Read/write API.
- Multi-sector operations.
- Flush semantics.
- Device error reporting.
- Request queue design.
- Cache interaction.

Must support devices with 512-byte and 4096-byte logical sectors.

Filesystem blocks must remain independent of device sector size.

---

ADR-015 — Keyboard Layouts and Terminal Input

Status: Open

Define keyboard input architecture.

Questions:

- Raw scancode representation.
- Keycode abstraction.
- Keyboard layout format.
- Modifier handling.
- Dead keys.
- Unicode input.
- Layout switching.
- Terminal line discipline.
- Input buffering.
- TTY signal generation.

The keyboard driver must not directly convert raw hardware input into terminal characters.

---

ADR-016 — Limine Boot and Deployment Profile

Status: Accepted

Decision (Phase 0): Pin upstream Limine v12.9.6 at commit
`89a9512c623a4779cc73e3869383876d54264b28`, distributed as the upstream
`limine-binary.tar.xz` release asset with SHA-256
`939f670e0831cc940453fd3a0ddfb2b324c0b4e3c5d0e741c190aa0d13df0103`.
The implementation uses only its x86_64 UEFI executable and UEFI CD image.
The release is BSD-2-Clause; required third-party notices are retained. The
Limine protocol header is pinned to dependency commit
`6e85ddd6f928b5b512015c4d47d1cfce2fd28079` and is 0BSD. UEFI is the only
deployment profile in this milestone; BIOS support remains deferred.

The kernel is ELF64 and uses Limine base revision 3. The ISO follows Limine's
documented UEFI image layout. Artifact hashes and license files are recorded in
`limine/PROVENANCE.md`.

Questions resolved for Phase 0:

- Limine version and protocol revision are pinned above.
- The UEFI CD boot image provides `EFI/BOOT/BOOTX64.EFI`.
- The kernel image is x86_64 ELF64 and configuration is `limine.conf`.
- initramfs, memory map, and framebuffer requests are deferred to later
  milestones.
- Legacy BIOS remains deferred.

UEFI is the initial target. Legacy BIOS is deferred.

---

ADR-017 — Timer Calibration and Timekeeping

Status: Accepted

Decision (Phase 5): run the LAPIC timer periodically at 100 Hz with divide-by-16.
Calibrate its initial count against PIT channel 2 over one 10 ms interval.
Mask both legacy PICs before enabling the LAPIC timer. The timer interrupt
increments a 64-bit monotonic tick count and sends LAPIC EOI; it does not
schedule from interrupt context. Kernel threads use cooperative Round Robin
switching at explicit yield points, consistent with the non-preemptive kernel
policy. User preemption begins with Ring 3 support in Phase 6. HPET discovery
and timekeeping are deferred until ACPI table support in Phase 10. TSC and wall
clock support are deferred. The timer tick is the initial time unit; sleeps
and sub-tick timeouts are not provided in this phase.

PIT channel 2 is a small, widely available calibration source that does not
depend on ACPI table discovery. An HPET reference would improve the timer
profile but would pull ACPI discovery earlier; using TSC frequency data would
depend on CPU features and firmware reporting. Both alternatives are deferred.

---

ADR Process

When an ADR is proposed:

1. Assign the next available ADR number.
2. Describe the problem.
3. List the relevant constraints.
4. Present alternatives.
5. Explain trade-offs.
6. Recommend one option.
7. Wait for approval.
8. Record the final decision.
9. Update "DESIGN.md" if necessary.
10. Update "ROADMAP.md" if implementation order changes.

Do not implement a major unresolved architectural decision without approval.
