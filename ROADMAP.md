OpenSillicon — Development Roadmap

Version: 1.0
Status: Initial roadmap

This roadmap defines the implementation order for OpenSillicon.

Development must proceed sequentially. A milestone must be completed and tested before starting the next one.

---

Development Rules

1. Work only on the first unfinished milestone.
2. Do not start later milestones prematurely.
3. Keep changes focused on the current milestone.
4. Build and test after implementation.
5. Run regression tests for previous milestones.
6. Do not mark a milestone complete if required tests fail.
7. Create one Git commit after each completed milestone.
8. Do not commit unfinished or broken work.
9. Preserve unrelated user modifications.
10. Update documentation when architecture or interfaces change.

---

Phase 0 — Toolchain and Boot Skeleton

Goal

Create the minimum bootable OpenSillicon kernel.

Tasks

- Create project directory structure.
- Configure system GCC and GNU binutils for freestanding x86_64 kernel builds.
- Configure GNU Make.
- Integrate Limine.
- Configure initial UEFI boot.
- Create minimal C11 kernel entry point.
- Add minimal x86_64 assembly where required.
- Implement serial output.
- Implement basic kernel logging.
- Create a bootable disk image.

Done criteria

- Cross compiler works.
- Kernel builds without warnings.
- Limine starts the kernel.
- Kernel reaches its entry point.
- Kernel prints a boot message over serial.
- Kernel runs successfully in QEMU.

Commit

"Phase 0: establish toolchain and bootable kernel skeleton"

---

Phase 1 — Boot Information and Early Memory

Goal

Read bootloader information and establish early memory infrastructure.

Tasks

- Parse Limine boot information.
- Read memory map.
- Read framebuffer information.
- Read basic firmware information.
- Implement bootstrap allocator.
- Establish early physical memory bookkeeping.

Done criteria

- Boot information is parsed correctly.
- Memory map is validated.
- Reserved memory is not allocated.
- Bootstrap allocations work.
- Invalid boot information is handled safely.

---

Phase 2 — CPU, Exceptions and Interrupts

Goal

Establish reliable CPU initialization and exception handling.

Per the approved dependency adjustment, LAPIC initialization moves to Phase 4
after MMIO mapping is available, and IOAPIC initialization moves to Phase 10
after ACPI MADT support.

Tasks

- Implement GDT.
- Implement IDT.
- Configure TSS.
- Implement CPU exception handlers.
- Implement interrupt dispatch.

Done criteria

- CPU exceptions are reported correctly.
- Invalid memory access causes a diagnostic page fault.
- Interrupt dispatch works.
- Kernel panic is reliable.

---

Phase 3 — Physical Memory Manager

Goal

Implement physical page allocation.

Tasks

- Implement bitmap PMM.
- Initialize from Limine memory map.
- Reserve kernel and bootloader memory.
- Reserve framebuffer and other required regions.
- Implement page allocation.
- Implement page release.
- Add allocation diagnostics.

Done criteria

- Physical pages can be allocated and released.
- Reserved regions are never allocated.
- Bitmap state is consistent.
- Exhaustion is handled safely.
- Tests cover allocation and release.

---

Phase 4 — Virtual Memory Manager

Goal

Establish proper virtual memory management.

Tasks

- Implement four-level x86_64 paging.
- Implement page mapping and unmapping.
- Map the LAPIC MMIO page and initialize the local APIC.
- Implement kernel higher-half mappings.
- Establish userspace address-space layout.
- Implement page fault handling.
- Implement address-space management.
- Add kernel heap.

Done criteria

- Kernel mappings work.
- Page permissions are enforced.
- LAPIC MMIO is accessible and local APIC initialization succeeds in QEMU.
- Address spaces can be created and destroyed.
- Invalid accesses are detected.
- Kernel heap allocation works.
- Memory tests pass.

---

Phase 5 — Timer and Scheduler

Goal

Introduce scheduling and preemption.

Tasks

- Initialize LAPIC Timer.
- Mask the legacy PIC before enabling LAPIC timer interrupts.
- Add HPET support for calibration and timekeeping where available.
- Implement timer interrupt handling.
- Implement kernel thread abstraction.
- Implement Round Robin scheduler.
- Introduce process scheduling structures.
- Prepare SMP-compatible interfaces.

Done criteria

- Timer interrupts occur reliably.
- Multiple kernel threads can execute.
- Round Robin scheduling works.
- Scheduler does not corrupt kernel state.
- Timer and scheduler regression tests pass.

---

Phase 6 — User Mode and System Calls

Goal

Introduce Ring 3 execution and a syscall interface.

Tasks

- Implement user-mode transition.
- Implement syscall entry and return.
- Define initial syscall ABI.
- Implement basic process memory protection.
- Implement basic file descriptor abstractions.

Done criteria

- Kernel can enter Ring 3.
- User code executes.
- User code cannot access protected kernel memory.
- System calls work.
- Invalid syscall requests are handled safely.

---

Phase 7 — ELF64 and Processes

Goal

Run independent userspace programs.

Tasks

- Implement ELF64 loader.
- Implement process creation.
- Implement "fork()".
- Implement "exec()".
- Implement "wait()" and "waitpid()".
- Implement "exit()".
- Implement process cleanup.
- Add basic process scheduling.

Done criteria

- Static ELF64 programs load.
- Processes have isolated address spaces.
- Parent and child processes behave correctly.
- Process exit releases resources.
- Invalid executables fail safely.

---

Phase 8 — initramfs, RAMFS and VFS

