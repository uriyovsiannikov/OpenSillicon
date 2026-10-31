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
- Phase 10 validates MADT data through ACPICA and initializes the IOAPIC with
  every redirection entry masked; device IRQ routing is deferred.
- PIC compatibility only where necessary during early boot.

Timers

- LAPIC Timer is the primary scheduling interrupt source.
- The Phase 5 LAPIC timer runs at 100 Hz and is calibrated against PIT channel 2.
- The timer interrupt advances a monotonic tick count and acknowledges the
  LAPIC. HPET and TSC timekeeping remain deferred despite Phase 10 ACPI table
  support.

Scheduling

Initial scheduling model:

- Round Robin.
- Equal priority for all processes initially.
- Kernel initially uses a non-preemptive execution model.
- Kernel threads use cooperative Round Robin switching at explicit yield
  points; timer interrupts do not switch threads.
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
Stage 13C reserves `[0x0000700000000000, 0x00007fffffffc000)` for anonymous
shared mappings. It retains unmapped boundary and inter-mapping guard pages,
uses a monotonic per-address-space cursor, and excludes the range from ELF
segments; see ADR-021.

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

ADR-001 defines ABI version 1: `int 0x80`, syscall number/result in `RAX`,
arguments in `RDI`, `RSI`, `RDX`, `R10`, `R8`, and `R9`; success clears CF,
while errors return positive errno in `RAX` with CF set. Initial calls are
append-only: 0 (ABI version), 1 (exit), 2 (write), and 3 (getpid). Phase 6
initially provides serial output descriptors 1 and 2 and a 4096-byte write
limit. Phase 11 binds descriptor 0 to the canonical TTY and routes each
descriptor 1/2 write to the framebuffer terminal and serial console once.
Write also accepts open file descriptors when VFS support is present.

Phase 7 appends `fork` (4), `exec` (5), and `waitpid` (6); `wait()` is
`waitpid(-1, status)`. Phase 8 appends VFS calls 7–11 and `exec_path` (12).
ADR-006 defines eager-copy fork, static ELF exec from a user memory buffer
(syscall 5), static ELF path exec through the VFS (syscall 12), direct-child
wait behavior, and single-threaded processes. Both exec calls replace the
image only after successful loading; syscall 5's ABI remains unchanged.

Phase 8 appends `open` (7), `close` (8), `read` (9), `stat` (10),
`readdir` (11), and static ELF `exec_path` (12). `exec_path` takes a VFS path
in `RDI`, requires an executable regular file, and returns errors without
changing the current image. `write` remains syscall 2. File reads and writes
have a 4096-byte limit; `stat` returns a fixed-width type, mode, and size structure.
`readdir` returns one entry per call, then zero at end of directory. See
ADR-001 and ADR-005 for the ABI and VFS contracts.

Phase 12 appends syscall 13, `sleep_ticks(ticks)`, for init's restart delay.
`RDI` is an unsigned tick count. Zero returns immediately; positive requests
block the calling process until at least that many timer ticks have elapsed.
The scheduler keeps the process off the runnable set while asleep, and the
timer interrupt wakes it without polling. Requests larger than
`UINT64_MAX - current_tick` fail with `EINVAL`, preventing deadline overflow.
No clock query or nanosecond sleep interface is provided.

