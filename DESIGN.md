OpenSillicon — Architecture and Design

Version: 1.0
Status: Approved architecture baseline
Architecture: x86_64, modular monolithic kernel
License: BSD-2-Clause

---

1. Project Overview

OpenSillicon is an experimental Unix-like operating system developed from scratch.

The project takes inspiration from BSD operating systems, particularly FreeBSD, for system interfaces, filesystem hierarchy, kernel organization, and userspace conventions.

OpenSillicon is not a FreeBSD fork and does not aim to provide FreeBSD binary compatibility.

Primary goals

- Simplicity and maintainability.
- Good performance.
- Stable and predictable architecture.
- Minimal unnecessary dependencies and abstractions.
- Gradual development through independently testable milestones.
- A practical Unix-like environment.

Target platform

- Architecture: x86_64.
- Bootloader: Limine.
- Boot protocol: Limine Boot Protocol.
- Initial firmware: UEFI.
- Legacy BIOS support: later.
- Kernel language: C11.
- Assembly: x86_64 Assembly, only where necessary.

---

2. Toolchain and Build System

Required tools

- System GCC and GNU binutils capable of targeting x86_64 ELF.
- GNU Make — build system.
- GNU binutils — assembler and linker.
- Limine — bootloader.
- QEMU — virtual machine and testing.
- Git — version control.

Compiler requirements

Kernel compilation must use freestanding options.

At minimum:

- "-ffreestanding"
- "-fno-stack-protector" where required by the early boot environment.
- "-mno-red-zone"
- "-Wall"
- "-Wextra"
- "-Werror"

Kernel and userspace must have separate compiler and linker configurations.

The kernel must not accidentally depend on host operating system headers or libraries.
Use freestanding compiler flags and link without startup files or libraries.

---

3. Kernel Architecture

OpenSillicon uses a modular monolithic kernel.

Kernel execution model

- Kernel runs in Ring 0.
- Userspace applications run in Ring 3.
- Kernel subsystems communicate through internal interfaces.
- Drivers initially execute inside the kernel.
- Static kernel modules are supported first.
- Dynamic module loading may be implemented later.

Kernel design principles

- Keep subsystem interfaces explicit.
- Avoid unnecessary abstraction layers.
- Separate hardware-specific code from generic kernel logic.
- Avoid mixing userspace and kernel responsibilities.
- Prefer small, independently testable components.

Source tree

Use a BSD-inspired source organization.

A proposed layout:

boot/
kernel/
    arch/
        x86_64/
    cpu/
    mm/
    proc/
    sched/
    ipc/
    sys/
    vfs/
    dev/
    net/
    lib/
drivers/
    bus/
    storage/
    input/
    video/
fs/
    ramfs/
    silliconfs/
include/
    kernel/
    arch/
userland/
    init/
    shell/
    bin/
    sbin/
    lib/
tests/
tools/
docs/

The exact directory structure may evolve, but subsystem separation must remain clear.

---

4. CPU, Interrupts and Timers

CPU initialization

The kernel must implement:

- GDT.
- IDT.
- CPU exception handling.
- Interrupt handling.
- TSS.
- Ring 3 transition support.

Interrupt controllers

- LAPIC initializes after the VMM can map its MMIO page.
- IOAPIC initialization waits for ACPI table support and ACPICA in Phase 10.
- PIC compatibility only where necessary during early boot.

Timers

- LAPIC Timer is the primary scheduling interrupt source.
- HPET is a secondary time source and may be used for calibration.
- Timer calibration and timekeeping details require an ADR.

Scheduling

Initial scheduling model:

- Round Robin.
- Equal priority for all processes initially.
- Kernel initially uses a non-preemptive execution model.
- Userspace processes are preempted by timer interrupts.
- SMP interfaces should be considered from the beginning.
- Only one CPU is activated initially.

Panic handling

Kernel panic must:

- Print a clear error message.
- Include diagnostic information where available.
- Output through serial logging.
- Stop execution safely.

Kernel debugging symbols must be preserved in development builds.

---

5. Memory Management

Virtual memory

- x86_64 four-level paging initially.
- Kernel occupies higher-half virtual memory.
- Userspace occupies lower-half virtual memory.
- User address spaces must be isolated.
- Kernel mappings must be managed consistently.

