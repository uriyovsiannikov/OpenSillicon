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

- System GCC and GNU binutils build a freestanding x86_64 kernel without
  host runtime or userspace library dependencies.
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
- Create, activate, and destroy isolated address spaces.
- Enforce read/write, user/supervisor, and no-execute mapping permissions.
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
- Calibrate the LAPIC timer against PIT channel 2 at 100 Hz.
- Defer HPET discovery and timekeeping until ACPI support in Phase 10.
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

The initial ABI is recorded in ADR-001. Phase 6 uses a small user-mode smoke
image to exercise ABI version, process ID, standard output, invalid descriptor
and pointer handling, unknown calls, and process exit. File descriptors are
limited to serial descriptors 1 and 2 until VFS work in Phase 8.

Done criteria

- Kernel can enter Ring 3.
- User code executes.
- User code cannot access protected kernel memory.
- System calls work.
- Invalid syscall requests are handled safely.
- User mappings cannot access supervisor-only kernel mappings.

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

ADR-003 is accepted for a custom v1 format: 4 KiB blocks, fixed metadata
regions and 128-byte inodes, 12 direct plus one single-indirect pointer,
variable-length directory records, and a maximum 4,243,456-byte file. Phase 9
uses a narrow 4 KiB block callback for host disk-image tests and VFS mounting;
AHCI and the general block-device layer remain in Phase 10.

Tasks

- Finalize on-disk format through accepted ADR-003.
- Implement filesystem formatter.
- Implement superblock handling.
- Implement inode allocation.
- Implement block allocation.
- Implement directories.
- Implement file read/write.
- Implement metadata and permissions.
- Implement filesystem validation.
- Add malformed-image and host disk-image tests.

Done criteria

- SilliconFS can be formatted.
- Files and directories can be created.
- Files can be read and written.
- Filesystem can be mounted.
- Corrupt structures are detected safely.
- Tests cover disk-image operations.

Journaling is not required in this milestone.

---

Phase 10 — PCI, ACPI, AHCI and Root Switch (completed)

Status: Implemented, built, host-tested, and QEMU UEFI-tested. The kernel uses
the system GCC/binutils freestanding configuration established in Phase 0.

Goal

Introduce real hardware discovery and persistent root storage.

Tasks

- Implement read-only PCI enumeration, using configuration mechanism 1 as
  fallback and ACPI MCFG ECAM when present; enumerate bridges and
  multifunction devices and identify AHCI controllers.
- Integrate pinned ACPICA 20260930 (BSD-3-Clause component) with OpenSillicon
  OSL; use Limine RSDP and load tables, namespace, and AML interpreter.
- Discover MADT through ACPICA and initialize the IOAPIC while preserving
  existing LAPIC, PIC-mask, IDT, and timer behavior. Keep redirection entries
  masked until a later device-interrupt milestone.
- Implement synchronous polling AHCI initialization, IDENTIFY, read, write,
  and flush with validated DMA alignment/address constraints.
- Implement the minimal synchronous block-device interface with negative
  errno returns and 512-byte and 4096-byte logical sectors.
- Detect storage devices.
- Mount and validate SilliconFS from the selected SATA device.
- Replace RAMFS at `/` with SilliconFS before userspace starts, retaining
  initramfs at `/init` and keeping Limine-owned archive memory valid.
- Add host tests for PCI topology and malformed MCFG/MADT tables, sector
  translation and I/O failures, and 512-byte/4096-byte disk images.
- Add QEMU UEFI SATA-disk tests for discovery, filesystem reads/writes and
  flush, root replacement, initramfs access, and serial diagnostics; run all
  prior regression tests. The 4096-byte logical-sector adapter is covered by
  host filesystem-image tests; QEMU's IDE device only supports 512-byte
  logical sectors in this configuration.

Done criteria

- PCI devices are enumerated.
- ACPI tables load through ACPICA and MADT entries are validated.
- IOAPIC initializes from MADT while existing LAPIC/timer regressions pass.
- AHCI controller is detected.
- SATA storage reads, writes, and flushes work through the block layer.
- 512-byte and 4096-byte logical-sector images pass block/filesystem tests.
- SilliconFS mounts from a disk image or virtual disk.
- System switches to persistent root, and `/init` remains accessible without
  invalidating initramfs backing memory.

Out of scope: NVMe, USB, journaling, NCQ, asynchronous block queues, block
caching, ACPI power management/events, and unrelated PCI device setup.