Phase 13A appends syscall 14, `pipe(fds)`, without changing any existing
syscall number or behavior. It returns a read descriptor and write descriptor
through an 8-byte `uint32_t[2]` at `RDI`. Pipe descriptors use existing
`read`, `write`, and `close`; the 4096-byte ring buffer, short I/O, EOF,
`EPIPE`, failure rollback, descriptor inheritance, and scheduler wait/wakeup
rules are specified in ADR-019. Pipe waits block processes and never busy-wait.
Phase 13A added anonymous pipes. Stage 13B adds only the minimal signals
specified in ADR-020: `SIGINT`, `SIGTERM`, and `SIGKILL`, with append-only
syscalls 15–17. Signal return uses a fixed nine-byte user restorer stub
(`B8 11 00 00 00 CD 80 0F 0B`); the kernel checks its bytes and executable
mapping, active-handler state, and exact syscall return site. Signal state,
delivery, and Ring 3 return selection are serialized with interrupts disabled.
`sigaction` validates and copies input, copies the previous action out, then
commits the new action, so output failure cannot partially change a
disposition. Fork during an active handler fails with `EBUSY`; other fork and
successful/failed exec signal-state rules are in ADR-020. Wait, sleep, and
pipe blocking can be interrupted as specified there. TTY reads are not
interruptible and no TTY blocking redesign is included. Stage 13C adds only
anonymous shared mappings through fork inheritance, using syscalls 18 and 19.
The mappings are zero-filled, RW/NX, and released by detach or address-space
destruction. Fork shares those backing pages while eagerly copying private
pages. Successful exec removes mappings; failed exec preserves them. Zombie
process mappings remain until reap. See ADR-021 for arena, guard, allocator,
and reference-count rules. Named shared memory, unrelated-process attach,
global object IDs, synchronization primitives, read-only mappings, `SIGPIPE`,
job control, terminal-generated signals, and broad libc/POSIX interfaces
remain deferred.

Userspace

The system will provide:

- Minimal init process.
- Basic shell.
- Basic command-line utilities.
- User and group management.
- File descriptor interface.
- Static executable support.

Phase 12 supplies a minimal `/sbin/init` and `/bin/sh` on the persistent
SilliconFS root. The kernel launches init with descriptors 0–2 on the
canonical TTY. For this launch path only, the ELF stack starts with
`argc = 0`, `argv[0] = NULL`, and a null environment pointer. Existing ELF
smoke-test execution keeps its current entry convention. Init supervises the
shell, waits for exit, sleeps ten timer ticks with syscall 13, then restarts
it. Failure to load init is reported over serial and halts the kernel.

The shell keeps `help`, `cd`, and `exit` as shell-specific commands. `ls`,
`cat`, `echo`, `pwd`, `true`, `false`, `clear`, `uname`, and `fetch` are
separate ELF executables in `/bin`. Bare command names resolve under `/bin`; absolute
executable paths are also accepted. Execution uses `fork`, `exec_path_argv`,
and `waitpid`, and reports launch and nonzero-child failures. Shell input is
split on spaces, tabs, CR, and LF; repeated whitespace is ignored. The line
buffer is 256 bytes, with at most 16 tokens. Quotes and backslashes are
ordinary token characters, not quoting syntax; there is no `PATH`, variable
expansion, script language, pipes, redirection, or job control. For an
external command, `argv[0]` is the command token exactly as typed, while the
resolved executable path is looked up separately (`/bin/<name>` for a bare
name). Syscall 27 accepts 1–16 arguments and at most 2048 aggregate bytes
including NUL terminators. It installs `argc`, `argv`, a null `argv[argc]`, a
null `envp`, and the existing auxv on a 16-byte-aligned initial stack. The
custom startup stub passes argc, argv, and the initial-stack pointer to
`user_main`; no environment vector is supported. The older path-only
`exec_path` continues to launch with argc zero. `pwd` reports `/`, since
there is no cwd process state or environment interface. Path operands accepted
by `ls` and `cat` treat relative paths as rooted at `/`; shell `cd` does not
change a child process working directory because no cwd syscall exists.

