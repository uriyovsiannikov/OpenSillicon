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
(`waitpid`); `wait()` uses `waitpid(-1, status)`. Phase 8 appends VFS calls
7–11 and `exec_path` (12). New calls append numbers;
assigned numbers and semantics are not reused or changed. The kernel supports
a 4096-byte maximum single `write`; Phase 6 initially bound standard
descriptors 1 and 2 to serial output. Phase 11 binds descriptor 0 to the
canonical TTY and routes each descriptor 1/2 write through the framebuffer
terminal and serial console once. The minimal process identity value remains 1.
Phase 12 appends syscall 13 (`sleep_ticks`): `RDI` is an unsigned tick count
and success returns zero after the calling process has remained blocked for
at least that many timer ticks. Zero returns immediately. The kernel rejects
a count greater than `UINT64_MAX - current_tick` with `EINVAL`; accepted
deadlines cannot wrap and cause an early wake. Sleeping processes consume no
CPU while blocked and the timer interrupt makes them runnable. Requests that
would cross the tick counter's `uint64_t` maximum are rejected. This is the
only time-related syscall added in Phase 12; there is no clock query or
nanosecond sleep API.

Phase 13A appends syscall 14, `pipe(fds)`. `RDI` points to two consecutive
32-bit descriptor numbers; success installs a read descriptor first and a
write descriptor second and copies both numbers to userspace. The call fails
with `EFAULT` for an invalid or unwritable output range and `EMFILE` when two
descriptor slots are unavailable. It validates the entire output range and
reserves both slots before publishing descriptors; failure leaves the table
unchanged and releases all allocated endpoint references. Pipe reads and
writes use existing syscalls 9 and 2, respectively. No existing syscall
number or behavior changes, and the ABI version remains 1.

Stage 13B appends syscall 15 (`kill(pid, signo)`), 16
(`sigaction(signo, new_action, old_action)`), and 17 (`sigreturn()`). The
exact signal contract, including restorer bytes, delivery atomicity, and
transactional action updates, is defined by ADR-020. ABI version remains 1.

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

Status: Accepted for SilliconFS v1

Decision (user-approved): use a native, little-endian inode filesystem with
4096-byte logical blocks. Phase 9 implements its formatter, validator,
read/write mount, and VFS adapter. It uses a narrow block callback for tests
and mounting; the general block layer and AHCI driver were added in Phase 10.

### Geometry and superblock

The filesystem occupies an image whose byte length is a nonzero multiple of
4096. Filesystem block numbers are absolute within that image and are unsigned
32-bit values. Block 0 contains the superblock. Its fields are little-endian:

| Byte offset | Size | Field | v1 value or meaning |
| --- | ---: | --- | --- |
| 0 | 8 | magic | bytes `SILFS1\0\0` |
| 8 | 2 | major version | 1 |
| 10 | 2 | header size | 80 |
| 12 | 4 | block size | 4096 |
| 16 | 4 | compatible features | 0 |
| 20 | 4 | read-only-compatible features | 0 |
| 24 | 4 | incompatible features | 0 |
| 28 | 4 | total blocks | image length / 4096 |
| 32 | 4 | inode count | format-time inode capacity |
| 36 | 4 | root inode | 1 |
| 40 | 4 | block bitmap start | 1 |
| 44 | 4 | block bitmap block count | ceil(total blocks / 32768) |
| 48 | 4 | inode bitmap start | immediately after block bitmap |
| 52 | 4 | inode bitmap block count | ceil(inode count / 32768) |
| 56 | 4 | inode table start | immediately after inode bitmap |
| 60 | 4 | inode table block count | ceil(inode count / 32) |
| 64 | 4 | data start | immediately after inode table |
| 68 | 4 | reserved | zero |
| 72 | 4 | superblock CRC-32 | CRC-32/ISO-HDLC of all 4096 bytes with this field zero |
| 76 | 4 | reserved | zero |
| 80 | 4016 | reserved | zero |

Every region must fit within `total_blocks`, have the exact derived length,
and appear at the specified contiguous offset. `data_start` must be less than
`total_blocks`. Unknown features, nonzero reserved bytes, invalid checksums,
or inconsistent geometry cause mount/validation failure. v1 writes all feature
bits as zero. CRC uses reflected polynomial `0xEDB88320`, initial value
`0xFFFFFFFF`, and final XOR `0xFFFFFFFF`. `total_blocks` and `inode_count`
must each be in `[1, UINT32_MAX]`; metadata geometry must still leave at least
one data block. Thus the representational image-size ceiling is
`UINT32_MAX * 4096` bytes, and actual capacity is reduced by metadata.