Completed host tests: `make test elf-test vfs-test silliconfs-test phase10-test
acpica-check`. Completed QEMU tests: `make process-test vfs-smoke-test
timer-test fault-test interrupt-test vmm-protection-test`. QEMU targets verify
serial output and expected timeout-based kernel halt behavior.

---

Phase 11 — Input, TTY and Terminal

Goal

Provide a usable framebuffer text terminal with canonical keyboard input.

Tasks

- Implement a polling i8042 PS/2 keyboard driver that emits key events from
  translated Set 1 scancodes; yield with `sti; hlt; cli` between polls while
  waiting for input so the LAPIC timer wakes the CPU without busy-spinning.
- Add a US keymap with Shift, Caps Lock, and Control handling.
- Implement canonical TTY input on standard input, including echo, backspace,
  newline, a 256-byte line limit, and the approved Ctrl-D EOF/line behavior.
- Implement four virtual terminals with independent screen buffers, cursors,
  parser states, current input lines, and completed-input queues; switch with
  Alt+F1 through Alt+F4.
- Render Limine's RGB framebuffer with the built-in 8-by-16 Modern DOS bitmap
  font (CC0 1.0).
- Implement newline, carriage return, backspace, tab, scrolling, CSI `2J`,
  `H`, `K`, and SGR reset, standard 8-color, and bright 8-color
  foreground/background sequences.
- Route standard output once through a console fan-out to framebuffer and
  serial; keep kernel diagnostics on the serial logger.
- Log unavailable i8042 hardware and keep terminal output usable when no
  keyboard controller initializes; timer-driven initialization waits time
  out after 100 ticks (one second at 100 Hz).

Done criteria

- Host tests cover framebuffer drawing, keyboard decoding, terminal parsing,
  scrolling, independent virtual-terminal screen/input state, TTY buffer
  limits, and Ctrl-D with empty and nonempty input.
- QEMU UEFI boots with framebuffer output and serial diagnostics.
- Keyboard input is available when the emulated i8042 controller initializes;
  absent hardware is reported without a busy-spin.
- Four virtual terminals retain their screen and input state when switched.
- Terminal input and the documented control-sequence subset work.
- The full regression suite passes.

Deferred: mouse, USB HID, keyboard IRQ routing, Unicode, alternate layouts,
termios, signals, window systems, and graphical desktop features.

---

Phase 12 — Init, Shell and Accounts

Goal

Create a usable minimal Unix-like userspace.

Tasks

- Launch `/sbin/init` from persistent SilliconFS with the documented minimal
  ELF startup stack and TTY descriptors 0–2.
- Append syscall 13 `sleep_ticks(ticks)` for scheduler-blocking tick delays;
  reject overflowing deadlines and return immediately for zero ticks.
- Implement init that starts `/bin/sh`, waits for exit, sleeps ten ticks, and
  restarts the shell. Missing or unloadable init is diagnosed over serial and
  halts.
- Implement a freestanding shell with `help`, `echo`, `pwd`, `cd`, `ls`,
  `cat`, and `exit`.
- Keep `cd` state in the shell and resolve VFS operations to absolute paths.
- Execute external programs by absolute path or bare name under `/bin` using
  `fork`, append-only `exec_path_argv`, and `waitpid` calls.
- Populate `/etc/passwd` and `/etc/group` with root and regular-user metadata;
  do not add authentication, kernel identities, or permission enforcement.
- Populate required `/bin`, `/sbin`, `/etc`, and `/tmp` directories on the
  persistent root image.
- Defer rc.d, persistent logging, `PATH`, environment support, and Phase 13
  IPC/POSIX work.

