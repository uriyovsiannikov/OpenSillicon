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
`ls`, `cat`, `echo`, `pwd`, `true`, `false`, `clear`, `uname`, `fetch`,
`head`, `wc`, `stat`, `basename`, `dirname`, `kill`, `sleep`, `which`, and `ps`
are standalone `/bin` ELF programs. `fetch` displays the supplied logo and
available system details. `kill`, `sleep`, and `which` use the existing signal,
timer, and VFS interfaces. `ps` remains unavailable because no process listing
API exists. Shell tokenization splits on spaces, tabs, CR, and
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
`tail` remains deferred because no seek operation exists; `yes` is omitted
because its unbounded output is not useful without shell
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

Phase 14 — Static Userspace (libos replaces the musl integration)

Status update (2026-11-02): the musl integration described below is historical
and is no longer an active build or runtime dependency. The pinned musl source
archive and historical fixtures remain in the repository for provenance; the
active `user/musl` adapter, scripts, and build/test wiring have been retired.
The current implementation is `user/libos`, a native, static, single-threaded
x86_64 runtime with a deliberately small API. Its supported surface and current
verification are documented in `user/libos/README.md`. The earlier musl
milestones below record what was built and tested at that time, not current
support or a compatibility promise.

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
  `user/musl`; verify provenance and reproduce a pristine extraction
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

Architecture is recorded in ADR-036. The network foundation and bounded
QEMU-only e1000 frame path are implemented as described below. Ethernet,
ARP, IPv4, ICMP, and UDP have bounded QEMU-tested implementations. TCP has
only the isolated wire/state-machine core in milestone 15.4B; live TCP and a
TCP socket API remain unimplemented.

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
accepts validated ranges fully covered by RESERVED memory or wholly absent
from the Limine memory map, rejecting described RAM/boot ranges and HHDM
RAM/cache aliases. The kernel requests Limine
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

### Milestone 15.2B — Bounded e1000 DMA rings and Ethernet smoke

Status: implemented and guest-tested for QEMU 10.2.2's explicit Intel
82540EM profile (`8086:100e`), using the Limine Base Revision 5 identity-DMA
contract. The driver configures eight legacy 16-byte TX descriptors and eight
legacy 16-byte RX descriptors in separate 128-byte, 128-byte-aligned DMA ring
allocations. Each ring slot has explicit CPU/device ownership; TX and RX use
separate contiguous 2048-byte DMA buffers. Packet length is bounded to 1518
bytes and completed RX frames require DD, EOP, no descriptor error, and a
valid Ethernet frame length. Polling is bounded, interrupts remain masked,
and no higher-level protocol is parsed.

PCI Bus Mastering is enabled only after the validated DMA contract, successful
ring and buffer allocations, descriptor publication, and ring register
programming. TX completion is reclaimed only after DD. RX data is copied into
the bounded core packet pool before its DMA buffer is rearmed. Shutdown disables
engines, performs bounded reset and PCI command readback, and releases DMA only
after the driver confirms stop. If stop cannot be confirmed, DMA allocations
are retained rather than freed while hardware access may remain possible.

The dedicated QEMU test uses q35, `-nodefaults -nic none`, explicit
`-netdev socket` to a loopback host peer, and `-device e1000` with an explicit
MAC. The peer observed the exact 60-byte TX frame and injected a deterministic
60-byte RX frame. Guest serial output confirmed TX descriptor 0 DD, RX
descriptor 0 DD/EOP, exact frame acceptance, and clean DMA shutdown. This is
QEMU profile evidence only; physical NICs, IOMMU translation, non-coherent DMA,
interrupts, and protocol/network-stack behavior remain unsupported or
unverified. See ADR-039. Phase 15 is not complete.

### Milestone 15.3A — Bounded ARP, IPv4, and ICMP Echo

Status: implemented for the static test profile over the QEMU 82540EM DMA
driver. The stack accepts bounded untagged Ethernet II frames, Ethernet/IPv4
ARP, IPv4 without options or fragmentation, and ICMP Echo Request/Reply only.
It uses MTU 1500, accepts at most 1518-byte Ethernet frames, and keeps an
eight-entry ARP cache expiring after 6000 timer ticks with deterministic
round-robin replacement. The QEMU peer uses guest `10.15.3.1/24`, MAC
`52:54:00:15:03:01`, and peer `10.15.3.2`, MAC `52:54:00:15:03:02`.

Host tests cover wire builders, checksums, ARP cache expiry/replacement,
malformed and unsupported frames, IPv4 bounds/fragment policy, and Echo
handling. A test-only guest poller resolves the peer with ARP, transmits an
ICMP Echo Request, verifies the matching Echo Reply, then safely shuts down
the e1000. See ADR-040.

### Milestone 15.3B — Bounded UDP and project-native socket descriptors

Status: implemented and QEMU guest-tested for the static `10.15.3.1/24`
profile and explicit `8086:100e` e1000. IPv4 UDP validates the exact UDP
length and packet bounds; nonzero checksums are validated over the IPv4
pseudoheader, while zero checksums are accepted as allowed for IPv4. Outbound
datagrams include a valid checksum. The fixed profile limits payloads to 512
bytes, active sockets to eight, and queued datagrams to four per socket.
Sockets are reference-counted file descriptors; this is a project-native
datagram interface, not POSIX/BSD or Linux socket ABI compatibility.

Syscalls 28–31 create, bind, sendto, and receive-from. Receive uses bounded
polling with a five-second deadline; queue overflow drops the newest packet,
and an undersized destination returns `EMSGSIZE` without dequeuing. Binding
supports wildcard and the configured local address, fixed ports, and
deterministic ephemeral ports for unbound senders. TCP, DHCP, IPv6, routing,
forwarding, and background network processing remain deferred. ADR-041 records
the ABI and current limitations. Phase 15 remains incomplete.

### Milestone 15.4B — TCP wire and deterministic state-machine core

Status: implemented as an isolated, host-tested core. `kernel/net/tcp/tcp_core.c`
parses and builds bounded IPv4 TCP segments, validates pseudoheader checksums,
and supports only MSS, NOP, and EOL options. Its generation-tagged table has
eight entries, at most four SYN children per listener, a 2048-byte receive
queue per connection, a 1480-byte segment bound, five modeled control retries,
and a 12000-tick TIME-WAIT interval. The model covers all listed TCP states,
active/passive handshake events, ACK and receive-sequence checks, in-order
data, duplicate ACK actions, FIN/RST, and deterministic cleanup. Host tests
exercise checksums, malformed input, sequence wrap/bounds, close paths,
backlog and table exhaustion, retained handles, and slot reuse.

This milestone does not connect TCP to netstack, e1000, the runtime timer
interrupt, descriptors, or syscalls. It does not expose a TCP socket API or
enable live connection establishment. Listener and active-open event variants,
including the explicit deterministic sequence field, are enabled only in the
host test build with `TCP_CORE_TESTING`; they are absent from the production
kernel object. No kernel random source is connected to production ISN
generation. TCP data transmission/retransmission, live IPv4
integration, socket ABI, and QEMU peer testing remain future work. This is an
experimental bounded model, not POSIX/BSD or standards-complete TCP.

### Milestone 15.4C — Bounded TCP timer service and entropy audit

Status: implemented as an isolated primitive, not wired into the runtime.
`tcp_core_service()` examines the fixed eight-entry table and processes at
most four due control-retransmission or TIME-WAIT events per call. It returns
actions and generation-tagged handles with temporary external references;
the caller must release each returned handle. `more_due` reports remaining
work. A future deferred service loop can call it outside interrupt context,
then translate the returned actions into packets. No such general network
service hook exists yet; the timer interrupt only advances the 100 Hz tick,
wakes processes, and schedules preemption, while current UDP polling occurs
in the socket/test polling path.

Control timers start at 100 ticks (1 second), double to a cap of 800 ticks (8
seconds), and abort after five retries. ACK of the outstanding SYN/FIN cancels
that timer. TIME-WAIT lasts 12000 ticks (120 seconds). Deadline comparisons
use unsigned half-range subtraction and are wrap-safe for these bounded
intervals. Host tests inject ticks and cover early/due events, backoff,
retry exhaustion, cancellation, TIME-WAIT and retransmit deadline wrap,
service budget exhaustion, and handle release during pending timers. Only
SYN/FIN control retransmission is modeled; TCP data retransmission is absent.