ADR-002 defines the address ranges: the existing `0xffffffff80000000` kernel
image base; Limine's runtime HHDM offset for the physical direct map; lower
canonical userspace with the null 64 KiB and top page reserved; vmalloc at
`0xffffc00000000000`–`0xffffe00000000000`; and kernel heap at
`0xffffe00000000000`–`0xfffff00000000000`. The VMM validates arena overlaps
against the boot HHDM span and linked kernel image before using them.

Physical memory management

Use a bitmap-based Physical Memory Manager (PMM).

Initialization sequence:

1. Read memory map from Limine.
2. Initialize a bootstrap allocator.
3. Initialize bitmap PMM.
4. Initialize Virtual Memory Manager.
5. Initialize kernel heap.
6. Introduce slab allocation later.

Virtual memory features

Planned:

- Page allocation and mapping.
- Address-space creation and destruction.
- Demand paging.
- Copy-on-write.
- Memory protection.
- Page fault handling.

Swap support is deferred.

Allocation failures

Allocation functions must report failure to callers.

The kernel must not silently ignore allocation failures.

Automatic process termination under memory pressure is not permitted until an explicit policy is designed.

---

6. Processes and Userspace

Process model

OpenSillicon uses a Unix-style process model.

Planned system calls:

- "fork()"
- "exec()"
- "wait()"
- "waitpid()"
- "exit()"

The exact semantics and supported flags require an ADR.

Executable format

- ELF64.
- Static linking initially.
- Dynamic linker and shared libraries later.

System call ABI

OpenSillicon will have its own BSD-inspired syscall ABI.

FreeBSD may be used as a reference, but binary compatibility is not a goal.

The syscall ABI must be documented and versioned carefully.

Userspace

The system will provide:

- Minimal init process.
- Basic shell.
- Basic command-line utilities.
- User and group management.
- File descriptor interface.
- Static executable support.

C library

musl is the intended userspace C library.

Porting musl is planned after the basic process and syscall infrastructure is stable.

Threads

- Kernel threads are planned.
- POSIX threads are deferred until the process model is stable.

---

7. IPC and Synchronization

Initial IPC mechanisms:

- Pipes.
- Signals.
- Shared memory.

Signals should follow a POSIX-like model.

File descriptors are the common interface for userspace I/O.

Synchronization primitives must be designed with interrupt context and future SMP support in mind.

The exact synchronization model requires an ADR.

---

8. Users, Permissions and Security

User model

- Root user has UID 0.
- Regular users have separate UIDs.
- Groups use GIDs.
- BSD-like account conventions.
- "/etc/passwd"
- "/etc/group"

File permissions

Use Unix-style permissions:

- Owner.
- Group.
- Others.
- Read, write, execute.

Capabilities are not part of the initial design.

ASLR is a future security feature.

Secure Boot is not a requirement.

---

9. VFS and Filesystems

Virtual File System

Implement a full VFS abstraction.

Use a BSD-like vnode model.

Important distinction:

- A vnode is an active VFS representation of a filesystem object.
- An inode is a persistent filesystem-specific object.

They are related, but they are not interchangeable.

The VFS must provide a consistent interface for:

- File operations.
- Directory operations.
- Mounting.
- File metadata.
- Permissions.
- Path resolution.

Exact VFS contracts require an ADR.

SilliconFS

SilliconFS is the native OpenSillicon filesystem.

Initial design:

- Inode-based.
- 4 KiB logical filesystem blocks.
- Little-endian on-disk format.
- Versioned format.
- Feature flags for future extensions.
- No journaling initially.
- Unix-style permissions.
- BSD-like mount model.

The filesystem must be designed to support future expansion without breaking older disk images unnecessarily.

The exact on-disk format requires an ADR.

Block device considerations

Filesystem block size and physical disk sector size are different concepts.

The block layer must account for devices exposing:

- 512-byte logical sectors.
- 4096-byte logical sectors.
- 512e physical sectors.
- 4Kn sectors.

A 4 KiB filesystem block must not imply that every disk uses 4 KiB sectors.

Initial filesystems

- initramfs.
- RAMFS.
- SilliconFS.