Current command model: `help`, `cd`, and `exit` remain shell built-ins.
`ls`, `cat`, `echo`, `pwd`, `true`, `false`, `clear`, `uname`, and `fetch`
are standalone `/bin` ELF programs. `fetch` displays the supplied logo and
available system details. Shell tokenization splits on spaces, tabs, CR, and
LF, with no quote or escape syntax; the 256-byte line permits at most 16
arguments. Syscall 27 accepts 1–16 arguments and at most 2048 total string
bytes including NULs. argv[0] is the command token as typed; envp is empty.
The user startup stack is 16-byte aligned and carries argc/argv plus the
existing auxv. The old path-only exec syscall remains supported. `ls` lists
`/` (excluding `.` and `..`) or requested paths, `cat` reads requested files
or stdin when no file is named, `echo` prints all operands, and `uname` accepts
`-s`, `-m`, and `-a`. Relative `ls`/`cat` paths are rooted at `/`, as the
process model has no cwd syscall. `pwd` reports `/`. File mutation
utilities are omitted because userspace has no create/unlink/rename syscall.
`head`, `tail`, and `wc` remain deferred as utilities not yet implemented;
`yes` is omitted because its unbounded output is not useful without shell
redirection or pipes. `date` has no clock
query syscall, and `whoami` has no kernel credential interface.
The framebuffer console supports bounded ANSI CSI plus SGR reset, 8 standard
and 8 bright foreground/background colors; it is not a full VT emulator.
Boot initialization lines use colored `* component ... [ OK ]` / `[ ER ]`
status text where the code has a checkable initialization result. The process
initializer is a nonallocating state reset with no failure path, and is logged
after it returns. No roadmap phase is marked complete by this work.

Done criteria

- System boots into userspace.
- Init starts and restarts the shell after a timer-based delay.
- Shell accepts commands.
- External static ELF execution passes argv through fork/exec_path_argv/waitpid.
- Account files contain the documented metadata records.
- Basic commands operate on persistent SilliconFS.
- Syscall sleep blocks without consuming CPU and wakes after timer ticks.

---

Phase 13A — Anonymous Pipes

Goal

Provide bounded, scheduler-blocking anonymous byte-stream pipes using the
existing process, descriptor, fork, exec, and syscall architecture.

Tasks

- Append syscall 14 `pipe(fds)` while preserving ABI v1 calls 0–13.
- Add a 4096-byte ring buffer with separate reference-counted read and write
  endpoints.
- Use existing `read`, `write`, and `close` syscalls; support short transfers,
  EOF, and `EPIPE` without `SIGPIPE`.
- Preserve endpoint lifetime through fork descriptor cloning and exec.
- Add scheduler wait channels and wake blocked readers/writers on data,
  capacity, and endpoint closure. No busy-waiting.
- Guarantee failed user copies do not consume input data or publish partially
  installed descriptors.
- Add host ring-buffer/failure tests and QEMU process integration tests.
- Run the full Phase 12 and earlier regression suites.

Done criteria

- Parent and child processes communicate through inherited pipe descriptors.
- Exec preserves descriptors as documented.
- Empty reads block then return data or EOF; full writes block then gain space
  or fail with `EPIPE` after the final reader closes.
- Invalid output pointers, descriptor exhaustion, and copy failures clean up
  without leaking resources or losing pipe bytes.
- Host, QEMU, and previous regression tests pass.

ADR-019 defines the Phase 13A contract. Signals and shared memory remain
deferred to separately reviewed stages. Also deferred: `dup2`, nonblocking
mode, `poll`/`select`, named pipes, shell pipelines/redirection, signal
interruption, libc work, and broader POSIX compatibility.

### Stage 13B — Minimal Signals

Goal

Add the minimal signal contract in ADR-020 without changing existing syscall
semantics or redesigning TTY blocking.

Tasks

- Append syscalls 15–17: `kill`, `sigaction`, and `sigreturn`.
- Add pending/disposition state, safe Ring 3 handler entry, and trusted
  kernel-frame restoration through the exact validated restorer stub.
- Add executable user-range validation using page-table NX permissions.
- Define fork and exec signal-state behavior; reject fork during an active
  handler with `EBUSY`.
- Add a serialized signal wake path distinct from pipe resource retry;
  support interruption of `waitpid`, `sleep_ticks`, pipe reads, and writes.
- Test host state transitions and QEMU delivery, return, default/ignored/
  uncatchable behavior, invalid inputs, blocked syscall interruption, and
  fork/exec disposition rules.

Done criteria

- No signal is lost between pending checks and Ring 3 return.
- Invalid restorer sites never restore user-supplied register state.
- Failed `sigaction` copy-out leaves the disposition unchanged.
- Supported blocked calls return or complete according to ADR-020 race rules.
- Stage 13A and earlier regression suites pass.

TTY reads remain outside signal interruption guarantees. Scheduler-aware TTY
blocking, terminal-generated Ctrl-C, process groups, job control, SIGPIPE, and
broad POSIX interfaces are deferred. Stage 13C defines the bounded anonymous
shared-memory subset below; named shared memory and independent attach remain
deferred.