The framebuffer console recognizes a bounded ANSI CSI subset. SGR supports
reset (`0`), standard foreground (`30`–`37`) and background (`40`–`47`), and
bright foreground/background (`90`–`97`, `100`–`107`) using the 16-color
palette. The parser retains state across writes, has two bounded parameters,
and drops malformed or unsupported control sequences. It is not a full
VT/xterm emulator. Coreutils do not emit decorative colors because there is
no terminal-detection syscall. Kernel boot status lines color only `*` blue,
keep `Initializing <component>` white, and color `[ OK ]` green or `[ ER ]`
red. Component names use a fixed-width field so status tokens align. ANSI
resets follow each colored segment. Status lines are buffered until the
framebuffer console is ready. Kernel smoke-test stdout remains serial-only;
framebuffer stdout is enabled when `/sbin/init` starts. Status lines
correspond to reached success paths or failure branches.
`process_system_init` has no failure path or return value;
it resets process state without allocating resources, then the boot log reports
that initialization step as complete.

The bounded coreutils set is `ls`, `cat`, `echo`, `pwd`, `true`, `false`,
`clear`, `uname`, and `fetch`. `fetch` displays the supplied OpenSillicon art,
OS/build architecture, shell name, PID, and VFS name. File mutation commands
(`mkdir`, `rm`, `rmdir`, `touch`,
`cp`, `mv`) await corresponding VFS syscalls. `head`, `tail`, and `wc` are
deferred until external argv is available; `yes` is omitted because no pipe or
redirection interface can contain its output. `date` has no clock query, and
`whoami` has no kernel credential interface.
`/etc/passwd` and `/etc/group` provide descriptive root and regular-user
records only. No login, authentication, kernel UID/GID, privilege separation,
or permission enforcement is added. rc.d and persistent logging are deferred.

C library

musl is the intended userspace C library.

Stage 14 selects a native OpenSillicon musl port, adapted to the existing
OpenSillicon syscall ABI. This does not provide Linux syscall compatibility.
The initial profile is static x86_64 programs; the exact libc/syscall surface
and runtime prerequisites remain subject to ADR-022 and later Stage 14
decisions. Dynamic linking, shared libraries, PIE/ASLR, threads, futexes, and
broad Linux compatibility remain deferred. ADR-023 defines the static ELF
startup stack and single-thread PT_TLS/FS-base lifecycle. ADR-024 pins musl
1.2.6 as the source-verification baseline and records the x86_64 TCB/TLS,
startup, auxv, and representative static ELF findings. This pin is not
approval to ship the release unchanged. ADR-025 records a reviewed security
patch series and validation limits; the patched source is a development and
porting baseline only, not distribution-qualified. Musl initializes its
user-space TCB/TLS and invokes an upstream Linux-specific FS setter. ADR-031
defines OpenSillicon's native syscall 20 `set_fs_base` and implements
PT_TLS validation plus process FS-base handling across interrupt return,
fork, and exec. This is a kernel TLS foundation, not a musl port: native
syscall adaptation, larger TLS allocation, and the musl startup path remain
unfinished. ADR-026 proposes the source, target, syscall-adapter,
private-memory, VFS, and sysroot integration architecture. No Linux syscall
compatibility is introduced.

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

Implement a full VFS abstraction under ADR-005.

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

The Phase 8 baseline uses reference-counted vnodes and open-file descriptions,
mount-aware absolute path lookup, a writable RAMFS root, and a read-only
`newc` initramfs mounted at `/init`. Phase 10 replaces the RAMFS root with
SilliconFS before userspace starts while retaining the initramfs backing data.

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

SilliconFS v1's exact little-endian 4 KiB on-disk layout, 128-byte inode,
bitmap geometry, direct/single-indirect mapping, variable-length directory
records, validation rules, and limits are specified by accepted ADR-003 in
`OPEN_ADRS.md`. Phase 9 uses one-block read/write callbacks so host disk-image
tests and VFS mounting do not depend on hardware discovery. Phase 10 adds the
general block-device layer and AHCI driver.

Block device considerations

Filesystem block size and physical disk sector size are different concepts.

Phase 10 supports devices with 512-byte or 4096-byte logical sectors.
Physical-sector reporting for 512e and 4Kn devices is deferred.

