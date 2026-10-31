OpenSillicon — Open Architecture Decision Records

Version: 1.0
Status: Open decisions

This document lists architectural decisions that have not yet been finalized.

The coding agent must not silently make these decisions.

When implementation depends on one of them, stop and request clarification or propose an ADR.

---

ADR-001 — System Call ABI

Status: Open

Define the exact syscall ABI.

Questions:

- Which registers carry syscall numbers and arguments?
- Which instruction is used for syscall entry?
- How are return values represented?
- How are errors represented?
- Which initial syscall numbers are required?
- How should the ABI evolve without breaking userspace?

Reference: BSD and FreeBSD syscall conventions.

Binary compatibility with FreeBSD is not required.

---

ADR-002 — Virtual Memory Layout

Status: Open

Define exact virtual address ranges.

Questions:

- Kernel higher-half base.
- Kernel image range.
- Direct physical map range.
- Kernel heap range.
- vmalloc range.
- User address-space range.
- Stack layout.
- Memory-mapped file region.
- Guard pages.
- Reserved address ranges.

Must be compatible with x86_64 four-level paging.

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

Status: Open

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

Status: Open

Define the timer and timekeeping subsystem.

Questions:

- LAPIC Timer calibration.
- HPET availability.
- TSC support.
- Invariant TSC detection.
- Clock source selection.
- Monotonic clock.
- Wall clock.
- Tick frequency.
- Timer resolution.
- Sleep and timeout APIs.

LAPIC Timer is the primary scheduling interrupt source. HPET is a secondary source for calibration and timekeeping where available.

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