### Stage 13C — Anonymous Shared Memory

Goal

Add zero-filled shared mappings that processes can share through `fork()`,
with explicit ownership and safe address-space teardown.

Tasks

- Append syscall 18 `shm_create(size)` and syscall 19 `shm_detach(base)`;
  preserve ABI v1 calls 0–17.
- Reserve arena `[0x0000700000000000, 0x00007fffffffc000)` with unmapped
  boundary and inter-mapping guard pages. Use a monotonic per-address-space
  cursor and do not reuse detached virtual addresses during that address
  space's lifetime.
- Reject ELF segments whose page-rounded ranges intersect the shared arena.
- Create zero-filled user-readable, user-writable, non-executable mappings.
- Preserve shared mappings at identical virtual addresses during fork while
  eagerly copying ordinary private pages.
- Track one shared-object reference per address-space mapping. Acquire a
  child's reference before installing shared PTEs; roll back partial mappings
  and release committed child mappings exactly once if fork later fails.
- Make private-page destruction skip shared leaves. Remove page-table mappings
  before releasing the final backing-page reference.
- Release mappings on detach or address-space destruction. Successful exec
  drops old mappings; failed exec preserves them. Zombie references remain
  until reap under current process lifetime rules.
- Test accounting and rollback on the host, and mapping, fork, exec, detach,
  zombie/reap, and permissions under QEMU. Run Stage 13B and earlier
  regression suites.

Done criteria

- Parent, child, and later fork descendants see writes to the same backing
  pages; private pages retain eager-copy behavior.
- Failed mapping and fork paths leave the parent unchanged and free no page
  still mapped by another process. A fork failure after one completed shared
  mapping releases that child reference exactly once.
- Detach and address-space destruction release one reference each; backing
  pages are freed exactly once after the final reference disappears.
- Successful/failed exec and zombie/reap behavior match ADR-021.
- Host, QEMU, and previous regression tests pass.

ADR-021 defines Stage 13C. Named shared memory, independent attach, global
object IDs, synchronization primitives, read-only or executable mappings,
and POSIX shared-memory compatibility remain deferred.

---

Phase 14 — musl and Static Userspace

Goal

Support a bounded static C userspace environment through a native OpenSillicon
musl port, without claiming Linux syscall ABI compatibility.

Tasks

- Stage 14A: accept the native musl port contract in ADR-022; preserve the
  existing syscall ABI and identify the later runtime decisions that remain
  open.
- Stage 14B design: define the bounded static ELF startup stack and
  single-thread PT_TLS/FS-base lifecycle in ADR-023.
- Stage 14C: pin musl 1.2.6 and verify its x86_64 CRT, TCB/TLS, auxv, and a
  representative static ELF in ADR-024. The pin is a source-verification
  baseline, not a release approval; Stage 14D records a reproducible security
  patch series and its validation limits in ADR-025. Stage 14C/14D do not
  vendor musl, and the patched baseline is not approved for distribution.
- Stage 14E: propose the source provenance layout, native target/toolchain,
  syscall adapter, private-memory, CRT/TLS ownership, VFS/process, sysroot,
  and first integration milestone in ADR-026. This design assigns no syscall
  numbers and does not establish that the target toolchain already works.
- Stage 14F: pin the musl 1.2.6 archive and reviewed patch bytes in
  `third_party/musl`; verify provenance and reproduce a pristine extraction
  plus the confirmed upstream patch series without building musl. Keep the
  unaccepted `regexec` candidate separate from the default patched tree.
- Stage 14J: build the pinned confirmed-patch source with Clang/LLD for
  `x86_64-unknown-none-elf`, install a generated static sysroot, adapt the
  bounded set of musl startup syscalls to the existing OpenSillicon ABI, and
  run one real musl CRT/TLS/errno/write/exit executable on the persistent root
  in QEMU. This is a first executable proof, not completion of practical libc
  support.
- ADR-031 resolves the native FS-base setter and its process/interrupt
  lifecycle. Before port implementation, resolve memory allocation, VFS/FD
  and process/signal adaptation, and the reproducible userspace toolchain.
  Measure representative programs' actual syscall needs
  before freezing the supported surface.
- Adapt musl to the OpenSillicon ABI; do not add Linux syscall-number or errno
  compatibility by assumption.