At the time of this audit, there was no kernel CSPRNG, CPU RNG use, or boot
entropy request. The bundled Limine header defines an entropy response, but
the kernel did not request it. The Phase 15.6A.1 follow-up now requests and
validates this optional response, but Limine specifies its values as best
effort and permits fallback to a timing-seeded PRNG, so they remain
unacceptable as a trusted ISN source under this policy.
`tcp_core_live_isn_available()` remains false; there is no live ISN generation
or connection-establishment path. A trusted, conditioned entropy source,
seeding/failure policy, and reviewed CSPRNG are prerequisites before live TCP.

### Milestone 15.4E — Trusted entropy and kernel CSPRNG readiness

At the time of this audit, no accepted entropy source or random-byte API was
present. The bundled Limine protocol defines an entropy response, but the
kernel does not request it and the values are best-effort: the bootloader may
fall back to a timing-seeded PRNG. That response is not accepted as a
production ISN source. The audit's fail-closed conclusion was retained until
the follow-up 15.4I capability implementation described below.

### Milestone 15.4F — Bounded deferred network service

Status: implemented with two safe points and no timer-ISR protocol work. A
fixed eight-event queue accepts RX-poll requests; one call processes at most
four queued events, and each event polls at most one frame. Every syscall
dispatch requests and drains bounded RX work, so service is not limited to
network syscalls. When all user processes are blocked, `schedule_from()` also
requests and drains work after `sti; hlt; cli` returns to the scheduler idle
loop. The LAPIC handler remains limited to tick advancement, wakeups, and
preemption.

The existing UDP receive polling remains available and shares an explicit
single-owner/reentrancy guard with deferred polling. On this single-CPU kernel,
short state guards mask local interrupts; this is not SMP synchronization.
Requests beyond queue capacity are rejected, and the service reports bounded
progress and pending work. e1000 shutdown is refused while service work is
running or queued. UDP QEMU regression verifies a queued poll is drained before
safe device shutdown. A syscall-free, continuously runnable user process does
not get network service on timer preemption; no dedicated kernel worker or
generic background poll loop exists. Kernel threads are cooperatively
scheduled and there is no network event-wait primitive, so a worker would
require scheduler/blocking changes outside this milestone. Events are not
coalesced: a full queue rejects the newest request while retaining queued
requests, and each runtime safe point drains up to four even if enqueue fails.
Progress therefore depends on future syscall or blocked-idle safe points.

The isolated `tcp_core_service()` timer model is not integrated: the runtime
has no owned TCP table/lifecycle. The conditional random-byte API is not
integrated with TCP; `tcp_core_live_isn_available()` remains false and no TCP
packet or syscall path is enabled. This milestone does not claim live TCP or
asynchronous service while arbitrary user code runs without kernel entry.

### Milestone 15.4I — Kernel RDRAND API foundation

Status: implemented and host-tested. `kernel_random_init()` checks CPUID
leaf 1 ECX.RDRAND, performs a bounded validity-checked RDRAND probe, and
reports unavailable if the feature is absent or ten attempts fail. The
kernel API returns at most 256 bytes per request, checks the RDRAND carry
flag, and fails the whole request without modifying the caller's output if
the source fails. Such failure permanently marks the source unavailable for
this boot. There is no deterministic production fallback. Host-only tests
can inject a marked deterministic source; those hooks are excluded from the
kernel build.

RDRAND output is generated by the processor DRBG (Intel documents CTR_DRBG
with AES); this wrapper does not implement a separate software DRBG, entropy
pool, or reseed schedule. Trust depends on the CPU implementation and the
platform or hypervisor exposing it faithfully. The current QEMU regression
uses its default x86 CPU profile without `-cpu`; on QEMU 10.2.2 the guest
reported the source unavailable, as intended, while continuing boot and UDP
traffic. A separate TCG boot using `-cpu max` reported random-source
initialization success, which verifies instruction execution in that
emulated profile, not physical hardware entropy quality. At this milestone,
Limine entropy remained unrequested because its protocol is best-effort and
permits timing-seeded fallback. The 15.6A.1 follow-up adds bounded inspection
but does not change that trust classification. No TCP code consumes this
API: `tcp_core_live_isn_available()` remains false, and live TCP and
production ISNs remain disabled. Local interrupt masking protects calls only
against timer preemption on this single-CPU kernel, not SMP concurrency.

### Milestone 15.4J — Software CSPRNG design review

Decision: defer a software DRBG. The kernel contains no reviewed ChaCha20,
AES, or hash implementation suitable for reuse. Introducing one would also
require an independently reviewed seed-conditioning, reseed, state-update,
and zeroization design; mixing RDRAND with RDSEED without that design is not
an acceptable shortcut. Intel documents RDRAND support at CPUID.1:ECX[30]
and RDSEED support at CPUID.7:EBX[18]. OpenSillicon checks only RDRAND and
does not execute RDSEED. These instructions are not treated as
interchangeable. The direct RDRAND API remains optional and fails closed when
the feature or instruction output is unavailable. There is no software
generator state, seed size, or reseed schedule. The default QEMU profile
remains unavailable; `-cpu max` was separately boot-tested and returned
successful RDRAND initialization, which is not evidence of physical
platform/hypervisor trust. Production random requests are therefore
platform-conditional, while production ISNs and live TCP remain disabled.

Phase 15.4K audit: the implementation queries only CPUID leaf 1 for RDRAND;
it does not query leaf 7 or execute RDSEED. Intel documents RDRAND as its
CTR_DRBG output and RDSEED as seed material, but those implementation details
are vendor-specific. AMD's architecture manual provides its instruction
interface, and AMD advisory SB-7055 documents an RDSEED success-reporting
defect on affected Zen 5 systems. Feature bits and CF therefore indicate the
advertised/architectural operation, not trustworthiness of the source. QEMU
exposes, suppresses, or emulates CPU features under hypervisor control, and
the guest cannot distinguish that from bare metal using this code. The host
does not provide a supported external RNG device. Limine entropy is now
requested but remains untrusted under its best-effort contract.

QEMU 10.2.2 guest boot observations: default CPU (no `-cpu`) => unavailable;
`-cpu max` => initialization succeeds; `-cpu max,-rdrand` => unavailable;
`-cpu max,-rdseed` => initialization succeeds; `-cpu max,-rdrand,+rdseed` =>
unavailable. The final profile explicitly configures RDSEED while disabling
RDRAND, but the guest does not query RDSEED CPUID state. The kernel does not
fall back from missing RDRAND to RDSEED. No project QEMU invocation uses
`-cpu host`. These are instruction-path checks, not entropy-quality tests.
Recommendation: defer production security-sensitive
random use until an explicit platform trust policy and reviewed software DRBG
seed/reseed policy exist. This applies to TCP ISNs, cryptographic keys,
security tokens, and any future userspace random API. Live TCP remains
disabled.

### Phase 15.4L — TCP core runtime integration readiness

The isolated core now supports bounded tuple lookup without enabling runtime
dispatch. Active tuples match all four fields (local/remote IPv4 addresses
and ports); duplicate active tuples are rejected. Listener tuples use remote
port zero and a zero remote address, and can bind either one local address or
wildcard `0.0.0.0`. Overlapping listeners on the same port are rejected.
Lookup prefers one exact active tuple over a listener, returns an explicit
ambiguity status rather than making an arbitrary choice, and gives the caller
a retained generation-tagged handle to release. A tuple snapshot accessor
is valid only while the supplied handle remains valid.

The core remains externally serialized and has no runtime-owned table. Parsed
payload is borrowed only during `tcp_core_step()`; accepted bytes are copied
into the bounded per-connection receive queue. Actions describe future packet
work but own no frame or payload storage. Timer-service outputs also carry
temporary references that the caller must release. Current netstack ignores
IPv4 protocol 6 and the e1000 TX path is not connected to TCP actions.
Before runtime integration, OpenSillicon still needs an approved trusted
entropy policy and reviewed production ISN source, one serialized runtime
table/service owner, TCP IPv4 dispatch, action-to-frame construction, bounded
TX ownership, and lifecycle/shutdown handling. `tcp_core_live_isn_available()`
remains false; live TCP, production ISNs, TCP syscalls, and runtime packet or
timer dispatch remain disabled.

---

### Phase 15.4N — TCP runtime ownership and RX classification scaffolding