The block bitmap uses least-significant-bit-first numbering: bit `b` describes
filesystem block `b`; one means allocated. The inode bitmap bit `i-1`
describes inode `i`. Bits in either bitmap beyond the declared count are set
and treated as reserved. All blocks before `data_start` are allocated. Block
and inode allocation use first-fit scanning and never return reserved bits.
Inode numbers are one-based; zero is invalid and inode 1 is the root.

### Inodes and block mapping

The inode table is a dense array of 128-byte records; inode `n` is at byte
offset `(n-1)*128` from `inode_table_start`. Fields are:

| Byte offset | Size | Field |
| --- | ---: | --- |
| 0 | 2 | POSIX mode: regular `0100000` or directory `0040000`, plus `07777` permissions |
| 2 | 2 | flags, zero in v1 |
| 4 | 4 | uid |
| 8 | 4 | gid |
| 12 | 4 | reserved, zero |
| 16 | 8 | file size in bytes |
| 24 | 48 | twelve little-endian `uint32_t` direct data block numbers |
| 72 | 4 | little-endian `uint32_t` single-indirect block number |
| 76 | 52 | reserved, zero |

Zero block pointers mean holes for regular files. Nonzero data pointers must
refer to allocated blocks in `[data_start, total_blocks)`. Direct pointers
cover logical blocks 0–11. The indirect pointer names one allocated block
containing 1024 little-endian `uint32_t` data block numbers for logical blocks
12–1035. Indirect entries follow the same zero/hole and range rules. Directories
may not contain holes. Files and directories are limited to 1036 data blocks;
the maximum file size is exactly `1036 * 4096 = 4,243,456` bytes (4 MiB plus
48 KiB). Inode records with invalid modes, flags, sizes, reserved bytes, or
block references are rejected. v1 has no timestamps, link count, hard links,
symbolic links, special files, or journaling.

### Directory records

Directories are byte streams whose size is a multiple of 4096. Each record is
contained within one block and starts at a four-byte-aligned offset. Its
8-byte header contains: inode number (`uint32_t`, offset 0), record length
(`uint16_t`, offset 4), name length (`uint8_t`, offset 6), and type (`uint8_t`,
offset 7; 1 regular, 2 directory). The name bytes immediately follow, with no
NUL terminator; zero padding extends the record to a four-byte boundary.
`record_length` is aligned, at least `align4(8 + name_length)`, and does not
cross a block boundary. Records exactly tile each directory data block.
Names are 1–255 bytes and may not contain NUL or `/`. Names within a directory
must be unique. Each directory contains exactly one `.` entry naming itself
and one `..` entry naming its parent; root's `..` names inode 1. Other entries
must reference allocated inodes of the matching type. Since unlink and hard
links are absent, every allocated non-root inode has exactly one non-dot
directory reference. Every such reference has one directory parent; parent
links must agree with `..`, and following parent links must terminate at root
inode 1 rather than a disconnected tree or cycle.

### I/O, errors, and validation

The filesystem I/O seam reads or writes exactly one 4096-byte block by its
filesystem block number. A callback returns zero on success or a negative
errno. A device's physical/logical sector size is outside this interface.
There is no filesystem block cache or locking in v1. Operations are ordered so
new data/metadata is written before its directory reference; crash recovery
and atomic multi-block updates are not guaranteed without journaling.

Mount and the host validator check the superblock checksum and reserved bytes,
all geometry and arithmetic bounds, bitmap lengths and reserved bits, inode
table records, every block reference and allocation bit, directory record
bounds, unique names and contents, inode parent/reference relationships,
root reachability, unique ownership of referenced data/indirect blocks, and
supported feature bits. Allocated but unreferenced data blocks are tolerated
as leaks from interrupted unjournaled operations.
Malformed images fail with an error and are never exposed as a partially
mounted filesystem. The formatter zeroes the whole image, reserves metadata,
allocates root inode 1 and its first directory data block, and initializes its
`.` and `..` records.

Alternatives considered:

- An ext2-compatible subset has more existing tooling and interoperability,
  but imports group descriptors, broader feature negotiation, and legacy
  compatibility obligations beyond this milestone.
- A compact native format keeps the implementation and validation surface
  small, while explicit versions, feature masks, and reserved fields leave a
  controlled path for future revisions. It does not claim ext2 compatibility.

This decision resolves the superblock, inode, directory, allocation, pointer,
size, naming, feature, and corruption-detection questions for v1. Any incompatible
format change requires a new major version or an accepted ADR.

---