- Implement only the missing runtime and libc interfaces approved for the
  initial profile.
- Configure static linking.
- Build basic userspace programs.

Done criteria

- musl builds for OpenSillicon.
- Static userspace programs execute.
- Basic libc functionality works.
- Unsupported interfaces fail predictably.

The Stage 14J first executable passed in QEMU using the pinned musl 1.2.6
development baseline. The implemented adapter supports only the operations
listed in ADR-032. Public anonymous mapping for mallocng, stdio/writev,
broader VFS/process/time operations, and thread support remain prerequisites;
the phase is not complete. The initial profile is static x86_64 userspace. Dynamic linking, shared
libraries, PIE/ASLR, threads, futexes, and broad Linux compatibility remain
deferred. ADR-023 and ADR-024 define the startup/TLS design and verify it
against the pinned source; ADR-025 records the development-only security
baseline. ADR-031 now defines the native FS-base syscall and kernel TLS
lifecycle. Remaining port prerequisites include the native memory allocator
interface, VFS/FD and process/signal adaptation, and reproducible target
toolchain under ADR-022 and ADR-026.

---

Phase 15 — Networking

Goal

Introduce a bounded native IPv4 stack and socket interface without Linux
syscall compatibility.

Tasks

- Stage 15A: implement the bounded packet-buffer ownership model, deferred
  network service hook, PCI BAR/DMA prerequisites, and polling e1000 driver.
  Acceptance: QEMU e1000 link initialization and repeatable frame transmit /
  receive verified with an isolated host peer; no protocol processing in IRQ
  context.
- Stage 15B: implement Ethernet II, ARP, IPv4 validation/checksums, ICMP echo,
  static interface configuration, and direct/default route selection.
  Acceptance: host parser/checksum and malformed-frame tests plus QEMU ARP,
  IPv4, and ICMP exchange.
- Stage 15C: add UDP and the first native socket descriptors/syscall contract.
  Acceptance: bind/send/receive across QEMU and host, bounded queue and
  descriptor lifecycle tests, with existing FD/process regressions passing.
- Stage 15D: resolve a kernel entropy source, then implement bounded TCP
  connection setup, retransmission, stream queues, listen/accept, and teardown.
  Acceptance: active and passive QEMU TCP exchanges, timeout/retry and
  malformed-segment tests. Do not use predictable production initial sequence
  numbers as a substitute for entropy.
- Stage 15E: integrated packet, socket, process, and regression suite; record
  supported behavior and exclusions before marking Phase 15 complete.

Architecture is recorded in ADR-036. These are proposed implementation
stages; none of the networking components is implemented yet.

Done criteria

- QEMU NIC packet transmit and receive are verified against an isolated peer.
- Static IPv4 connectivity, ARP, and ICMP behavior pass packet-level tests.
- UDP socket operations work across the QEMU boundary.
- TCP active/passive connections, bounded retransmission, and teardown pass
  deterministic tests.
- Resource bounds, malformed packet handling, and existing subsystem
  regressions pass.

### Milestone 15.1 — PCI resources, packet pool, and e1000 foundation

Status: partial; generic MMIO and DMA foundations exist, but PCI aperture
discovery remains blocked on a safe device lifecycle.

Implemented: a non-probing PCI BAR decoder distinguishes I/O, 32-bit memory,
and 64-bit memory BARs and marks the upper slot of a 64-bit BAR as consumed;
a fixed 64-entry packet pool provides 1600-byte storage with 32-byte
headroom, generation-tagged handles, explicit core/device ownership transfer,
and bounded exhaustion; a generic device lifecycle/TX/RX interface validates
its state and ownership contract. Host tests cover BAR decoding, pool
exhaustion/stale handles, lifecycle, and packet ownership.

Stage 15.1B implements contiguous bounded PMM runs, generation-tagged DMA
allocation handles, CPU/device ownership transfer with x86 ordering fences,
and releasable uncached MMIO leases inside a dedicated vmalloc window. MMIO
accepts validated apertures and ranges residing in Limine base revision 4
RESERVED memory, rejecting HHDM RAM/cache aliases. The kernel requests Limine
base revision 5; base revision 4 remains the minimum for its ACPI/HHDM boot
contracts.