Implemented: one private, statically allocated eight-slot TCP core table is
initialized once after the existing UDP stack and cooperative network service.
Duplicate initialization is rejected. Shutdown enters a sticky stopping state
and refuses to complete while any table entry or external handle remains; it
does not force-release references. Reinitialization after stopping or
successful shutdown is unsupported so generations cannot be reset under stale
handles. The host lifecycle test fills the table, verifies the ninth
allocation fails without changing its output handle, frees/reuses a slot with
a new generation, tests stale-handle rejection and retained references, and
closes a modeled active entry before successful one-shot shutdown. There is no
production connection/listener creation path, so the runtime table remains
empty and no TCP service or timer call runs. Runtime lifecycle entry points
require external serialization; they do not claim SMP safety.

The shared netstack path now has bounded IPv4 protocol classification after
header, checksum, and packet-length validation. It recognizes ICMP, TCP, UDP,
and unsupported protocol values. Protocol 6 is returned as
`NETSTACK_TCP_DISABLED` only after IPv4 destination, fragment, TTL, and link
checks plus bounded TCP header/options/pseudoheader-checksum validation. A
truncated or malformed TCP segment is ignored. No TCP parse view, tuple
lookup, core step, connection allocation, response, or extra service event is
produced. UDP parsing and RX ownership remain on the existing
`udp_socket_poll()` path; payload is copied to a bounded four-datagram socket
queue before the RX netbuf is released, and no second e1000 poller was added.

Future TCP dispatch belongs at the validated protocol-6 branch in
`process_ipv4()`: parse/checksum the segment while the existing RX owner holds
the CORE-owned frame, look up the four-field tuple, step the core, create an
owned output frame, and hand it to the bounded e1000 TX path. Steps after
classification remain disabled. Timer work must be bounded and serialized in
the cooperative service outside the LAPIC ISR, with budget reserved even
under RX backlog and all returned timer handles released. Cooperative progress
still depends on syscall/idle safe points; a continuously runnable userspace
task without kernel entry can delay service. Live TCP remains blocked by the
fail-closed production ISN/entropy policy. No TCP syscalls, packet TX, runtime
dispatch, or timer integration are enabled.

The host-only synthetic checksum-valid SYN/SYN-ACK/ACK fixtures now exercise
the parser and deterministic core state transitions; they do not exercise
runtime dispatch or packet transmission. The RDRAND wrapper remains
fail-closed on unavailable/failing hardware; deterministic entropy injection
remains host-test-only, and neither RDRAND availability nor QEMU CPU profiles
enable production ISNs.

---

### Phase 15.4P — Isolated TCP handshake control plane

Implemented and host-tested only. The deterministic core supports active
SYN-SENT to ESTABLISHED from a valid SYN-ACK, passive LISTEN to
SYN-RECEIVED child to ESTABLISHED from a valid final ACK, and simultaneous
open through an active-origin SYN-RECEIVED transition. It checks the exact
four-field tuple and handshake sequence/acknowledgment values, permits
32-bit sequence wraparound, rejects payload before establishment, and limits
handshake flags to the modeled combinations. The listener stays available;
an exact duplicate SYN reissues the SYN-ACK action without allocating another
child. The four-child backlog and eight-slot table remain fixed.

Invalid SYN-ACK acknowledgments, unexpected flags, out-of-window RSTs,
checksum-invalid packets, stale handles, and tuple mismatches do not advance
the handshake. SYN control retransmission retains the existing 100-tick
initial RTO, exponential backoff capped at 800 ticks, five retransmission
actions, and bounded abort on exhaustion. Timer-service references remain
caller-owned and tests release them. Host-generated IPv4/TCP packet fixtures
validate IPv4 lengths/checksum and TCP pseudoheader checksums before testing
core transitions.

All initial sequence values are explicit host-test inputs. Test-only runtime
hooks remain excluded from production objects. `tcp_core_live_isn_available()`
is false; kernel RDRAND is not used as an ISN source. No live TCP packet
dispatch, TCP syscalls, TCP TX, runtime timer integration, or e1000 handshake
test is enabled. This is an experimental bounded control-plane model, not a
complete RFC TCP implementation. Trusted production entropy and a separately
reviewed ISN policy remain prerequisites.

### Phase 15.4Q — RFC 9293 state-machine audit

The source and host tests were compared with RFC 9293 Sections 3.3, 3.3.2,
3.4, 3.5.2–3.5.4, 3.9, and 3.10.7. The audit added wrap-safe serial
comparisons for established ACK processing, ignores stale ACKs without
blocking in-order receive data, requests an ACK for future ACKs, emits the
modeled reset action for unacceptable handshake ACKs, ignores unknown
well-formed TCP options, exercises simultaneous open, and restarts the
TIME-WAIT deadline on a duplicate FIN. Focused host tests cover wraparound,
ACK/sequence boundaries, duplicate handshake packets, options, reset behavior,
and TIME-WAIT restart.