ADR-004 — initramfs and switch-root

Status: Accepted for Phase 8 and Phase 10 root switch

Decision (user-approved for Phase 8): request the Limine module response and
load one explicitly named initramfs module. Use the `newc` CPIO
archive format, validate every header and file range before exposing entries,
and mount its contents read-only. Keep initramfs memory owned by the boot
loader until kernel shutdown; Phase 8 does not perform switch-root.

Phase 8 accepts uncompressed `070701` `newc` archives with regular files and
directories. CRC (`070702`), symbolic links, hard links, devices, and other
record types are rejected. A `.` root directory and `./` path prefixes are
accepted. The archive adapter exposes entries without extracting file data.

Decision (user-approved for Phase 10): during kernel startup, after hardware
discovery and block-device registration, mount and fully validate SilliconFS
from the selected SATA disk before starting userspace. Replace the VFS `/`
mount (currently RAMFS) with SilliconFS through an explicit root-replacement
operation. Keep initramfs mounted at `/init`. Limine module memory remains
bootloader-owned and must remain valid for as long as any initramfs vnode or
open file can reference it; replacing `/` must not release, overwrite, or
invalidate those bytes. If persistent-root setup fails, halt with a serial
diagnostic rather than expose a partial root. The kernel performs the switch
before userspace starts. The old RAMFS root may be released after its mount
reference is replaced; initramfs lifetime is independent of the root mount.

Alternatives:

- A custom archive is easy to parse, but creates a project-specific image
  format and requires tooling before there is a strong need.
- tar variants are familiar, but have more format variations and metadata
  behavior than this first boot archive needs.
- Extracting the archive into RAMFS simplifies later writes, but duplicates
  all archive data and metadata in memory. A read-only archive mount avoids
  that copy and preserves a clear separation from writable RAMFS.

`newc` is documented and simple to validate, and can be generated by common
host tools. The initramfs parser rejects malformed/truncated entries, integer
overflow, invalid names, and unsupported record types. The Phase 10 root
transition and backing-memory lifetime are resolved above.

The system boots using initramfs and mounts SilliconFS as the persistent root
before starting userspace.

---

ADR-005 — VFS Contracts

Status: Accepted

Decision (user-approved): use reference-counted vnodes
for filesystem objects, distinct reference-counted open-file objects for
per-open offsets and flags, and a mount table rooted at `/`. A vnode carries
type, permissions, filesystem-private data, and operations for lookup,
create, read, write, and directory enumeration. A file object owns a vnode
reference, offset, and open flags. Start with one root mount; do not add
mount namespaces or caching yet. RAMFS is writable; the initramfs vnode
adapter is read-only. Internal VFS calls return zero or a negative errno;
syscall glue translates errors to the positive errno plus carry-flag ABI.

The first syscall-facing operations are open, close, read, write, stat, and
readdir, with path lookup performed by VFS. VFS does not encode filesystem
specific inode layout, archive format, or device behavior in vnode fields.
Open file descriptions can later be shared by fork and backed by SilliconFS
without changing path lookup.

Alternatives:

- A vnode and a separate file object preserve the distinction between an
  object in a filesystem and one opened handle, which is needed for shared
  offsets after descriptor duplication or fork. A single combined structure
  is smaller initially but couples object lifetime and open-handle state.
- Per-filesystem path resolution is simpler for one filesystem, but makes
  mounts and cross-filesystem traversal harder. A central mount-aware path
  walk keeps those semantics in VFS.
- A minimal callback-only interface could defer operations, but risks
  repeated API changes as directory traversal and mounts arrive. The proposed
  small vnode operation set supports the Phase 8 requirements while leaving
  implementation details private to each filesystem.
- `newc` may be consumed directly or extracted into RAMFS. The preceding
  ADR-004 decision chooses a read-only archive vnode adapter to avoid copying
  contents and keep archive parsing separate from RAMFS behavior.