The system boots using initramfs, initializes the native filesystem, mounts it as root, and performs a switch-root operation.

---

10. Device Drivers

Initial hardware support

Priority order:

1. Serial output.
2. Limine framebuffer.
3. Keyboard.
4. Basic input infrastructure.
5. PCI enumeration.
6. AHCI storage.

Bus support

PCI/PCIe enumeration must be implemented early.

USB support is deferred.

Storage

Initial storage target:

- AHCI SATA.

Later:

- NVMe.

Input

Initial input:

- PS/2 keyboard.
- PS/2 mouse.

Later:

- USB HID.

Keyboard drivers should provide raw keycodes/scancodes.

A separate input and TTY layer converts key events into characters and terminal input.

Audio

Audio is not required for the initial system.

---

11. Graphics, TTY and Terminal

Framebuffer

Use the framebuffer provided by Limine during early development.

A separate VESA/GOP driver is not required initially.

Implement an OpenSillicon framebuffer API.

Terminal

Implement:

- Virtual terminals.
- TTY subsystem.
- ANSI/VT100-compatible terminal subset.
- Bitmap fonts initially.
- TrueType font support later.

Graphics architecture

The long-term goal is support for a real Xorg server.

OpenSillicon will not initially develop its own X11 server or compositor.

A DRM/KMS-like interface may be introduced later.

This does not imply Linux DRM driver compatibility.

Framebuffer support alone does not mean Xorg is ready to run.

---

12. Initialization and Services

Use a BSD-inspired startup model.

Planned:

- Own minimal init.
- BSD-style rc.d service scripts.
- Simple syslog-like logging.
- Basic account initialization.
- Mounting of required filesystems.
- Starting the shell.

The initial userspace must remain minimal.

---

13. Networking

Networking is deliberately postponed until the core operating system is stable.

Future goals:

- BSD-inspired network stack.
- Socket API.
- Basic Ethernet support.
- IPv4.
- TCP and UDP.
- Routing.

The exact protocol scope and socket ABI require an ADR.

---

14. Testing and Quality

Every milestone must be tested before it is considered complete.

Required tests

- Full build.
- QEMU boot test.
- Serial output verification.
- Relevant subsystem tests.
- Regression tests for previously completed milestones.
- Host-side unit tests where practical.
- Failure-path tests where applicable.

CI is not required initially.

Build policy

Warnings should be treated as errors.

Exceptions must be documented and justified.

No feature may be marked complete merely because it compiles.

---

15. Code Style

- C11.
- Procedural and modular design.
- K&R / BSD-inspired formatting.
- Small functions.
- Clear ownership of resources.
- Explicit error handling.
- Minimal unnecessary abstractions.
- Minimal assembly.

Kernel code must remain freestanding.

Kernel and userspace code must not share inappropriate assumptions about runtime libraries.

---

16. Licensing

The project uses BSD-2-Clause.

Third-party components must have their licenses reviewed before integration.

In particular, ACPICA integration must be checked for license compatibility and the exact subset required.

---

17. Git Policy

- Main development branch: "main".
- One commit after each completed milestone.
- Commits must be small and focused.
- Commit messages must describe the completed milestone or fix.
- Check "git status" and the diff before committing.
- Preserve unrelated user changes.
- Never commit unfinished or knowingly broken work.
- Never commit if required tests fail.

The coding agent must report:

- Commit hash.
- Commit message.
- Changed files.
- Build commands.
- Test results.
- Known limitations.

---

18. Deferred Features

The following are explicitly postponed:

- SMP runtime.
- Dynamic kernel modules.
- USB.
- NVMe.
- Audio.
- Swap.
- ASLR.
- Dynamic linker.
- Shared libraries.
- POSIX threads.
- Full POSIX compliance.
- Networking.
- Xorg.
- TrueType fonts.
- Secure Boot.

These features must not distract from completing the foundational milestones.

---

19. Architectural Change Policy

Significant architectural decisions must be documented in ADRs.

The coding agent must not silently change approved architecture.

If an implementation requires a decision that is not covered by this document or an existing ADR, the agent must stop and request clarification or propose an ADR.

This document is the approved architectural baseline for OpenSillicon.
