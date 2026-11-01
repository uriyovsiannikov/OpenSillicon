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
`clear`, `uname`, `fetch`, `head`, `wc`, `stat`, `basename`, and `dirname`.
The process utilities are `kill`, `sleep`, `which`, and `ps`: `kill` supports only
SIGINT, SIGTERM, and SIGKILL; `sleep` accepts whole seconds using the current
100 Hz timer; `which` searches `/bin` or checks an explicit path. `ps` uses
the bounded syscall 28 snapshot (PID, PPID, state, status; at most 128
processes) and does not report command names because the kernel does not retain
them.
`head` supports `-n N`; `wc` supports `-l`, `-w`, and `-c`; `stat` reports
available vnode type, size, and mode metadata. `ls` and `cat` accept path
operands; relative paths are rooted at `/` because processes have no cwd.
`fetch` displays runtime root availability, `/bin` entry count, terminal size,
and PID alongside OS and architecture labels. File mutation commands
(`mkdir`, `rm`, `rmdir`, `touch`,
`cp`, `mv`) await corresponding VFS syscalls. `tail` is deferred because the
VFS has no seek operation and bounded streaming support has not been added;
`yes` is omitted because no pipe or
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

The first bounded Ethernet II/ARP/IPv4/ICMP implementation now exists on top
of the QEMU e1000 driver. It validates untagged Ethernet II frames, maintains
an eight-entry expiring ARP cache, handles local ARP requests, and validates
IPv4 ICMP Echo messages with checksums. Static test configuration is
10.15.3.1/24 at MAC 52:54:00:15:03:01; its isolated peer is 10.15.3.2 at
52:54:00:15:03:02. The QEMU test sends an ARP request and ICMP Echo Request
and receives a matching Echo Reply through actual e1000 DMA rings. This is
not yet a continuously serviced production interface. Bounded UDP and
project-native socket descriptors have since been added for the QEMU profile.
TCP currently has an isolated, host-tested wire/state-machine core only; it is
not connected to netstack, e1000, timers, descriptors, or syscalls. Live TCP
connections and a TCP socket API remain unavailable. DHCP, IPv6, routing,
forwarding, fragmentation, and VLAN remain unimplemented.
ADR-036 defines the broader bounded native stack architecture: an e1000 QEMU first driver,
Ethernet/ARP/IPv4/ICMP/UDP/TCP,
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
PCI device-claim/quiesce lifecycle. MMIO leases accept validated ranges fully
covered by RESERVED memory or wholly absent from Limine's memory map, while
rejecting any overlap with described RAM/boot ranges. They use uncached
mappings in the reserved vmalloc window and can be released.

DMA identity addressing is enabled only when Limine honored the requested
base revision 5 and reports loaded revision 5 or newer; the kernel does not
request `LIMINE_X86_64_KEEP_IOMMU`. Otherwise DMA allocation and DMA-dependent
drivers are disabled. This does not provide IOMMU translation or cache
maintenance for non-coherent platforms; those profiles are unsupported. DMA
allocations are contiguous, zero-filled, alignment/address-width bounded, and
cannot be freed while device-owned or pinned by an active descriptor ring.
Descriptor ownership is tracked per slot; publish/completion barriers order
coherent x86 DMA memory and MMIO notifications. A caller must confirm a
device-specific stop/reset before ring memory is unpinned. The supported
QEMU-only e1000 profile now configures eight 16-byte TX and eight 16-byte RX
descriptors (128-byte rings aligned to 128 bytes) with separate 2048-byte DMA
buffers. Bus mastering is enabled only after DMA contract validation, buffer
allocation, ring publication, and ring register programming. Polling TX/RX
passes one host-peer Ethernet smoke exchange in QEMU; this does not implement
or validate Ethernet protocol processing, general hardware support, or
non-coherent/IOMMU DMA.

The bounded QEMU preparation profile is q35 with `-nic none`, explicit
`-device e1000`, and an isolated socket peer for the frame test. QEMU 10.2.2
reported
`8086:100e`, BAR0 128 KiB non-prefetchable 32-bit MMIO, BAR1 64-byte I/O, and
an unassigned expansion ROM. This is a QEMU profile, not generic PCI sizing;
the guest validator rejects other IDs/layouts and does not probe BARs.

Milestone 15.2A connects that profile to guest PCI enumeration. It validates
the BAR layout, enables memory decode while clearing bus mastering, maps BAR0
through an uncached lease, performs a bounded CTRL.RST poll, and validates
CTRL/STATUS. Guest QEMU logs verify these steps. Milestone 15.2B adds bounded
DMA rings and a polled raw Ethernet TX/RX path; the test-only guest smoke build
is verified against a host socket peer. Interrupt-driven I/O, protocol parsing,
protocol processing and general socket service remain limited. ADR-039 records
the exact ring lifecycle and evidence. Milestone 15.3A adds bounded
Ethernet/ARP/IPv4/ICMP parsing and a
peer-verified QEMU Echo exchange; ADR-040 records the supported profile and
limitations. Milestone 15.3B adds validated IPv4 UDP datagrams and a
project-native socket-as-FD API over syscalls 28–31; see ADR-041 for wire,
queue, endpoint, reference-lifetime, and timeout semantics. It is a static
`10.15.3.1/24` profile on the supported e1000 device, with no TCP or POSIX
socket compatibility.

Milestone 15.4B adds an isolated TCP state model in
`kernel/net/tcp_core.c`. Phase 15.4R separates bounded wire parsing and
serialization into `kernel/net/tcp_wire.c`; the core's compatibility helpers
delegate to that module. It supports the eleven modeled states, IPv4
pseudoheader checksums, MSS/NOP/EOL options, ACK and sequence checks, in-order
receive data, and explicit actions for future packet output.
Limits are eight connection slots, four pending children per listener, 2048
queued receive bytes per connection, 1480 bytes per segment, five modeled
control retransmission attempts, and 12000 timer ticks in TIME-WAIT. Table
handles use generations and external references so closed entries are not
reused while held by a caller. The model does not perform packet I/O. Listener
creation, active-open events, and their explicit deterministic sequence seed
are compiled only with `TCP_CORE_TESTING`; these event variants and the seed
field are absent from the production kernel object. The
15.4I RDRAND API is not integrated with this model, and current QEMU test
profile reports it unavailable; TCP remains disabled
on the live network path, with no TCP syscall/API, runtime timer hook, data
retransmission, or end-to-end verification. Milestone 15.4C adds an isolated
bounded service primitive that progresses at most four due control or
TIME-WAIT timers per call and returns retained handles/actions to a future
deferred network service. Control RTO starts at 100 ticks, doubles to 800
ticks, and aborts after five retries; TIME-WAIT is 12000 ticks. The timer
source is 100 Hz, and comparisons handle uint64 wraparound. This service is
not called by the timer interrupt or current UDP poll path. The Limine entropy
request is not enabled: the bundled protocol defines its response as
best-effort and permits timing-seeded fallback, so it is not accepted as a
trusted ISN source.