The syscall ABI v1 extension assigns 7 (`open`), 8 (`close`), 9 (`read`),
10 (`stat`), 11 (`readdir`), and 12 (`exec_path`). Syscall 12 takes `RDI`
pointing to a NUL-terminated VFS path and loads static ELF64 executables. It
requires a regular file with at least one execute bit. Missing paths return
`ENOENT`, non-executable files return `EACCES`, and invalid ELF returns
`ENOEXEC`. Failure leaves the current process image running; success replaces
it only after the complete file has been read and the ELF image validated.
Dynamic linking and shared libraries remain deferred. Existing syscall 5
retains its ABI v1 memory-buffer semantics and number. Existing syscall 2
(`write`) gains support for writable file descriptors while preserving its
descriptor 1/2 serial
behavior and existing size and error rules. `readdir` returns one directory
entry per call and advances that open file description's position. `stat`
returns type, mode, and 64-bit size in a fixed-width structure. All calls use
the existing CF/positive-errno convention. `open` flags are 0 (`O_RDONLY`),
1 (`O_WRONLY`), 2 (`O_RDWR`), 0x40 (`O_CREAT`), 0x200 (`O_TRUNC`), and
0x10000 (`O_DIRECTORY`). User paths must be absolute and NUL-terminated within
256 bytes. Descriptor allocation starts at 3 and the per-process table holds
32 entries. `struct vfs_stat` is 16 bytes (`uint64_t size`, `uint32_t mode`,
`uint32_t type`); `struct vfs_dirent` is 280 bytes (`uint64_t inode`,
`uint64_t size`, `uint32_t mode`, `uint32_t type`, and a 256-byte name).
`readdir` returns 1 for an entry and 0 at end of directory.

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
child PID to the parent and zero to the child. Syscall 5 `exec()` takes a
pointer and length for a static x86_64 ELF64 image in the calling process and
replaces that process image only after the replacement image has loaded
successfully. Its memory-buffer ABI remains unchanged. Phase 8 adds syscall 12
`exec_path(path)` for static ELF64 files resolved through the VFS; it requires
an execute bit and leaves the current image intact on any failure. Dynamic
linking and shared libraries are deferred.
`waitpid()` blocks for a direct child PID; `wait()` waits for any direct child.
Exit status is the low 8 bits of the supplied status; a non-null wait status
pointer receives that single byte. Exited children remain as zombies until
reaped; exit closes all descriptor references immediately, while reaping
releases the process memory and remaining process resources. Fork inherits the
descriptor table. The initially supported standard
descriptors retain their existing serial behavior.

The scheduler remains single-CPU. LAPIC timer interrupts preempt userspace by
saving and restoring the complete user interrupt frame on each process's
kernel stack. Kernel threads continue to use cooperative Round Robin. Stage
13B adds only the signals defined by ADR-020. Shared memory and
multithreaded processes remain deferred.

Phase 13A adds anonymous pipes only. Each pipe has a bounded 4096-byte kernel
ring buffer and distinct read and write open-file objects. Descriptor
references, including references cloned by fork and retained by exec, govern
endpoint lifetime. Closing the last descriptor reference to a write endpoint
wakes readers; an empty pipe then returns EOF. Closing the last read endpoint
wakes writers; writes then fail with `EPIPE` and do not send `SIGPIPE`.
Nonempty reads return up to the bytes currently available, even if fewer than
requested. Writes accept up to the free ring-buffer capacity and may return a
short count; a write to a full pipe blocks while a reader exists. An empty
read blocks while a writer exists. Zero-length operations return zero without
blocking. Waiters block through process scheduler state and are explicitly
woken by data, capacity, or endpoint-count changes; no polling or busy-waiting
is permitted. Pipe data is copied to the userspace destination before it is
consumed, so an invalid output buffer returns `EFAULT` without losing bytes.
Writes copy the complete requested input to kernel memory before changing the
pipe, so invalid input returns `EFAULT` without partially writing. Once valid,
a write may commit and report a short count; it is not required to fill the
request. A blocked operation retries after wakeup. Pipes are not seekable.

The process syscall numbers append to ABI v1: 4 (`fork`), 5 (`exec`), and 6
(`waitpid`). `wait()` uses `waitpid(-1, status)`. PID allocation is monotonic
for the lifetime of the kernel and does not reuse identifiers. Syscall 14 is
reserved for `pipe` as described in ADR-001. Stage 13B adds only the signals
defined by ADR-020; shared memory remains deferred. There is no `dup2`,
shared-memory mapping, process groups, or broad libc/POSIX expansion in
Phase 13.

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

These decisions resolve process creation, replacement, exit, wait, descriptor
inheritance, Phase 13A anonymous pipe behavior, and Stage 13B signal semantics.
Shared-memory lifecycle remains open for a later milestone.

---

ADR-007 — Synchronization and Interrupt Context

Status: Accepted for Phase 13A pipe waits and Stage 13B signal wakeups