Goal

Introduce filesystem abstractions and initial filesystems.

Tasks

- Define VFS interfaces.
- Implement vnode model.
- Implement path resolution.
- Implement file descriptors.
- Implement directory operations.
- Implement RAMFS.
- Parse initial initramfs format.
- Implement basic mounting.

Done criteria

- Files can be opened, read, written and closed.
- Directories can be traversed.
- Path resolution works.
- RAMFS works.
- initramfs contents are accessible.
- VFS tests pass.

---

Phase 9 — SilliconFS

Goal

Implement the native filesystem.

Tasks

- Finalize on-disk format through ADR.
- Implement filesystem formatter.
- Implement superblock handling.
- Implement inode allocation.
- Implement block allocation.
- Implement directories.
- Implement file read/write.
- Implement metadata and permissions.
- Implement filesystem validation.

Done criteria

- SilliconFS can be formatted.
- Files and directories can be created.
- Files can be read and written.
- Filesystem can be mounted.
- Corrupt structures are detected safely.
- Tests cover disk-image operations.

Journaling is not required in this milestone.

---

Phase 10 — PCI, ACPI, AHCI and Root Switch

Goal

Introduce real hardware discovery and persistent root storage.

Tasks

- Implement PCI/PCIe enumeration.
- Integrate ACPICA with OpenSillicon glue.
- Discover ACPI MADT through ACPICA and initialize the IOAPIC.
- Implement AHCI driver.
- Implement block device interface.
- Support 512-byte and 4096-byte logical sectors.
- Detect storage devices.
- Mount SilliconFS.
- Implement switch-root from initramfs.

Done criteria

- PCI devices are enumerated.
- ACPI tables are parsed.
- IOAPIC initializes using ACPI MADT data.
- AHCI controller is detected.
- SATA storage is accessible.
- SilliconFS mounts from a disk image or virtual disk.
- System can switch to persistent root filesystem.

---

Phase 11 — Input, TTY and Terminal

Goal

Provide usable keyboard input and terminal output.

Tasks

- Implement PS/2 keyboard driver.
- Implement PS/2 mouse driver.
- Add raw keycode handling.
- Add keyboard layout translation.
- Implement TTY.
- Implement virtual terminals.
- Implement ANSI/VT100-compatible subset.
- Add bitmap font rendering.

Done criteria

- Keyboard input works.
- Mouse input works.
- Terminal input is reliable.
- Terminal output supports basic control sequences.
- Multiple virtual terminals work.

---

Phase 12 — Init, Shell and Accounts

Goal

Create a usable minimal Unix-like userspace.

Tasks

- Implement init.
- Add BSD-style rc.d startup infrastructure.
- Implement minimal shell.
- Add basic commands.
- Implement "/etc/passwd".
- Implement "/etc/group".
- Add root and regular users.
- Add simple system logging.

Done criteria

- System boots into userspace.
- Init starts required services.
- Shell accepts commands.
- Root account works.
- Regular user accounts work.
- Basic commands operate on the filesystem.

---

Phase 13 — IPC and POSIX Subset

Goal

Introduce basic inter-process communication.

Tasks

- Implement pipes.
- Implement signals.
- Implement shared memory.
- Define synchronization rules.
- Improve process waiting and lifecycle semantics.
- Document supported POSIX behavior.

Done criteria

- Processes communicate through pipes.
- Signals can be delivered and handled.
- Shared memory works with proper isolation.
- Process synchronization is reliable.
- Tests cover IPC failure cases.

---

Phase 14 — musl and Static Userspace

Goal

Support a practical C userspace environment.

Tasks

- Port musl.
- Implement missing syscall compatibility.
- Implement required libc interfaces.
- Configure static linking.
- Build basic userspace programs.

Done criteria

- musl builds for OpenSillicon.
- Static userspace programs execute.
- Basic libc functionality works.
- Unsupported interfaces fail predictably.

---

Phase 15 — Networking

Goal

Introduce basic networking.

Tasks

- Define network stack architecture through ADR.
- Implement network device interface.
- Add Ethernet.
- Add IPv4.
- Add ARP.
- Add ICMP.
- Add UDP.
- Add TCP.
- Introduce socket API.
- Add basic routing.

Done criteria

- Network device can transmit and receive packets.
- Basic IPv4 connectivity works.
- UDP sockets work.
- TCP connections work.
- Network tests pass.

---

Phase 16 — Xorg Groundwork

Goal

Prepare the graphics subsystem for a real Xorg server.

Tasks

- Define graphics device interfaces.
- Improve framebuffer API.
- Introduce DRM/KMS-like abstractions where justified.
- Implement required input interfaces.
- Improve terminal and graphics coexistence.
- Investigate Xorg porting requirements.

Done criteria

- Graphics interfaces are documented.
- Display and input interfaces are stable.
- Xorg integration requirements are understood.
- No claim of Xorg compatibility is made until an actual Xorg server runs.

---

Deferred Features

The following are not required for the initial roadmap:

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
- Secure Boot.
- TrueType fonts.
- Advanced networking.
- Xorg support.

---

Milestone Completion Policy

A milestone is complete only when:

1. All required tasks are implemented.
2. The project builds successfully.
3. QEMU smoke tests pass.
4. Relevant subsystem tests pass.
5. Previous regression tests pass.
6. Failure paths are tested where applicable.
7. Documentation is updated.
8. Git diff is reviewed.
9. A focused commit is created.

If a test fails, the milestone remains unfinished.

The next milestone must not begin until the current one is complete.