Stage 15.1C establishes a fail-closed DMA address policy: DMA is available
only when the revision-5 request was honored, the loaded base revision is at
least 5, and no Keep-IOMMU request is present. A lower loaded revision leaves
DMA unavailable and DMA-dependent drivers disabled with an explicit
diagnostic. The current persistent-root startup requires AHCI, so it halts
with a specific message if that prerequisite is absent. Limine's revision-5
IOMMU guarantee is not IOMMU translation support and does not establish
non-coherent cache synchronization; those environments remain unsupported.

Per-descriptor DMA ring ownership now has tested CPU/device transitions,
publication/completion barriers, and allocation pinning: ring backing cannot
be freed or handed off as a whole buffer until the caller confirms the device
was stopped and the ring is quiesced. This is software contract testing, not
device DMA validation. Generic PCI BAR aperture sizes remain unknown and are
not probed. A strict QEMU-only 82540EM profile accepts `8086:100e` with
BAR0 128 KiB non-prefetchable 32-bit MMIO and BAR1 64-byte I/O; it rejects
other IDs or layouts and does not claim generic PCI sizing. QEMU 10.2.2 QMP
confirmed the configured model's region sizes, but no guest e1000 mapping,
initialization, DMA, TX, or RX has been exercised. Stage 15.1 remains
incomplete; see ADR-037 for exact contract and test evidence.

### Milestone 15.2A — QEMU e1000 discovery and reset

Status: implemented for the bounded QEMU `8086:100e` 82540EM profile.
The guest PCI enumeration callback retains only this exact device; profile
validation rejects unexpected BAR layouts. The driver enables memory decode,
explicitly clears and verifies PCI bus mastering, maps BAR0 through an uncached
MMIO lease, issues CTRL.RST, and polls with a bounded count. It validates
post-reset CTRL and STATUS reads and logs the BAR base, aperture, command value,
and register values. Initialization is idempotent. Failure paths leave the
device failed and disable PCI memory/bus-master decode where config access
allows; an uncertain MMIO lease is retained rather than released unsafely.

This milestone does not configure DMA rings, enable interrupts, or transmit or
receive packets. QEMU guest logs prove discovery, BAR mapping, reset completion,
and register reads only. Generic PCI BAR sizing remains unresolved; the 128
KiB BAR0 aperture is accepted only as the explicit QEMU profile. See ADR-038.

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

### Stage 14K-A — Native private anonymous mapping ABI

Expose syscalls 21–23 for bounded private anonymous mappings, complete-map
unmap, and protection changes, then use the ABI from pinned musl 1.2.6
mallocng. Acceptance requires the real static musl allocator fixture to pass
under QEMU and the existing regression suites to remain green. File-backed,
shared, executable, and partial-unmap behavior, plus `brk`, `mremap`, threads,
and Linux syscall compatibility, remain deferred. See ADR-033.

Stage 14K-A validation passed: the pinned musl build and ELF inspection, the
real mallocng allocator QEMU fixture, and the existing kernel/process/private
mapping/protection/shared-memory/FS-base regression targets completed
successfully. The musl detached-signature check remains skipped when no
signature/key pair is supplied; archive and patch hashes are verified.

### Stage 14K-F — Per-process x87/SSE context

Enable the x86-64 legacy FXSAVE state for Ring 3, enforce an integer-only
kernel build, and eagerly save/restore independent process state at every
process scheduler selection. Verify fork, exec, syscall, and timer-preemption
behavior with Ring 3 tests. AVX, XSAVE, threads, and kernel FPU use remain
deferred. Validation passed: `make -B kernel fpu-context-qemu-test`,
`make native-musl-stdio-qemu-test`, and the kernel, ELF, process, Phase 13B,
private mapping/protection, shared-memory, allocator, and FS-base regression
targets recorded in ADR-034. This resolves the stdio SSE #UD but does not
complete Stage 14K-B. See ADR-034.

### Stage 14K-B — Native musl stdio and terminal I/O

The bounded native `writev`/`readv`/`TIOCGWINSZ` ABI is implemented as
syscalls 24–26. The pinned musl stdio fixture covers output, terminal
detection/query, VFS input, allocation, TLS, and exit. Its final test record
is recorded in ADR-035. The native musl verification/build/ELF inspection,
stdio and allocator QEMU fixtures, FPU and FS-base tests, process/Phase 13B,
private mapping/protection, and shared-memory regression targets passed.
The detached source signature check was skipped because no signature/key
pair was supplied. Full termios, PTYs, terminal job control, and arbitrary
ioctl requests remain deferred.