Pipe operations execute on the single active CPU and are serialized while
kernel syscall handling has interrupts disabled. A blocked pipe operation
records a wait channel in its process and uses the existing process scheduler
to leave the runnable set. Data arrival, buffer space, and final endpoint
closure wake the corresponding waiters. Wakeups only change process state;
pipe operations retry in syscall context after they run again. No spinning,
sleeping from interrupt context, or SMP-safety guarantee is introduced. This
decision covers anonymous pipes and serialized signal state/wakeup on the
single active CPU. Signal checks, state updates, and interrupt-frame return
selection occur with interrupts disabled; a signal wake is distinct from a
resource-ready retry. A future SMP or general kernel-thread synchronization
design requires a separate decision.

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

Status: Accepted for Phase 10

Decision (user-approved): vendor upstream ACPICA release `20260930`, tag
`20260930`, commit
`e85aa3bebaae81ed1e6d163511ac7e1eafe732cf`. Use the BSD-3-Clause-licensed
component, retain `LICENSE.BSD-3-Clause` and upstream notices, and do not
select the alternative GPL-2.0-only license. The official release archive
`acpica-unix-20260930.tar.gz` has SHA-256
`aa18901b92e30749be0edc3081c8d550c61fce4fa37546fc6a65d367a4ae71a5`.
Integrate only the source required for the ACPI table manager, namespace, and
AML interpreter. Do not import ACPICA's Unix OSL; implement OpenSillicon's
own OS services. Record the exact source subset and provenance in a vendor
manifest.

Use the RSDP physical address supplied by Limine to initialize ACPICA and
load firmware ACPI tables. Obtain MADT through ACPICA's table manager; validate
its checksum and length, then have the kernel x86_64 ACPI adapter interpret
the MADT records needed for local APIC and IOAPIC discovery. IOAPIC setup
consumes MADT IOAPIC addresses and GSI bases, interrupt source overrides, and
NMI records. It validates record sizes and address ranges, maps controller
MMIO uncached, and initially masks all redirection entries. No device IRQ
routing is enabled in this phase. Keep existing LAPIC, legacy PIC masking,
IDT, and timer initialization behavior; add only ACPI/IOAPIC behavior required
by Phase 10. The Phase 10 IOAPIC setup does not replace the current
LAPIC/PIC/IDT/timer path. x2APIC remains unsupported.

Initialize ACPI table loading and the AML namespace/interpreter, but do not
evaluate firmware control methods or initialize ACPI event delivery in Phase
10. ACPICA's public subsystem initialization/shutdown objects have link-time
dependencies on event and hardware support units, so those upstream units are
included in the selected build subset; the kernel does not enable ACPI mode,
initialize events, handle SCI delivery, or invoke power-management methods.
Implement the OpenSillicon OS services required by the linked ACPICA subset;
unsupported operations return the ACPICA-defined failure status.

Rationale: ACPICA supplies the requested reference table, namespace, and AML
implementation while retaining a small integration boundary. The release
offers BSD-3-Clause or GPL-2.0-only licensing; selecting and retaining the
BSD-3-Clause terms keeps the third-party component and notices explicit and
avoids importing GPL-only source. Pinning the release and archive hash makes
the vendor input reproducible.

The choices above resolve the Phase 10 integration profile. The selected
source and adaptation details are recorded in `third_party/acpica/PROVENANCE.md`;
no Unix runtime or host library is linked into the freestanding kernel.

---

ADR-009 — POSIX.1-2008 Compatibility Profile

Status: Open

Phase 12 uses the existing ABI v1 calls plus syscall 13 `sleep_ticks(ticks)`
as recorded in ADR-001. The extension lets init delay shell restarts without
busy-waiting. It adds no clock API, `nanosleep`, libc, or broader POSIX
process or terminal semantics. Phase 12 init and shell are freestanding,
statically linked ELF64 programs using the syscall ABI directly. Broader
POSIX compatibility remains open and out of scope.

Phase 13's supported userspace subset includes Stage 13A anonymous pipes and
the minimal Stage 13B signal calls 15–17 defined in ADR-020. Signal behavior
is limited to `SIGINT`, `SIGTERM`, and `SIGKILL`; it does not claim general
POSIX compatibility. Shared memory, `dup2`, process groups, shell
pipelines/redirection, libc, and unrelated interfaces are deferred. TTY reads
are not interruptible; scheduler-aware TTY blocking requires a separate
decision.

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

Status: Accepted for Phase 12

Phase 12 installs `/sbin/init`, `/bin/sh`, and an external test utility on
the persistent SilliconFS root. The required hierarchy is `/bin`, `/sbin`,
`/etc`, and `/tmp`; this phase does not add devices, procfs, or boot mounts.
The kernel starts `/sbin/init` with descriptors 0, 1, and 2 attached to the
canonical TTY. Its ELF initial stack contains `argc = 0`, a null `argv[0]`,
and a null environment pointer. Existing ELF smoke-test entry behavior is
unchanged. Init launches `/bin/sh`, waits for it, sleeps ten timer ticks via
syscall 13, and restarts it after every exit. Missing or unloadable init
produces a serial diagnostic followed by kernel halt.