Milestone 15.4I adds a bounded kernel random-byte API backed directly by the
CPU RDRAND instruction when CPUID leaf 1 advertises it and the instruction
returns valid output. RDRAND is the processor's CTR_DRBG-based generator; the
kernel checks CF, retries at most ten times, and latches the API unavailable
after initialization or runtime source failure. Requests are all-or-nothing,
limited to 256 bytes, and run with local interrupts masked to prevent
single-CPU timer-preemption reentrancy. This is not SMP synchronization and
does not provide independent software state, a reseed policy, or protection
against a faulty or malicious CPU, firmware, or hypervisor. A successful
feature check is a capability signal, not proof of source quality. The
default QEMU CPU profile used by current tests omits `-cpu` and reports the
random source unavailable; UDP networking continues to boot and operate. A
separate QEMU TCG boot with `-cpu max` returned successful random-source
initialization, confirming feature detection and a valid RDRAND result in that
emulated profile only.
`tcp_core_live_isn_available()` remains false and the random API is not wired
to TCP. Deterministic source injection exists only in the host test build.
There is still no live TCP packet or syscall path.

Milestone 15.4J reviewed whether to add a software DRBG. It was deferred:
the kernel has no reviewed ChaCha20/AES/hash implementation to reuse, and
adding a new primitive plus seed conditioning, reseed limits, and state-update
policy would expand the cryptographic code surface without improving the
current default QEMU profile, which has no exposed RDRAND. RDRAND is treated
as a separate hardware DRBG interface, not as interchangeable with RDSEED.
Intel identifies RDRAND at CPUID.1:ECX[30] and RDSEED at CPUID.7:EBX[18]; the
kernel currently checks and executes only RDRAND. RDSEED is not part of the
implemented policy. No software seed size, software state, or reseed policy
is therefore claimed. If a reviewed software DRBG is proposed later, it must
define those requirements and fail closed on absent/failed seed acquisition.
This decision leaves production TCP ISNs and live TCP disabled.

The hardware trust boundary is platform-specific. Intel's DRNG guide describes
Intel RDRAND as CTR_DRBG output and RDSEED as seed material for software DRBGs;
those implementation claims are not generalized to every vendor. AMD's
programmer manual defines its instruction feature/CF interface, while AMD's
SB-7055 documents an RDSEED defect on affected Zen 5 systems where CF could
indicate success for a bad result. Thus CPUID and CF establish the documented
instruction interface, not entropy quality or universal vendor correctness.
QEMU can suppress or expose these CPUID features and emulate the instructions;
the guest has no current mechanism to distinguish that from bare metal or
verify the host entropy source.

The observed QEMU 10.2.2 profile matrix is: default (no `-cpu`) reports
unavailable; `-cpu max` reports initialization success;
`-cpu max,-rdrand` reports unavailable; `-cpu max,-rdseed` still reports
success; and `-cpu max,-rdrand,+rdseed` reports unavailable. The last profile
explicitly configures RDSEED on while RDRAND is off, but this kernel does not
read guest RDSEED CPUID state. The results confirm that the kernel consults
RDRAND only and does not fall back to RDSEED. `run-gui` and
the UDP QEMU fixture do not use `-cpu host` or host passthrough. The default
model is QEMU's compatibility model, documented as `qemu64` for x86_64 when
no CPU option is specified. These emulation results test feature handling,
not production entropy quality.

Source policy: RDRAND is an implemented but conditional hardware DRBG
interface, not a universal OS CSPRNG; RDSEED is not implemented; Limine entropy
is not requested and is rejected as a trusted source because the protocol is
best-effort; no external RNG device is supported; deterministic injection is
host-test-only and never production entropy. No kernel caller currently uses
`kernel_random_bytes()`. Keep production security-sensitive use unavailable
until a platform trust policy defines acceptable CPU/hypervisor configurations
and a separately reviewed software CSPRNG has a defensible seed and reseed
policy. This applies to ISNs, keys, tokens, and any future userspace random
API.

Milestone 15.4F adds a fixed eight-entry deferred network event queue. The
service drains at most four events per call, with each RX-poll event processing
at most one frame through the existing UDP/netstack path. It runs at generic
syscall entry and after the blocked-process scheduler idle path wakes from
`hlt`; it does not run in the LAPIC timer handler. The existing UDP syscall
poll remains, with a single-owner guard preventing recursive RX polling.
Shutdown refuses while the service is running or work remains queued. This is
bounded cooperative progress, not a background worker: a user process that
runs without syscalls is not serviced during timer preemption. The TCP timer
primitive is still not connected because no runtime-owned TCP table/lifecycle
exists. The conditional RDRAND API is not an ISN source here, so live TCP
remains fail-closed.
Kernel threads are cooperatively scheduled and the runtime has no network
event-wait primitive, so this milestone does not add a worker or alter the
scheduler to create one. Requests are not coalesced: a full queue rejects only
the new request and preserves its eight queued indications. Runtime safe
points run the service even when enqueueing was rejected, so repeated safe
points drain a saturated queue within the four-event budget; no progress is
promised if safe points stop occurring.

### TCP core readiness (Phase 15.4L)

The isolated TCP table now models connection identity as the complete local
IPv4 address, remote IPv4 address, local port, and remote port tuple. Duplicate
active tuples are rejected. Listener entries use remote port zero and require
a zero remote address; a listener may bind a specific local address or
`0.0.0.0`. Same-port listener binds conflict when either address is wildcard
or both specific addresses match. Lookup selects an exact live tuple before a
matching listener, reports ambiguity rather than selecting arbitrarily, and
returns a retained generation-tagged handle that the caller must release.
Tuple snapshots require a currently valid handle. Callers must externally
serialize table operations; this core has no internal lock or SMP guarantee.

The parser accepts a bounded IPv4 TCP segment view and validates its
pseudoheader checksum, but the caller must first validate the enclosing IPv4
header and packet bounds. Current `netstack_process_frame()` classifies only
ICMP and UDP; it ignores IPv4 protocol 6, so no live TCP dispatch exists.
Future dispatch must validate Ethernet and IPv4, classify TCP, parse and
checksum the segment, look up its four-field tuple/listener, step the core,
then translate bounded actions into output packets.

TCP actions are values, not packet ownership transfers. A parsed segment's
payload pointer borrows the caller's input bytes only for the duration of the
step. Accepted in-order bytes are copied into the fixed per-connection
2048-byte receive buffer; no action retains an RX DMA pointer. Actions such
as SEND_SYN, SEND_ACK, SEND_FIN, SEND_RST, and RETRANSMIT describe future
output but contain no serialized packet. A future integration must build an
owned frame and follow the network device's CORE-to-DEVICE transfer and
completion rules. The current e1000 TX API has not been connected to TCP
actions.

The table has eight fixed slots. Creation returns one external reference;
active/listening entries acquire table ownership, and passive SYN children
are table-owned and returned with a caller reference. Closed entries are
reclaimed only after table ownership and all external references are gone.
Timer service results likewise carry references that callers release. The
core requires external serialization for create, lookup, retain/release,
step, read, and timer service. It has no runtime-owned table or shutdown hook.
Production entropy remains unavailable under the current platform policy:
`tcp_core_live_isn_available()` is false, explicit active-open seeds remain
for deterministic model tests only, and live TCP, packet dispatch, syscalls,
and timer integration remain disabled.

### TCP runtime ownership and IPv4 classification (Phase 15.4N)