A 4 KiB filesystem block must not imply that every disk uses 4 KiB sectors.
The Phase 9 callback addresses filesystem blocks; the Phase 10 block adapter
translates those operations to device sectors using aligned bounce storage.

Initial filesystems

- initramfs.
- RAMFS.
- SilliconFS.

Phase 10 uses Limine's RSDP to initialize pinned ACPICA with an OpenSillicon
OSL. PCI enumeration is read-only, using configuration mechanism 1 as a
fallback and ACPI MCFG ECAM when available. ACPICA provides MADT through its
table manager; the x86_64 ACPI adapter validates and consumes the MADT entries
needed to initialize the IOAPIC. Existing LAPIC, PIC masking, IDT, and timer
initialization remain in place; IOAPIC redirection entries start masked.
ACPICA power management and events are deferred.

The minimal block layer uses synchronous logical-sector reads, writes, and
flushes with negative errno errors. It supports 512-byte and 4096-byte logical
sectors and keeps sector size independent from the 4 KiB SilliconFS block
size. AHCI runs serialized polling commands with validated DMA constraints;
NCQ, async queues, and caching are deferred. Phase 10 mounts and validates
SilliconFS before userspace, replaces RAMFS at `/`, and keeps initramfs mounted
at `/init` with Limine-owned backing memory valid for all outstanding VFS
references.

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

Phase 11 input uses a polled i8042 PS/2 keyboard. The driver decodes
translated Set 1 scancodes into key events; a separate US keymap handles
Shift, Caps Lock, and Control and produces terminal input. Polling must yield
with `sti; hlt; cli` between attempts while a canonical read is blocked, so
the existing LAPIC timer wakes the CPU without a busy-spin. Keyboard IRQ
routing, mouse input, and USB HID are deferred. If the i8042 controller is
missing or initialization fails, log the failure and leave keyboard input
unavailable while retaining serial and framebuffer output.
Initialization waits use the same timer-driven sleep and time out after 100
ticks (one second at the approved 100 Hz timer rate).

Audio

Audio is not required for the initial system.

---

11. Graphics, TTY and Terminal

Framebuffer

Use the framebuffer provided by Limine during early development.

A separate VESA/GOP driver is not required initially.

Implement an OpenSillicon framebuffer API.

Terminal

Phase 11 provides four virtual terminals. Each owns its screen cell buffer,
cursor and parser state, canonical input line, and completed-input queue.
Alt+F1 through Alt+F4 switches the active terminal without discarding screen
or input state. The initial renderer uses Limine's RGB framebuffer and a
built-in 8-by-16 bitmap font (Modern DOS, CC0 1.0). It supports newline, carriage return,
backspace, tab, scrolling, CSI `2J`, `H`, `K`, and SGR reset, standard 8-color,
and bright 8-color foreground/background sequences.

Standard input is a canonical TTY: Enter queues the line with a newline;
backspace edits the current line; Ctrl-D on an empty line delivers EOF, while
Ctrl-D on a nonempty line queues that line without inserting Ctrl-D or a
newline. The line buffer is 256 bytes including newline, and excess input is
rejected without overwriting buffered data. Standard output is routed once
through a console fan-out to the active framebuffer terminal and serial.
Kernel diagnostics continue through the serial logger directly. Unicode,
termios, signal delivery, mouse input, and TrueType fonts remain deferred.

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
- Supervise and restart the shell after a timer-based delay.
- Basic account initialization.
- Start from the mounted persistent SilliconFS root.
- Keep serial diagnostics separate from terminal output.

BSD-style rc.d services and persistent syslog-like logging are deferred until
the system has services and a logging facility. Phase 12 account files are
metadata only; authentication and permission enforcement are not implemented.
The initial userspace must remain minimal.

---

13. Networking