The shell provides `help`, `echo`, `pwd`, `cd`, `ls`, `cat`, and `exit`.
`cd` changes only the shell's current-directory string. VFS paths remain
absolute: builtins resolve relative operands against that string, while
external commands accept absolute paths or bare names resolved beneath
`/bin`. There is no `PATH`, environment, scripting, pipelines, or job
control. External execution uses the existing `fork`, syscall 12
`exec_path`, and `waitpid` model.

`/etc/passwd` and `/etc/group` contain descriptive records for `root`
(UID/GID 0) and `user` (UID/GID 1000). They do not create kernel credentials
or provide authentication, login, privilege separation, or permission
enforcement. No password hashes are stored. Init supervises only the shell;
rc.d services and persistent logging are deferred. Serial diagnostics remain
kernel diagnostics and are not mirrored copies of all terminal output.

The Phase 12 hierarchy and metadata-only account policy above are resolved.
Remaining hierarchy and authentication questions apply only to future work.

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

Status: Accepted for Phase 10

Decision (user-approved): expose a minimal synchronous block-device interface
whose address unit is a logical sector. A device reports its logical sector
size (512 or 4096 bytes) and sector count. Read/write requests specify a
starting LBA, nonzero sector count, and kernel buffer; the complete range must
fit the reported capacity. The buffer address must be aligned to the logical
sector size and its length must equal `count * sector_size` without overflow.
Each operation completes before return. Flush requests stable device
completion; unsupported flush is reported explicitly. Operations return zero
on success or negative errno on failure. Physical-sector geometry is not
exposed in this phase. The block layer has no async queue, cache, or request
merging. The SilliconFS adapter translates a 4096-byte filesystem block into
one 4096-byte sector or eight 512-byte sectors, using aligned bounce storage
as needed.

AHCI implements this interface with one serialized, polling command at a time
on the single active CPU. It supports controller/device initialization,
IDENTIFY, sector reads/writes, and flush only. DMA command structures and
buffers must meet AHCI alignment and addressability requirements. The driver
uses separately page-aligned per-port command-list, received-FIS,
command-table, and sector-bounce allocations, validates all DMA physical ranges, uses 64-bit
addresses only when the HBA advertises support, and otherwise requires all
DMA addresses below 4 GiB. No NCQ, interrupts, queues, or block cache are
included.

Rationale: synchronous sector operations provide the smallest usable boundary
between an HBA and SilliconFS while keeping filesystem blocks independent of
device sector size. Serialized polling fits the current single-CPU execution
model; explicit range and alignment checks keep DMA failures contained.

The decision above resolves the Phase 10 block interface. Physical-sector
reporting and advanced scheduling remain deferred until a driver requires
them.

---

ADR-015 — Keyboard Layouts and Terminal Input

Status: Accepted for Phase 11

Decision: the initial keyboard is an i8042 PS/2 keyboard polled by the
kernel. The driver decodes translated Set 1 scancodes into key events; it does
not generate terminal characters. A separate US keymap translates key events
with Shift, Caps Lock, and Control modifiers. Unicode, dead keys, alternate
layouts, and layout switching are deferred. If the controller is absent or
does not initialize, keyboard input remains unavailable and the kernel logs
the reason; framebuffer and serial output remain usable.

Polling must not busy-spin. A blocked canonical TTY read polls the controller,
then yields the CPU with `sti; hlt; cli`; the existing LAPIC timer wakes it to
poll again. This is compatible with the current one-CPU scheduler, where a
timer interrupt does not switch away from a kernel-mode syscall. No keyboard
IRQ routing is added in this phase. Controller initialization waits use the
same timer-driven sleep and time out after 100 timer ticks (one second at the
approved 100 Hz rate).

The kernel provides four virtual terminals. Each terminal owns an independent
screen cell buffer, cursor and escape parser state, canonical input line, and
queued completed input. Alt+F1 through Alt+F4 selects a terminal. Terminal
input uses a 256-byte line buffer including the newline terminator; excess
characters are rejected and do not overwrite buffered data. Backspace edits
the current line. Enter queues the line with a newline. Ctrl-D on an empty
line queues one EOF indication; on a nonempty line it queues the buffered
line without adding a Ctrl-D character or newline. Ctrl-C and process signal
delivery are deferred.