`tcp_runtime` now owns one private, fixed-size TCP model table. Boot initializes
it once after the existing UDP stack and cooperative network service are ready;
there is no production API to create a listener or connection, and no table
internals are exposed. Initialization is one-shot and fails on duplicate
calls. The table has no dynamic allocations or fallible sub-steps, so there is
no partial allocation state to unwind. Shutdown is also explicit and
one-shot: it first enters STOPPING, rejects further runtime work, and refuses
completion while any table entry or external handle remains. It never forcibly
invalidates a handle. The UDP QEMU test exercises this refusal/release/shutdown
contract before asking the existing service to drain and stop e1000. Production
currently has no TCP handle-creation path, so its table stays empty.

The bounded `netstack_ipv4_classify()` helper validates an option-free IPv4
header, total length against both the available frame bytes and configured
MTU, and the IPv4 header checksum before classifying ICMP (1), TCP (6), UDP
(17), or unsupported protocols. It rejects null/short input, a wrong version,
unsupported or truncated IHL/options, undersized/oversized totals, and bad
header checksums. The existing `process_ipv4()` path still rejects fragments,
invalid TTL/address/MAC, and nonlocal destinations before transport handling.
For protocol 6 it then validates the bounded TCP wire format and IPv4
pseudoheader checksum using `tcp_wire_parse()`; malformed or truncated TCP is
classified as invalid. A valid segment receives only `NETSTACK_TCP_DISABLED`.
It is not passed to `tcp_segment_parse_ipv4()`, tuple lookup, or
`tcp_core_step()`, and no response, connection, or work item is created. The
future dispatch insertion point is immediately after this validation in
`process_ipv4()`, while the single existing `udp_socket_poll()` call owns the
RX packet; validation must finish before its CORE-owned netbuf is released.

RX ownership is unchanged: deferred network service and UDP syscall polling
share the existing guarded `udp_socket_poll()`/netstack path, with no TCP poller
and no second device receive. UDP still copies payload into its fixed
socket-owned datagram slot before the RX packet is released; each socket keeps
its four-datagram queue limit. The UDP host regression injects a checksum-valid
TCP SYN and confirms it does not appear in a bound UDP queue. A future TCP service must reserve bounded timer
work on every invocation, process only a bounded RX/event batch, run outside
the LAPIC interrupt handler, preserve single-owner serialization, and release
each timer-service returned handle. Cooperative service still cannot promise
progress while userspace runs without entering the kernel. Shutdown must stop
new TCP work, drain/reject pending work, prove all references are released,
then destroy the logical table; e1000 DMA teardown remains separately gated
on device quiescence. Production ISNs remain unavailable because the entropy
policy is fail-closed; the current RDRAND wrapper is not wired to TCP.
Host-only synthetic SYN/SYN-ACK/ACK fixtures exercise parser,
tuple-selection, and core-state behavior as described below; they cannot
establish live handshake support or justify production sequence numbers.

### Isolated TCP handshake control plane (Phase 15.4P)

The host-only TCP core now exercises active and passive connection
establishment using explicit deterministic event seeds. Active open enters
SYN-SENT and requests one SYN. It accepts a SYN-ACK only with the exact
expected acknowledgment (`SND.NXT`), no payload, and the supported SYN|ACK
flags; it records the peer sequence and returns one final ACK action. A bare
SYN in active SYN-SENT enters active-origin SYN-RECEIVED; a matching peer
SYN-ACK completes the simultaneous-open model and emits the final ACK.
Unexpected ACKs,
wrong acknowledgments, payload before establishment, unsupported control-flag
combinations, and tuple mismatches leave the handshake unadvanced.

Passive open accepts a SYN-only segment at a listening tuple, creates a child
with the exact four-field peer/local tuple, enters SYN-RECEIVED, and emits a
SYN-ACK action. The listener remains listening. An exact duplicate SYN
reissues the bounded SYN-ACK actions without another child. The final pure ACK
must exactly match `SND.NXT` and `RCV.NXT`, contain no payload, and have no
other flags before the child enters ESTABLISHED. Four SYN-RECEIVED children
per listener are allowed within the global eight-slot table; excess backlog
or allocation pressure fails without allocation and requests the modeled RST
action.

For these handshake states, active SYN-SENT RST closes only for exactly
RST|ACK with the exact outstanding acknowledgment; SYN-RECEIVED accepts only
RST or RST|ACK at exact `RCV.NXT`. Outside SYN-SENT, the ACK field does not
validate a reset. Removing a passive child leaves its separate listener in
LISTEN; an active-origin SYN-RECEIVED reset closes the active entry. In
synchronized states, RST sequence numbers are accepted anywhere in the
bounded receive window. This does not implement RFC 5961 challenge ACKs for
in-window but non-exact resets. Extra RST flag combinations are ignored.
Control retransmission starts at 100 ticks, backs off to a maximum of 800
ticks, emits at most five retransmission actions, then closes on retry
exhaustion. A duplicate FIN in TIME-WAIT is ACKed and restarts the fixed
12000-tick timer. Timer service remains isolated and returned handles must be
released. Sequence ordering uses modulo-2^32 serial comparisons for ranges
smaller than 2^31; the receive model accepts only fully in-order segments and
does not trim old prefixes or queue future segments.

Host fixtures construct IPv4/TCP packets, verify IPv4 total length/header
checksum and TCP pseudoheader checksum, then feed parsed views to the isolated
state machine. Tests include checksum and pseudoheader failures, truncation,
data-offset and total-length errors, duplicate SYN, incorrect ACK/sequence,
unknown well-formed option handling, simultaneous open, RST validation,
duplicate SYN-ACK/final ACK, serial-number boundary cases, and TIME-WAIT
restart/expiration. These tests do not transmit any packet. The production kernel still does not call the TCP parser or
`tcp_core_step()` from RX, has no TCP syscall or TX path, and has no approved
production ISN source. `tcp_core_live_isn_available()` remains false; this
bounded model is not a standards-complete TCP implementation.