Networking remains unimplemented. ADR-036 defines a bounded native stack
architecture: an e1000 QEMU first driver, Ethernet/ARP/IPv4/ICMP/UDP/TCP,
static IPv4 configuration and routing, and sockets as refcounted descriptors
through the existing FD layer. The initial device path is polled from deferred
kernel context; interrupt handlers must not parse packets or wake sockets.
The initial profile excludes IPv6, DHCP, forwarding, fragmentation, and Linux
socket ABI compatibility. See ADR-036 for packet ownership, protocol limits,
socket semantics, prerequisites, and the staged test plan. The first
foundations include a fixed packet pool, a generic device ownership contract,
read-only decoding of assigned PCI BAR type/address, a bounded PCI MMIO
mapping lease for resources with a separately validated aperture, and
contiguous DMA allocations with explicit CPU/device ownership. Generic BAR
apertures remain unknown: live probing is not performed because there is no
PCI device-claim/quiesce lifecycle. MMIO leases accept only ranges fully
described as unmapped RESERVED memory by Limine base revision 4; they use
uncached mappings in the reserved vmalloc window and can be released.

DMA identity addressing is enabled only when Limine honored the requested
base revision 5 and reports loaded revision 5 or newer; the kernel does not
request `LIMINE_X86_64_KEEP_IOMMU`. Otherwise DMA allocation and DMA-dependent
drivers are disabled. This does not provide IOMMU translation or cache
maintenance for non-coherent platforms; those profiles are unsupported. DMA
allocations are contiguous, zero-filled, alignment/address-width bounded, and
cannot be freed while device-owned or pinned by an active descriptor ring.
Descriptor ownership is tracked per slot; publish/completion barriers order
coherent x86 DMA memory and MMIO notifications. A caller must confirm a
device-specific stop/reset before ring memory is unpinned. No guest e1000 DMA,
transmit, or receive is implemented or verified.

The bounded QEMU preparation profile is q35 with `-nic none`, explicit
`-device e1000`, and a private user network backend. QEMU 10.2.2 reported
`8086:100e`, BAR0 128 KiB non-prefetchable 32-bit MMIO, BAR1 64-byte I/O, and
an unassigned expansion ROM. This is a QEMU profile, not generic PCI sizing;
the guest validator rejects other IDs/layouts and does not probe BARs.

Milestone 15.2A connects that profile to guest PCI enumeration. It validates
the BAR layout, enables memory decode while clearing bus mastering, maps BAR0
through an uncached lease, performs a bounded CTRL.RST poll, and validates
CTRL/STATUS. Guest QEMU logs verify these steps. Interrupts and DMA remain
disabled; no packet TX/RX is implemented.

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

---

20. Native musl Integration Status

Stage 14J builds the security-reviewed musl 1.2.6 development source for the
native x86_64 OpenSillicon ABI and runs one static ET_EXEC program using musl's
CRT, a small TLS image, `open` errno handling, libc `write()`, and normal
process exit. The target sysroot and build output are generated under
`build/musl-port/`; provenance, the bounded syscall adapter, flags, and test
results are recorded in ADR-032.

This does not provide Linux syscall compatibility or general musl/POSIX
support. Stage 14K-A adds a bounded private anonymous mapping ABI for the
musl 1.2.6 mallocng paths (`mmap`, complete-mapping `munmap`, and `mprotect`)
as documented in ADR-033. File-backed/shared/executable mappings, partial
unmap, `mremap`, `brk`, threads, futexes, full termios, and broad time, file,
and process interfaces remain deferred. Native stdio uses bounded `writev`,
`readv`, and `TIOCGWINSZ` support documented in ADR-035.

### x87/SSE process state

The kernel verifies FXSAVE, SSE, and SSE2 at boot, enables the legacy x87/SSE
control-register settings, and uses eager per-process FXSAVE/FXRSTOR at the
process scheduler boundary. Kernel C remains built with MMX/SSE disabled and
soft-float; only the architecture FPU initializer explicitly clears XMM state.
AVX and XSAVE remain disabled. See ADR-034 for ownership and lifecycle rules.