The initial terminal renderer uses Limine's RGB framebuffer and a built-in
8-by-16 bitmap font. It supports newline, carriage return, backspace, tab,
scrolling, and CSI `2J`, `H`, `K`, and SGR reset/basic 8-color foreground and
background sequences. Each standard-output write is sent once through the
console fan-out to the active framebuffer terminal and serial; kernel
diagnostics continue to use the serial logger directly. No separate logger
path writes terminal output back into the framebuffer.

These decisions resolve the Phase 11 keyboard, keymap, buffering, EOF,
virtual-terminal, output-routing, and initial control-sequence behavior.
POSIX terminal compatibility, termios, job control, Unicode, mouse input,
USB HID, and interrupt-driven keyboard input remain deferred.

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

ADR-018 — PCI Configuration and Enumeration

Status: Accepted for Phase 10

Decision (user-approved): perform read-only PCI enumeration. Use PCI
Configuration Mechanism #1 (CF8/CFC) as fallback for legacy 256-byte
configuration space. When a valid ACPI MCFG table is present, map its ECAM
regions uncached and use them for PCIe extended configuration space. Validate
MCFG length, checksums, segment and bus ranges, base alignment, and address
arithmetic before mapping. Enumerate each segment's bus range, bridges, device
functions, and multifunction devices while avoiding duplicate or cyclic bus
traversal. Read vendor/device IDs, class/subclass/programming interface,
header type, and BAR values required to identify AHCI controllers. Do not
configure unrelated devices or rewrite unrelated PCI state. AHCI-specific
command, BAR, and bus-master setup occurs only in the AHCI driver after
resource validation.

Rationale: mechanism #1 provides a small fallback for the initial x86_64
platform, while MCFG enables full PCIe configuration space where firmware
provides it. Read-only enumeration prevents unrelated device state changes.

---

ADR-019 — Phase 13A Anonymous Pipe Contract

Status: Accepted for Phase 13A

Decision: syscall 14 `pipe(fds)` creates one anonymous pipe with a 4096-byte
ring buffer and installs two descriptors in the caller's descriptor table.
`RDI` points to an 8-byte userspace array of two `uint32_t` values. The first
value is the read descriptor and the second the write descriptor. Allocation
uses the lowest free descriptors starting at 3. If fewer than two slots are
available, return `EMFILE` and leave the descriptor table unchanged. A null,
overflowing, unmapped, or non-writable output range returns `EFAULT`; validate
all eight bytes before allocating or publishing descriptors. Any later setup
failure rolls back both slots and all allocated endpoint references. The
kernel copies out both descriptor numbers only after setup succeeds.

The read and write ends are separate open-file objects referencing one pipe.
The existing reference-counted descriptor objects define endpoint lifetime.
`fork` clones those references and `exec` retains them under existing rules.
Closing a descriptor or destroying a process descriptor table drops a
reference. Closing the final reference to an endpoint changes its endpoint
count and wakes waiters on the opposite operation. The pipe allocation is
released only after both endpoint counts reach zero and no operation can still
reference it.

Existing syscall 9 `read` and syscall 2 `write` operate on pipe descriptors;
there are no new read/write calls. A zero-length request returns zero without
waiting or changing pipe state. For a nonzero read, if data exists, copy and
consume up to `min(requested, available)` bytes and return that count. If no
data exists and writers remain, block the caller in the process scheduler,
then retry the syscall after wakeup. If no data exists and no writers remain,
return zero (EOF). For a nonzero write, if no readers remain, return `EPIPE`
and do not generate a signal. If the buffer has capacity, append up to
`min(requested, available_capacity)` bytes and return that count, including a
short count when capacity is less than requested. If full while readers
remain, block and retry after wakeup. Existing syscall maximum I/O size of
4096 bytes applies. These are byte-stream semantics; writes are not atomic
with respect to another writer.

Syscall write copies its complete input to kernel memory before pipe state is
changed, so an invalid source returns `EFAULT` without writing any bytes. Pipe
read stages bytes in kernel memory, copies them to userspace, and only then
advances the ring-buffer head. A failed copy-out returns `EFAULT` without
consuming staged bytes. Since only one CPU is active and the syscall does not
schedule between staging and commit, another pipe operation cannot invalidate
the staged range. Data arrival, consumed capacity, and endpoint closure wake
the corresponding waiters; they retry in syscall context. No callback may
spin or sleep in interrupt context.

No `dup2`, nonblocking mode, `poll`, `select`, `SIGPIPE`, signal interruption,
named pipes, packet boundaries, or pipe persistence is included. Shell
pipelines and redirection are deferred. This decision does not establish SMP
locking semantics.