This is not RFC 9293 conformance. The model does not trim overlapping data,
queue out-of-order segments, accept data on SYN, process the full flag/state
matrix, generate wire reset sequence/acknowledgment fields, emit RFC 5961
challenge ACKs, or provide full FIN/queue/application-notification semantics.
RST validation is stricter than the base receive-window rule in handshake
states; synchronized states accept any sequence inside the bounded window.
MSS/NOP/EOL are interpreted, unknown length-delimited options are skipped,
and other header features such as ECN/URG are unsupported. The timer is a
fixed bounded SYN/FIN control retry model, not an RTT estimator or general
retransmission engine. See the Phase 15.4Q matrix in DESIGN.md and
[RFC 9293](https://www.rfc-editor.org/rfc/rfc9293.html). Live TCP, production
ISNs, TCP syscalls, TCP TX, and runtime dispatch remain disabled.

### Phase 15.4R — Bounded TCP wire parser and serializer

Implemented as a standalone, host-tested wire module. `tcp_wire_parse()`
validates a raw IPv4 TCP payload against an explicit IPv4 pseudo-header and
returns borrowed header/option/payload views only after all bounds, option,
reserved-bit, and checksum checks pass. `tcp_wire_build()` serializes into
caller-owned output storage, validates options and capacity, writes a zeroed
reserved field and data offset, and computes the TCP checksum. The supported
segment bound is 1480 bytes (IPv4 MTU 1500 less the minimum IPv4 header).

MSS, Window Scale, SACK-Permitted, SACK block layout, and Timestamp layouts
are parsed with fixed length checks; unknown well-formed options are skipped.
Option metadata does not activate window scaling, SACK recovery, timestamp
RTT measurement, or ECN. Tests include independently calculated even/odd
checksum vectors, SYN/SYN-ACK and payload round trips, every truncation length
below the base header, malformed options, reserved bits, checksum corruption,
maximum length, and deterministic byte mutations. The prior core parser and
builder now delegate to the wire module. Production IPv4 still only validates
TCP and returns the disabled result; no TCP connection lookup, state step,
response, TX path, syscall, timer integration, DMA, or production ISN is
enabled. This module is packet-format infrastructure, not TCP support.

### Phase 15.4S — TCP IPv4 receive validation only

The shared IPv4 receive path now invokes `tcp_wire_parse()` only after the
existing IPv4 and Ethernet/source/destination checks. It passes the exact TCP
payload slice selected by IPv4 total length, excluding the IPv4 header and any
Ethernet padding. Results distinguish valid-but-disabled TCP from malformed
header, checksum, and option input. Parsed pointers are borrowed for the
synchronous call only; the existing single RX owner releases the packet after
classification. Host netstack tests cover valid SYN/ACK/data/options, unknown
well-formed options, malformed headers/options/checksums, IPv4 validation
gates, and the maximum segment bound. A host-only linker wrapper asserts that
IPv4 checksum/MTU failures do not invoke the TCP parser. The UDP receive-owner
test injects valid, malformed, and unsupported-protocol frames, confirms their
netbufs return to the pool, and verifies they never enter UDP queues or add
network-service work.

This remains validation and discard only: no tuple lookup, TCP core call,
connection, response, transmission, TCP timer, syscall, or live networking
is enabled. Production ISNs remain unavailable and no RFC 9293 conformance is
claimed.

### Phase 15.4T — TCP runtime connection ownership scaffold

The runtime owns a private fixed table with the existing capacity of eight;
listeners and modeled connections share those slots. Listener registration
accepts a specific local IPv4 address or wildcard address, rejects port zero
and conflicting same-port listeners, and transitions a new core entry to
LISTEN without generating an ISN or packet. Lookup gives an exact four-field
connection tuple precedence over a listener, applies specific/wildcard
listener rules, and returns explicit no-match, ambiguity, disabled, stopping,
invalid, or reference-exhaustion outcomes. A successful result owns one
generation-tagged handle reference, released explicitly by the caller.

Runtime operations use local interrupt masking for serialization on the
single-CPU kernel; this is not SMP-safe and is not an interrupt-context API.
Initialization is one-shot. Shutdown is sticky: it rejects new routing and
listener creation, refuses completion while entries/references remain, and
does not invalidate handles. Reinitialization after shutdown is unsupported.

The optional `OPEN_SILLICON_ENABLE_TCP_ROUTING` compile gate is not present in
the normal kernel build. A separate host build tests routing and the netstack
boundary. Normal production IPv4 processing still validates TCP wire format
and discards it without tuple lookup, listener lookup, connection creation,
core events, payload retention, queueing, timers, or response. The gated
boundary also only looks up and retains a handle; payload remains unsupported
and no core event is submitted. There is no TCP service integration, TX path,
syscall, or production ISN source. This is ownership and routing scaffolding,
not working TCP connections or RFC 9293 conformance.

### Phase 15.4U — Isolated passive-open runtime/core integration

Host-test-only ingress connects runtime tuple routing to the core's existing
passive listener `TCP_EVENT_SEGMENT` behavior. A valid SYN creates one
SYN-RECEIVED child with the exact local/remote IPv4 and port tuple; the core
records the listener generation and a fixed runtime sidecar tracks the child
by slot and generation. Listeners and children share the existing eight
table slots, and the four-child SYN backlog is enforced. Backlog or table
exhaustion returns a bounded resource result without registering a partial
child.

Duplicate SYNs route to the same child and return abstract retransmit actions
without resetting its timer. A final ACK is routed to that exact child and
only the core validates its sequence and acknowledgment before entering
ESTABLISHED. Unsupported flags and payload are rejected. RST cleanup removes
the runtime mapping and drops its owned reference; caller references keep a
closed slot alive, and stale generation handles cannot access reused slots.
Listener close is refused while child mappings remain. The reference created
by the core is runtime-owned; returned handles are independent caller refs.

This ingress and deterministic ISN injection are compiled only for host tests
with `TCP_RUNTIME_TESTING`/`TCP_CORE_TESTING`; no production API accepts an
ISN seed. Normal kernel IPv4 continues to validate TCP and discard it, with
the optional routing gate absent from normal flags. Core timer state may be
armed. Abstract SYN/ACK actions are never built or transmitted. Production
ISNs, TCP TX, TCP syscalls, and production TCP dispatch remain disabled; this
is not live TCP or RFC 9293 conformance.

### Phase 15.4V — TCP runtime timer ownership

Implemented as a bounded runtime API, not a runtime hook. It delegates to
`tcp_core_service()` with the existing 100 Hz `timer_ticks()` time domain and
accepts budgets of zero through four events. Results expose scalar state and
actions only. The runtime validates each returned generation-tagged handle
and releases the core service reference exactly once before returning. Core
`more_due` is propagated so a future safe caller can schedule another bounded
pass; no unbounded timer drain or allocation is performed.

Timer expiration is not automatically treated as closure: retry events may
re-arm their timer, while retry exhaustion or TIME-WAIT expiry may close table
ownership. The temporary service reference protects a closed connection while
the runtime copies its result. External references remain valid. In the
host-only passive-child model, a closed child's runtime-owned link reference
is dropped before clearing its parent relationship; listener close remains
refused while that link exists. Failed validation/release returns an error
without force-destroying handles. STOPPING and STOPPED reject timer service;
shutdown refuses while service is marked active. Local interrupt masking
serializes runtime operations only on the current single CPU and is not SMP
safety.

Host tests use explicit ticks to cover no-due, bounded batches, `more_due`,
no duplicate processing, retry exhaustion with a retained external handle,
child timeout/link cleanup, busy/shutdown behavior, and calls before/after the
runtime lifecycle. Timer service remains unwired from LAPIC interrupts,
`network_service_run()`, and production boot. No production TCP routing,
packet TX, syscall, or ISN generation is enabled; future integration still
requires an approved entropy policy and a safe deferred caller.

### Phase 15.4W — TCP Runtime active-open lifecycle

Implemented in host tests only. Runtime test API creates a bounded active
tuple entry with an explicitly supplied deterministic ISN, submits Core's
active-open event, and returns the retained handle only after Core confirms
SYN-SENT and SEND_SYN. Duplicate tuples and full-table requests fail without
leaking a handle. A second host-only operation parses raw TCP payload bytes,
validates the pseudoheader checksum, checks the exact four-field tuple, and
submits the segment to Core. The final ACK is reported as a scalar abstract
action and is never sent.

Core remains the only owner of handshake validation and state transitions.
Bad ACKs, unsupported flags, malformed/checksum-invalid packets, stale
handles, and any address/port mismatch do not establish a connection. An
arbitrary first peer SYN-ACK sequence is valid as the peer's initial sequence;
there is no prior peer sequence against which to call it unexpected. A valid
RST follows the existing Core ACK rules and can close table ownership without
invalidating a caller-held handle. Runtime timer service observes SYN-SENT,
releases its own returned reference, and bounded retry exhaustion closes the
connection; externally retained references prevent slot reuse until released.

The deterministic sequence argument and both active-open operations exist
only in host builds with `TCP_RUNTIME_TESTING` and `TCP_CORE_TESTING`. The
normal kernel still excludes `OPEN_SILLICON_ENABLE_TCP_ROUTING` and both test
macros, validates/discards incoming TCP, and does not create active entries,
transmit TCP, add syscalls, generate production ISNs, or schedule TCP timers.
This milestone is a lifecycle test harness, not live TCP or a claim of RFC
9293 conformance. Entropy trust, runtime RX dispatch, packet serialization,
and owned TX handoff remain future prerequisites.

---

### Phase 15.4X — TCP Runtime lifecycle unification

Active and passive connections use the same eight Core table slots and the
same terminal rule: Core state `CLOSED` drops table ownership; Runtime then
performs idempotent passive relationship cleanup. Active caller handles,
passive caller handles, child-link references, parent-listener references, and
bounded timer-service references have explicit independent owners. A slot is
reusable only after all references are released, and reuse advances its
generation. Listener close is refused while any child link remains. Backlog
counts linked SYN-RECEIVED children, while established children continue to
occupy table capacity until terminal cleanup.

Host lifecycle coverage exercises both connection types, shared capacity and
exhaustion, supported FIN/close transitions, RST and retry/TIME-WAIT cleanup,
duplicate FIN, retained handles, parent unlinking, and stale-handle rejection
after reuse. This does not add a data plane: production TCP routing,
transmission, syscalls, ISN generation, and timer-service hooks remain
disabled. Runtime/Core serialization is single-CPU local-interrupt masking,
not SMP synchronization.

---

### Phase 15.4Y — Host-only TCP Runtime local close

`tcp_runtime_test_local_close()` validates a generation-tagged handle and
submits the existing Core application-close event under Runtime serialization.
It returns scalar state/actions/sequence/timer observations only. From
ESTABLISHED it reports FIN-WAIT-1; from CLOSE-WAIT it reports LAST-ACK. Both
paths emit abstract SEND_FIN|SEND_ACK, consume one sequence number, and arm
the existing bounded retransmission timer. A caller may release its handle
only when finished; close does not invalidate it.

Core's existing SYN-SENT and SYN-RECEIVED close behavior is preserved as an
abort that reports SEND_RST|ABORT and reaches CLOSED; a passive child is
unlinked through the unified cleanup path. LISTEN remains on its dedicated
child-aware close API. Repeated close in FIN-WAIT-1/2, CLOSING, LAST-ACK,
TIME-WAIT, or CLOSED returns unsupported without changing state or repeating
FIN. Core alone validates FIN acknowledgments and performs FIN-WAIT,
CLOSE-WAIT, LAST-ACK, CLOSING, TIME-WAIT, retry, and terminal transitions.
The test API can drain handles while Runtime is STOPPING, but returns disabled
before initialization and after completed shutdown. It reports BUSY during
timer service. This API is compiled only for host tests; production TCP
routing, packets, syscalls, ISNs, and timer hooks remain disabled.

### Phase 15.4Z — TCP Core/Runtime close-state audit

The close-event audit confirms Core's behavior by state: LISTEN closes without
an action; SYN-SENT and SYN-RECEIVED abort with abstract RST and ABORT;
ESTABLISHED enters FIN-WAIT-1; CLOSE-WAIT enters LAST-ACK; FIN-WAIT-1/2,
CLOSING, and LAST-ACK treat repeated close as a no-op; TIME-WAIT and CLOSED
also remain unchanged. The two graceful paths emit FIN and ACK, advance
`snd_nxt` once, and arm the bounded control timer. The host Runtime close
wrapper rejects repeated/unsupported states without calling Core. Core remains
the authority for ACK, FIN, RST, retry-exhaustion, and TIME-WAIT transitions.

Runtime test results expose a scalar projection of Core outcomes rather than
embedding Core results with internal child handles. Passive event results
preserve Core actions and state even when Runtime reports a non-establishing
outcome. Terminal cleanup is keyed only to Core CLOSED: Core drops table
ownership, Runtime unlinks a matching passive child relationship once, timer
service releases its retained result reference once, and external handles
remain valid until explicitly released. The shared capacity stays eight;
generation checks prevent stale handles or links from referring to reused
slots. Repeated-close tests assert unchanged actions, sequence values, and
timer state across supported no-op states. These are isolated host tests only;
production TCP dispatch, TX, ISNs, syscalls, and timer hooks remain disabled.

### Phase 15.4AB — TCP Runtime API ownership contract

The Runtime header now documents preconditions, lifecycle availability, output
ownership, and release duties for each entry point. Runtime handles are
slot/generation tokens with Runtime ownership semantics, even though their
representation is shared with Core. Core table ownership, caller references,
passive child-link and parent references, and Runtime-consumed timer-service
references are distinct; references survive CLOSED until released, and a slot
cannot be reused before reclamation. Timer results are scalar snapshots and
never transfer a handle to the caller.

Passive result status classifies Runtime handling separately from the scalar
Core outcome. On initial SYN, Core outcome describes the listener while the
Runtime handle identifies the child. Test-only active/passive/close APIs and
deterministic ISNs remain host-only. Production has no TCP event dispatch,
transmission, syscalls, ISN generation, or timer hook. Runtime serialization
uses local interrupt masking on one CPU and is not SMP synchronization.

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

### Phase 15.5A — Preparation for a future official package repository client

This phase audits the existing network and user-space boundaries; it does
not implement a downloader or package manager. The only demonstrated
guest-facing transport is bounded project-native IPv4 UDP over the explicit
QEMU `8086:100e` e1000 profile. The current static guest/peer test uses
`10.15.3.1/24` and `10.15.3.2`. UDP payloads are limited to 512 bytes, with
eight socket slots and four queued datagrams per socket. Syscalls 28–31
provide socket creation, bind, sendto, and recvfrom through reference-counted
FD objects; calls copy user buffers and endpoint values. Receive and ARP
resolution poll for at most 500 ticks (five seconds at 100 Hz), and receive
returns a whole datagram or an error. These semantics are neither a stream
transport nor a POSIX/BSD socket ABI.

The cooperative service is bounded (eight queued poll events, four events per
call, at most one RX frame for each poll request) and runs at syscall and
blocked-process idle safe points, not in the LAPIC ISR. A process that stays
in userspace without syscalls can delay network service indefinitely. Static
IPv4 is configured; DHCP, DNS, gateway routing, HTTP, HTTPS/TLS, and production
TCP are absent. TCP wire/Core/Runtime code is not connected as a reliable
production transport. Existing UDP must not be repurposed as an unreliable
package-download substitute.

Future boundary: a user-space network client obtains bytes through a future
bounded reliable transport API. A separate repository verifier authenticates
signed metadata using a trust root provisioned independently of network data,
checks freshness/version policy, and verifies package hashes and authenticity
before a separate privileged installer writes system files. The kernel
remains responsible only for packet transport, bounded resources, user-memory
validation/copying, and transport errors; it must not parse package metadata,
make authenticity decisions, or install packages. The initial repository
policy is official OpenSillicon only, with no third-party repositories.
Metadata signatures remain mandatory even if future HTTPS is added. Key
rotation, revocation, and rollback prevention need a reviewed design.

Prerequisites before a usable package downloader include production reliable
transport with bounded connect/read/write timeouts, retry and cancellation,
partial stream I/O, backpressure, close/reset/EOF reporting, bounded buffers,
and process-exit cleanup; bounded DNS and HTTP framing; reviewed TLS if HTTPS
is selected; reviewed signature/hash primitives and independent trust-root
provisioning; freshness and key lifecycle rules; and a kernel privilege model
for system-wide installation. Current VFS mode metadata is not enforced as an
authorization boundary, and current RDRAND availability is not an approved
signature, key, token, or ISN policy. No `.ospkg` format, package parser,
repository client, cryptographic verifier, installer, or package-specific
kernel API was added in this phase. Validation reuses the existing network,
process, kernel, and ELF tests; no package-download behavior is claimed.

### Phase 15.5B — Userspace network diagnostics

`/bin/netdiag` is built and installed through the existing coreutils list and
SilliconFS root-population path; the shell launches it as an ordinary external
program. The supported forms are `netdiag help`, `netdiag capabilities`, and
`netdiag udp <IPv4> <port>`. Parsing rejects malformed/non-unicast IPv4
destinations, non-decimal or out-of-range ports, and malformed argument
counts before any syscall. `capabilities` does not pretend to query the
kernel: it labels `10.15.3.1/24` and peer `10.15.3.2` as the documented QEMU
test profile because no userspace configuration-query API exists.

`udp` creates and binds a UDP FD to wildcard local port 45000, sends
`OS-UDP-REQ-15.3B`, then verifies the single received datagram's source
address/port and exact `OS-UDP-REPLY-15.3B` payload. It closes the FD on every
post-creation path and reports operation-specific failures. recvfrom already
enforces a bounded 500-tick (five second at 100 Hz) timeout. The utility does
not add a receive loop, but the existing syscall implements that deadline by
bounded polling rather than an event wait; no cancellation API is available.
The host argument test covers IPv4, port, and command forms. The dedicated
`e1000-netdiag-qemu-test` runs the diagnostic as a guest Ring 3 program through
the existing syscalls and peer harness, while the unchanged
`e1000-udp-qemu-test` remains the socket regression baseline. Successful
diagnostics verify one UDP exchange only; they do not validate TCP, DNS,
HTTP(S), TLS, Internet connectivity, or package downloads. No production TCP,
new syscall, package-manager code, or network configuration API was added.

### Phase 15.5C — Userspace ICMP Echo (`ping`)

ICMP Echo was kernel-only before this phase: the stack could form Echo Replies
and validate Echo Replies, but had no userspace request/reply interface. The
new syscall 33 accepts one fixed-size, copied request with a numeric on-link
IPv4 destination and up to 32 payload bytes. It is Echo-specific; it does not
expose raw packets, DMA buffers, or arbitrary IPv4 protocols. It has no FD or
persistent handle, allows one outstanding Echo transaction, and returns
`EAGAIN` for a second concurrent request. It uses the existing ARP and unique
`udp_socket_poll()` RX ownership path. Kernel validation matches reply source,
type/code, identifier, sequence, exact payload, and checksums before returning
the source and elapsed ticks. A matching ICMP Destination Unreachable quotation
returns `EHOSTUNREACH`; malformed or unrelated packets are ignored until the
500-tick deadline returns `ETIMEDOUT`. The cooperative single-CPU model remains
in effect.

`/bin/ping` is installed with coreutils and supports `ping help`,
`ping <IPv4>`, and `ping <IPv4> <count>` with count 1–4. It uses numeric IPv4
only. The QEMU peer test validates request checksums and fields, sends an actual
Echo Reply, and checks the Ring 3 command's result. Success demonstrates only
an on-link ICMP Echo exchange in that QEMU profile; it does not imply Internet
reachability or any TCP/DNS/HTTP/TLS support. No TCP routing, TCP TX, TCP
syscall, production ISN, or package-manager feature is enabled.

### Phase 15.5D — Isolated TCP reliability foundation

The isolated Core provides fixed 2048-byte send and receive rings, strict
in-order receive, MSS/window-bounded single-segment sending, partial-ACK
reclamation, zero-window queuing/reopening, and bounded retransmission through
the Core timer model. Phase 15.5E adds host-only Runtime write/read operations
and caller-buffer TCP segment serialization. The deterministic simulated peer
test covers handshake, data, ACK, retransmission, read, zero-window reopening,
and close. A successful serializer result means a checksummed segment was
constructed, not that a device accepted or transmitted it. RST response
serialization remains unsupported because required triggering-packet context
is not retained.

Writes are all-or-nothing and bounded by the 2048-byte send ring; reads are
nonblocking and partial from the 2048-byte receive ring. FIN is refused while
queued application bytes remain. Empty read distinguishes WOULD_BLOCK, peer
FIN EOF after drain, and reset. Runtime copies all caller data and retains no
RX or caller buffer pointers. Runtime timer results feed bounded retransmit
actions to the isolated serializer. Production RX still validates/discards
TCP, and production TX, syscall, and timer hooks remain absent. Production
ISNs remain unavailable. Out-of-order reassembly, fast retransmit, congestion
control, robust RST response construction, and reviewed production entropy
remain prerequisites; this is not a complete or live TCP transport.

### Phase 15.5F — deterministic two-endpoint validation

Host-only `make tcp-runtime-sim-test` switches between two fixed, independent
Runtime/Core contexts and exchanges copied, checksummed serialized TCP segments
through an eight-entry, 1480-byte-bounded deterministic packet queue. The
queue supports immediate/delayed delivery, drop, identical duplication, and
selected-order delivery. Coverage includes lossy handshake, data, ACK and FIN
recovery; duplicates; delayed and reordered segments; in-order rejection of a
future segment; bidirectional stream transfer; zero-window backpressure and
reopening; retry exhaustion; and 10,000 mixed lifecycle/slot-reuse iterations,
including 78 injected dropped-SYN retry-exhaustion cases and prior-generation
ACK delivery after reuse.
This is serialized host simulation, not concurrent or SMP execution. It does
not add production TCP dispatch, TX, syscalls, or timer hooks and does not
claim out-of-order reassembly or RFC 9293 conformance.

### Phase 15.5G — parser and Runtime hardening

The TCP Wire parser now clears the caller's parsed view on every failed parse,
including failures before packet fields are read, so stale borrowed pointers
cannot survive an error. Its host regression harness runs 100,000 seeded
arbitrary parser inputs (seed `0x15c5f00d`), systematic truncation and
structured mutations, and checks output guards, input immutability, mandatory
success invariants, and cleared failure results. This is deterministic
regression coverage, not a universal memory-safety proof. The simulator's
bounded queue-full behavior is checked and its lifecycle/reuse loop runs
10,000 iterations. Clang AddressSanitizer and UndefinedBehaviorSanitizer runs
passed; GCC sanitizer linking was unavailable because the matching runtimes
are absent. Production TCP remains disabled.

### Phase 15.5H — RFC 9293 correctness/security audit

The scoped audit corrected synchronized-state RST acceptance (exact
`RCV.NXT` closes, in-window nonexact RST produces only an abstract challenge
ACK, outside-window RST is ignored), unexpected SYN handling, zero-length
receive-window sequence acceptability, and stale advertised-window updates
using `SND.WL1/SND.WL2`. The Wire parser preserves nonzero reserved bits for
diagnostics rather than rejecting them; unsupported bits gain no semantics.
Regression tests cover these paths and ACK wraparound/partial retirement.

This remains a partial host-only transport: strict in-order receive, no
partial-overlap reassembly, congestion control, RTT estimator, SACK/window
scale/timestamp negotiation, fixed retry/TIME-WAIT policies, and no production
TCP dispatch, TX, syscall, timer hook, or ISN generation. This audit does not
claim RFC 9293 conformance.

### Phase 15.5I — isolated Linux TCP interoperability

`make tcp-linux-interop-test` runs the host-only TCP Wire/Core/Runtime packet
endpoint against the Linux kernel TCP implementation in a disposable
`unshare -Urn` user/network namespace with a veth pair. Both active and
passive handshakes, multi-segment bidirectional byte streams, advertised
MSS/window exchange, and orderly close completed. The bridge validates each
IPv4/TCP checksum independently before Core processing and records structured
packet traces. Adversarial malformed/checksum/ACK/RST/SYN/data cases are
injected through the Runtime Wire parser and preserve unrelated state.

This does not establish broad Linux interoperability or RFC conformance.
Loss against Linux, reset/challenge response serialization, zero-window and
partial-read interoperability, and several close edge cases remain untested.
The harness is host-test-only; production routing, TCP TX, syscalls, timers,
and production ISNs remain disabled.

### Phase 15.5J — production readiness assessment (planning only)

The normal kernel links the experimental TCP Wire/Core/Runtime code and
initializes an inert eight-entry Runtime owner when the UDP network stack is
available. Production RX still validates the IPv4 packet and TCP Wire segment,
then discards protocol 6. The cooperative network service polls UDP only; the
LAPIC timer ISR increments ticks and wakes/preempts processes, but does not
service TCP timers. Runtime routing/timer APIs are not called from those
production paths, and no TCP response is passed to the device TX API. The
normal build omits `OPEN_SILLICON_ENABLE_TCP_ROUTING`, `TCP_CORE_TESTING`, and
`TCP_RUNTIME_TESTING`; deterministic ISN injection and the simulator remain
host-test-only. `tcp_core_live_isn_available()` remains false.

Current host-tested implementation is bounded: eight Core/Runtime table
entries shared by listeners and connections, passive backlog four, 2 KiB
send and receive rings per entry, max TCP segment 1480 bytes, local MSS 1460,
Core timer service budget four, initial retransmission interval 100 ticks,
maximum 800 ticks, five retries, and fixed TIME-WAIT 12,000 ticks. The model
has wrap-safe sequence arithmetic, bounded ACK retirement, strict in-order
receive, selected reset/close behavior, and serialized host tests. It lacks
out-of-order reassembly, congestion control, RTT estimation, active option
negotiation beyond MSS, production RX/TX connection, production timer hookup,
and any TCP userspace API. Linux namespace tests establish narrow handshake,
multi-segment data, and orderly close evidence, not production readiness.

The production ISN blocker is not just an API switch: the random module exposes
bounded RDRAND words when CPUID advertises the instruction, but the kernel has
no reviewed CSPRNG, approved platform entropy-source trust policy, or wired
boot entropy contract. RDRAND availability is not accepted as sufficient
trust by this project. Keep production opens fail-closed. Before enabling
ISNs, approve trusted seed sources and failure/reseed policy, use a reviewed
CSPRNG, then use a keyed tuple/time ISN construction consistent with RFC 9293
§3.4.1 (which incorporates the RFC 6528 design goals).

The existing device boundary is potentially reusable but not yet connected:
e1000 RX copies completed DMA data into a CORE-owned netbuf before descriptor
reuse; transmit copies a CORE netbuf into a bounded TX DMA slot and transfers
ownership to DEVICE until completion. The configured QEMU 8086:100e profile
uses identity DMA and does not prove generic hardware support. TCP needs
explicit IPv4/Ethernet response construction, ARP next-hop resolution,
queue-full and completion policy, and proof of exactly-once packet/reference
release. TCP payload must be copied into Core storage before the RX netbuf is
released. TCP timer work must run in bounded cooperative context, honor
`more_due`, reject stale generations, and never execute from the timer ISR.

Recommended first API: a project-native, file-descriptor-backed TCP object
with dedicated socket operations. Existing `fd_table_clone()` shares
`fd_object`s by reference across fork and final put invokes the object's close
callback; define whether fork shares one connection, make listener/child
ownership explicit, and clean up on process exit. Add explicit bind/listen/
accept/connect/send/receive/shutdown/close operations without changing UDP
syscalls 28–31 or ICMP Echo 33. Use validated copy-in/copy-out buffers,
bounded Core rings, partial IO and distinct EOF/reset/timeout errors. Blocking
needs scheduler wait queues, cancellation, timeout semantics, and exit-safe
unregistration; busy polling inside a syscall is not the target design. This
is not a POSIX compatibility promise.

#### Proposed 15.6 dependency sequence

| Phase | Work and likely areas | Exit criteria |
| --- | --- | --- |
| 15.6A — entropy and ISN readiness | Trust review and boot entropy policy; reviewed CSPRNG/reseed/fail-closed behavior; `kernel/random.*`, boot interface, Core provider. | Security review approves source/trust model; unavailable/failed source blocks active/passive opens; production/test symbol separation verified. |
| 15.6B — production timer service | Bounded Runtime servicing from cooperative network work; generation-tagged due events, retransmit/TIME-WAIT action scheduling. | Deterministic due/more_due, retry, stale event, close/timeout tests; ISR only updates tick/scheduler state; UDP progress preserved. |
| 15.6C — production RX/TX integration | Route validated protocol 6 after IPv4 checks; copy accepted bytes before RX release; bounded TCP+IPv4+Ethernet output to net_device. | Checksum/length/fragment/tuple tests, packet ownership and TX completion/failure tests, saturation and device-down handling, UDP/ICMP regressions. |
| 15.6D — descriptor API | FD-backed TCP socket operations, user-copy contract, fork/exit cleanup, blocking/cancel/timeout semantics. | ABI and user-memory fault tests; shared descriptor semantics, references, close/accept ownership, partial IO and error distinction validated. |
| 15.6E — production-path interoperability | Independent Linux peer through production QEMU packet path, in addition to host namespace harness. | Handshake/data/close/loss/window/reset/TIME-WAIT and process lifecycle verified; exact packets and bytes checked; QEMU and host tests reproducible. |
| 15.6F — hardening / controlled enablement | Resolve scoped protocol/security gaps; explicit unsupported-feature policy and device support scope. | Every production gate passes; enablement is an explicit later change. Failure leaves current validation-and-discard path intact. |

#### Production enablement gates

Do not enable production routing until all gates pass: (1) reviewed trusted
entropy and secure ISN source; (2) test-only hooks absent from kernel objects
and ELF; (3) bounded connection/backlog/buffer allocation and atomic failure;
(4) production bounded timers with generation-safe stale-event handling;
(5) RX buffer lifetime and exactly-once release; (6) TX packet/DMA ownership,
completion and failure behavior; (7) IPv4/TCP checksums, lengths, fragmentation
policy and tuple isolation; (8) descriptor, fork, process-exit and cancellation
ownership; (9) retransmission, ACK/window, RST/FIN, timeout and TIME-WAIT tests;
(10) independent Linux interoperability under loss, duplicates, delay,
partial IO, zero-window and close/reset cases; (11) QEMU tests through actual
production RX/TX and only supported e1000 profile claims; (12) fuzz, sanitizer
and lifecycle stress with documented seeds/counts; (13) UDP and ICMP ABI and
behavior regressions; and (14) documented RFC deviations with no unsupported
claim of RFC 9293 conformance. Build success alone is not a readiness gate.

No production functionality was enabled in 15.5J. See the readiness assessment
in `DESIGN.md` for the component diagram, capability matrix, ownership detail,
and entropy/timer/RX/TX rationale.

### Phase 15.6A — trusted entropy and TCP ISN readiness (blocked)

Audit found only the legacy raw RDRAND interface. It checks CPUID, probes with
at most ten attempts, returns bounded raw words, and fails closed on instruction
failure. It does not implement an entropy pool, software CSPRNG/DRBG, reseeding,
or a trusted-source policy. At that audit point, boot requested no entropy
from Limine; ACPI is used for platform topology, and no RDSEED, TPM RNG, or
virtio-rng path was found.
The source is queried early, before timer/scheduler/network/userspace, but
early availability is not evidence of trust. Timestamps, counters, addresses,
test seeds, network timing, and user input are not production substitutes.

No CSPRNG or ISN provider was implemented because the trusted-source gate is
not met. The legacy `kernel_random_available()` means only that the RDRAND
probe succeeded; the API/header and boot log now say it is not approved for
keys or TCP ISNs. Deterministic random/ISN injection remains test-macro-only.
`tcp_core_live_isn_available()` remains false and the production TCP boundary
is unchanged. The test build's deterministic random source is not evidence of
entropy quality or CSPRNG correctness.

The remaining 15.6A entry criteria are: approve an entropy source for each
supported platform; specify seed trust and failure policy; select a reviewed
DRBG construction rather than treating a primitive (including ChaCha20 alone)
as a DRBG; define bounded state, reseed limits, counter exhaustion, zeroization
and single-CPU concurrency; add published known-answer and failure tests; then
implement a tuple/time keyed ISN PRF only after that provider is cryptographically
ready. Test ISNs must remain separate, production must fail closed, and no
entropy-bit claim may be made without a defensible source analysis. QEMU's
RDRAND behavior is not physical-hardware entropy evidence.

In this phase's four e1000 QEMU profiles, boot logs reported RDRAND
unavailable, so they verified fail-closed behavior only. No physical hardware
was tested.

Phase 15.6A is blocked until an approved trusted entropy source and CSPRNG
design are available. Do not start Phase 15.6B or enable production ISNs,
TCP dispatch, packet transmission, timers, or syscalls on the basis of the
existing RDRAND probe.

### Phase 15.6A.1 — boot-time entropy architecture and source evaluation

The pinned Limine release is v12.9.6 and the bundled protocol header is
revision commit `6e85ddd6f928b5b512015c4d47d1cfce2fd28079`. Its revision-0
Entropy feature is requested for four 64-bit values. The response is optional;
the bootloader may leave the response null when unsupported or unavailable.
The kernel validates revision, count, null/count consistency, HHDM conversion,
overflow-safe ranges, and that the response and values reside in
bootloader-reclaimable memory. It copies no more than four values, then clears
the kernel temporary immediately. Memory-map traversal is capped at 512
entries. Invalid or missing data is ignored and does not block boot. The
kernel retains no pointer into bootloader-reclaimable entropy memory.

Limine explicitly describes the source as best-effort, allows a timing-seeded
PRNG fallback, and gives no hardware-grade entropy guarantee. The normal QEMU
UEFI boot with the pinned Limine binary returned a non-empty response and the
kernel logged that it inspected and discarded untrusted values. This confirms
the request path for that QEMU setup, not entropy quality. No physical
hardware test was performed. No entropy bits are credited and no values are
passed to the legacy RDRAND API or any TCP code.

Source status: RDRAND remains a CPUID-gated, bounded, carry-checked raw CPU
DRBG interface, not a trusted pool or software CSPRNG; RDSEED is not queried
or executed. QEMU 10.2.2 exposes `virtio-rng-pci`; a configuration using
`rng-random` backed by `/dev/urandom` parsed and instantiated, but the kernel
has no virtio transport or RNG driver. EFI64 is the current Limine handoff,
but Limine exits EFI boot services before kernel entry, so EFI boot-services
protocols such as EFI_RNG_PROTOCOL cannot be called. No TPM RNG driver or
ACPI RNG-device driver exists; a TPM event log is not itself an RNG. Time,
TSC values, memory layout, addresses, IDs, traffic, and test seeds are never
trusted entropy sources.

The future acquisition boundary is provider-specific bounded acquisition,
response/range validation, source classification, fixed kernel-owned
staging, then a separately reviewed CSPRNG initialization. This phase adds
only Limine response inspection; it does not add an entropy manager state
machine, source mixing, a trusted pool, CSPRNG, reseeding, or an ISN generator.
Missing/partial/invalid provider output must never block boot indefinitely or
be promoted to trusted input without source-specific policy. The production
ISN and TCP gates remain closed.

### Phase 15.6A.2 — QEMU VirtIO RNG provider

The kernel now contains a bounded driver for QEMU transitional VirtIO RNG
PCI (`1af4:1005`) using only legacy PCI I/O and queue 0. Runtime probing is
behind `OPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG`, absent from ordinary kernel
flags and enabled by the dedicated QEMU present/absent profiles. The
QEMU-specific PCI helper sizes BAR0 with decode and bus mastering disabled,
then uses an existing firmware I/O assignment or `0xc000` for an unassigned
BAR. It leaves bus mastering disabled until DMA buffers and the queue are
ready. This fixed-range helper is not a general PCI allocator.

The queue is physically contiguous, page aligned, 32-bit-addressable, and
bounded to device queue sizes up to 256; one writable descriptor and one
256-byte data buffer support one request at a time. Completion polling is
capped at one million iterations. Timeout or malformed completion marks the
provider unavailable and attempts reset plus verified bus-master disable;
DMA remains retained if quiescence cannot be proven. The code relies on the
existing identity-DMA boot contract and coherent x86 memory behavior. Modern
VirtIO PCI, MMIO transport, interrupts, IOMMU translation, generic hardware,
and physical-device support remain out of scope.

Host fake-transport tests cover setup, feature negotiation, descriptor and
ring ownership, short completions, malformed completions, timeout, reset
failure quarantine, and shutdown. The QEMU present profile completed a
256-byte request; the absent profile verifies nonfatal absence. QEMU uses
`rng-random` backed by the host `/dev/urandom`, so bytes remain host- and
hypervisor-provided candidate data. They are not credited as entropy, fed to
a CSPRNG, or used for ISNs. No CSPRNG, production ISN generation, or
production TCP functionality is enabled.

### Phase 15.6A.3 — unified kernel entropy manager

Implemented `kernel_entropy_*` as an allocation-free, bounded source inventory.
It preserves source identity for Limine bootloader candidates, RDRAND raw
output, and the explicitly gated QEMU legacy VirtIO RNG provider. RDSEED is
not implemented. The manager copies validated Limine values into a bounded
kernel-owned staging buffer, reports per-source lifecycle/failure status, and
requires callers to select a source; there is no fallback or anonymous
combined stream. Output remains unchanged on failure, and result metadata
reports the selected source and exact byte count.

No source is promoted to trusted. Limine remains best-effort, RDRAND remains
raw CPU DRBG output, and VirtIO remains host/hypervisor-provided candidate
data. Temporary failures require explicit provider-status recheck; permanent
failures remain latched. `kernel_entropy_cryptographic_ready()` is always
false. There is still no entropy credit, CSPRNG, reseed policy, TCP ISN key,
or production ISN generation. The normal build does not probe or read VirtIO;
only the dedicated QEMU test profile finalizes that source's availability and
reads its bounded sample through the manager. Tests cover source selection,
provenance, bounds, failure atomicity, exhaustion, retry, and trust remaining
disabled. QEMU RDRAND+RDSEED, RDRAND-only, and neither-instruction profiles
all completed the VirtIO request; this is virtual CPU/device evidence only.

### Phase 15.6B — CSPRNG architecture and security design (design only)

`docs/security/csprng-design.md` records a source-backed design review. It
compares the NIST HMAC_DRBG/SHA-256 and AES-256 CTR_DRBG constructions with a
ChaCha20-based design, recommends HMAC_DRBG conditionally for a later
implementation, and defines a fail-closed lifecycle, bounded API proposal,
test plan, threat model, and prerequisites. This recommendation does not
approve any current seed source: Limine remains best-effort, RDRAND remains
raw hardware output, VirtIO RNG remains host/hypervisor-provided candidate
data, and RDSEED is not implemented. The entropy manager's cryptographic
readiness remains false, as does `tcp_core_live_isn_available()`. No CSPRNG,
production entropy behavior, or TCP ISN behavior changed in this phase.
Implementation remains blocked on an explicitly approved source trust policy
and independent review; this entry does not start Phase 15.6C or authorize
production TCP.

### Phase 15.6C — cryptographic entropy trust assessment (audit only)

`docs/security/entropy-trust-assessment.md` records the current provider,
boot-chain, CPU, TPM, EFI, and virtualization assessment. The current sources
remain candidate data only; no supported configuration establishes an
approved unpredictable seed under the project's current policy. The practical
default remains fail-closed with cryptographic readiness disabled. The
assessment identifies possible narrower future deployment assumptions but
does not approve them. No entropy collection, driver, CSPRNG, build behavior,
or TCP ISN behavior changed; `tcp_core_live_isn_available()` remains false.

### Phase 15.6D — RDSEED hardware entropy provider

Added `kernel_rdseed_*` as a separately identified, allocation-free candidate
provider. Initialization checks CPUID's maximum basic leaf before leaf 7,
subleaf 0, EBX[18], without a vendor-string restriction. Reads use the 64-bit
RDSEED destination and CF status, accept zero when CF=1, and retry at most ten
times per word with `pause`. This retry limit is an OpenSillicon bound, not a
vendor-prescribed RDSEED count. Exhaustion is temporary and retryable; the
provider is not permanently disabled by CF=0. Requests are all-or-nothing,
unchanged on failure, and capped at 256 bytes. The entropy manager exposes
RDSEED as its own source, preserves provenance, and performs no implicit
fallback.

The provider initializes before the manager and does not block boot or make
RDSEED mandatory. QEMU-only test builds make bounded requests under enabled
and disabled CPU profiles; host tests inject CPUID and instruction results
without executing RDSEED. These tests validate software behavior and QEMU
feature exposure, not physical entropy quality. RDSEED remains an untrusted
candidate and is not considered independent of RDRAND. No CSPRNG, entropy
credit, production ISN, TCP routing, transmission, timer integration, or
syscall was added. `kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` remain false.

### Phase 15.6E — RDSEED physical-hardware validation and reliability

Added the standalone Linux diagnostic `tools/rdseed_hw_test.c` and target
`make rdseed-hw-test`. It checks CPUID before executing RDSEED and performs
exactly 100 requests per invocation, with at most ten attempts per request.
It reports only feature and aggregate operational counts; values are not
logged, stored, hashed, or sent. On the available Fedora 44 host, CPUID
reported GenuineIntel, Core Ultra 5 225H, family 6/model 197/stepping 2,
maximum basic leaf 35, RDSEED/RDRAND present, and no hypervisor bit. OS
virtualization detection reported none and DMI identified an HONOR FMB-P
platform. Because these signals are not attestation, execution is classified
unknown/ambiguous rather than confirmed physical.

Six bounded 100-request bursts completed without faults: 1,737 instruction
attempts produced 498 CF=1 results and 1,239 CF=0 results; 102 requests
exhausted the ten-attempt bound and were reported as temporary unavailability.
The final three bursts took 140, 140, and 146 microseconds. The optional
longer run was not performed after repeated short-burst retry exhaustion.
These operational measurements do not estimate entropy or change trust
policy. Native userspace testing did not execute the kernel provider; no
reboot or active-kernel replacement was performed. QEMU tests continue to be
emulator-only. The manager still treats RDSEED as an untrusted candidate, and
`kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` remain false.

### Phase 15.6F — bounded entropy-source availability policy

The entropy manager now separates unsupported hardware, absent optional
devices, supported-but-not-yet-read providers, recent candidate availability,
temporary failures, consumed Limine material, and permanently disabled
providers. Candidate reads remain explicitly source-selected, all-or-nothing,
and bounded at 256 bytes; there is no fallback, source mixing, request queue,
timer, or background polling. RDSEED and RDRAND keep their ten-attempt
instruction bound. Their transient exhaustion is retryable only through an
explicit caller request; unsupported CPUID remains unavailable without
retry. VirtIO BUSY is transient, while timeout or malformed completion is
permanent in the current non-reinitializable driver. Limine remains one-shot.
No production caller schedules retries and no normal kernel path waits for
candidate material. The availability status is not a trust status:
cryptographic readiness and `tcp_core_live_isn_available()` remain false.
The manager retains the current single-CPU/non-interrupt-call assumptions and
does not claim SMP safety.

### Phase 15.6G — TPM 2.0 feasibility and entropy trust (assessment only)

Read-only discovery on the development host found Linux TPM 2.0 sysfs and
character devices, bound through the ACPI CRB driver (`tpm_crb_acpi`,
`INTC7001:00`). Both `/dev/tpm0` and `/dev/tpmrm0` denied access to the current
unprivileged user, so TPM capabilities and `TPM2_GetRandom` were not queried.
The implementation may be firmware TPM / Intel PTT, but its exact type is
unknown. No TPM driver or host configuration change is part of this phase.

Decision: a provider for a narrowly supported CRB platform appears technically
feasible, but platform and vendor trust evidence is insufficient. Before a
driver phase, identify the device and firmware, obtain authorized capability
data and vendor RNG/reseed/evaluation evidence, define a threat model, and
review bounded CRB command/response, timeout, and failure handling. TPM output
remains unapproved and receives no entropy credit. The CSPRNG and live TCP ISN
gates remain closed. Details and source references are in
`docs/security/tpm2-feasibility-assessment.md`.

### Phase 15.6H — TPM 2.0 CRB access and protocol validation (design only)

`docs/security/tpm2-crb-driver-design.md` records the observed Linux CRB
binding, the still-inaccessible TPM2 ACPI table and device, the TCG ACPI/PTP
CRB model, TPM command framing, bounded timeout/recovery policy, and a staged
fake-backend-first implementation plan. The actual host start method, CRB
addresses/capacities, TPM identity, and `TPM2_GetRandom` behavior remain
unknown. No TPM code, build configuration, tests, or host state changed. TPM
is still an unapproved candidate with no entropy credit;
`kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` remain false.

### Phase 15.6I — TPM2 ACPI table parser

A pure host-tested parser for the TCG ACPI v1.50 TPM2 table is implemented in
`kernel/drivers/tpm/tpm2_acpi.c`; it accepts revision 6 and only the PC Client CRB start
method (method 7), with output unchanged on failure. It is not linked into the
production kernel and performs no TPM or MMIO access. The focused test and
Clang ASan/UBSan run pass. The broader Phase 15.6I regression gate remains
open: the existing standalone `acpi-test` target currently fails to link
because it omits the `vmm_vmalloc_device_start` dependency referenced by the
ACPI implementation. That unrelated target was not changed. Do not mark this
phase complete until the regression gate is resolved. TPM remains an
unapproved entropy candidate with no entropy credit.