RFC 9293 audit (Sections 3.3, 3.3.2, 3.4, 3.5.2–3.5.4, 3.9, and 3.10.7;
reference: <https://www.rfc-editor.org/rfc/rfc9293.html>):

| Feature | RFC requirement | Current model | Status | Required action |
|---|---|---|---|---|
| §3.3 sequence space | Modulo-2^32 arithmetic; SYN/FIN each consume one; validate ACK and receive-window overlap | Serial comparisons are wrap-safe for bounded spans; handshake SYN consumption and boundary tests exist. Receive accepts only exact in-order data and rejects overlap/out-of-order data rather than trimming/queuing. Data on SYN is unsupported. | Partially implemented | Add segment trimming/reassembly and support or explicitly negotiate SYN data before claiming conformance. |
| §3.3 TCP options | Parse mandatory options and ignore unknown well-formed options | MSS/NOP/EOL parsed; unknown length-delimited options are skipped; malformed lengths are rejected. Reserved/ECN/URG bits are unsupported. | Partially implemented | Broaden header/option feature handling and malformed-option reporting. |
| §3.3.2 state machine | State-specific event transitions | Eleven states are represented; common handshake and close paths are modeled, not the complete RFC event table. | Partially implemented | Complete state-specific packet/event processing and verify all transitions. |
| §3.4 establishing | Active/passive handshake and simultaneous open | Active, passive child, and simultaneous-open transitions are deterministic host-tested. No production ISN source, runtime listener, or packet path exists. | Requires runtime integration | Add trusted ISN policy, one runtime owner, dispatch, and packet output before live connections. |
| §3.5.2 reset generation | Generate state-appropriate reset sequence/ACK fields | Core returns only `TCP_ACTION_SEND_RST`; it does not build the reset segment or its SEQ/ACK fields. | Requires runtime integration | Add RFC state-aware reset packet construction and peer-fixture validation. |
| §3.5.3 reset processing | Validate reset sequence; SYN-SENT uses ACK of SYN; passive SYN-RECEIVED returns to LISTEN, active SYN-RECEIVED closes | SYN-SENT checks ACK; other resets use exact `RCV.NXT` in handshake states and receive-window membership when synchronized. Passive child removal leaves the independent listener alive. No RFC 5961 challenge ACK is produced. | Partially implemented | Decide and implement standards-compatible window/challenge behavior and expose reset effects to a future socket API. |
| §3.5.4 closing | Correct FIN sequence consumption and close-state transitions | Common FIN-WAIT/CLOSE-WAIT/CLOSING/LAST-ACK/TIME-WAIT paths and timer expiry are tested. Queue draining, application notification, and combined data/control cases are incomplete. | Partially implemented | Complete the transition matrix, queued-data semantics, and API notifications. |
| §3.9 interfaces | TCP/user and TCP/IP interfaces | No TCP user API, syscall, or live lower-layer handoff is in scope. | Not applicable to current scope | Define the native socket and IP handoff in a later approved milestone. |
| §3.10.7 arrival processing | Ordered state-specific sequence, RST, SYN, ACK, data, and FIN processing | A bounded subset processes handshake controls, in-order established data, resets, and close events. Future ACKs request ACK; stale ACKs do not block in-order data. Out-of-order data, full ACK/window-update rules, challenge ACK, and many flag combinations remain unsupported. | Partially implemented | Implement and test the complete arrival algorithm before claiming RFC conformance. |

This matrix audits the deterministic model; it is not a conformance claim.
Action bits are abstract requests and do not prove that wire packets are
generated correctly.

### TCP wire boundary (Phase 15.4R)

`tcp_wire_parse()` accepts exactly one bounded IPv4 TCP payload and an
explicit pseudo-header containing source/destination addresses, protocol 6,
and the exact segment length. It validates the 20–60-byte header, reserved
nibble, ports, option bounds, 20–1480-byte segment limit, and checksum before
publishing a parsed view. Option and payload pointers borrow the input buffer;
callers must keep that buffer owned while using them. The parser does not
modify the input. `tcp_wire_build()` writes into caller-owned storage, validates
the option profile and total size, zeroes reserved/checksum fields, computes
the IPv4 pseudo-header checksum, and returns the exact length. Neither API
allocates, transmits, or retains packet memory.

The option parser validates MSS (4 bytes on SYN), Window Scale (3 bytes on
SYN, shift at most 14), SACK-Permitted (2 bytes on SYN), SACK (one to four
8-byte blocks), and Timestamp (10 bytes). It recognizes NOP and EOL, requires
zero padding after EOL, and skips unknown length-delimited options. Parsed
metadata is descriptive only: Window Scale is not negotiated, SACK has no
recovery queue, and timestamps do not drive RTT measurement. ECN, URG
semantics, IPv4 fragmentation, and live TCP remain unsupported. The older
`tcp_segment_*` core helpers retain the modeled subset and delegate wire
validation/serialization to `tcp_wire`; netstack's protocol-6 path continues
validation-only and does not dispatch connections or emit responses.

### TCP IPv4 receive validation (Phase 15.4S)

In `process_ipv4()`, protocol 6 reaches `tcp_wire_parse()` only after the
existing IPv4 classifier has checked version, option-free IHL, total length,
MTU, and IPv4 checksum, and the receive path has checked fragments, TTL,
source/destination policy, and Ethernet destination. The parser receives
source/destination addresses from the validated IPv4 header, the pointer
`ip + 20`, and exactly `IPv4 total_length - 20` bytes. Ethernet padding and
the IPv4 header are excluded. A malformed enclosing IPv4 packet never reaches
the TCP parser.

The bounded receive result distinguishes `NETSTACK_TCP_DISABLED` for a valid
wire segment, `NETSTACK_TCP_INVALID_HEADER`,
`NETSTACK_TCP_INVALID_CHECKSUM`, and `NETSTACK_TCP_INVALID_OPTIONS`. The
classification is discard-only: unsupported TCP semantics and all otherwise
valid flags still result in `NETSTACK_TCP_DISABLED`. Parsed option and payload
pointers are borrowed only inside this synchronous call and are discarded
before returning. The netstack does not mutate the input frame. The existing
`udp_socket_poll()` remains the sole RX owner and releases its CORE-owned
netbuf once after processing; it copies only accepted UDP payloads into socket
storage. TCP packets neither enter nor wake UDP queues.

No TCP tuple lookup, connection allocation, `tcp_core_step()`, runtime event,
timer, response construction, TX, or syscall is reachable from this branch.
This validation boundary is not TCP connectivity or an RFC 9293 conformance
claim. Production ISNs and live TCP remain disabled.

### TCP runtime connection ownership scaffold (Phase 15.4T)

The runtime owner contains one private fixed table of eight core entries and
uses no dynamic allocation. Listener registration creates a local-only
`LISTEN` entry through the core; it emits no packet and requires no ISN.
Listeners share the table with active/test entries. A listener binds either a
specific local IPv4 address or wildcard `0.0.0.0`; same-port wildcard and
specific listeners conflict, as do duplicate specific listeners. The core
lookup matches all four tuple fields for established entries first, then
tries one applicable listener. Multiple listener matches fail as ambiguous.
Successful lookup returns a generation-tagged external reference, which the
caller must release. Slot reuse changes generation, so stale handles fail.

Runtime mutation is serialized by local interrupt masking on the current
single-CPU kernel. This is not SMP synchronization and these APIs must not be
called from interrupt context. Initialization is one-shot. Shutdown enters a
sticky stopping state, refuses new listeners/routing, and completes only
after entries and external references are gone; reinitialization is not
supported. No force-destroy invalidates retained handles.

`tcp_runtime_route_segment()` accepts only a parsed, bounded segment and
validated unicast IPv4 endpoints. It copies no payload and retains no packet
pointer. The result can identify a connection/listener and reports payload
presence as unsupported; it never submits a core event. The optional
`OPEN_SILLICON_ENABLE_TCP_ROUTING` gate is absent from normal kernel flags.
The normal IPv4 path therefore keeps Phase 15.4S behavior: validate TCP and
discard it without tuple/listener lookup. A gated host integration test
exercises a listener match and verifies the retained reference is released,
with no core transition or response. No service queue, TCP timer, TCP TX,
packet builder, or e1000 handoff is connected.

The future insertion point remains the validated protocol-6 branch after
`tcp_wire_parse()`. Payload delivery and passive child creation are not
supported by this routing layer. Production ISNs remain unavailable, and
live TCP, TCP syscalls, TCP packet transmission, and runtime core-event
dispatch remain disabled.

### TCP passive-open core integration (Phase 15.4U)

Passive-open event processing is isolated to host builds with
`TCP_RUNTIME_TESTING` and `TCP_CORE_TESTING`. It routes a wire-validated
segment by the exact IPv4/port tuple. A suitable SYN first matches an exact
child (for duplicate handling) or a listener. The existing core listener
`TCP_EVENT_SEGMENT` path creates a SYN-RECEIVED child in the shared eight-slot
table and records its parent listener slot and generation. A fixed runtime
sidecar is indexed by child slot and checks generation; no dynamic allocation
is used. Four SYN-RECEIVED children per listener are allowed. Listeners and
children share the eight core slots.

The core's initial child reference becomes the runtime child-link reference;
the runtime takes a separate reference for the caller. Callers release their
handle explicitly. An exact duplicate SYN reaches the same child and returns
abstract SYN/ACK retransmit actions without changing the original retry count
or deadline. The final ACK routes to the exact child and the core validates
sequence and acknowledgment values before entering ESTABLISHED. Payload and
unsupported flag combinations are rejected. A valid RST that closes a child
removes its sidecar and drops the runtime reference; other references keep the
closed core slot alive until released. Listener close returns busy while
child links remain. Generation checks reject stale handles after reuse.
Backlog/table exhaustion and core rejection do not leave a partial child.

The deterministic local ISN is accepted only by the test-only function and is
absent from the production runtime API. Passive child processing and the
deterministic seed field are test-only. Production has no child creation,
incoming TCP core event, or response. The normal IPv4 object still calls only
the wire parser for TCP; the optional routing gate remains absent from
production flags. The core arms model timer state, but runtime timer service
is host-testable through the runtime-owned bounded timer API described below.
Returned SYN/ACK actions are never serialized or transmitted. This is
host-tested lifecycle integration, not live TCP or RFC 9293 conformance.

### TCP runtime timer ownership (Phase 15.4V)

`tcp_runtime_service_timers(now_tick, budget, result)` delegates due-time
decisions and timeout state transitions to the existing `tcp_core_service()`;
it does not create a second clock. `now_tick` is in the kernel timer's 100 Hz
`timer_ticks()` domain. A call accepts a budget from zero through four, and
the core scans its fixed eight slots once. The runtime copies only bounded
scalar observations (actions, state, sequence values, timer deadline/interval,
and retry count) into the result. Core handles never escape this API.

For every core service item, the runtime checks slot bounds, generation-backed
handle validity, and the reported state, then releases the service-owned
reference exactly once. If a timeout closes table ownership, this temporary
reference keeps the closed slot valid until scalar copying and cleanup finish.
An external handle remains independently valid and can observe CLOSED; the
slot is reusable only after all references are released. In the host-only
passive-child model, a CLOSED child first drops its runtime child-link
reference and clears that link only after a successful release. This removes
the parent's child relation before the service reference is retired; a
listener cannot close while any child link remains. A failed consistency or
release check returns an error and does not intentionally force-destroy
references.

The result's `more_due` preserves the core indication that due work remains;
the caller must schedule another bounded invocation. A zero budget can report
pending work without processing it. Calls before init/after shutdown return
DISABLED, calls during sticky STOPPING return STOPPING, and reentrant or
already-active service returns BUSY. Shutdown refuses while timer service is
active and does not enter STOPPING in that case. The runtime serializes the
entire call with local interrupts masked on this single CPU; this is not SMP
synchronization. The API is not called from the LAPIC ISR, network service,
or production boot path. There is no liveness guarantee absent a future safe
caller, and live TCP remains disabled by the production ISN policy.

### TCP Runtime active-open lifecycle (Phase 15.4W)

The host-only `tcp_runtime_test_active_open()` accepts a complete four-field
tuple, an explicit deterministic ISN, and a test tick. It rejects invalid
addresses/ports, duplicate exact tuples, and table exhaustion; creation and
the Core active-open event occur under the Runtime's single-CPU serialization
guard. The returned generation-tagged handle owns the creation reference and
is returned only after Core confirms SYN-SENT, SEND_SYN, and an armed timer.
Failure rolls back the allocation. This API and the ISN parameter are
compiled only with `TCP_RUNTIME_TESTING` and `TCP_CORE_TESTING`.

The test-only `tcp_runtime_test_active_input()` accepts raw IPv4 TCP payload
bytes, validates them through `tcp_wire_parse()` (including the IPv4
pseudoheader checksum), matches source/destination addresses and ports to the
exact active handle tuple, then submits a borrowed Core view synchronously.
It copies only scalar Core results; neither the input bytes nor a Core handle
is retained or returned. Core remains authoritative: a valid SYN-ACK must
acknowledge the local SYN to reach ESTABLISHED, and Core reports the final
SEND_ACK action without any packet serialization or transmission. Invalid
ACK values can yield an abstract SEND_RST while leaving the connection in
SYN-SENT. Unsupported flags are rejected by Core, and malformed/checksum-bad
segments are rejected before Core input. A matching RST follows Core's ACK
validation and can close table ownership; the caller's retained handle remains
valid until explicitly released.

There is no pre-known peer sequence value in active open: the sequence number
of the first valid peer SYN-ACK is that peer's ISN, so an arbitrary 32-bit
value is not by itself an unexpected sequence. ACK-only input without SYN
does not establish the connection. Tuple mismatches in either address or
either port are rejected before a Core event. Duplicate SYN-ACK input after
establishment is passed to Core and cannot restart SYN or allocate another
slot. Active connections use the same eight-slot table and bounded Runtime
timer API; SYN-SENT retries remain bounded, external handles survive timeout
closure, and a slot is reusable with a new generation only after references
are released. Active entries do not use the passive-child sidecar.

Production behavior remains unchanged: normal flags omit the routing and test
macros, no production API can inject an ISN, IPv4 validates/discards TCP,
and there is no TCP TX, syscall, or timer hook. This host lifecycle does not
establish live TCP or RFC 9293 conformance. Trusted production entropy,
deferred packet dispatch, owned TCP output, and a reviewed runtime integration
remain prerequisites.

### Unified TCP Runtime connection lifecycle (Phase 15.4X)

Active and passive Core entries share one fixed eight-slot Core table; Runtime
does not maintain a second connection allocator. The common reference rules
are:

| Reference | Owner | Lifetime |
| --- | --- | --- |
| Core table reference | Core after LISTEN, active-open, or child creation | Until Core transitions the entry to CLOSED |
| Caller handle | The API caller receiving a generation-tagged handle | Until that caller explicitly releases it; it remains releasable after CLOSED |
| Passive child relationship | Runtime sidecar, one child reference and one parent listener reference | From child registration until the child's first terminal cleanup |
| Timer-service reference | Core service result, temporarily owned by Runtime | Through validation and scalar-result copying; released exactly once before the service call returns |

For an active entry, Core's initial external reference is transferred to the
caller when the test-only open succeeds. For a passive child, Core's initial
external reference becomes the Runtime relationship's child reference;
Runtime retains an additional caller reference and a reference to the parent
listener. These are the only ownership difference between the two kinds of
connection. No public Runtime API exposes a Core handle or pointer.

Core owns protocol transitions and drops table ownership on terminal events.
Runtime's shared idempotent terminal cleanup predicate is exactly
`state == TCP_STATE_CLOSED`; FIN-WAIT, CLOSE-WAIT, LAST-ACK, CLOSING, and
TIME-WAIT are not terminal. Once CLOSED is observed, Runtime removes a passive
child link once, releasing its child and parent references, then lets the
current caller or timer path release its own temporary reference. A closed
slot is reusable only after every external, relationship, and service
reference is gone; allocation then advances the generation. Repeated cleanup
for an already-unlinked handle is harmless, and stale generations cannot
remove a new slot's relationship.

Listener close is refused while child links exist. Child terminal cleanup
unlinks only the matching child generation, so a retained closed child cannot
leave a dangling parent link and a listener cannot be reclaimed while a
relationship refers to it. Pending backlog counts only linked children in
SYN-RECEIVED; established children continue to consume table capacity and
retain their parent relationship until terminal cleanup. Listener, child,
and active entries all count toward the same eight slots.

Host tests cover passive and active establishment, RST, retry exhaustion,
supported FIN transitions, duplicate FIN in TIME-WAIT, timeout cleanup,
retained handles, listener-child unlink, shared capacity exhaustion, and
generation-changing reuse. Core currently supports local close from
ESTABLISHED/CLOSE-WAIT, remote FIN transitions through CLOSE-WAIT or the
FIN-WAIT/TIME-WAIT paths, FIN acknowledgment, and TIME-WAIT expiry. Repeated
close while a FIN is outstanding is a no-op. Payload retransmission and a
general application data plane are not provided. Runtime shutdown is
one-shot: it enters sticky STOPPING, rejects new work, and succeeds only when
the Core table is empty and timer service is inactive; it never invalidates
retained handles. Runtime operations are serialized with local interrupts
masked on this single CPU, not SMP-safe.

This lifecycle is exercised only by host-test interfaces for active/passive
event injection. Production TCP routing, Core event dispatch, TCP TX, TCP
syscalls, production ISNs, and timer hooks remain disabled. The runtime timer
API stays bounded and unwired from LAPIC and network service.

### TCP Runtime local close (Phase 15.4Y)

`tcp_runtime_test_local_close(handle, now_tick, result)` is compiled only with
`TCP_RUNTIME_TESTING`. It validates the generation-tagged handle under the
Runtime serialization guard, submits Core's `TCP_EVENT_APPLICATION_CLOSE`,
and returns only scalar state, action, sequence, timer, and retry fields. It
does not expose a Core handle, serialize the abstract action, or transmit a
packet. During sticky STOPPING it remains available to drain a live modeled
connection; before initialization and after STOPPED it returns DISABLED. It
returns BUSY if timer service is marked active. This is a host-test seam, not a
production close API.

Core transitions ESTABLISHED to FIN-WAIT-1 and CLOSE-WAIT to LAST-ACK, emits
SEND_FIN|SEND_ACK, advances `snd_nxt` by one for FIN, and arms the existing
bounded control timer. Runtime reports FIN_STARTED only when those state and
action results match. ACK validation and transitions remain Core-owned:
valid FIN acknowledgment moves FIN-WAIT-1 to FIN-WAIT-2 or LAST-ACK to CLOSED;
invalid acknowledgment/sequence values do not complete the close. FIN-WAIT-2
waits for peer FIN. Simultaneous close follows Core's CLOSING then TIME-WAIT
path; duplicate FIN in TIME-WAIT is ACKed and restarts its deadline. Retry
exhaustion and TIME-WAIT expiration can reach CLOSED and use the shared
terminal cleanup path. External caller references remain valid until release.

The wrapper rejects local close from LISTEN (the dedicated listener close
checks child links), CLOSED, FIN-WAIT-1/2, CLOSING, LAST-ACK, and TIME-WAIT
without changing state or emitting another action. Core explicitly defines
SYN-SENT and SYN-RECEIVED application close as abort: Runtime reports
HANDSHAKE_ABORTED only when Core returns CLOSED with SEND_RST and ABORT;
passive child terminal cleanup then unlinks its parent relationship. Repeated
FIN close requests are unsupported and do not step Core. There is one global
Runtime table rather than per-instance identity, so a foreign-instance handle
concept does not apply; stale generations are rejected.

Host tests cover active and passive FIN initiation, sequence consumption,
invalid and valid FIN acknowledgments, repeated requests, SYN-state abort,
LISTEN separation, unsupported FIN/TIME-WAIT states, CLOSING, RST, timeout,
timer-service busy behavior, retained references, child link lifetime, and
slot reuse. Production routing, TCP TX, syscalls, production ISNs, and timer
hooks remain disabled; this is not live TCP or RFC 9293 conformance.

### TCP close-state audit (Phase 15.4Z)

The Core application-close event has these implemented results (actions are
abstract and are not transmitted):

| Core state | Result | Actions and ownership effect |
| --- | --- | --- |
| LISTEN | CLOSED | No action; Core drops its table reference. Runtime uses the separate listener-close API, which refuses while children are linked. |
| SYN-SENT, SYN-RECEIVED | CLOSED | SEND_RST and ABORT; control timer is canceled and Core drops table ownership. |
| ESTABLISHED | FIN-WAIT-1 | SEND_FIN and SEND_ACK; `snd_nxt` advances once and control retransmission timer is armed. |
| CLOSE-WAIT | LAST-ACK | SEND_FIN and SEND_ACK; `snd_nxt` advances once and control retransmission timer is armed. |
| FIN-WAIT-1, FIN-WAIT-2, CLOSING, LAST-ACK | Same state | Core accepts the event as an idempotent no-op: no action, sequence change, or timer change. The host Runtime wrapper reports unsupported and does not submit it. |
| TIME-WAIT, CLOSED | Same state | Core no-op; no action or sequence/timer change. The host Runtime wrapper reports unsupported. |

Core segment handling remains authoritative for FIN acknowledgment and
terminal transitions: FIN-WAIT-1 can become FIN-WAIT-2 or CLOSING, a completed
simultaneous close enters TIME-WAIT, LAST-ACK acknowledgment closes, RST and
retry exhaustion close, and TIME-WAIT expiration closes. Only CLOSED is
terminal for Runtime cleanup. Core alone drops Core table ownership on those
transitions. Runtime then removes a matching passive child link once, dropping
the relationship's child and listener references; timer service releases its
own retained result reference once after copying scalar results. Caller-held
Runtime references survive closure and must be released before slot reuse.

Runtime host-test observations are scalar projections rather than embedded
Core results: Core handles, pointers, and borrowed packet data do not escape.
The passive input result preserves the actual Core action/state fields even
when Runtime classifies an event as rejected or unsupported. A Core call that
rejects an event produces no Runtime-side transition. This corrects the prior
result shape that exposed a Core child handle and could hide Core actions on a
non-establishing passive event. Listener-child links remain through FIN and
TIME-WAIT and are removed only on CLOSED; matching generations prevent an old
child from unlinking a reused slot. The table remains fixed at eight entries,
and Runtime/Core operations require external serialization with local
interrupts masked on the single CPU (not SMP synchronization).

The additional Core tests assert that repeated application-close is a true
no-op in LAST-ACK, FIN-WAIT-1/2, CLOSING, TIME-WAIT, and CLOSED, including
unchanged sequence and timer state. This audit does not establish full TCP
conformance. Production TCP dispatch, packet transmission, syscalls, ISNs,
and timer hooks remain disabled.

Runtime regression coverage also drives a passive child through establishment,
remote FIN, local LAST-ACK close, and in-window RST. It verifies that terminal
cleanup removes the parent link once while a retained child handle remains
valid, then reuses the child slot and confirms the stale generation cannot
release or access the replacement.

### TCP Runtime API contract (Phase 15.4AB)

The API declarations in `include/kernel/tcp_runtime.h` are the contract:

| Entry point | Availability and result | Handle/lifecycle effect |
| --- | --- | --- |
| `tcp_runtime_init()` | One-shot; succeeds only from UNINITIALIZED. | Initializes the fixed eight-slot Core table; no caller handle. |
| `tcp_runtime_ready()` | True only in READY. | No mutation. |
| `tcp_runtime_shutdown()` | Non-forcing; false before init/after STOPPED. From READY it enters sticky STOPPING and returns false while entries or service remain; true only after table empty and service idle. | Does not invalidate references. During STOPPING, callers may close listeners and release handles; test-only close hooks can drain modeled connections. No reinit after STOPPED. |
| `tcp_runtime_listener_open()` | Requires nonzero port and unicast or wildcard local IPv4; reports invalid/disabled/stopping/conflict/full/Core error. | Writes an output token only on success; caller owns one reference and must close then release it. Failure leaves the output token untouched and creates no entry. |
| `tcp_runtime_listener_close()` | Boolean Core LISTEN close; refuses linked children and stale/non-listener handles. Allowed in READY or STOPPING. | Drops Core table ownership, not caller reference. Caller separately releases every handle it retained. |
| `tcp_runtime_handle_release()` | Releases one external Runtime reference; false for stale or disallowed lifecycle state. | May be called in STOPPING and after the Core state is CLOSED, while the token still refers to an allocated generation. |
| `tcp_runtime_route_segment()` | Production default returns DISABLED. With the explicit routing gate it can report tuple match, ambiguity, no-match, or errors. | A match returns one retained Runtime token (`has_reference`); caller releases it. It submits no Core event and does not accept payload. |
| `tcp_runtime_service_timers()` | Accepts budget 0–4 and the 100 Hz Core tick domain. Returns status/count/`more_due`; initializes a non-null result even for invalid budget. | Runtime consumes every Core service reference before return; timer items contain scalar event outcomes only. |
| `tcp_runtime_test_*()` | Present only under `TCP_RUNTIME_TESTING`; deterministic active/passive opens, event processing, close, snapshots, and ownership test seams. | Rules are described below; none are production entry points. |

Runtime token fields use the same slot/generation representation as Core's
`tcp_connection_handle`, but ownership is governed by Runtime. A token is not
a transferable Core handle: callers use Runtime release/close APIs and cannot
operate on the private Core table. Generation rejects stale tokens; reuse is
also prevented until Core table ownership and all external/relationship
references are gone.

Ownership is divided as follows. `tcp_core_connection_create()` reserves an
unstarted slot with one caller reference but no table ownership; releasing its
last reference before start reclaims that reservation. A successful LISTEN,
active-open, or passive-child start establishes Core table ownership, which is
dropped only by Core's terminal transition to CLOSED (or listener close).
Active-open's initial external reference transfers to the test caller.
Passive-open's Core-created child reference transfers to the Runtime child
link; Runtime retains one additional child reference for the returned caller
token and one listener reference for the parent link. Lookups and duplicate
SYN/establishment results may return one additional caller reference, which
must be released. Terminal child cleanup removes the matching-generation
link once and releases its child and parent references. Core timer service
retains one reference per returned event; Runtime copies the event and
releases it exactly once before its API returns. No timer handle is retained
by the caller. External references can outlive CLOSED, but prevent slot reuse
until individually released.

Result status and Core outcome are separate. Active-open OK means Core entered
SYN-SENT and a caller handle was returned; active-input OK means Core accepted
the event call, not necessarily that the connection established. For passive
input, status classifies Runtime handling (created, duplicate, established,
closed, processed, rejected, unsupported, or error), while `outcome` carries
the Core scalar result whenever Core processed the event. For an initial SYN,
`outcome.state` is the listener's LISTEN state and `outcome.has_child` is Core
metadata; `handle` is the separate Runtime child token. `has_reference` alone
determines whether that result transfers a token to the caller. The scalar
projection includes actions, state, sequence numbers, windows, timer state and
values, accepted bytes, and child-presence metadata; it excludes Core child
handles, internal pointers, and borrowed packet data. Local-close result
status reports FIN_STARTED or HANDSHAKE_ABORTED only when the corresponding
Core state/action result matches; unsupported paths return the prior scalar
snapshot with no event mutation.

`tcp_runtime_test_snapshot()` is a copy-only query: it succeeds for a CLOSED
entry while some reference still keeps that generation allocated and fails
after reclamation. `tcp_runtime_test_close()` is a Core-event test helper with
a boolean "event accepted" result, not a Runtime close-status classifier.
Test hooks can inject deterministic ISNs and manipulate references/timer
busy-state only in host builds. The production build has no active/passive
connection creation, TCP dispatch, TX, syscalls, or timer hook. Runtime
serialization is local-interrupt masking on the single CPU; host tests must
serialize calls themselves. Neither provides SMP safety. This API contract
does not imply RFC 9293 conformance or live TCP support.

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

### Package-download boundary preparation (Phase 15.5A)

The current guest network path is the explicitly profiled QEMU Intel
82540EM (`8086:100e`) e1000. Limine Base Revision 5+ identity-DMA availability
is required before DMA setup; descriptor and packet ownership transfers
between the bounded netbuf pool, kernel, and device are explicit. Ethernet II,
ARP, static IPv4, ICMP Echo, and bounded IPv4 UDP are implemented. The current
test configuration uses guest `10.15.3.1/24`, MAC
`52:54:00:15:03:01`, and a test peer at `10.15.3.2`, MAC
`52:54:00:15:03:02`. It is a static on-link profile: there is no DHCP, DNS,
default-gateway routing, or general Internet configuration. IPv4 accepts only
the bounded option-free, unfragmented profile. UDP datagrams are limited to
512 payload bytes; there are eight socket slots and four queued datagrams per
socket. The Ethernet netbuf pool has 64 slots of 1600 bytes each.

The current user interface is project-native IPv4 UDP, not POSIX/BSD sockets.
Syscalls 28–31 create a UDP FD, bind, send one datagram, and receive one
datagram. The six-byte endpoint ABI is four IPv4 octets followed by a
host-order port. Socket state is held behind the existing reference-counted
`fd_object`; fork clones descriptor references and process descriptor-table
teardown drops them, so the socket's final close callback releases its fixed
queue. Syscalls copy endpoint and payload bytes between user and kernel
buffers. Send accepts a whole bounded datagram or returns an error. Receive
returns a whole queued datagram; if the caller's capacity is too small it
returns `EMSGSIZE` without consuming that datagram. It does not expose a
partial stream API. There is no nonblocking flag or network wait channel:
receive and ARP resolution poll with a 500-tick bound at the current 100 Hz
timer rate, and TX completion uses bounded polling. Errors are project ABI
errno values; callers must not assume Linux socket error or interruption
semantics. This ABI is not declared stable for a general-purpose networking
library. The kernel currently has no UID/GID or permission enforcement, so a
future system-wide installer cannot rely on today's VFS mode bits as a
privilege boundary.

RX is polled by the existing `udp_socket_poll()` owner and shared with the
cooperative network service. The service has an eight-entry event queue, runs
at most four events and one frame per poll event, and is invoked at syscall
entry and at the blocked-process idle safe point. It is not a worker: a
userspace process that executes indefinitely without entering the kernel can
delay network progress indefinitely. The LAPIC interrupt only advances time
and scheduling state; it does not parse packets. The current UDP syscall path
also polls directly under the existing single-flight guards. Local interrupt
masking protects short shared-state operations on the current single CPU and
is not SMP synchronization. The verified host-peer test demonstrates one
guest UDP request/reply on the explicit e1000 profile; it does not establish
general Internet access.

For a future package downloader, the boundary is user space: a network client
may use a future reliable transport API and pass opaque bounded byte streams
to a separate metadata verifier/downloader. The kernel owns packet transport,
socket resource limits, user-pointer validation, and copying only. It must not
parse repository metadata, decide package trust, or install packages. The
metadata verifier must authenticate signed metadata with a repository public
key provisioned independently of downloads, check a monotonic version or
other freshness policy, bind package hashes and sizes to authenticated
metadata, and fail closed on signature, hash, truncation, or policy errors.
The installer must separately enforce privileged system-wide writes through
an authorization boundary that does not yet exist. Transport encryption
would protect a connection but would not replace repository signature
verification. The initial policy is one official OpenSillicon repository;
third-party repositories are out of scope. Key rotation, revocation, and
rollback policy must be designed before release.

Transport prerequisites remain absent. Production TCP dispatch, output,
syscalls, and ISN generation are disabled; the TCP Core/Runtime models are
not a usable reliable transport. There is no DNS resolver, HTTP client, TLS
implementation, stream connect/read/write API, partial stream I/O contract,
connection cancellation, or robust retry policy. A future TCP API must define
connect and receive deadlines, cancellation on signals/close/process exit,
bounded send/receive queues, backpressure, short reads/writes, EOF/half-close,
peer resets, timeout propagation, and truncated-download detection. DNS must
be added only with bounded parsing, timeout, and configuration policy. HTTP
must enforce response/body size limits and distinguish framing completion
from transport closure; HTTPS needs a reviewed TLS implementation and trust
policy. UDP is not a substitute for reliable package downloads.

No package format, `.ospkg` parser, repository client, signature verifier,
package hash verifier, package installer, or signing-key policy is currently
implemented. The current random-byte wrapper is direct RDRAND availability,
not an approved general-purpose CSPRNG or package-signing trust mechanism;
it is not suitable by itself for keys, signatures, tokens, or TCP ISNs. Before
package downloads can be trusted, the project needs reviewed cryptographic
primitives, independently provisioned trust roots, freshness/key lifecycle
rules, a privilege model, a reliable user-space transport, and end-to-end
tests for invalid signatures, rollback, truncation, interruption, and storage
failure. This preparation phase adds no package-specific kernel API and
changes no network behavior.

### Userspace network diagnostics (Phase 15.5B)

`netdiag` is built as `/bin/netdiag` alongside the existing coreutils and is
launched by the shell's normal external-command path. Its commands are
`netdiag help`, `netdiag capabilities`, and `netdiag udp <IPv4> <port>`.
The argument parser accepts four decimal IPv4 octets in range, rejects
unspecified, multicast, and broadcast destinations, requires exactly four
arguments for `udp`, and accepts decimal ports 1–65535. Invalid arguments
are rejected before syscalls. `capabilities` explicitly says userspace cannot
query live network configuration; the printed `10.15.3.1/24` and `10.15.3.2`
values describe the documented QEMU test profile, not discovered runtime
settings.

The UDP diagnostic creates a project-native IPv4 datagram FD, binds wildcard
local port 45000, sends the fixed request `OS-UDP-REQ-15.3B`, receives one
datagram, and checks both the requested source IPv4/port and exact reply
`OS-UDP-REPLY-15.3B`. It reports socket, bind, send, receive, peer, payload,
and close failures separately and closes the FD on each path after creation.
The existing recvfrom syscall supplies a 500-tick timeout (five seconds at
100 Hz); timeout is reported separately from other receive errors. The
utility adds no retry or receive loop. The kernel syscall itself performs the
existing bounded polling; it is not an event-driven wait and has no network
cancellation API. Port 45000 can conflict with another local socket. The
`e1000-netdiag-qemu-test` target runs this diagnostic routine as a Ring 3
program against the existing socket peer; host tests cover CLI parsing.
Passing proves one UDP request/reply and the stated payload/endpoint checks
only. It says nothing about TCP, DNS, HTTP, HTTPS/TLS, Internet access, or
package downloads.

### Userspace ICMP Echo diagnostic (Phase 15.5C)

Before this phase, ICMP Echo existed only in the kernel: netstack could build
Echo Requests and answer incoming Requests, and it validated incoming Echo
Replies, but no userspace API could submit a request or observe the reply.
Syscall 33 is a narrow, project-native Echo operation, not a raw socket. It
copies one fixed 56-byte `icmp_echo_request` in/out structure, accepts a
numeric destination, identifier, sequence, and at most 32 payload bytes, and
returns only the validated reply source and elapsed timer ticks. Kernel input
copying and output validation use the existing VMM user-copy checks. No FD or
persistent handle is created; one global Echo transaction can be active at a
time, and another caller receives `EAGAIN`.

The kernel accepts only unicast on-link destinations other than its own
address and configured subnet/broadcast endpoints. It resolves the neighbor
through the existing ARP cache/request path, creates an actual IPv4 protocol-1
Echo Request with the existing checksum builder, and uses the existing single
`udp_socket_poll()` RX owner. The parser exposes a borrowed ICMP view only
after IPv4 and ICMP bounds/checksum validation; the view is consumed before
the RX netbuf is released. A reply completes the call only when source, Echo
type/code, identifier, sequence, payload size, and payload bytes match. A
matching ICMP Destination Unreachable quotation returns `EHOSTUNREACH`.
Malformed or unrelated packets are discarded and the caller eventually gets
`ETIMEDOUT`. The receive/ARP deadline is 500 timer ticks (five seconds at
100 Hz); frame transmission retains the existing bounded TX polling cap. The
syscall has no persistent resource to close, and process exit cannot leak an
Echo handle.

`/bin/ping` is built with the coreutils and launched through the shell's normal
external-command path. Its commands are `ping help`, `ping <IPv4>`, and
`ping <IPv4> <count>` where count is 1–4. It accepts numeric IPv4 only and
rejects malformed, unspecified, multicast, and limited-broadcast addresses
before calling the syscall; subnet/network and directed-broadcast checks are
also enforced by the kernel against its configured mask. It reports each
validated reply, timeout, unreachable response, and other syscall error.
Malformed/checksum-invalid or unmatched replies are ignored by netstack and
therefore appear as a timeout rather than a separate user-visible error.

`e1000-icmp-ping-qemu-test` runs `/bin/ping`'s Ring 3 implementation through
syscall 33 on the existing explicit QEMU e1000 device and an Ethernet peer. The
peer verifies the outgoing IPv4 and ICMP checksums, destination, Echo fields,
and payload, then injects a matching Echo Reply. This proves one on-link QEMU
ICMP exchange only; it does not establish Internet access, TCP, DNS, HTTP(S),
TLS, or package-download capability. The operation shares the cooperative
polling limitation: network service remains cooperative, while this synchronous
request itself polls RX until its bounded deadline. There is still no general
raw-packet API, TCP API, or network-configuration query API.

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