Tests must cover ring-buffer empty/full states, wraparound, short transfers,
EOF, `EPIPE`, endpoint references through fork and exec, descriptor exhaustion,
invalid and cross-page output pointers, failed user copies without data loss,
blocked-reader/writer wakeups, process cleanup, and absence of busy-spinning.

---

ADR-020 — Stage 13B Minimal Signals

Status: Accepted for Stage 13B implementation

Stage 13B appends syscall 15 `kill(pid, signo)`, 16
`sigaction(signo, new_action, old_action)`, and 17 `sigreturn()` to ABI v1.
Existing syscall numbers and behavior do not change. The supported signals
are `SIGINT` (2), `SIGKILL` (9), and `SIGTERM` (15). All default to process
termination with status `128 + signo`; INT and TERM may be ignored or caught.
KILL cannot be caught or ignored. Pending signals coalesce. `kill` accepts a
positive PID only; in the absence of kernel identities and permission checks,
any process may signal any live process. This is not a security boundary.

`sigaction` uses an OpenSillicon structure of two `uint64_t` fields: handler
then restorer. Handler 0 selects default and 1 selects ignore; other handler
values are user handler entry addresses. KILL only accepts default. Both
action pointers null is `EINVAL`. When both pointers are supplied, the kernel
first validates both user ranges, copies `new_action` into a kernel local,
snapshots the current action, copies that old action to `old_action`, and only
then commits the new disposition. The copy-in precedes copy-out so aliased
pointers have defined behavior. Any validation or copy failure leaves the
disposition unchanged; `old_action` may contain the prior value only when the
call succeeds. Invalid action structure pointers or output ranges return
`EFAULT`; unsupported signal numbers, non-executable handler/restorer ranges,
and restorer byte mismatches return `EINVAL`. Values outside the syscall's
32-bit signal-number range also return `EINVAL` rather than truncating.

Caught delivery occurs only on a Ring 3 return. The kernel keeps the original
interrupt frame in the owning process structure. The user stack receives one
64-bit restorer return address; `RDI` receives the signal number and `RIP`
receives the handler. Entry `RSP` is adjusted downward with checked arithmetic
to satisfy SysV x86_64 function-entry alignment (`RSP % 16 == 8`). The full
range from that slot through the interrupted stack pointer must be writable
user memory. Insufficient or invalid stack space terminates the process using
the existing user-fault exit convention. Only one handler may be active;
additional caught signals remain pending. Fork while a handler is active
fails with `EBUSY` and creates no child.

The restorer is an exact nine-byte executable stub at the registered restorer
entry: bytes `B8 11 00 00 00 CD 80 0F 0B`. This is `mov eax,17`, `int 0x80`,
`ud2`; the interrupt instruction begins at byte offset 5 and its following
instruction address is offset 7. The kernel validates the bytes and
executable mapping when installing the action, and revalidates them on
`sigreturn`. A return is accepted only with an active handler, syscall number
17, and saved user `RIP` exactly equal to restorer entry + 7. A wrong call
site during an active handler terminates via the existing user-fault exit
convention. Calling `sigreturn` without an active handler returns `EINVAL`.
Valid return restores only the trusted kernel-side frame; there is no
user-supplied context pointer.

Pending-state changes, delivery decisions, disposition changes, block/wakeup
state, and selection of the frame returned to Ring 3 are serialized with
interrupts disabled on the single CPU. The return hook checks and consumes
pending signals while interrupts are disabled immediately before returning a
user frame. No maskable interrupt or other process syscall can run between
that check and `iretq`; a signal arriving after `iretq` is handled on the next
user interrupt/return boundary. Signal wakes are distinct from resource-ready
retries. For waitpid and pipes, an already-completed operation wins; otherwise
a caught signal returns `EINTR` without retry. An expired sleep deadline wins;
otherwise sleep returns `EINTR` and clears its deadline. If resource readiness
wakes a pipe operation first but the retry would block again, an already
pending caught signal interrupts that new block. Ignored signals do not wake
waiters. TTY reads are outside the interruption guarantee because they do not
use scheduler-aware blocking; no TTY redesign is included.

Fork outside a handler inherits dispositions and clears pending signals.
Successful exec resets caught actions to default, preserves ignored actions,
clears active-handler state, and preserves pending signals. Failed exec leaves
all signal state unchanged. Shared memory, signal masks, nested handlers,
SIGPIPE, terminal-generated signals, process groups, job control, and broad
POSIX compatibility remain deferred.

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
