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
CPU. Syscall 27 (`exec_path_argv`) appends an argument-capable path launch:
`RDI` is the executable path, `RSI` is argc (1–16), and `RDX` points to argc
user pointers followed by NULL. The aggregate NUL-terminated strings are
limited to 2048 bytes. The kernel copies them before replacing the image;
syscall 12 retains its path-only, argc-zero behavior. Syscall 32
(`process_snapshot`) copies up to 128 fixed-size process records to a caller
buffer and returns the record count; it reports `E2BIG` if the process table
does not fit. Each record contains PID, parent PID, process state, and exit
status. It does not expose command names or credentials. Because networking
uses syscall numbers 28–31, process snapshots use syscall 32.
For syscall 32, RDI is the writable record array and RSI is capacity (1–128);
success returns the number of records. Each 24-byte record contains two
64-bit IDs, a 32-bit state (`0` ready, `1` running, `2` blocked, `3` zombie),
an 8-bit exit status, and three reserved bytes. Zombies remain visible until
reaped. Invalid capacity returns `EINVAL`, an invalid output range returns
`EFAULT`, and insufficient capacity returns `E2BIG` without a partial list.
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

Stage 13C appends syscall 18 (`shm_create(size)`) and 19
(`shm_detach(base)`) for anonymous shared mappings inherited through
`fork()`. Their exact mapping, lifetime, and rollback rules are defined by
ADR-021. ABI version remains 1.

Stage 14I-D appends syscall 20 (`set_fs_base(base)`) for the native x86-64
thread pointer. Its validation and process/interrupt lifecycle are defined
by ADR-031; no Linux `arch_prctl` ABI is introduced.

Stage 14 selects a native OpenSillicon port of musl as defined by ADR-022.
This does not adopt Linux syscall numbers, entry conventions, or errno
semantics; the existing OpenSillicon ABI remains distinct.

Stage 14B's ELF startup and single-thread TLS/FS-base contract is defined by
ADR-023 and refined against pinned musl 1.2.6 in ADR-024. Its TCB offsets,
startup inputs, and TLS initialization behavior are source-specific; ADR-024
also records that musl owns construction of the user TCB/TLS and that its
upstream FS setter must be adapted to a separately approved native mechanism.
ADR-025 records Stage 14D's security review, patch provenance, and limits on
using this source as a development baseline.
ADR-026 records the native musl source, target, adapter, memory, and sysroot
integration architecture; its Stage 14F source-bootstrap portion is accepted
and implemented, while later prerequisites remain proposals. It assigns no
syscall numbers and approves no unresolved kernel ABI extension.

The existing ABI remains append-only. ADR-031 appends syscall 20
(`set_fs_base(base)`) for native musl initial-thread setup; calls 0–19 retain
their numbers and semantics. ADR-027 records the Stage 14H recommendations
for errno translation, private allocator mappings, process adaptation, and
feature tiers. Private-memory and argv/envp exec syscalls remain separate ABI
decisions.
ADR-028 resolves the user virtual-address layout and reserves a private
anonymous mapping arena; it does not allocate syscall numbers.
ADR-029 specifies internal private-map protection behavior without exposing a
syscall. ADR-030 records the implemented standard x86-64 startup stack and
validated AT_PHDR contract; neither ADR changes the syscall ABI.

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

Stage 13C reserves the lower-half range
`[0x0000700000000000, 0x00007fffffffc000)` for anonymous shared mappings.
The first and final arena pages are unmapped guards. Each mapping has an
unmapped guard page after it, which also separates it from the next mapping.
The per-address-space cursor advances monotonically; detach unmaps and releases
the mapping but does not recycle its virtual range until that address space is
destroyed. ELF load segments must reject any page-rounded range intersecting
the shared-memory arena. The process stack remains at its existing address
above the arena and is separated by the existing unmapped page.

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

For the Stage 14B TLS contract, fork inherits the TLS virtual address and
FS-base value while eager private-page cloning gives the child a private copy
of the TLS block. Successful exec installs replacement TLS and FS-base only
at the exec commit point; failed exec preserves the old TLS state. See
ADR-023.

Stage 13C adds anonymous shared mappings through syscalls 18 and 19. A process
creates a zero-filled RW/NX mapping; `fork()` maps the same backing pages at
the same virtual addresses in the child, while private pages retain eager
copy semantics. Descendants can inherit the mapping through later forks.
Successful exec destroys the old address space and its mappings; failed exec
leaves them intact. Detach removes only the caller's mapping. Shared backing
pages are released after the last address-space mapping reference is dropped.
Exited processes retain their references while zombies; reaping destroys the
address space and releases them. Unrelated processes cannot attach. Named
shared memory, global object IDs, synchronization primitives, read-only
mappings, and POSIX shared-memory compatibility remain deferred.

The scheduler remains single-CPU. LAPIC timer interrupts preempt userspace by
saving and restoring the complete user interrupt frame on each process's
kernel stack. Kernel threads continue to use cooperative Round Robin. Stage
13B adds only the signals defined by ADR-020. Stage 13C adds the anonymous
shared mappings defined by ADR-021. Multithreaded processes remain deferred.

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
defined by ADR-020. Stage 13C adds only anonymous shared mappings through
fork inheritance as defined by ADR-021. There is no `dup2`, named shared
memory, unrelated-process attach, process groups, or broad libc/POSIX
expansion in Phase 13.

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
inheritance, Phase 13A anonymous pipe behavior, Stage 13B signal semantics,
and Stage 13C anonymous shared-memory lifecycle.

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
resource-ready retry. Stage 13C shared-mapping creation, fork installation,
detach, and teardown are serialized on the existing single active CPU. Each
address-space mapping holds exactly one reference to its shared object. Fork
acquires a child reference before installing child PTEs and publishes the
child mapping record only after the entire region is mapped. Partial mapping
rollback removes its PTEs before dropping that acquired reference. Completed
child mappings are released by child-address-space teardown if a later clone
step fails. Generic private-page cleanup skips shared PTEs; page-table changes
are complete before the final object reference frees backing pages. This adds
no SMP-safety or userspace synchronization guarantee. A future SMP or general
kernel-thread synchronization design requires a separate decision.

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

Stage 14's initial musl profile is a bounded static x86_64 userspace port to
the native OpenSillicon ABI, not Linux syscall compatibility. The exact
supported libc and syscall surface remains open pending a representative
musl-linked binary and measured requirements; see ADR-022. Dynamic linking,
shared libraries, PIE/ASLR, threads, futexes, and broad Linux compatibility
are outside the initial profile.

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

The shell provides `help`, `cd`, and `exit`; `ls`, `cat`, `echo`, `pwd`,
`true`, `false`, `clear`, `uname`, and `fetch` are separate ELF programs in
`/bin`.
`cd` changes only
the shell's current-directory string. External commands accept absolute
paths or bare names resolved beneath `/bin` and run through `fork`, syscall
27 `exec_path_argv`, and `waitpid`; argv[0] is the typed command token.
Syscall 12 remains the argc-zero path-only interface. `cat` reads named files
or standard input, `ls` accepts path operands, and `pwd` prints `/`.
There is no `PATH`,
environment, scripting, quoting, pipelines, redirection, or job control.

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

Status: Resolved by ADR-036 (architecture; implementation pending)

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

ADR-036 — Network Stack and Socket Architecture

Status: Accepted (architecture only; implementation pending)

Context

Phase 15 requires packet transmit/receive, IPv4 connectivity, UDP sockets,
TCP connections, routing, and repeatable tests. Inspection found PCI
enumeration and generic interrupt dispatch, but no generic NIC driver
registry, BAR resource mapper, network packet queues, packet buffers,
protocol stack, socket descriptor type, or socket syscalls. The existing FD
layer is reference-counted and can host socket objects. The runtime is
single-CPU and has user-process wait channels, but no deferred network service
integrated with that scheduler. This ADR resolves the open architecture
questions in ADR-012; it does not implement the stack or freeze syscall
numbers.

Decision

1. Layering and ownership

Build a small in-kernel stack under `kernel/net/`: bounded packet buffers and
network interfaces, Ethernet II, ARP, IPv4, ICMP, UDP, TCP, routes, and socket
adaptation. Ethernet owns framing and dispatch; ARP resolves IPv4 next hops;
IPv4 validates and dispatches datagrams and selects routes, but does not
forward. ICMP and transport modules own their validation, checksums, state,
and bounded receive queues. Socket objects adapt to the existing refcounted
FD layer. VFS files and sockets remain distinct object types behind the FD
operations; do not introduce a general protocol plug-in framework.

2. Device interface and first NIC

The first driver target is QEMU's PCI e1000 / 82540EM model (commonly PCI ID
`8086:100e`), selected only by an explicit supported-ID check. Local QEMU
inspection confirms the model is available; no NIC driver exists today.
Start with bounded polling from deferred kernel service context. The current
interrupt code has no network IRQ route, and protocol work is not safe in
interrupt context. A later IRQ path may acknowledge/enqueue bounded device
events only; parsing, allocation, protocol processing, and socket wakeups
remain deferred.

The interface provides init/shutdown, MAC/link/MTU queries, bounded RX poll,
and TX with explicit ownership. Initially, RX DMA ring buffers remain
driver-owned: on completion the driver copies a frame into a bounded core
packet buffer, rearms the DMA buffer, and transfers the packet buffer to the
network core. Successful TX submission transfers ownership to the driver
until completion; rejected submission leaves ownership with the caller.
Each packet has exactly one owner and is released once. Shutdown stops DMA
before releasing rings. Init failure unwinds in reverse order. RX pressure
drops packets with counters; TX pressure returns a retryable resource error.

3. Bounded packet buffers

Use a fixed pool, initially 64 buffers of 1600 bytes with 32 bytes headroom.
This fits untagged Ethernet II frames for MTU 1500 (1514 bytes excluding FCS)
plus headroom; VLAN and jumbo frames are excluded. A buffer records capacity,
data offset, length, and owning interface. No implicit sharing or packet
refcount is used. Ownership transfers explicitly between driver and protocol
layer. Queue lengths and per-socket buffers are bounded separately. RX pool
exhaustion drops packets; no unbounded allocation is permitted. These initial
capacity values are implementation constants to validate under QEMU.

4. Initial protocol profile

- Ethernet: untagged Ethernet II, ARP and IPv4 only, one configured MAC,
  MTU 1500. Drop unsupported EtherTypes and malformed frame lengths.
- IPv4: explicit static host address and prefix. Validate version, header and
  total lengths, and header checksum; initially accept only IHL 5. Reject
  fragments rather than reassemble; do not forward. Outbound TTL is 64 and
  TTL-zero packets are rejected. Compute/verify required checksums.
- ARP: bounded 32-entry cache with an initial 60-second aging interval;
  request/reply handling; at most three requests one second apart. Bound
  unresolved-next-hop pending traffic and drop/fail on overflow.
- ICMP: echo request/reply and local destination/port-unreachable errors
  needed by this profile. No router ICMP forwarding or broad message suite.
- UDP: preserve datagram boundaries. Bind IPv4/port and allocate ephemeral
  ports from 49152–65535. Verify nonzero receive checksums, accept IPv4 UDP
  checksum zero, and compute outbound checksums. The maximum unfragmented
  payload at MTU 1500 is 1472 bytes. Initially cap receive queues at 16
  datagrams and 16 KiB per socket. A short receive copies the available
  prefix and discards the remainder of that datagram. Blocking semantics and
  exact nonblocking errors are finalized with the syscall ABI.
- TCP: bounded single-stream active/passive opens and only states needed for
  SYN, established traffic, FIN, and TIME_WAIT. Check sequence/acknowledgment
  windows and checksums. Bound send/receive queues to 16 KiB per connection,
  listen backlog to 8, and initial outstanding data to one 1460-byte MSS.
  Start RTO at 1 second, exponential backoff capped at 8 seconds, and stop
  after five retries with timeout. Negotiate MSS only. Defer SACK, window
  scaling, timestamps, ECN, urgent data, simultaneous open, and advanced
  congestion control. Do not call TCP usable until a non-predictable kernel
  entropy source is available for initial sequence numbers; no trusted
  entropy API was found. Deterministic seeds are test-only.

These are bounded experimental semantics, not RFC-complete or broad Internet
compatibility.

5. Address configuration and routing

Start with explicit static IPv4 address, prefix, gateway, and optional DNS
server metadata. Do not hardwire QEMU values as universal defaults. DHCP is
deferred. Install a connected-subnet route and optional default route; send
on-link traffic directly and resolve the gateway by ARP for off-link traffic.
There is no forwarding, IPv6, route discovery, or DNS resolver in the initial
numeric-IP profile. Automated QEMU user-network tests may use
`10.0.2.15/24`, gateway `10.0.2.2`, and DNS metadata `10.0.2.3`. Isolated
socket-peer tests may instead use QEMU's socket backend and documentation
addresses.

6. Socket and descriptor ABI

Use a bounded BSD-inspired native interface, not Linux syscall numbering or
compatibility. Initial family is IPv4 and types are stream/datagram. Needed
operations are socket, bind, listen, accept, connect, sendto, recvfrom, and
shutdown; existing close releases a socket FD. Connected stream read/write
map to receive/send; connected datagram read/write use the connected peer.
Represent sockets as refcounted FD objects, preserving close and fork
reference ownership. Forked descriptors refer to the same open socket state;
exec preserves descriptors according to current process behavior, and failed
exec changes nothing. Sockets are not VFS pathnames.

Use a fixed 16-byte native IPv4 socket address with 16-bit family, network
byte-order port and address, and zero padding. Copy and validate structures at
the syscall boundary. Exact constants, argument registers, error cases, and
syscall numbers require a separate ABI review before implementation; none are
assigned here. Blocking connect, accept, and receive use process wait
channels. Queue predicates and wait installation must be ordered atomically
against single-CPU preemption to prevent lost wakeups; interruption follows
the existing signal contract.

Initial exclusions: threads, poll/select, socket options, Unix sockets, raw
sockets, IPv6, ancillary data, and Linux socket compatibility.

7. Concurrency and deferred work

The initial design assumes one CPU and serialized protocol execution, not
SMP safety. Add a bounded deferred service hook at safe scheduler/kernel
service points. Each pass drains at most 32 received frames, advances
protocol timers/retries, and wakes wait channels outside interrupt context.
The existing cooperative kernel-thread scheduler is separate and is not a
general network worker integrated with process wait channels. Timer IRQ work
only advances a tick or marks work pending. Queue update and block/wake
handoff use existing interrupt-disable discipline. SMP would require locks
and cross-CPU wake rules and is deferred.

8. Test plan and implementation stages

- 15A: bounded network service, PCI BAR/DMA resource prerequisites, and
  polling e1000 ownership. Pass when QEMU initialization and frame TX/RX are
  asserted by an isolated peer with bounded-queue/failure tests.
- 15B: host-tested parsing/checksums and malformed/truncated packets; Ethernet,
  ARP, IPv4, ICMP, static config, and routes. Pass with deterministic QEMU
  ARP/IPv4/ICMP exchange.
- 15C: UDP and a separately reviewed socket FD/syscall ABI. Pass with QEMU
  host-peer datagrams, boundary/error/queue tests, and fork/close lifecycle.
- 15D: resolve entropy first, then TCP state/timers/buffers/listen/accept. Pass
  with host state-machine tests and active/passive QEMU exchange, retries,
  timeout, and teardown.
- 15E: integrated malformed traffic, resource pressure, process/FD lifecycle,
  and existing kernel regression suite.

Use host C tests for parsers, checksums, routes, and TCP transitions, including
bad checksums, truncation, overflow, and queue exhaustion. Use QEMU q35 with
e1000 and an isolated socket-backed Ethernet peer for deterministic frame
assertions. Use a separate QEMU user-network profile with host forwarding only
for end-to-end socket tests. Tests must not use the host's physical network or
public Internet; capture peer frames and QEMU serial output.

Consequences and prerequisites

PCI currently enumerates devices and reads BAR registers but has no generic
BAR sizing/mapping or DMA-resource ownership. The interrupt framework has no
NIC IRQ path. There is no packet pool, deferred network service, socket FD
type, static configuration interface, or socket syscall ABI. Polling avoids
an IRQ prerequisite for first bring-up, but e1000 still requires safe BAR
mapping, PCI bus-master handling, and DMA allocations suitable for the
device. Implement and review those before protocol code depends on them. TCP
is blocked as a usable feature until trusted entropy exists. Socket ABI
numbers and exact structures remain for a separate decision. This ADR changes
no source code and introduces no networking implementation.

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

Stage 14's musl target configuration and reproducible source pin are governed
by ADR-022 and the proposed integration architecture in ADR-026. The Clang
target and compiler-runtime choice in ADR-026 is a candidate requiring a
target probe; kernel and userspace build configurations remain separate.

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

The initial terminal renderer uses Limine's RGB framebuffer and the vendored
Modern DOS 8-by-16 bitmap font under CC0 1.0. It supports newline, carriage
return, backspace, tab, scrolling, and CSI `2J`, `H`, `K`, and SGR reset,
standard 8-color, and bright 8-color foreground and background sequences.
Each standard-output write is sent once through the
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
POSIX compatibility remain deferred except for the anonymous shared mappings
specified by ADR-021.

---

ADR-021 — Stage 13C Anonymous Shared Memory

Status: Accepted for Stage 13C implementation

Stage 13C provides anonymous memory mappings shared only through `fork()`
inheritance. Syscall 18, `shm_create(size)`, takes a nonzero byte size in
`RDI`, rounds it up to 4096-byte pages with checked arithmetic, allocates and
zeroes the backing pages, and returns the user virtual base in `RAX`. Syscall
19, `shm_detach(base)`, takes the exact mapping base in `RDI`, unmaps it from
the calling address space, and returns zero. Errors use the ABI v1
positive-errno-in-`RAX`, carry-flag convention. Zero or overflowing sizes and
invalid, stale, unaligned, or non-live detach bases return `EINVAL`;
physical-memory or arena exhaustion returns `ENOMEM`. No existing syscall
number or behavior changes.

The arena is `[0x0000700000000000, 0x00007fffffffc000)`, with the end
exclusive. The first and final arena pages remain unmapped as boundary guards.
Each shared region is page-aligned and followed by one unmapped guard page;
that page separates it from the next region. A per-address-space monotonic
cursor begins one page after the arena start and advances past each mapping
and its guard. Detach releases physical mapping ownership but does not rewind
the cursor or reuse virtual addresses before address-space destruction. This
avoids stale bases detaching a later mapping; repeated create/detach can
eventually exhaust the virtual arena. ELF loading rejects any page-rounded
segment range intersecting the arena. The existing process stack remains
above the arena, with its current unmapped guard page below it.

Mappings are user-readable, user-writable, and non-executable (RW/NX). There
are no permission options. Backing pages are owned by a kernel shared object
with one mapping reference per address space, not by a global PMM frame
reference count. `fork()` preserves each shared mapping at the same virtual
address and increments its object reference once; all ordinary private pages
continue to be eagerly copied. A process can share with its child and later
fork descendants. An unrelated process cannot attach to an existing mapping.

Fork cloning copies private leaves first and skips leaves marked shared in the
page tables. It then installs each shared region in the child. For each region
the kernel allocates an unlinked child record, checks reference overflow,
acquires the object reference before installing any shared PTE, and maps all
backing pages at the parent's virtual addresses. The child mapping record is
linked only after the full region is installed. If installation fails, the
kernel unmaps the partial child PTEs, drops the reference acquired for that
region exactly once, frees its unlinked record, and destroys the child address
space. Destruction frees copied private pages and releases each earlier,
committed shared region exactly once. The parent's mappings and references
remain unchanged. A rollback failure to remove a newly installed PTE is a
kernel invariant failure and must not proceed to backing-page release.

Shared PTEs carry a software `PTE_SHARED` marker. Private-page clone and free
walkers skip those physical leaves. Shared-aware address-space teardown first
removes the address space's page tables, then drops its shared mapping
references; the last reference frees the backing pages and object metadata.
Direct page-table destruction must not discard a nonempty shared-mapping
list. Detach removes the caller's PTEs before dropping its reference and
leaves the virtual slot reserved until the address space is destroyed.

Successful exec destroys the old address space and releases its mappings;
failed exec leaves the old address space and mappings intact. Process exit
marks the process zombie and retains its address space under existing process
lifetime rules. Its shared references are released when `waitpid()` reaps the
zombie and address-space destruction occurs. Each detach or address-space
destruction releases one mapping reference exactly once.

Named shared memory, independent attach, global object identifiers,
synchronization primitives, read-only or executable mappings, and POSIX
`shm_open()`/`shm_unlink()` compatibility are deferred. Shared memory is not
an isolation boundary: participating fork descendants can observe and modify
the same bytes, and no kernel identity or permission policy restricts them.

---

ADR-022 — Stage 14 Native musl Port Profile

Status: Accepted for Stage 14A; implementation prerequisites remain open

Decision: adapt musl to OpenSillicon's native userspace ABI. This is an
OpenSillicon operating-system port of the C library, not Linux syscall ABI
compatibility. The existing syscall ABI remains the contract: syscall numbers
0–19, `int 0x80` entry, arguments in `RDI`, `RSI`, `RDX`, `R10`, `R8`, and
`R9`, and errors returned as positive errno in `RAX` with CF set. Existing
syscall numbers and semantics do not change under this decision. Linux syscall
numbers, Linux `int 0x80`/`syscall` conventions, and Linux negative-errno
handling must not be assumed or automatically passed to the kernel. A musl
port layer must adapt libc operations to the OpenSillicon ABI and translate
the established error convention.

The initial support profile is static x86_64 userspace programs linked against
musl and using only the libc operations explicitly supported by subsequent
Stage 14 decisions. It does not promise Linux or general POSIX binary
compatibility. Dynamic linking, shared libraries, PIE/ASLR, threads, futexes,
and broad Linux compatibility remain deferred.

The exact syscall and libc surface is not fixed here. It must be determined
from the selected musl revision and a representative statically linked test
program in a later sub-stage. Inspect that program's ELF headers and observe
its actual kernel-call requirements before approving each required ABI or
runtime extension; do not infer the complete surface from Linux musl behavior
or from this ADR.

The exact porting and ABI-verification baseline is musl 1.2.6, pinned by the
official release archive URL and SHA-256 in ADR-024. This pin establishes a
reproducible source revision, not approval to ship it unchanged: ADR-024 records
known upstream security/correctness advisories against 1.2.6 and requires a
separate patch/release qualification before distribution. Preserve the main
MIT license, `COPYRIGHT`, and all applicable file-level notices when source is
later imported. No musl source was imported in Stage 14A or 14C.

The startup-stack and single-thread TLS/FS-base design is resolved by
ADR-023, with source-verified musl details and representative ELF evidence in
ADR-024. ADR-024 supersedes ADR-023's provisional kernel-owned TLS/TCB
preparation: musl's startup code constructs its own TCB/TLS in user memory and
calls an architecture hook to set FS. The native OpenSillicon mechanism for
that hook remains a separately approved ABI decision. The
following other prerequisites remain open for separately reviewed Stage 14
sub-stages:

- The smallest safe user-memory allocation interface needed by the selected
  musl build, including address-space layout and cleanup semantics.
- Adaptation of libc file and descriptor operations to the current VFS and
  FD interfaces, with any syscall additions decided from measured needs.
- Adaptation of libc process and signal operations to OpenSillicon's existing
  fork, exec, wait, and limited signal semantics.
- Userspace compiler, linker, startup objects, sysroot, and reproducible
  build rules, kept separate from kernel build flags.

ADR-026 proposes an integration order for these prerequisites while leaving
the compiler runtime, FS-base setter, private mapping API, and their syscall
numbers for separate validation and approval.

Stage 14A makes no kernel, libc, toolchain, linker-script, build-system, or
test changes and does not claim that a musl binary can already run.

---

ADR-023 — Static ELF Startup and Single-Thread TLS/FS-Base Contract

Status: Accepted for Stage 14B design; source-specific details and ownership
are refined by ADR-024

Context

The ELF loader accepts static x86_64 ET_EXEC images and rejects PT_TLS.
Before Stage 14I-C, process startup used project-specific empty/zero-word
stacks and did not track AT_PHDR. The current stack contract and loader
metadata are specified in ADR-030. Interrupt frames and process state still
do not include FS base. ADR-022 selects a native musl port; ADR-024 pins the
source revision.

Decision: startup stack

The musl launch path will use a conventional x86-64 process-entry stack:

    RSP -> argc
           argv[0] ... argv[argc-1]
           NULL
           envp[0] ... envp[envc-1]
           NULL
           auxiliary-vector (type, value) pairs
           AT_NULL, 0
           alignment padding and pointed-to strings/data

RSP is 16-byte aligned at initial entry. All pointers refer to readable user
memory in the process stack, and the argv, envp, and auxv terminators are
mandatory. The existing ELF smoke-test entry convention remains unchanged.
Path-only process launch and the existing `exec_path(path)` interface use
`argc=0`, `argv={NULL}`, and `envp={NULL}`. Memory-buffer syscall 5 also uses
empty argv/envp because its ABI carries no launch arguments. This preserves
the existing syscalls; an argument- and environment-capable exec interface
requires a separate ABI decision. On the standard startup path, user general
registers other than RSP are initialized to zero; startup code must use the
stack contract, not inherited kernel-call register contents. ADR-030 records
the implemented layout and supersedes earlier path-as-argv[0] proposals.

The initial stack remains one 4 KiB page, as in the current process model,
with no stack growth. The complete pointer tables, strings, auxv, and any
auxiliary data must fit in that page. Oversize startup state fails before the
exec commit point with `E2BIG` (errno value 7); stack/TLS allocation failure
reports the existing allocation error. The errno header currently lacks an
`E2BIG` definition, which Stage 14B implementation must add without changing
any syscall number or existing behavior. The lower guard page remains
unmapped. Stack
construction is transactional: failures release only replacement-image
resources and leave the old image, stack, TLS, and FS base active.

Decision: auxiliary vector

The native startup contract supplies these entries for static ET_EXEC:

- `AT_PHDR`: runtime address of the program-header table. The loader computes
  it from a PT_LOAD that file-backs the complete table; images without such a
  mapped table are rejected for this startup profile.
- `AT_PHENT` and `AT_PHNUM`: `e_phentsize` and `e_phnum` from the validated ELF
  header.
- `AT_PAGESZ`: 4096, matching the kernel page size.
- `AT_ENTRY`: the validated executable entry address.
- `AT_BASE`: zero, because this profile has no interpreter or dynamic linker.
- `AT_NULL`: zero-valued terminator.

These values are available from the ELF image and current memory configuration,
subject to adding program-header address tracking and validation. `AT_RANDOM`
is not fabricated. No kernel entropy source is established by the current
architecture documents; omit it unless the selected musl revision requires it.
If it is required, a real entropy source and its failure policy must be
separately approved before implementation. UID/GID and other identity entries
are omitted because the kernel has no process identity state. The exact auxv
entries consumed or required by musl remain subject to the source audit below;
this ADR does not claim Linux syscall or complete Linux auxv compatibility.

Decision: PT_TLS and TLS ownership

The process model remains single-threaded and supports at most one PT_TLS
module per executable. The loader validates `p_filesz <= p_memsz`, the
template file range, overflow in `p_vaddr + p_memsz`, and `p_align` as zero,
one, or a power of two. It verifies that PT_TLS template bytes correspond to
file-backed bytes in a PT_LOAD image. Musl 1.2.6's CRT constructs its TLS/TCB
in user memory: it copies the PT_TLS template and relies on zeroed allocation
for the remainder, then initializes the DTV and TCB as specified in ADR-024.
The kernel must ensure the template is readable and the resulting user memory
is private, writable, non-executable, aligned, and within user address space;
it must not invent TCB fields or claim to initialize musl's TCB. The allocator
interface required by musl remains a separate Stage 14 decision.

Place the single TLS mapping in an unmapped user-space gap below
`SHM_ARENA_START`, with one unmapped guard page on each side. Search downward
from the arena boundary for the first suitably aligned contiguous range that
does not intersect any ELF load segment or reserved range. Do not reuse the
virtual range during that address space's lifetime. If no such range exists,
replacement exec fails with `ENOMEM` and the old image remains active. The
TLS mapping is ordinary private user memory: fork clones its pages privately
at the same virtual address, and address-space teardown frees them through the
normal private-page path. No shared-memory mapping is used for TLS.

Executables without PT_TLS start with user FS base zero. Multiple threads,
dynamic TLS modules, and TLS module loading remain out of scope.

ADR-024 source inspection refines this ownership model: musl itself creates
the initial TLS/TCB block and calls its x86_64 `__set_thread_area` hook; that
upstream hook currently uses Linux `arch_prctl`, which cannot be used by
OpenSillicon. Kernel TLS allocation/TCB initialization is therefore not the
selected contract. Keep the process-owned private mapping, fork-copy, and
exec/exit/reap lifetime rules above, but implement FS-base access only after a
native OpenSillicon ABI mechanism is separately approved.

Decision: FS-base lifecycle

User FS base is per-process architecture state, separate from the ordinary
general-register interrupt frame. On Ring 3 entry, the low-level interrupt
path captures IA32_FS_BASE into entry-local kernel state before calling C,
then installs kernel-safe FS base zero. Entry-local state must tolerate a
nested kernel interrupt; one global scratch value is insufficient. For a
user-origin frame, the captured value is saved to the process that owned that
frame. Kernel-origin nested interrupts must not overwrite that process value.

Immediately before returning to Ring 3, the return path disables interrupts,
loads the selected process's FS base, and executes the final `iretq` as one
protected transition. This prevents a timer interrupt from replacing the
just-restored value before user execution. An interrupt after `iretq` captures
the current process value normally. Kernel execution uses FS base zero; kernel
threads do not use per-process FS state.

- Timer preemption saves the interrupted user's FS base with its process
  state; scheduling restores the selected process's value at user return.
- Ordinary interrupt/syscall return restores the same process's value.
- Fork copies the parent's FS-base virtual address. Eager private-page cloning
  gives the child a private TLS block at that address, so later child writes
  cannot change the parent's private TLS bytes.
- Successful exec prepares new TLS and startup state before commit, then
  switches address space and FS base together. The old address space is not
  freed until inactive. Failed exec leaves old FS base and TLS untouched.
- Exit leaves user context using kernel-safe FS base. A zombie retains its
  address space/TLS until wait/reap destroys it. Reap must not destroy an
  address space still active in CR3 or selected for Ring 3 return.

The system is single-CPU. Implementation must still make entry-local capture
and the final restore/iret sequence explicit and auditable.

Verification gate

The source and representative ELF verification required by this ADR was
completed for musl 1.2.6 in Stage 14C and is recorded in ADR-024. That evidence
supersedes the former open verification gate. Before implementation, use the
verified TCB/TLS and auxv contract in ADR-024, then separately approve the
native OpenSillicon FS-base ABI operation and resolve the remaining Stage 14A
memory, VFS/FD, process/signal, and reproducible toolchain decisions.

Implementation boundary and order

Stage 14B implementation is limited to: (1) ELF metadata validation and
PT_TLS template capture; (2) bounded one-page startup-stack construction and
the documented auxv; (3) aligned per-process TLS/TCB preparation after the
musl verification gate; (4) per-process FS-base capture, kernel-safe reset,
and protected restore before Ring 3; and (5) fork/exec/exit/reap integration.
Each step must remain independently buildable and tested. Dynamic linking,
shared libraries, PIE/ASLR, multiple threads, multiple TLS modules, Linux
syscall compatibility, and unrelated syscall/process redesign are excluded.

Acceptance tests

- Host tests validate stack word order, pointer bounds, terminators, 16-byte
  entry alignment, auxv values, PT_TLS bounds/size/alignment checks, template
  copying, zero-fill, and allocation rollback.
- QEMU tests verify TLS initialized and zero-fill values, FS-base restoration
  across timer preemption between two processes, fork's inherited TLS value
  and private-write isolation, successful exec replacement, failed exec
  preservation, and zombie retention until reap.
- Fault-injection tests verify partial stack/TLS preparation and exec failures
  preserve the active address space and release replacement resources once.

ADR-023 defines startup and TLS architecture. ADR-031 later appends the
native FS-base setter as syscall 20; no existing syscall number or behavior
changes.

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

---

ADR-024 — Pin musl 1.2.6 and Verify x86_64 Startup/TLS Contract

Status: Accepted for Stage 14C source and ABI verification; port implementation
and release qualification remain separate decisions

Context

ADR-022 selects a native OpenSillicon musl port, not Linux ABI compatibility.
ADR-023 defined a bounded static ET_EXEC startup/TLS design but left the exact
musl TCB layout and startup inputs open. This decision records evidence from
the upstream source and a representative static x86_64 executable.

Source selection and integrity

Select upstream musl 1.2.6 as the immutable porting and ABI-verification
baseline. The official release announcement is dated 2026-03-20
([announcement](https://www.openwall.com/lists/musl/2026/03/20/1)). Its canonical source URL
is `https://musl.libc.org/releases/musl-1.2.6.tar.gz`; the detached signature
URL is `https://musl.libc.org/releases/musl-1.2.6.tar.gz.asc`.

Archive SHA-256:
`d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`

Detached signature SHA-256:
`8a696228404cd4bae6379cf7538fc5d18c133b0ada54a41c1c6cef31d3c22ab6`

The official archive host timed out during this research. The archive was
obtained from a mirror and matched the hash independently listed by the
Fossies and Buildroot package records; the official detached signature was
obtained from a distribution mirror. GPG reported a good signature dated
2026-03-20 for key fingerprint
`8364 8929 0BB6 B70F 99FF DA05 56BC DB59 3020 450F` (UID `musl libc
<musl@libc.org>`). The local GPG trust database does not certify this key, so
the signature is cryptographically valid but local web-of-trust identity is
not established. The fingerprint was cross-checked against an Arch package
recipe's `validpgpkeys` entry. Reproducible acquisition must fetch the
canonical archive and signature, verify this fingerprint and detached
signature, and enforce the recorded archive hash.

The source tree's `COPYRIGHT` SHA-256 is
`b870108ec5e7790e9f9919064f1b9421d62d5f9b0e6c230c6adf7ea2da62e97b`.
Musl's main license is MIT, copyright Rich Felker et al. The same file
describes additional notices: TRE regex sources carry a modified 2-clause BSD
notice; math and complex files contain their own permissive notices; DES
contains a BSD notice; and selected architecture-specific files have
additional notices. Any future import must retain `COPYRIGHT` and each
applicable source-file notice. The release announcement and license/source
notices are linked below.

This pin is a source-verification baseline, not permission to ship 1.2.6
unchanged. Stage 14D's security findings, current release check, reproducible
patch series, and validation limits supersede the preliminary advisory
summary in ADR-024; see ADR-025. In particular, ADR-025 classifies the patched
tree as suitable only for continued development and porting work, not
distribution.

Verified source contract: static CRT and initial stack

Relevant musl 1.2.6 files and symbols:

- `arch/x86_64/crt_arch.h`, `_start`: clears RBP, passes the incoming RSP in
  RDI, passes `_DYNAMIC` in RSI, aligns RSP down to 16 bytes, and calls
  `_start_c`.
- `crt/crt1.c`, `_start_c`: reads `argc = p[0]`, sets `argv = p + 1`, then
  calls `__libc_start_main`.
- `src/env/__libc_start_main.c`, `__libc_start_main` and `__init_libc`:
  computes `envp = argv + argc + 1`, scans the auxv immediately after the
  envp NULL, initializes libc/TLS/stack-protector state, then runs init
  functions and calls `main(argc, argv, envp)`.
- `src/env/__init_tls.c`, `static_init_tls`, `__copy_tls`, and `__init_tp`:
  scans the program-header table, finds PT_TLS, initializes TLS/TCB, then
  installs the thread pointer through `__set_thread_area`.

Path-only launches through syscall 12 and memory-buffer syscall 5 supply
`argc=0`, `argv={NULL}`, and `envp={NULL}`. Append-only syscall 27
`exec_path_argv(path, argc, argv)` accepts 1–16 arguments with at most 2048
aggregate NUL-terminated bytes; the kernel copies strings into the new user
stack. Environment vectors remain unsupported.

Verified x86_64 TLS/TCB contract

Sources: `src/internal/pthread_impl.h`, `arch/x86_64/pthread_arch.h`,
`src/env/__init_tls.c`, and `src/thread/x86_64/__set_thread_area.s`. The
default x86_64 configuration does not define `TLS_ABOVE_TP`: it is the
variant-II layout, with the TLS module data below the thread pointer. FS base
equals the thread pointer (TP), and `arch/x86_64/pthread_arch.h::__get_tp()`
reads `%fs:0`.

For the verified default layout, `struct pthread` is 200 bytes and 8-byte
aligned on x86_64. Its relevant fields are: `self` at TP+0, `dtv` at TP+8,
`prev` at +16, `next` at +24, `sysinfo` at +32, `canary` at +40, `tid` at
offset 48, and `errno_val` at offset 52. The initial TCB's `self` points to
itself.
`MIN_TLS_ALIGN` is `offsetof(struct builtin_tls, pt)`, which is 8 bytes for
the x86_64 build. These are musl-internal ABI details pinned to this exact
revision; do not treat them as a generic platform ABI or change them in a
local port without updating this contract and tests.

`static_init_tls` reads PT_TLS `p_vaddr`, `p_filesz`, `p_memsz`, and `p_align`
from the executable's program-header table. It uses the static ET_EXEC
address as the image base when there is no PT_PHDR/PT_DYNAMIC-derived base.
`__copy_tls` copies exactly the template bytes (`p_filesz`) into the module
image below TP; the remaining `p_memsz - p_filesz` bytes must be zero. It
sets `dtv[0]` to the module count and `dtv[1]` to the module TLS address.
The TLS allocation also contains the TCB and DTV. Musl uses an internal
builtin area when the required total fits; otherwise it requests anonymous
memory through Linux `mmap`. A native port must provide an equivalent
userspace allocation route or a separately designed adaptation, not have the
kernel fabricate musl's TCB.

`__init_tp` sets `self`, calls `__set_thread_area`, initializes the initial
thread bookkeeping, and invokes Linux `set_tid_address`. The x86_64 setter
`src/thread/x86_64/__set_thread_area.s` issues Linux syscall 158,
`arch_prctl(ARCH_SET_FS, tp)`. OpenSillicon must replace/adapt both this FS
setter and any required thread-ID setup to native interfaces. It must not
invoke Linux syscall 158 or inherit Linux syscall semantics. Single-threaded
startup can omit clone/futex/thread-creation support, but it cannot omit
initial TLS/TCB setup when the CRT runs.

The kernel owns and saves each process's user FS-base as architecture state;
musl owns the user-memory TLS/TCB layout and initializes it at CRT startup.
The kernel must capture and restore FS base across user entry, preemption,
fork, exec, and return to Ring 3 using the lifecycle in ADR-023. The native
mechanism by which musl asks the kernel to set its own initial FS base is
syscall 20, specified in ADR-031. Failed exec preserves the old address space
and saved FS base; successful exec resets the replacement image's saved FS
base to zero and replaces both together.
Fork's existing eager private-page clone gives the child a private copy of
the TCB/TLS bytes at the inherited address and copies the saved FS-base value.

PT_TLS requirements for the loader are `p_filesz <= p_memsz`, a file-backed
template of `p_filesz` bytes, a non-overflowing memory range, and power-of-two
alignment after normalization. ELF permits `p_align` 0 or 1 to mean no
additional alignment, but musl 1.2.6's `static_init_tls` applies `p_align-1`
as a mask and is not safe if raw zero is passed through. Therefore a native
port must either normalize zero to one in a reviewed musl adaptation or reject
zero-alignment PT_TLS images; it must never pass raw zero into that code. The
user mapping must be writable and non-executable, satisfy normalized
`p_align` and musl's 8-byte minimum, copy the template exactly, and zero-fill
the remainder. Permit at most this one static TLS module; reject
unsupported/malformed cases before exec commit. This tightens ADR-023's
provisional allowance of raw zero alignment, pending the implementation-stage
choice between normalization and rejection.

Verified auxv contract

Musl's startup source scans auxv pairs through `AT_NULL`. The following
distinguishes direct startup consumption from useful optional metadata:

| Entry | Stage 14 contract | Source use/value |
|---|---|---|
| `AT_PHDR` | Required | Runtime address of the complete readable program-header table; `static_init_tls` scans it. |
| `AT_PHENT` | Required | Validated ELF `e_phentsize`; used to stride through the table. |
| `AT_PHNUM` | Required | Validated ELF `e_phnum`; bounds the scan. |
| `AT_PAGESZ` | Required | Kernel page size (4096); stored as `libc.page_size`. |
| `AT_NULL` | Required | Zero type/value terminator for auxv. |
| `AT_RANDOM` | Optional for startup, required for a hardened entropy claim | `__init_ssp` consumes it when provided; mallocng can also use its bytes. Do not fabricate. Musl has a deterministic fallback, which is not a substitute for entropy. A real kernel entropy source and policy must be approved before claiming randomized canaries. |
| `AT_HWCAP` | Optional; pass zero if no feature data is exposed | Sets musl's hardware-capability mask; zero disables capability-based dispatch. |
| `AT_ENTRY` | Not read by this static CRT; may be supplied | Validated ELF entry address, useful to consumers but not required by startup. |
| `AT_BASE` | Omit for static ET_EXEC | Dynamic-linker load base; not needed without an interpreter. ADR-023's zero value is harmless but not required. |
| `AT_EXECFN` | Optional; omit when `argv[0]` is non-NULL | Fallback program name only. |
| `AT_SYSINFO`, `AT_SYSINFO_EHDR` | Omit | Linux vsyscall/vDSO facilities are not part of this profile. |
| `AT_UID`, `AT_EUID`, `AT_GID`, `AT_EGID`, `AT_SECURE` | Omit while there is no identity/security model | Musl's zero-initialized aux array makes equal IDs and `AT_SECURE=0`; do not synthesize a privileged identity. Add only with a separately approved identity contract. |

Other auxv entries are not part of the initial CRT contract unless a measured
program or selected libc operation demonstrates a need. In particular, the
kernel must not imply Linux vDSO, dynamic-loader, or process-identity support.

Representative ELF verification

A small program with initialized and zero-initialized `__thread int` values
was linked using the musl 1.2.6 static wrapper on the host. `readelf` reported
ELF64 little-endian `ET_EXEC`, machine x86-64, entry `0x400263`, eight
program headers, and no `PT_INTERP`, `PT_DYNAMIC`, or dynamic relocations.
Its PT_TLS was `p_offset=0x1fc4`, `p_vaddr=0x402fc4`, `p_filesz=4`,
`p_memsz=8`, `p_align=4`. The initialized TLS symbol was at TP-8 and the
zero-initialized symbol at TP-4; disassembly accessed them via `%fs:-8` and
`%fs:-4`. The program header table was mapped at `0x400040` through the first
PT_LOAD. This validates musl's static CRT/ELF/TLS expectations, not
OpenSillicon syscall compatibility: the probe ran only on Linux.

Static link profile: fixed-address x86_64 `ET_EXEC`, `-static`, no PIE,
`crt1.o` plus `libc.a`, and compiler support libraries as required by the
compiler. The representative executable had a writable, non-executable GNU
stack declaration and no dynamic section. The OpenSillicon linker/build
configuration must be verified separately; do not assume Linux-targeted
linker output is accepted by the kernel loader.

Native adaptation boundary and deferred functionality

Upstream's x86_64 `arch/x86_64/syscall_arch.h`,
`arch/x86_64/bits/syscall.h.in`, and `src/internal/syscall.h` (including
`__syscall_ret`) assume Linux syscall instructions, numbers, argument/result
conventions, and negative errno. These are the
primary native port adaptation points, together with libc syscall wrappers
and `src/thread/x86_64/__set_thread_area.s`. Port code must issue OpenSillicon
`int 0x80`, preserve its existing argument registers and positive-errno/CF
contract, and map only explicitly supported operations. Do not use Linux
syscall numbers or add a Linux compatibility layer.

The initial profile remains single-threaded static x86_64 ET_EXEC. Dynamic
linking, shared libraries, PIE/ASLR, multiple threads, multiple TLS modules,
futexes, Linux vDSO, Linux syscall compatibility, and broad POSIX compatibility
are deferred. Memory allocation, libc VFS/FD adaptation, process/signal
adaptation, the native FS-base setter, and reproducible cross-userland build
configuration remain later Stage 14 decisions. A representative musl binary's
actual syscall requirements must be measured before freezing that surface.

---

ADR-025 — musl 1.2.6 Security Baseline and Patch Review

Status: Accepted as a development/porting baseline only; not approved for
distribution

Context

ADR-024 pins upstream musl 1.2.6 for source and ABI verification. Stage 14D
checked for a newer stable release, reviewed security reports against the
exact archive, and validated candidate fixes in temporary trees outside the
repository. This decision does not claim musl is fully secure or change the
native OpenSillicon ABI.

Source identity and release status

Retain upstream musl 1.2.6 from
`https://musl.libc.org/releases/musl-1.2.6.tar.gz`, SHA-256
`d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`.
The archive hash matches ADR-024's detached-signature-verified source. Stage
14D obtained a fresh copy and confirmed its hash. At review time
(2026-10-31), the official `musl-1.2.7.tar.gz` URL returned HTTP 404, and
upstream's `master` was commit
`7813e9119eb5fe681b6750b8a033a0490bca0941`; no newer official stable archive
was available. Recheck upstream before each future baseline refresh.

The source archive's main license is MIT, with additional file-level notices
described in its `COPYRIGHT`; retain that file and all applicable notices as
required by ADR-024. The patches below are modifications to upstream files
and do not replace those notices.

Security findings and patch series

The selected development tree is reproduced by applying these patches in
order to the exact archive above. Patch files were kept outside the project;
the hashes identify the reviewed artifacts and must be checked before any
future use.

1. `regcomp` integer-overflow checks (CVE-2026-107891). Origin: upstream
   advisory attachment, authored by Szabolcs Nagy, 2026-04-23; upstream
   commit `7813e9119eb5fe681b6750b8a033a0490bca0941`
   ([commit](https://git.musl-libc.org/cgit/musl/commit/?id=7813e9119eb5fe681b6750b8a033a0490bca0941),
   [advisory](https://www.openwall.com/lists/musl/2026/10/21/1)). Patch SHA-256:
   `0cf0090ab433e8f9ed08ae03a0c93aff557c1d3e347260f62a61a10281f5333b`.
   Changes `src/regex/regcomp.c`; bounds regex input and adds checked sizing
   and allocation paths. Oversized expressions can now fail with
   `REG_ESPACE`; no ABI change. The patch applied and is in current upstream
   master.
2. GB18030 `iconv` denial-of-service and invalid mapping handling
   (CVE-2026-6042). Origin: upstream commit
   `67219f0130ec7c876ac0b299046460fad31caabf`
   ([commit](https://git.musl-libc.org/cgit/musl/commit/?id=67219f0130ec7c876ac0b299046460fad31caabf),
   [advisory](https://www.openwall.com/lists/oss-security/2026/04/03/2));
   authored by Rich Felker, 2026-03-30. Patch SHA-256:
   `444fa70e52ca158fb7d4bad560637790bbf8f72e80b82fff840dd66fa83091e3`.
   Changes `src/locale/iconv.c` and adds generated mapping data in
   `src/locale/gb18030utf.h`. It replaces an expensive table scan and corrects
   invalid mapping results; no ABI change.
3. `qsort` workspace and shift hardening (CVE-2026-40200). This ordered bundle
   contains upstream commits `228da39e38c1cae13cbe637e771412c1984dba5d`,
   `b3291b9a9f77f1f993d2b4f8c68a26cf09221ae7`, and
   `5122f9f3c99fee366167c5de98b31546312921ab`, authored by Rich Felker on
   2026-04-09 and Luca Kellermann on 2026-04-10
   ([CVE notice](https://www.openwall.com/lists/musl/2026/04/10/3),
   [Debian tracking](https://security-tracker.debian.org/tracker/CVE-2026-40200)).
   Bundle SHA-256:
   `1ee29f64f9ca8e8ad7c349779d661ff6b52126a27575d3586981357a52c406fb`.
   Changes `src/stdlib/qsort.c`; it fixes the trailing-zero corner case and
   adds workspace-index and shift-count guards. The reported 64-bit extreme
   requires impractical memory, but the upstream hardening is included. No
   ABI change.
4. BRE backreference state correction for multibyte `regexec` input. Origin:
   Szabolcs Nagy's proposed mailing-list patch, 2026-10-27, Message-ID
   `<30a7f9efc7f1563a355579fcd23589e851dee5de>`
   ([report](https://www.openwall.com/lists/musl/2026/10/26/1),
   [patch discussion](https://www.openwall.com/lists/musl/2026/10/27/6)).
   Patch SHA-256:
   `6cfe87c3794562841a80fee8d1cd1b99d5ec67d4565f459b3812760912fe97ff`.
   Changes `src/regex/regexec.c`, recomputing the multibyte position state on
   backtracking retries and using the correct byte start for backreference
   comparison. It is not present in the reviewed upstream `master` and no
   accepted upstream commit was found. Treat it as a local candidate, not an
   upstream fix. It has no intended ABI change. Its current tests establish
   the reported failure family but do not prove the matcher free of other
   out-of-bounds reads.

All four patches applied to a clean copy of the pinned archive. The complete
host musl configure/build/install succeeded both before and after patching
with `./configure --prefix=<temporary-prefix> --disable-shared`, `make -j4`,
and `make install`. Relevant separate `libc-test` cases and focused probes
passed as detailed below. Patch application and compilation are not by
themselves proof of security.

Validation results and limits

- For CVE-2026-107891, source inspection confirmed the checked arithmetic and
  allocation changes. Nested-overflow and oversized-pattern probes returned
  `REG_ESPACE` in original and patched builds under the available memory
  limits; UBSan emitted no diagnostic for the exercised patterns. The probe
  did not reproduce memory corruption or distinguish old from new behavior,
  so this is patch/build validation, not an exploit regression proof.
- For the `regexec` issue, an isolated ASan build used a subject ending at a
  protected page. The original source faulted on the reported backreference
  path; the patch completed with expected offsets. Additional mixed ASCII and
  multibyte cases exposed stale offsets or missed matches in the original and
  produced expected results in the patched copy. This is focused evidence,
  not complete bounds proof. Valgrind was unavailable.
- For CVE-2026-6042, the reported bytes `82 35 8f 33` converted to the same
  output, while the patched build reduced the local CPU measurement from
  126 ms to below timer resolution. An invalid GB18030 sequence accepted by
  the original was rejected by the patched build. Host timing is indicative,
  not a portable performance guarantee.
- For CVE-2026-40200, the upstream functional `qsort` test and a million-item
  ordering probe passed on both builds. A UBSan probe found the old
  trailing-zero corner case and invalid full-width shifts; the patched code
  returned the expected value without diagnostics. The extreme-size failure
  was not reproduced because its memory requirement is impractical.
- Selected separate `libc-test` cases passed: regex backreferences, escaped
  high byte, negated range, bracket case-folding, `REG_NOSUB`, iconv
  roundtrips, and qsort. The test sources are from libc-test commit
  `7b95dfa5f5d5ca4d949221e0228ccc290bacc14e`. From that checkout, the
  following command ran the eight static tests, all of which returned zero:

      for t in src/regression/regex-backref-0-static.exe src/regression/regex-ere-backref-static.exe src/regression/regex-escaped-high-byte-static.exe src/regression/regex-negated-range-static.exe src/regression/regex-bracket-icase-static.exe src/regression/regexec-nosub-static.exe src/regression/iconv-roundtrips-static.exe src/functional/qsort-static.exe; do (cd /tmp/opensillicon-musl14d/libc-test && "$t") || exit; done

  The full `libc-test` suite was not run. The 1.2.6 release archive has no
  bundled test directory or check target.

The scoped distribution-tracker review also listed CVE-2025-26519, an iconv
out-of-bounds write fixed in upstream 1.2.6, and the three issues above. The
sources checked did not identify another later applicable issue in the
reviewed regex, ELF startup, or TLS areas. This is not a complete audit of
musl.

No patch changes x86_64 CRT startup, TLS/TCB layout, PT_TLS handling, FS-base
setup, syscall wrappers, allocator interfaces, or ELF assumptions. The
Stage 14B contract in ADR-023 and source evidence in ADR-024 remain unchanged.

Decision and use limit

Use the exact 1.2.6 archive plus the ordered four-patch series above only as a
reproducible development and porting baseline while the native port is
implemented and tested. Do not distribute it or describe it as security
qualified. Distribution requires rechecking upstream releases and advisories,
replacing local candidates with accepted upstream fixes where possible, and
reviewing and testing the complete final patch set on the target profile.
Remaining risks include unreviewed musl code, incomplete full-suite and
fuzzing coverage, the unmerged `regexec` candidate, and possible issues outside
the reviewed areas. CVE-2025-26519's iconv out-of-bounds write is fixed in
upstream 1.2.6 and is not regressed by the reviewed patch set.

---

ADR-026 — Native musl Port Integration Architecture

Status: Stages 14F and 14G findings recorded. Clang/LLD is the recommended
freestanding toolchain candidate; runtime selection and target integration
remain unresolved and require separate validation and approval.

Context and verified source facts

ADR-022 selects a native OpenSillicon musl port, not Linux syscall
compatibility. ADR-023/024 define the static ET_EXEC startup and musl 1.2.6
x86_64 Variant-II TLS contract. ADR-025 pins the security-reviewed 1.2.6
development source, including three upstream fixes and a separately marked,
unaccepted `regexec` candidate. This ADR does not change that security status.

The current Makefile uses system GCC and GNU ld for the kernel and separately
builds freestanding, no-libc userspace programs with custom entry points and
linker scripts. `user/os.c` invokes OpenSillicon syscalls directly. Stage 14G
verified GCC 16.2.1 (configured target `x86_64-redhat-linux`), Clang 22.1.8
(host target `x86_64-redhat-linux-gnu`), LLD 22.1.8, GNU binutils 2.46.1,
and LLVM tools 22.1.8. The only installed `x86_64-*-gcc` drivers found are
Fedora host-target aliases; no `x86_64-elf-gcc` cross compiler was found.
GCC's host `libgcc.a` exists. Clang's host compiler-rt builtins archive also
exists at `/usr/lib/clang/22/lib/x86_64-redhat-linux-gnu/`, but Clang reports
missing `libclang_rt.builtins.a` paths for the tested non-Linux triples.
Neither installed runtime is yet selected as an OpenSillicon target runtime.

The current kernel ABI is syscalls 0–19, `int 0x80`, syscall number/result in
RAX, arguments in RDI, RSI, RDX, R10, R8, R9, and positive errno in RAX with
CF set. The dispatcher currently defines `ENOSYS` as 78, while pinned musl's
generic errno header uses Linux value 38. Most currently used errno values
coincide, but this discrepancy must be handled explicitly in the native
adapter or target errno definitions.

The current ELF loader accepts fixed-address x86_64 ET_EXEC and rejects
PT_DYNAMIC, PT_INTERP, and PT_TLS. Stage 14I-C now validates and tracks the
readable PT_LOAD-backed program-header table and constructs the ADR-030
argc/argv/envp/auxv stack. Current process state still has no FS-base field;
interrupt entry/return does not capture or restore user FS base. The process
model is single-threaded, eagerly copies private pages on fork, and supports
path-only `exec_path`. VMM exposes page mapping primitives and shared/private
mapping internals but no public private mmap/munmap syscall.

The VFS exposes open, close, read, write, path stat, and one-entry-at-a-time
readdir through syscalls 7–11. Standard input is canonical TTY; descriptors
1/2 write to the framebuffer/serial console. The descriptor table has 32
slots. `write` is limited to 4096 bytes per call. There is no writev, ioctl,
fstat, lseek, poll, clock query, or general private mapping syscall. Signals
are limited to SIGINT, SIGTERM, and SIGKILL with OpenSillicon-specific
sigaction/restorer semantics.

Source provenance and Stage 14F bootstrap

Track port metadata, not an unreviewed source snapshot:

    third_party/musl/
      README.md
      manifest/source.json
      manifest/patches.json
      patches/
        upstream/0001-regcomp-overflow.patch
        upstream/0002-iconv-gb18030.patch
        upstream/0003-qsort-hardening.patch
        candidates/regexec-backref-candidate.patch
      scripts/common.py
      scripts/bootstrap.py
      scripts/verify.py

Stage 14F implements the provenance layout under `third_party/musl/`: JSON
source and patch manifests, exact reviewed patch artifacts, scripts, and a
README. Generated archive and source trees remain under
`build/musl-bootstrap/` by default. The bootstrap verifies the archive and
patch hashes, verifies the extracted version and `COPYRIGHT`, applies only
the three confirmed patches to a copy of pristine source with zero fuzz, and
keeps the local `regexec` candidate separate unless explicitly requested for
an experimental tree. The candidate is never applied to the default patched
tree. The verification script replays the confirmed series against a clean
extraction without configuring or compiling musl. The bootstrap does not
install musl or modify system state. `third_party/musl/README.md` documents
the commands and notice-preservation rules. No upstream source archive or
extracted source is tracked.

Target and toolchain proposal

musl 1.2.6's `configure --target` parsing reduces the target string to an
architecture such as `x86_64`; it does not select an operating-system port.
The Makefile selects `arch/$(ARCH)` and generic sources. The x86_64 tree
contains reusable CPU ABI components (atomic instructions, TLS layout,
register conventions, CRT entry), but also Linux-specific syscall assembly,
Linux syscall-number definitions, Linux signal/stat/mmap headers, and a Linux
`arch_prctl` FS setter. A plain Linux x86_64 target is therefore not an
OpenSillicon target.

Keep `ARCH=x86_64` for CPU-specific code. Put OS behavior in a separate
OpenSillicon port configuration; do not create a new CPU architecture
directory. Stage 14G recommends Clang 22.1.8 with
`--target=x86_64-unknown-none-elf`, Clang's integrated assembler, and explicit
direct invocation of LLD 22.1.8. Use `llvm-ar` and `llvm-ranlib` explicitly
for target archives. This is a freestanding code-generation candidate, not a
complete OpenSillicon SDK or OS target. Clang also accepts
`x86_64-unknown-opensillicon`, but it defines no `__opensillicon__` macro and
selects no OpenSillicon headers, SDK, or runtime. All three tested triples
still search host `/usr/local/include` and `/usr/include` by default; use
`-nostdinc` and explicit target headers/sysroot during a port build. The
tested `x86_64-unknown-elf` triple canonicalizes to
`x86_64-unknown-unknown-elf`.

The system GCC can compile and link the integer-only freestanding proof with
`-ffreestanding` and `-nostdlib`, but its target remains
`x86_64-redhat-linux`; it defines Linux macros and emits Linux-specific stack
protector access through `%fs:0x28`. It is not a native-target compiler.
A separately configured cross GCC remains possible, but is not available
or selected. Do not let musl configure silently select host `-lgcc`, libc,
startup files, or Linux headers.

In this Fedora installation, `clang -### -fuse-ld=lld` reports a link command
through `/usr/bin/gcc` and `collect2`; the proof therefore invoked `ld.lld`
directly with a custom linker script, rather than relying on driver routing.
The Clang `-rtlib=compiler-rt` lookup for the tested non-Linux triples points
to absent triple-specific archives. The host compiler-rt archive and GCC
`libgcc.a` each contain x86_64 helpers, but the target runtime choice remains
open; any helper archive must be selected explicitly and audited rather than
inferred from a successful compiler invocation.

Stage 14G proof and findings

Installed tool inventory (Fedora 44): GCC and G++ 16.2.1-2 at
`/usr/bin/gcc` and `/usr/bin/g++` (invoked through `/usr/lib64/ccache` in this
shell), configured target `x86_64-redhat-linux`; Clang 22.1.8-4 at
`/usr/bin/clang`; LLD 22.1.8-4 at `/usr/bin/ld.lld`; LLVM `llvm-ar`,
`llvm-ranlib`, `llvm-objcopy`, `llvm-readelf`, and `llvm-nm` 22.1.8;
GNU binutils 2.46.1-1 (`as`, `ld.bfd`, `ar`, `ranlib`, `objcopy`, `readelf`,
and `nm`). The `x86_64-redhat-linux-gcc` and G++ names resolve to the same
host-target GCC. No bare-metal or OpenSillicon cross compiler was found.
G++ is not relevant to the C-only musl profile.

Clang target probe:

| Triple passed to Clang | Reported target | Relevant macros and result |
|---|---|---|
| `x86_64-unknown-none-elf` | `x86_64-unknown-none-elf` | `__x86_64__`, `__LP64__`, `__ELF__`; no Linux or OpenSillicon OS macro |
| `x86_64-unknown-elf` | `x86_64-unknown-unknown-elf` | Same full macro set as `none-elf` |
| `x86_64-unknown-opensillicon` | `x86_64-unknown-opensillicon` | Same full macro set; no `__opensillicon__` and no OpenSillicon SDK/runtime |

The sorted complete preprocessor macro output for all three triples had SHA-256
`a396b29d59e21ac3cbd6de70292931ac97a5b2de16ed68f71f35f046f249e1a6`. Clang
produced identical C objects, assembly objects, and final ELF bytes for these
triples in the tested fixture. Thus the custom OS spelling did not create a
different ABI or ELF for that code; it is metadata/configuration only. This
does not generalize to every target-dependent feature: stack-protector probes
showed platform-specific code generation (see below).

`clang -v -E` showed host include search paths `/usr/local/include`, the
Clang resource headers, and `/usr/include` for each non-Linux triple. The
proof used `-nostdinc`; a future build must supply only the selected musl and
OpenSillicon target headers. `clang -### -c entry.S` invoked `-cc1as`,
confirming the integrated assembler path for this probe.

The temporary fixture in `/tmp/opensillicon-stage14g-oi9hehip/` exercised
integer arithmetic, nested calls, stack locals, initialized global data,
read-only data, BSS, and a word-copy loop. Its source identities are:

| Fixture | SHA-256 |
|---|---|
| `proof.c` | `07dd97c8d54cda32a236059a1be2e31d012d098e43ab9e52e513e628a26796f5` |
| `entry.S` | `811bab9e975062657915b2a5bbf6ce42e24bb34337bb2381d9e6f380a798499b` |
| `proof.ld` | `178941f6742f4b85ac0c17d5aa738f1d2111aecdef893c49bfeabda1d86c062d` |

The entry stub clears RBP, passes the incoming RSP in RDI, aligns RSP down to
16 bytes, calls `proof_main`, then halts. This matches the project's current
assembly-to-C startup alignment pattern. The C flags used for the integer-only
proof were:

    -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables
    -m64 -mno-red-zone -mcmodel=small -mno-sse -mno-sse2 -msoft-float
    -Wall -Wextra -Werror -nostdinc

For Clang, the reproducible commands (from a fresh temporary directory with
those fixture files) were:

    clang --target=x86_64-unknown-none-elf -std=c11 -O2 -ffreestanding \
      -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
      -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 \
      -mno-red-zone -mcmodel=small -mno-sse -mno-sse2 -msoft-float \
      -Wall -Wextra -Werror -nostdinc -c proof.c -o proof.o
    clang --target=x86_64-unknown-none-elf -m64 -mno-red-zone -ffreestanding \
      -fno-pic -fno-pie -c entry.S -o entry.o
    ld.lld -m elf_x86_64 -static --build-id=none -z max-page-size=0x1000 \
      -T proof.ld entry.o proof.o -o proof.elf

The Clang proof was repeated twice from separate clean temporary build
directories. Both produced C object SHA-256
`b181d0553cf2ca608e1d53837215d3aa496eb01e7cb1645bbb0a19a7854b40b5`, assembly
object SHA-256 `df2b3ce6d375e984c81c67ad28b2c055a7f31e9d4bf44af38f899dfd3fa9e07d`,
and executable SHA-256
`a458cff41327a130572efc4efd078fb18df825402165982220566f45648c6af3`.
All three tested Clang triples produced those same hashes.

The GCC comparison used these commands, with the same C flags but without a
target argument, GCC's GNU assembler, and direct GNU `ld`:

    gcc -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
      -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
      -fno-unwind-tables -m64 -mno-red-zone -mcmodel=small -mno-sse \
      -mno-sse2 -msoft-float -Wall -Wextra -Werror -nostdinc \
      -c proof.c -o proof.o
    gcc -m64 -mno-red-zone -ffreestanding -fno-pic -fno-pie \
      -c entry.S -o entry.o
    ld -m elf_x86_64 -static --build-id=none -z max-page-size=0x1000 \
      -T proof.ld entry.o proof.o -o proof.elf

The GCC driver link variant was also run with:

    gcc -nostdlib -static -no-pie -Wl,-T,proof.ld \
      -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 \
      entry.o proof.o -o proof.elf

The clean-repeat C object hash was
`36eedff12b840cf2326aa0a433507860a05e64c6eda1432e557f645495348437`, assembly
object hash `98aef3f9a3775caf56452c50136397be57c521cfb67893d37c1dc07ded77bf7e`,
and executable hash
`8c60199bf7184d7e696b1504a379152645b7b179dd912de79aaab9e0005fdaa8`. Both
clean GCC builds and the GCC `-nostdlib` driver link reproduced that ELF hash.
The sample had no unresolved symbols and no host startup or libc dependency.
This demonstrates that `-ffreestanding` plus `-nostdlib` can produce a small
standalone ELF when the program needs no external helpers; it does not change
GCC's Linux target macros, headers, multilib, or runtime configuration.

The Clang ELF is ELF64 little-endian ET_EXEC for x86-64, System V OSABI, entry
`0x400000`, with two 4 KiB-aligned PT_LOAD segments: RX at virtual address
`0x400000` and RW at `0x401000`. `.text` and `.rodata` are in RX; `.data` and
`.bss` are in RW. There is no PT_INTERP, PT_DYNAMIC, dynamic section, final
relocation, GOT, or PLT, and `nm -u` is empty. Object relocations were
`R_X86_64_PC32`; the static linker resolved them. No GNU stack header was
emitted; the current process stack mapping is user-writable and non-executable.
The entry stub aligns the stack before its C call, giving the callee the SysV
AMD64 16-byte call-site alignment.

By inspection, the load segments meet the current validator's fixed-address,
range, congruence, permission, and entry-in-executable-segment checks. The ELF
was not passed through the kernel loader or run in QEMU; its entry deliberately
halts after the C probe. The two ELF/program headers are at file offset 64,
outside the first PT_LOAD (which starts at file offset `0x1000`). The current
loader reads program headers from the file and does not require them mapped,
but a musl startup contract using `AT_PHDR` will need a future linker layout
that maps the program-header table. The current loader also rejects PT_TLS and
does not construct the standard auxv stack, so this ELF proof is not evidence
that a musl executable can run.

Compiler-runtime probes found these requirements and limits:

- A 128-bit unsigned division emits `__udivti3`. Linking without a runtime
  fails with that unresolved symbol. Explicitly adding either the installed
  host compiler-rt builtins archive or host `libgcc.a` resolves this helper in
  a direct LLD link. This tests that helper's x86-64 calling convention only;
  it does not qualify either host archive as the target runtime. The
  `__multi3` helper was present in the compiler-rt archive but not emitted by
  this fixture. 128-bit atomic helpers were not tested.
- A 512-byte C aggregate assignment emitted an unresolved `memcpy` even with
  `-ffreestanding -fno-builtin`. The static libc must provide the ordinary
  `memcpy`/`memset`/`memmove`/`memcmp` interfaces required by compiler output
  and source code; they are not supplied by compiler-rt. The basic proof had
  no such undefined symbol.
- With `-fstack-protector-strong`, Clang's non-Linux target referenced global
  `__stack_chk_guard` and `__stack_chk_fail`; Clang's Linux target and GCC
  emitted a canary load from `%fs:0x28` plus `__stack_chk_fail`. The kernel
  does not currently establish user FS-base or a canary contract, and neither
  builtins archive supplies the failure routine. Disable stack protection for
  the initial proof/profile or separately implement and review its guard and
  failure ABI before enabling it. Unwind tables were disabled; C++ exceptions
  and unwinding remain out of scope.
- `-mno-sse -mno-sse2 -msoft-float` was used only to keep this integer proof
  within the current kernel's lack of FPU/SSE context save/restore. These flags
  are not approved as blanket musl flags: x86-64 floating-point calling
  conventions and musl code may require SSE state. FPU/XSTATE handling or a
  clearly bounded integer-only userspace profile remains a prerequisite.

The current loader does not relocate dynamic objects; therefore future static
links must continue to resolve all relocations at link time and contain no
PT_INTERP/PT_DYNAMIC. No QEMU execution was attempted, and no musl configure,
build, or install was run in Stage 14G.

Keep kernel flags and userland flags separate and use explicit freestanding,
no-host-startup linking. Musl's configure options and wrapper generation must
be tested with the selected compiler rather than copied from its Linux build.
Build only static libc and CRT objects; omit the dynamic loader and shared
libc.

Native syscall adapter boundary

The OS adapter is libc-side. x86_64 `syscall_arch.h` currently issues the
`syscall` instruction and returns Linux raw values. The OpenSillicon adapter
must use `int $0x80`, capture CF, and convert the kernel's positive errno/CF
pair into musl's internal negative-errno raw result before common
`__syscall_ret` processing. It must use a target-local syscall table, never
Linux syscall numbers. Translate numeric errno differences (at minimum
OpenSillicon's current ENOSYS 78 versus musl's Linux ENOSYS 38) at this
boundary or through an explicitly reviewed target errno definition. This
normalization inside libc does not make the kernel ABI Linux-compatible.

| musl operation | Current Linux assumption | Existing OpenSillicon facility | Gap / initial disposition |
|---|---|---|---|
| `read`, `write`, `close` | Linux numbers and negative errno | 9, 2, 8; same first three argument registers | Required; map native numbers and normalize errors. Respect short transfers and the 4096-byte write limit. |
| `open` | `openat`, Linux flags and mode | 7; absolute VFS path and limited flags | Required for file access; translate only supported flags; relative paths and unsupported flags fail. |
| `stat` | `newfstatat`, Linux `struct stat` | 10 returns fixed size/mode/type | Limited path stat can be adapted; timestamps, device/inode details, and `fstat` are not represented and must not be fabricated. |
| directory traversal | `getdents64` records | 11 returns one fixed `vfs_dirent` per call | Adapt a libc iterator and translate fields; no Linux record ABI. |
| stdio output | `writev` | None; syscall 2 accepts one buffer | Needed for `printf`/`puts`; first probe can call `write()` directly. Before stdio support, adapt `__stdio_write` to write buffers sequentially with correct short/error handling, or separately approve writev. |
| terminal detection | `ioctl(TIOCGWINSZ)` | No ioctl or window-size API | Not required for direct `write`; adapt stdout initialization to skip the probe. Window sizing/isatty support remains limited. |
| process exit | `exit_group`, then Linux `exit` | 1 exits the single-threaded process | Required; map libc termination paths to syscall 1 and flush supported stdio first. |
| process ID | Linux `getpid` | 3 | Available with the current process meaning. |
| fork | `fork` or `clone`, signal masks and TID reset | 4 eager private-page fork | Later profile; replace musl fork hooks without Linux clone or signal-mask calls. Verify allocator state after fork. |
| exec | `execve(path, argv, envp)` | 12 accepts only a path | Not sufficient for general `execve`; initial path-based startup can be tested. A general wrapper must fail predictably until argv/envp ABI is approved; never discard arguments. |
| wait | `wait4` status/options/rusage | 6 waits a direct child and copies one 8-bit status | Limited adapter can encode supported exit status; options, rusage, and general child selection are unsupported. |
| signals | Linux `rt_sigaction`, masks, restorer frame | 15–17 limited three-signal model | Not needed for hello. Later adapter can expose only representable semantics; masks, `SA_RESTART`, and broad POSIX signals remain unsupported. |
| TLS thread pointer | `arch_prctl(ARCH_SET_FS)` | No current setter or FS process state | Required for musl startup; requires Stage 14B kernel FS lifecycle and separately approved setter ABI. |
| `set_tid_address` | Linux clear-child-TID registration | None; no kernel threads | Not needed for the single-thread profile; adapt `__init_tp` to process-local bookkeeping and omit kernel TID registration. Audit uses before enabling threads. |
| anonymous memory | `mmap`, `munmap`, `mprotect`, `mremap`, `brk` | No private user mapping ABI; 18/19 are shared mappings | Required by mallocng and possibly oversized TLS. Shared mappings are not a substitute. Requires private mapping ABI and allocator adaptation. |
| time/sleep | `clock_gettime`, `clock_nanosleep`, `nanosleep` | 13 sleeps in timer ticks; no clock query | Deferred for hello. A sleep adapter could round up to ticks only after resolution and interruption semantics are specified. No clock API is implied. |
| polling, vectored input, ioctl, sockets | Linux poll/readv/ioctl/socket families | No general equivalents | Deferred unless measured use requires a separately approved ABI addition. |

This table is an architecture audit, not a frozen musl surface. Measure a
representative static binary and transitive libc calls at later stages.
Unsupported functions must fail predictably rather than issue Linux-numbered
traps.

Memory-management proposal

Pinned musl's mallocng uses `brk` for an initial metadata area, anonymous
`mmap`/`mprotect` for metadata and allocation groups, `munmap` for release,
and optionally `mremap` for large reallocations. TLS uses anonymous private
`mmap` if its initial TLS/TCB area exceeds musl's builtin storage. The kernel
has no private user heap interface; `shm_create` is shared and inherited on
fork, so it is semantically wrong for malloc.

Prefer a small private anonymous mapping interface over a process-wide `brk`
initially. A `brk` heap needs collision rules with ELF, TLS, stack, and other
mappings, yet does not support mallocng's individually released large
allocations. The candidate scope is private user mappings with page-rounded
lengths, RW/NX or PROT_NONE permissions, anonymous/private only, optional
protection changes, exact unmap, and address-space cleanup on exec/reap.
Reject file-backed, shared, executable, overlapping fixed mappings, invalid
alignment, and arithmetic overflow. The VMM owns mapping metadata and
transactional map/unmap; fork eagerly copies these private pages; exec and
final address-space destruction release them. A dedicated private mapping
arena must be reserved separately from ELF segments, the shared-memory arena,
and stack. ADR-002 must record exact boundaries and guard policy before
implementation; this ADR deliberately assigns no numeric range. Adapt
mallocng to disable its `brk` path and use its allocate/copy fallback instead
of `mremap` until those operations are separately needed. Private map,
unmap, and protection semantics still require a new ABI decision; no syscall
numbers are assigned here.

Mapping allocations are zero-filled where anonymous mapping semantics require
it. Unmap releases the exact mapping; process exit/reap also cleans remaining
user mappings. Fork creates a private allocator snapshot under existing
eager-copy semantics. Cross-process shared allocations remain exclusively
the explicit syscall 18/19 feature.

CRT, ELF, TLS, and startup ownership

Keep the ADR-024 x86_64 Variant-II contract. The kernel ELF loader owns ELF
validation, PT_LOAD mapping, PT_TLS field/range/alignment validation, and
ensuring the complete program-header table is mapped. For the first native
profile, reject malformed PT_TLS and `p_align==0` rather than passing zero
into musl 1.2.6's alignment mask; accept `p_align==1`. Continue rejecting
PT_INTERP and unsupported dynamic linking. The kernel builds the ADR-030
initial stack and auxv, with empty argv/envp,
AT_PHDR/AT_PHENT/AT_PHNUM/AT_PAGESZ/AT_ENTRY, and AT_NULL. Do not fabricate
AT_RANDOM; the lack of a kernel entropy source remains a security limitation.

Musl CRT owns allocation and initialization of its TLS block, DTV, and TCB:
copy PT_TLS template bytes, zero the remainder, and set the self pointer as
verified in ADR-024. Musl's x86_64 hook requests FS-base installation through
a native syscall 20 setter specified by ADR-031. The kernel validates the
base as a user pointer, stores per-process FS state, captures/restores it on user
entry/return and context switches, and keeps the address space alive while
active. Fork copies the FS-base value and eagerly clones TLS privately;
successful exec commits new address space and initial FS state together;
failed exec preserves both; exit uses kernel-safe state and reap frees the
inactive image. Multiple threads and TLS modules remain excluded. These are
ADR-023/024 contracts, not current implementation facts.

The current `user/c_linker.ld` is a test script with explicit PT_LOAD
segments only and is not the musl linker contract. Add a separate static
ET_EXEC linker configuration during implementation: `_start` from musl
`crt1.o`, a readable PHDR table inside a PT_LOAD for AT_PHDR, PT_TLS attached
to the writable load image, non-executable stack metadata, fixed user
addresses, and no PT_INTERP/PT_DYNAMIC or PIE. Do not link `user/start.S` or
the custom freestanding user linker script into musl programs.

VFS and process adaptation

Initially support only explicit libc operations mapped in the table. Standard
descriptors map to TTY/console, preserving canonical input and current output
routing. Path adaptation begins with absolute paths and existing open flags;
descriptor count and errors remain kernel-defined. `stat` and directory
iteration translate only fields available in kernel structures. Do not claim
full POSIX file semantics.

For process APIs, direct `fork` can use syscall 4, but musl's Linux fork
wrapper's signal-mask and set-TID operations must be replaced. `waitpid` can
expose only direct-child blocking and supported exit status; no WNOHANG or
rusage. The custom `exec_path_argv` supports argv only; environment vectors
remain unsupported. It does not imply a Linux `execve` contract. Use only
SIGINT/SIGTERM/SIGKILL semantics supported by
the kernel; do not emulate Linux signal masks, process groups, or terminal
job control.

Sysroot and reproducible build proposal

Keep host build tools under `build/host-tools` and target outputs under a
separate `build/musl-opensillicon` tree. The target sysroot contains
`sysroot/usr/include` for installed musl headers and generated target bits,
`sysroot/usr/lib` for `libc.a`, `crt1.o`, `crti.o`, and `crtn.o`, and an
optional `sysroot/bin` only for target executables. Compiler, linker,
archiver, patch tools, and generators are host tools and do not belong in the
target sysroot. Runtime `/bin` programs belong in the SilliconFS root image,
not among host build tools.

Future clean build order: (1) verify pinned archive signature/hash; (2) verify
and apply three upstream security patches, then separately apply the labeled
candidate and OpenSillicon patch series; (3) configure explicit target,
ARCH=x86_64, compiler flags, CC/AR/RANLIB, linker, and compiler-runtime
archive; (4) build static musl and CRT; (5) install to a clean sysroot; (6)
link a static ET_EXEC with musl CRT, libc, approved compiler builtins, and the
dedicated linker script; (7) inspect ELF headers, program headers, undefined
symbols, and dependencies; and (8) package target programs into a test
SilliconFS image. Clean operations remove only generated build output, never
the source lock, patch files, or user worktree data.

Initial integration milestone

The first meaningful target is static `musl-hello`, with no malloc, stdio,
filesystem, fork, or signal use: `main` calls libc `write(1, message, len)`
and returns. It still requires a native-target libc/CRT build, valid
argc/argv/envp/auxv and PT_TLS handling, a working TCB/FS-base setter, and
termination through syscall 1. The existing kernel write and exit calls cover
the final output/termination operations, but loader, stack/TLS, FS-base, and
native syscall-adapter prerequisites are not implemented. A `puts` or
`printf` hello is a later target because musl stdout currently probes
`ioctl(TIOCGWINSZ)` and emits with `writev`, neither available in the kernel.

Proposed implementation stages and acceptance gates

- **14F — source bootstrap (implemented):** pin and verify the upstream source
  archive and patch artifacts; reproduce pristine and confirmed-patched trees
  without building musl. The default tree contains only three confirmed
  upstream patches; the local `regexec` candidate remains separately labeled
  and is opt-in for an experimental tree.
- **14G — target toolchain proof (completed):** Clang 22.1.8 with
  `x86_64-unknown-none-elf` plus direct LLD 22.1.8 is the recommended
  freestanding candidate. Integer-only C/assembly ET_EXEC output reproduced
  byte-for-byte without headers, CRT, libc, or runtime. Compiler runtime
  selection, FPU/SSE state, target headers, and musl configure integration
  remain unresolved; no project-wide toolchain was adopted.
- **14H — ABI/address decisions:** separately approve FS-base setter and
  private anonymous mapping/protection/free interfaces, errno translation,
  and mapping arena in ADR-001/002. Acceptance: no syscall number is assigned
  without approval; host tests cover mapping arguments, overflow and rollback.
- **14I — kernel startup/memory:** implement approved FS-base lifecycle,
  private mapping lifecycle, PT_TLS loading, and ADR-023 stack/auxv.
  Acceptance: host ELF/VMM tests and QEMU TLS, preemption, fork, exec rollback,
  and cleanup tests pass.
- **14J — native libc adapter:** implement syscall veneer and narrowly scoped
  allocator, TTY/stdio, VFS/FD, process, and signal shims. Acceptance: host
  ABI translation and operation-specific QEMU tests pass; unsupported calls
  return predictable errors.
- **14K — first static program:** build and boot `musl-hello`; verify output,
  exit status, no Linux runtime dependencies, and Phase 13 regression tests.
  Expand the libc profile only in separately reviewed increments.

Deferred and unresolved

Dynamic linking, shared libraries, PIE/ASLR, multiple threads, futexes,
multiple TLS modules, Linux syscall compatibility, broad POSIX claims,
general `execve` argv/envp, `writev`/ioctl unless libc adaptation proves
insufficient, file-backed mappings, kernel `brk`, `mremap`, clock_gettime,
poll, sockets, and broad signal semantics remain deferred.

Before implementation, separately resolve target triple/compiler runtime;
FPU/SSE state policy; native FS-base setter syscall; private anonymous map/unmap/protect ABI and
arena placement; errno translation including ENOSYS; static exec stack and
PT_TLS implementation; supported auxv and entropy policy; libc stdio and
VFS subset; and any argv/envp-capable exec requirement. This ADR changes no
syscall number or semantics and does not approve distribution of the ADR-025
development baseline.

---

ADR-027 — Native musl ABI Gap and Initial Profile

Status: Accepted as the Stage 14H design baseline; syscall additions and
implementation details remain subject to their own review

Context and verified implementation facts

ADR-022 selects a native musl port, not Linux syscall compatibility. The
current ABI is defined in `include/kernel/syscall.h` and dispatched by
`kernel/syscall.c`: `int 0x80`, number/result in RAX, arguments in RDI, RSI,
RDX, R10, R8, R9, and failure indicated by CF with a positive errno in RAX.
The current syscall assignments are 0 ABI version, 1 exit, 2 write, 3
getpid, 4 fork, 5 image-buffer exec, 6 waitpid, 7 open, 8 close, 9 read,
10 path stat, 11 readdir, 12 path-only exec, 13 sleep_ticks, 14 pipe, 15
kill, 16 sigaction, 17 sigreturn, 18 shm_create, and 19 shm_detach.
ADR-031 appends syscall 20 `set_fs_base`; numbers 0 through 19 and their
meanings remain unchanged.

OpenSillicon's kernel returns `ENOSYS` as 78 for an unknown call in
`kernel/syscall.c`; `include/kernel/errno.h` does not define ENOSYS, and
several syscall errors are defined locally in dispatch/process code. The
existing values used for EIO, EBADF, ENOMEM, EFAULT, EINVAL, EEXIST, EAGAIN,
EMFILE, ENOSPC, EPIPE, EOVERFLOW, EOPNOTSUPP, ETIMEDOUT, EINTR, ECHILD,
ESRCH, EACCES, ENOEXEC, and EBUSY match musl's generic x86_64 values.
The verified musl 1.2.6 `arch/generic/bits/errno.h` defines ENOSYS as 38;
its `arch/x86_64/syscall_arch.h` emits Linux `syscall`, and
`src/internal/syscall_ret.c` expects negative Linux error returns in the
range -1 through -4095. These are not the OpenSillicon convention.

Process state in `kernel/proc/process.c` is single-threaded. `fork` eagerly
clones private pages, inherits descriptors and shared mappings, and returns
child PID/zero. `waitpid` waits for a direct child and returns the child's
PID plus one byte of raw low-eight-bit status; no WNOHANG, options, rusage,
or POSIX wait-status encoding exists. `exec_path` accepts only a path, no
argv/envp; it requires a regular executable vnode and preserves the old
image on failure. Successful exec resets caught signal dispositions and
preserves ignored dispositions. `exit` stores the low eight bits, closes
descriptors immediately, retains a zombie address space until reap, and
reparenting is to the exiting process's parent. There is no gettid syscall.
In the single-thread profile, libc may define `gettid()` as `getpid()`;
`set_tid_address` has no required role until a thread/clear-child-tid
contract exists.

The ELF loader in `kernel/elf.c` accepts fixed-address ET_EXEC PT_LOADs,
rejects PT_DYNAMIC and PT_INTERP, validates one PT_TLS (treating a structurally
empty PT_TLS as no TLS image), excludes reserved arenas, and requires the
complete program-header table in readable PT_LOAD memory. User path launches
and successful exec construct the one-page ADR-030 initial stack. It contains
argc, argc argument pointers, a null argv terminator, a null envp, and the
existing auxv. The stack pointer is 16-byte aligned at entry. `_start` passes
argc, argv, and the initial-stack address in the first three C argument
registers to `user_main`, then invokes syscall 1. Legacy path-only launch
places argc zero and argv[0] NULL. ADR-031 and Stage 14I-D add per-process FS-base state, syscall 20,
and capture/restore on user entry and return.

The VFS and FD implementation (`kernel/vfs/vfs.c`, `kernel/fd.c`,
`include/kernel/vfs.h`) supports open/close/read/write, path stat, and
indexed directory iteration. `stat` is path-based only; descriptors have a
shared object offset, but there is no seek operation. Descriptor tables have
32 slots and are cloned at fork; descriptor references survive exec. TTY
input is canonical and blocks inside `kernel/tty/tty.c` using `hlt` after
polling; it is not a process scheduler wait and has no signal interruption
contract. Console writes go to the framebuffer terminal and serial output.
Kernel write/read syscalls cap a single transfer at 4096 bytes.

The signal subsystem (`kernel/proc/signal_state.c`, `process.c`) supports
SIGINT, SIGTERM, and SIGKILL, one pending bit per signal, default/ignore/
handler dispositions, one active handler, and a kernel-trusted saved frame.
It has no signal masks, sigaction flags, or POSIX rt signal ABI. Blocking
waitpid, sleep_ticks, and pipe operations use scheduler-aware blocking and
can return EINTR; TTY reads are outside that guarantee. No `writev`, ioctl,
fstat, lseek, mmap, munmap, mprotect, brk, clock query, entropy syscall,
fcntl, dup, dup2, or sched_yield syscall exists. Syscall 18/19 are only
anonymous shared mappings inherited through fork, not private allocator
mappings.

Errno policy

Decision: preserve the existing kernel errno values and CF/positive-RAX
syscall ABI. Implement a target-specific native musl syscall adapter that
recognizes CF, maps the returned OpenSillicon errno to musl's target errno,
and supplies musl's expected negative raw result to `__syscall_ret` (or an
equivalent target-local return helper). In particular, OpenSillicon ENOSYS
78 must become musl ENOSYS 38. The adapter must use an explicit mapping
table and tests; it must not pass the positive value through as success or
assume that all Linux errno numbers match. Existing values currently used
by the kernel are otherwise numerically aligned with musl's x86_64 errno
set. Future kernel error additions must update the adapter mapping and
tests.

This selects option B from the Stage 14H analysis. Changing ENOSYS to 38
would alter existing syscall ABI behavior; introducing a new shared errno
ABI would add a second contract without fixing existing callers. This
decision preserves current kernel/userspace behavior and localizes the
one verified numeric mismatch. No errno constant or syscall behavior is
changed in Stage 14H.

Private anonymous memory interface

Decision: the future allocator interface should be a minimal private,
anonymous `mmap`/`munmap` pair with a narrowly necessary `mprotect` operation
for the pinned musl mallocng path, rather than a brk-only heap. Do not assign
numbers here. The initial contract is page-rounded, overflow-checked,
zero-filled, user-private mappings with kernel-selected addresses in a
dedicated per-process user mapping arena disjoint from ELF PT_LOADs, the
initial stack/TLS, and the ADR-021 shared arena. Initially allow read/write
and inaccessible mappings, always NX; executable mappings are denied.
`munmap` must accept only page-aligned valid ranges and remove only the
caller's private mappings. `mprotect` may change RW versus inaccessible
state only, never grant execute or affect shared mappings. Invalid ranges,
collisions, and allocation failures return errors without partial visible
mappings; rollback returns allocated physical pages and metadata. Exit/reap
destroys remaining private mappings; fork eagerly copies them as private
pages; successful exec releases the old address space only at commit and
failed exec leaves it intact. These lifecycle details require a concrete
implementation ADR before syscall allocation.

The reason for this choice is source-verified musl 1.2.6 mallocng behavior
in `src/malloc/mallocng/malloc.c` and `glue.h`: it first attempts `brk`,
then obtains `PROT_NONE` anonymous private ranges, enables pages with
`mprotect`, and releases memory with `munmap`; it also has `mremap` paths.
ADR-026 proposed disabling brk and avoiding mremap, but a pair without
protection support does not match the default mallocng metadata allocation
path. The target port must either retain the small RW/PROT_NONE/mprotect
subset and disable `brk`/`mremap`, or carry a separately reviewed allocator
adaptation that maps writable memory directly. This ADR recommends the
first option as the smaller semantic change to upstream allocator behavior.
ADR-028 resolves the fixed mapping arena bounds. Partial-unmap metadata
remains an implementation detail to settle and test during Stage 14I-A. A
bounded heap alone is insufficient for unmodified mallocng, though a bounded
heap could be reconsidered if a reviewed allocator adaptation eliminates
those calls.

File and terminal adaptation

Decision: implement the first direct `write()` adapter using syscall 2.
`puts`/`printf` are a later milestone. musl 1.2.6 `src/stdio/__stdio_write.c`
uses `writev`, and `src/stdio/__stdout_write.c` probes `ioctl(TIOCGWINSZ)`;
these calls are not needed by a direct `write` program. For the initial
stdio milestone, prefer a libc-side `writev` adapter that iterates vectors
through syscall 2, checks vector/total-length overflow, and returns bytes
already written if a later vector fails. It must document that multiple
kernel writes are not atomic as one vector operation and may interleave.
If measured programs require single-operation atomicity or behavior that
cannot be represented, request a separate append-only syscall decision.

Do not add a general ioctl interface for initial printf. Provide a narrowly
defined terminal-size response for TIOCGWINSZ in a later libc/TTY decision,
or make the musl stdout adapter skip the probe and use the existing write
path. Unsupported terminal requests should report ENOTTY; this requires
adding that errno to the explicit adapter map. `tcgetattr`, terminal mode
changes, and terminal control requests remain unsupported. stdout/stderr
are currently synchronous writes (framebuffer plus serial); musl stdio
buffering remains libc behavior and must not duplicate output through a
separate serial adapter.

Process and signal contract

Decision: retain the existing process model and adapt only its supported
subset. `getpid` maps to syscall 3; the single-thread `gettid` result equals
that PID. `fork` maps to syscall 4. `exit` maps to syscall 1. `exec_path`
and syscall 5 do not implement POSIX execve: a future argv/envp-capable,
append-only exec ABI is required for Tier 3, preserving syscalls 5 and 12.
It must copy and validate path, argv, and envp before committing the new
image; failure leaves the old image, descriptors, and TLS untouched.
`waitpid` syscall 6 currently returns raw eight-bit status, not POSIX wait
encoding; a libc wrapper can only expose a documented OpenSillicon-specific
status until an approved ABI provides unambiguous normal-versus-signal
termination information. No WNOHANG, wait options, or rusage are promised.

musl's Linux `fork()` wrapper in `src/process/fork.c` masks signals and
coordinates multithread locks before `_Fork`; that behavior must be replaced
or reduced in the single-thread native port rather than implemented through
unsupported `rt_sigprocmask`. Existing fork copies caught/ignored actions
and clears pending signals. Successful exec resets caught/default actions,
preserves ignored actions, and clears active-handler state; failed exec
preserves all signal state. The initial native profile exposes only the
existing SIGINT/SIGTERM/SIGKILL subset and its supported handler/return
behavior. No signal masks, process groups, job control, terminal-generated
SIGINT, SIGPIPE, or broad POSIX signal semantics are included. Existing
scheduler-aware blocking calls can be interrupted with EINTR, but TTY read
cannot be promised interruptible until separately redesigned.

TLS, FS-base, and FPU/XSTATE

Keep ADR-023/024's verified musl 1.2.6 Variant-II TLS contract. Musl CRT
owns TLS block allocation/copy/zero-fill, DTV, TCB, and calling the target
thread-pointer setter. The kernel owns validating PT_TLS and the setter
argument, storing per-process FS-base, capturing user state at interrupt
entry, and restoring the selected process's FS-base before Ring 3 return.
Fork inherits the FS-base value while eager private-page cloning gives the
child an independent TLS copy. Exec installs TLS and FS-base atomically at
commit; failure preserves both. Exit switches away before the image is
released; reap frees only an inactive address space. No Linux arch_prctl
number or calling convention is adopted. The native FS setter still needs a
separate append-only ABI approval. `set_tid_address` is not needed for the
single-thread profile; thread exit/clear-child-tid remains deferred.

The kernel build in the current Makefile disables MMX/SSE/SSE2 and uses
soft-float. Inspection of `kernel/arch/x86_64/interrupts.S`, `context.S`,
and process switching finds no FXSAVE/FXRSTOR or XSAVE/XRSTOR state handling.
The inspected GDT setup does not configure CR0/CR4 for an FPU policy. Thus
the current code does not establish that userspace SSE is enabled or that
its state survives switching between processes; this is a verified gap.
Decision: before permitting preemptible floating-point/SSE userspace, use
eager per-process FXSAVE/FXRSTOR for the architectural x87/MMX/SSE baseline,
with explicit CPUID and control-register setup and kernel code kept free of
those registers. Prefer eager switching over lazy #NM switching for the
single-CPU/small-process model. XSAVE extensions remain deferred until
feature detection and a need for AVX-class state exist. The initial Tier 1
integer-only test must compile with SSE/MMX disabled and soft-float; this is
only a bootstrap restriction, not a claim of full x86_64 ABI support.
Before ordinary musl code generation, implement and test FPU state support
or explicitly build a documented integer-only target variant. Do not
assume ordinary x86_64 musl binaries are safe before that decision is
implemented.

Time and entropy

There is no clock_gettime or wall-clock syscall. The kernel timer counts
100-Hz LAPIC ticks calibrated against PIT in `kernel/arch/x86_64/timer.c`;
syscall 13 sleeps for ticks and returns EINTR when a non-ignored signal
interrupts it. This supports a future coarse relative nanosleep adapter by
rounding durations up to ticks, but cannot provide a POSIX timespec
remaining-time result precisely or CLOCK_REALTIME. Tier 1/2 must not claim
clock APIs until a monotonic clock interface and resolution contract are
approved. No trustworthy entropy source or getrandom syscall is present.
ADR-023 omits AT_RANDOM; musl mallocng uses an address-derived fallback
secret if AT_RANDOM is absent. That is not cryptographic entropy and must
not be described as such. Programs requiring secure random bytes fail with
ENOSYS/unsupported until an entropy source and ABI are approved. Do not
fabricate clock or random values.

Syscall compatibility inventory (musl 1.2.6, native profile)

“Startup” means CRT entry/initialization for the proposed static profile;
“simple” means a direct output program; “stdio” means puts/printf; “alloc”
means mallocng allocation; “threads” means musl threading. “Yes” below is
a requirement of the stated capability, not every program in that tier.
Linux operation names describe musl-facing interfaces, not syscall numbers
to pass to OpenSillicon.

| Operation | Startup | Write-only | stdio | Allocator | Threads | OpenSillicon now | Gap / initial profile |
|---|---|---|---|---|---|---|---|
| read | No | No | stdin use | No | No | syscall 9; <=4096, canonical TTY or file/pipe | Adapter; Tier 2 |
| write | No (except program call) | Yes | Yes underneath | No | No | syscall 2; <=4096, short count, console mirrored to serial | Native veneer; Tier 1 |
| writev | No | No | Yes; musl `__stdio_write` calls it | No | No | Missing | libc loop over write; Tier 2 |
| open | No | No | No | No | No | syscall 7; VFS path and limited flags/mode | map subset; Tier 2 |
| close | No | No | No | No | No | syscall 8; closes FD reference | Adapter; Tier 2 |
| stat/fstat | No | No | No | No | No | syscall 10 path stat only; size/mode/type | path stat adapter; fstat missing; Tier 2 |
| lseek | No | No | No | No | No | Missing, FD offset advances on reads/writes | New API for seekable files; Tier 2 extension |
| getdents/readdir | No | No | No | No | No | syscall 11 returns one indexed entry and advances FD | libc directory adapter; Tier 2 |
| ioctl | No | stdout probes `TIOCGWINSZ` in musl | Conditional stdio probe | No | No | Missing | bypass probe or narrow winsize request; Tier 2 |
| mmap | No | No | No | Yes (mallocng) | Yes | Only syscalls 18/19 for shared mappings | private anonymous mappings; Tier 2 |
| munmap | No | No | No | Yes | Yes | No private unmap; shm detach is distinct | private mapping release; Tier 2 |
| mprotect | No | No | No | Yes, mallocng protects metadata pages | Yes | Missing | RW/PROT_NONE only; Tier 2 |
| brk | No | No | No | Default mallocng attempts it | No | Missing | Disable in target mallocng config/port; not add kernel brk |
| set_fs_base (native equivalent) | CRT TLS startup | No | No | errno TLS may need FS | Threads | syscall 20; process FS capture/restore, no Linux arch_prctl ABI | native adapter; Tier 1 prerequisite |
| set_tid_address | No for one thread | No | No | No | Linux thread-exit clear-TID | Missing | Omit; threads deferred |
| getpid | No | No | No | No | useful | syscall 3 | Adapter; Tier 3 |
| gettid | No | No | No | No | Yes | Missing | libc returns getpid in single-thread profile |
| fork | No | No | No | malloc state naturally copied | Yes | syscall 4 eager copy; single thread | adapt musl lock/signal wrapper; Tier 3 |
| execve | No | No | No | No | Yes | syscall 12 path-only; syscall 5 image buffer | argv/envp ABI required; Tier 3 |
| wait4/wait | No | No | No | No | No | syscall 6 direct-child wait; one-byte raw status | limited wait wrapper; status contract decision; Tier 3 |
| exit/exit_group | CRT termination | Yes (return) | Yes | No | Yes for group | syscall 1, process exit only | exit mapping; exit_group equals exit only single-thread; Tier 1 |
| rt_sigaction | No for simple startup | No | No | No | Yes | syscall 16 custom two-pointer action | adaptation to supported actions; Tier 3 |
| rt_sigprocmask | No | No | No | No | Yes and musl fork wrapper | Missing | unsupported; replace port fork hooks; Tier 3 boundary |
| rt_sigreturn | Handler return only | No | No | No | Yes | syscall 17 trusted-frame custom return | musl restorer adaptation; Tier 3 |
| clock_gettime | No | No | No | malloc seed does not call it | No | Missing; internal 100-Hz ticks only | requires clock ABI; defer Tier 2 |
| nanosleep | No | No | No | No | No | syscall 13 sleep_ticks | coarse tick conversion; remainder semantics absent; Tier 2 decision |
| getrandom | No | No | No | optional malloc secret source is AT_RANDOM, not syscall | No | Missing | no entropy; unsupported until source/ABI; Tier 2+ |
| uname | No | No | No | No | No | Missing | useful metadata, not required; defer |
| fcntl | No | No | No | No | Yes for locks | Missing | no descriptor flags/locks; defer |
| dup/dup2 | No | No | No | No | No | Missing | descriptor alias API; defer Tier 3 |
| pipe | No | No | No | No | No | syscall 14, bounded blocking pipe, fork/exec inherited | Adapter can expose limited pipe; Tier 3 |
| sched_yield | No | No | No | No | Yes | Missing direct syscall; timer scheduler preempts | no need in single-thread profile; defer |

Tiered support profile

Tier 1 — first executable: static x86_64 ET_EXEC; musl CRT and ordinary
initial stack (argc/argv/envp/auxv); PT_TLS and Variant-II TLS/TCB; native
FS-base setter and lifecycle; direct write(1,...) and clean exit status.
It requires ELF/stack/TLS work and an adapter for syscalls 1/2 plus the
approved FS setter. It must use integer-only code until FPU state is safe.
It does not require allocation, stdio, VFS, time, randomness, signals, or
process creation.

Tier 2 — basic libc programs: Tier 1 plus puts/printf, stdin/stdout/stderr,
read/open/close/path stat/readdir, malloc/free, and bounded sleep. It needs
writev adaptation (or equivalent stdio writer), a decision to bypass or
provide TIOCGWINSZ, private anonymous mmap/munmap/mprotect, VFS field/flag
translation, and a documented tick-based sleep mapping. File seek/fstat,
clock_gettime, terminal attributes, and secure random remain unsupported
unless separately approved. Allocation requires mallocng configuration
that disables brk and mremap and the approved mapping interface.

Tier 3 — selected process/IPC functionality: fork, path+argv+envp exec,
direct-child wait, exit status translation, limited signals, pipe, and
descriptor duplication only if later justified. It needs an append-only
exec argument interface; explicit wait-status semantics; adapters replacing
musl's Linux signal-mask and multithread fork hooks; and wrapper definitions
for only supported signal actions. Existing `waitpid` status cannot be
advertised as POSIX until normal versus signal termination is unambiguous.
Threads, futexes, shared-library loading, dynamic linking, job control, and
broad POSIX/Linux compatibility remain excluded.

Deferred features and separate decisions

No syscall number is assigned by this ADR. ADR-031 separately resolved and
implemented the native FS-base setter and Ring 3 lifecycle. Remaining
decisions include: (1) the mmap/munmap/mprotect syscall ABI; (2) an
argv/envp-capable exec interface and startup argument policy; (4) wait
status representation; (5) whether Tier 2 includes a TIOCGWINSZ request or
changes the musl stdout probe; (6) clock_gettime semantics and a trustworthy
monotonic source; (7) an entropy source/getrandom policy; and (8) FPU state
save/restore and compiler flags. Existing syscall numbers and behavior are
preserved. No Linux syscall number, `syscall` instruction convention, or
negative Linux errno value is sent directly to the kernel.

Implementation order: establish native syscall/errno adapter tests without
changing kernel ABI; complete TLS/stack/FS-base and FPU prerequisites; add
private mapping semantics and allocator-specific tests; adapt basic file,
stdio, and time behavior; then separately approve process/exec/status and
signal adaptations. Each sub-stage must prove its own guest behavior before
the next tier is claimed. This ADR records Stage 14H design decisions only;
it changes no code, syscall assignment, errno value, or prior ADR contract.

---

ADR-028 — User Virtual Address Space Layout and Private Mapping Arena

Status: Accepted and implemented internally in Stage 14I-A; public mapping
syscall ABI remains open

Context and verified current layout

This ADR records the actual layout in `kernel/mm/vmm.c`, `kernel/elf.c`,
`kernel/proc/process.c`, and `include/kernel/shm.h`, then reserves a fixed
interval for private anonymous mappings. It resolves the arena-bounds
question left open by ADR-026/027. Stage 14I-A implements the internal
mapping facility without adding a syscall or changing the existing ABI.

The x86_64 canonical lower-half user range used by VMM is
`[0x0000000000010000, 0x00007ffffffff000)`. `vmm_map_page()` requires
page-aligned addresses, canonicality, and user mappings below `USER_END`;
user page-table lookup and copy helpers use the same lower bound. The
canonical lower-half limit is `0x0000800000000000`; kernel mappings begin at
`0xffff800000000000`. The interval between those canonical halves is not a
valid canonical mapping range. Kernel vmalloc occupies
`[0xffffc00000000000, 0xffffe00000000000)` and the kernel heap virtual range
is `[0xffffe00000000000, 0xfffff00000000000)`.

Current ELF validation rounds every PT_LOAD memory range outward to 4 KiB
page boundaries and rejects page-rounded overlap between load segments. It
accepts page starts at or above `0x0000000000010000`, with page ends no
greater than `0x00007fffffffc000`, subject to file-range, alignment,
permission, canonical/user and arithmetic checks. It excludes intersections
with both the private mapping arena and shared-memory arena. Thus addresses
throughout the lower user range below `0x600000000000` may be accepted for ELF segments,
although ordinary project images are linked near `0x400000`. The broad
loader range is an implemented acceptance rule, not a claim that all such
addresses are occupied.

The current process stack is one 4 KiB page at
`[0x00007fffffffd000, 0x00007fffffffe000)`. Its stack top is
`0x00007fffffffe000`. The page below it at
`[0x00007fffffffc000, 0x00007fffffffd000)` is unmapped; the page above the
stack at `[0x00007fffffffe000, 0x00007ffffffff000)` is also not mapped by
the stack path. ELF PT_LOAD ends at or below `0x00007fffffffc000`, leaving
the stack range outside the loader's accepted segment end. No stack growth
is implemented.

The approved shared-memory arena is
`[0x0000700000000000, 0x00007fffffffc000)`. `kernel/mm/vmm.c` uses a
per-address-space monotonic cursor, leaves boundary/inter-mapping guard
pages, and maps shared pages RW/NX with a software PTE marker. This is
implemented by syscalls 18/19 and is not available as private allocator
memory. The space above its end contains the current stack and a small
remaining user interval; it is not large enough for the native private
mapping arena.

TLS is not implemented. The ELF loader currently rejects PT_TLS. ADR-023
plans one private TLS mapping below the private mapping arena, in a gap with guard
pages; it does not reserve fixed TLS bounds. ADR-023's startup stack/auxv
are also planned, not implemented; the current stack remains one page. No
other user virtual region is reserved by current code. Stage 14I-A now adds
per-address-space private mapping metadata and internal map/unmap helpers;
public mmap/munmap syscalls remain undecided.

Decision: dedicated private mapping arena

Reserve the following page-aligned, canonical lower-half interval for
private anonymous mappings:

    PRIVATE_MAP_ARENA_START = 0x0000600000000000
    PRIVATE_MAP_ARENA_END   = 0x0000700000000000  (exclusive)

The arena is 16 TiB (`0x100000000000` bytes), immediately adjacent to and
below the already approved shared-memory arena. It lies below the canonical
lower-half limit, above the ordinary low ET_EXEC image addresses used by the
project, and leaves the entire existing shared arena and high stack area in
place. The split uses the existing `0x700000000000` shared-arena boundary
and the preceding 16-TiB aligned interval; it does not relocate existing
regions. The arena is virtual reservation only: physical pages are allocated
on demand, and the unused interval remains unmapped. The VMM must validate
every page against these bounds, canonicality, 4-KiB alignment, and checked
range arithmetic before touching page tables.

Future mapping allocation is per address space. A mapping's user PTEs are
private, user-accessible, writable, and NX by default. The kernel owns the
mapping metadata and each physical page; pages are zero-filled before they
become user-visible. A mapping must not overwrite a present PTE or any
reserved guard page. `fork()` creates private page copies at the same virtual
addresses under the existing eager-copy model; metadata is cloned only
transactionally with the child address space. Detach/unmap removes the
requested mapping's PTEs, completes required TLB invalidation, then frees
only its owned physical pages and metadata. Address-space teardown frees all
remaining private mappings once. Successful exec releases the old mapping
set only after replacement image setup and activation; failed exec retains
the old image and mappings. A zombie retains its address space until reap,
matching ADR-006.

No guard page is mapped as part of a private allocation. The arena boundary
pages remain unavailable to allocations as permanent edge guards, so the
usable placement interval is
`[0x0000600000001000, 0x00006ffffffff000)`. Adjacent mapping extents need
not have a guard page, because mallocng itself requests PROT_NONE pages for
its metadata protection scheme and explicit guard policies belong to the
eventual mapping ABI. The Stage 14I-A internal API
must initially support exact whole-mapping unmap only; subrange unmap and
partial metadata splitting are not required. The eventual mmap/munmap and
mprotect syscall ABI remains a separate decision under ADR-027; this ADR
assigns no syscall number and does not expose an internal operation to Ring 3.

ELF and TLS constraints

ELF validation rejects a PT_LOAD when its page-rounded half-open
range intersects `[PRIVATE_MAP_ARENA_START, PRIVATE_MAP_ARENA_END)`. It
must retain the existing overlap, permission, file-range, alignment,
canonicality, and overflow checks. A segment ending exactly at arena start
or starting exactly at arena end is non-overlapping, though the existing
ELF_USER_END means the present profile does not accept a segment at/above
the shared arena. Validate both rounded start and rounded end; do not test
only raw `p_vaddr`/`p_memsz`. Future stack construction must continue to
reject overlap with the fixed stack page and its unmapped guards.

The private arena is disjoint from the shared arena, stack, and lower ELF
range. To preserve ADR-023's TLS design, the future TLS placer searches only
the user interval below `PRIVATE_MAP_ARENA_START`, chooses a free page-aligned
range, leaves one unmapped guard page on each side, and excludes all
page-rounded PT_LOADs. No fixed TLS address or maximum TLS size is selected
here; those depend on PT_TLS validation and bounded startup-stack/TLS setup
in the later Stage 14I implementation plan. The large lower user interval
is sufficient in address-space capacity, but the loader must return ENOMEM
if no valid gap exists. Do not place TLS in this private mapping arena.

This reservation narrows the ELF loader's broad accepted address range:
ELF segments that intersect the newly reserved arena become invalid. Existing
project ELF layouts near `0x400000` remain valid. That compatibility change
is intentional and required to guarantee that future private mappings never
collide with executable segments. No current PT_LOAD addresses are moved.

Alternative evaluation

Option A, selected: reserve a fixed private arena and reject intersecting
ELF PT_LOADs. It gives deterministic bounds, simple collision checks,
predictable allocator behavior, straightforward fork metadata cloning, and
clear mapping tests. It costs part of the ELF loader's theoretical address
range, while preserving current conventional layouts.

Option B, dynamic first-fit from all user free gaps, is rejected for this
profile. It avoids narrowing ELF acceptance but needs gap discovery over
arbitrary segment/page-table layouts, cursor/fragmentation rules, and more
complex fork/exec rollback. The result is harder to test and makes future
allocator placement less predictable. The fixed 16-TiB interval provides
ample capacity without using dynamic collision search outside the arena.

Stage 14I-A implementation

- `address_space` owns a sorted private-mapping list and a monotonic
  allocation cursor. `vmm_private_map()` places from that cursor and never
  reuses detached ranges. It leaves the cursor unchanged on failure.
  Mappings are exact whole-range objects identified by base and original
  requested byte size; `vmm_private_unmap()` rejects wrong-size, stale, or
  partial ranges.
- Each mapping stores base, requested and rounded sizes, page count, and its
  owned physical-page array. `PTE_PRIVATE` uses software bit 10; bit 9
  remains `PTE_SHARED`. Generic page unmap rejects private leaves.
- Mapping pages are allocated on demand, zeroed through the HHDM before
  installation, and mapped user/RW/NX. VMM initialization requires CPU NX
  support and enables EFER.NXE; executable permission is not offered.
- Mapping creation allocates metadata first, validates range arithmetic,
  then maps pages transactionally. A failure removes installed PTEs, frees
  backing pages and metadata, reclaims empty page-table branches, and does
  not advance the cursor. QEMU failure injection exercises both metadata
  allocation points and partial physical-page map rollback.
- `clone_user_tables()` retains eager copying. It preserves private PTE
  marking, and child metadata records the child's copied frames. Clone
  failure tears down the partial child and releases copied pages exactly
  once; shared PTE and reference semantics are unchanged.
- Address-space validation cross-checks private metadata, frames, and RW/NX
  PTE flags, and rejects orphan private markers. Teardown frees leaf pages,
  metadata, and page tables in order. Teardown requires an inactive address
  space. Active unmap uses `invlpg` before backing-page release; inactive
  spaces rely on the existing single-CPU, no-PCID CR3-switch flush model.
- The existing exec path activates a fully prepared replacement before
  destroying the old image; failed replacement leaves the old address space
  untouched. QEMU verifies failed ELF preparation leaves the existing
  mapping readable, then activates a successfully loaded replacement and
  tears down the old mapping-bearing address space. Exit retains mappings
  through zombie state until reap invokes address-space teardown. A
  process-level exec syscall with a live private map cannot be exercised
  until userspace can create one.
- Host ELF tests cover page-rounded overlap rejection and a segment whose
  rounded end is exactly the private arena start. QEMU tests cover
  allocation, alignment, zero-fill, RW/NX, invalid sizes, arena and PTE
  collision rejection, monotonic placement after detach, exact unmap, clone
  isolation, injected metadata/page allocation and partial fork failures,
  and replacement teardown. Existing shared-memory, process, ELF, and Phase 13B regression
  tests were also run.
- Stage 14I-A1 adds a dedicated fork/wait QEMU fixture. The parent creates a
  shared mapping, then forks; a test-only hook adds a private RW/NX mapping to
  the child's address space and redirects the child's saved user RIP to that
  page. The child instruction fetch raises a real present-page user
  instruction-fetch protection fault (PF error code `0x15`). The normal
  Ring 3 exception path exits the child. The parent observes it as a zombie
  with the private physical page still allocated, reaps it through ordinary
  `waitpid`, and verifies that address-space teardown freed that page. It
  then verifies the shared bytes remain valid and detaches the shared mapping.
  The fault handler test checks RW/NX state and reads, writes, and rereads a
  marker through the normal user-copy helpers without changing PTE
  permissions. QEMU reported the lifecycle test passed.
- Stage 14I-A1 also adds a bounded QEMU PMM exhaustion test using a 128 MiB
  guest and the real `pmm_alloc_page()` path. It warms the kernel heap for
  mapping metadata, requests one more page than the currently free count,
  and confirms an allocation attempt occurs with zero free pages. In the
  recorded run, free pages were `19718` immediately before and after the
  failed mapping; the cursor, sole neighboring mapping, and its data remained
  intact, and a smaller mapping then succeeded. After address-space teardown
  the count was `19723`, including the five page-table pages released with
  that test address space. A test-only counter observes the actual PMM
  exhaustion branch; production allocation behavior is unchanged.
- Validation commands passed: `make -j4 kernel elf-test`,
  `make private-map-qemu-test`, `make private-map-lifecycle-qemu-test`,
  `make private-map-exhaustion-qemu-test`, `make phase13b-test`, and
  `make shm-test shm-failure-qemu-test shm-lifecycle-qemu-test
  shm-fork-failure-qemu-test`. A parallel `make -j4 phase13b-test` attempt
  failed because independent QEMU targets attempted write locks on the same
  `build/test-root.img`; the aggregate passed when rerun serially. The
  lifecycle and exhaustion assertions run only in their dedicated test
  builds.
- No public mapping syscall, `mprotect`, `brk`, executable mapping, shared
  mapping, copy-on-write, partial unmap, or user allocator is included.

Unresolved decisions

Syscall numbers and public `mmap`/`munmap`/`mprotect` argument/result
semantics remain unresolved and require a separate append-only ABI decision.
TLS size/placement remains bounded by the ADR-023 PT_TLS implementation and
one-page startup-stack work; the fixed private arena is excluded from TLS.
Partial unmapping and executable mappings remain out of scope. This ADR
records the implemented ELF exclusion for page-rounded segment ranges; it
does not alter any current syscall or move existing user addresses. Internal
protection behavior for private mappings is specified by ADR-029; the public
mapping syscall ABI remains unresolved.

---

ADR-029 — Private Mapping Protection Semantics

Status: Accepted and implemented internally in Stage 14I-B

Scope

This ADR defines internal permission changes for existing private anonymous
mapping objects created by `vmm_private_map()`. It adds no userspace syscall,
syscall number, wrapper, shared-mapping behavior, or public mapping ABI. The
arena bounds and reservation rules remain those in ADR-028.

Protection modes

The internal `vmm_private_protect(space, base, length, protection)` operation
supports `VMM_PROT_NONE`, `VMM_PROT_READ`, `VMM_PROT_WRITE`, and
`VMM_PROT_READ | VMM_PROT_WRITE`. `VMM_PROT_WRITE` makes a page user-readable
and writable, as required by x86-64 page permissions. All supported modes
remain non-executable. `VMM_PROT_EXEC` returns `-EOPNOTSUPP`; unknown or
unsupported combinations return `-EINVAL`.

The starting address must be page-aligned and length must be nonzero. Length
is rounded upward to a page boundary with checked arithmetic. The complete
rounded range must belong to one live private mapping. Requests outside a
mapping or crossing a mapping boundary fail without changing permissions.
The operation does not accept shared mappings, ELF pages, stack pages, or
arbitrary addresses.

Backing and page-table representation

Each private mapping stores one protection byte per page alongside its
physical-frame list. The metadata is cloned during fork. `PROT_NONE` retains
the present PTE, physical address, `PTE_PRIVATE` marker, and NX bit, while
clearing `PTE_USER` and `PTE_WRITE`. The page therefore faults on Ring 3
access but remains owned by the mapping and can be restored without allocating
or copying a replacement frame. Read-only pages have `PTE_USER` set and
`PTE_WRITE` clear. Writable pages have both bits set. Every private page
retains NX.

The VMM validates the entire range, metadata states, PTE flags, and physical
frame identities before committing any change. The commit loop only rewrites
already validated leaf PTEs and metadata; it performs no allocation and has
no recoverable failure point. The current kernel is single-CPU, and a
protection call executes in Ring 0, so the process cannot concurrently access
its address space during the update. A changed leaf in the active address
space is invalidated with `invlpg`. Inactive address spaces receive the
existing no-PCID CR3-switch flush before they run. No SMP shootdown protocol
is part of this implementation.

Fork and cleanup

Fork eagerly copies private physical pages and copies each page's protection
metadata. A child `PROT_NONE` PTE retains its own copied frame and supervisor-
only leaf permissions. Later permission changes affect only the selected
address space. Clone failure uses existing child address-space rollback.
Address-space destruction frees each present private leaf regardless of
whether its leaf is user-accessible; thus `PROT_NONE` backing is released
exactly once with the mapping. Successful and failed exec, zombie retention,
reap, and process cleanup retain the Stage 14I-A ownership rules.

Validation

Host tests cover supported/rejected modes, length rounding, alignment,
zero-length and overflow rejection. The QEMU protection test exercises RW to
READ/NONE and restoration, retained contents and physical frames, invalid and
cross-mapping ranges, neighboring mappings, NX state, and independent fork
permissions and backing. It attempts real Ring 3 reads/writes: `PROT_NONE`
read expects page-fault error `0x5`; `PROT_NONE` write and read-only write
expect `0x7` (present-page user protection violation with the write bit set).
The test also checks teardown releases parent and child backing pages.
Validation passed with `make -j4 kernel elf-test`,
`make private-protection-test`, `make private-protection-qemu-test`,
`make private-map-qemu-test`, `make private-map-lifecycle-qemu-test`,
`make private-map-exhaustion-qemu-test`, `make phase13b-test`, and
`make shm-test shm-failure-qemu-test shm-lifecycle-qemu-test
shm-fork-failure-qemu-test`. The QEMU targets use bounded timeouts and
assert their expected serial markers; all completed successfully.

Executable anonymous mappings remain unsupported because they are not needed
for the current allocator use case and would expand the kernel's executable
memory surface. They require a separate security and ABI decision. Public
`mmap`/`munmap`/`mprotect`, `brk`, partial unmapping, copy-on-write, and
dynamic linking remain deferred.

---

ADR-030 — Standard x86-64 ELF Process Initial Stack

Status: Accepted and implemented internally in Stage 14I-C

Scope and pinned CRT evidence

This ADR defines the process-entry stack for path-only launches and the
append-only argument-capable path exec interface. It preserves existing
syscall numbers and semantics and does not add
TLS, FS-base, dynamic linking, or a userspace launch ABI. The contract is
checked against the pinned musl 1.2.6 sources recorded in ADR-024:
`arch/x86_64/crt_arch.h::_start` passes the incoming RSP to `_start_c` and
aligns the call stack; `crt/crt1.c::_start_c` reads argc at `p[0]` and sets
argv to `p+1`; `__libc_start_main` locates envp after the argv NULL and auxv
after the envp NULL. Musl's `__init_tls` scans the table at `AT_PHDR` using
`AT_PHNUM` and `AT_PHENT`. The exact auxv tag values below match musl 1.2.6's
`include/elf.h`.

Stack contract

The existing mapped stack remains one writable user page at
`[0x00007fffffffd000, 0x00007fffffffe000)`, with its lower guard page
unmapped. The kernel reserves `argc + 17` 64-bit words, rounds the initial
RSP downward to a 16-byte boundary, and leaves alignment padding above the
auxv. Argument strings are copied into the high end of this page. Layout:

    RSP+0                 argc
    RSP+8 .. RSP+8*argc   argv[0..argc-1] user pointers
    RSP+8*(argc+1)        argv[argc] = NULL
    RSP+8*(argc+2)        envp[0] = NULL
    RSP+8*(argc+3)        AT_PHDR, value = runtime program-header address
    subsequent auxv       AT_PHENT, AT_PHNUM, AT_PAGESZ, AT_ENTRY,
                          AT_BASE, and AT_NULL pairs

This layout puts the auxv immediately after the envp terminator as required by
the CRT. All present pointers are user virtual addresses and argument strings
are NUL-terminated. Path-only launch uses argc zero; argument launch uses
argc 1–16 and at most 2048 aggregate bytes including terminators. Initial RSP
is 16-byte aligned. The freestanding `_start` passes argc, argv, and the
initial-stack pointer to `user_main`. There is no envp support. The internal
ELF smoke-test entry convention remains unchanged.

No `AT_RANDOM`, `AT_EXECFN`, identity, vDSO, or dynamic-linker entries are
provided. `AT_RANDOM` is not fabricated; musl 1.2.6 treats it as optional for
startup and uses a deterministic fallback, which is not an entropy guarantee.
`AT_BASE` is explicitly zero for the static ET_EXEC profile. The auxv ends
with the required `AT_NULL,0` pair.

Program-header address

ELF validation records `e_phentsize`, `e_phnum`, and the runtime address of
the complete program-header table in the load plan. If a single `PT_PHDR` is
present, it must describe the table at `e_phoff`, cover the complete table,
have readable flags and valid ranges/alignment, and match a readable
file-backed `PT_LOAD` translation. Without `PT_PHDR`, exactly one readable
`PT_LOAD` must file-back the complete `[e_phoff, e_phoff + e_phnum*e_phentsize)`
range. The runtime address is `p_vaddr + (e_phoff - p_offset)`, with checked
arithmetic and containment in both the segment's file and memory ranges.
Images with absent, ambiguous, malformed, or not-user-readable table coverage
are rejected by `elf_image_validate()`; the kernel ELF buffer address is
never exposed through auxv.

The project user linker scripts mark the first RX `PT_LOAD` with
`FILEHDR PHDRS`, mapping the ELF and program headers at the beginning of that
segment while leaving the normal entry address at `0x400000`. This also makes
the table available to user code. ELF loader tests cover derived addresses,
explicit PT_PHDR, malformed/missing coverage, and page-rounded arena bounds.

Entry, lifecycle, and failure behavior

`initialize_user_frame()` zeroes the interrupt frame before setting RIP, RSP,
user selectors, and `RFLAGS=0x202`. The initial `user_enter()` path now clears
all user general registers before `iretq`; the C startup assembly includes a
probe that fails if any non-RSP GPR is nonzero. Initial path launches and
successful exec use the standard stack. Existing `fork()` copies the active
stack page and saved register frame. Exec builds and validates the replacement
ELF and stack before activating it; any image, PHDR, stack, or allocation
failure releases replacement resources and leaves the old address space,
stack, and syscall frame intact. On successful exec the old image is released
after replacement activation. Stack/page ownership otherwise follows the
existing process exit, zombie, and reap rules.

Validation and limits

Host ELF fixtures test PHDR derivation, PT_PHDR validation, missing coverage,
and malformed tables. The init Ring 3 probe checks argc/argv/envp, every auxv
entry, 16-byte initial RSP alignment, AT_ENTRY, and reads the mapped program
header table through AT_PHDR. The shell repeats the check after successful
`exec_path`; init's child verifies fork inheritance and that failed exec
preserves its startup stack. The standard stack is 144 bytes including
padding, fitting comfortably in the existing one-page stack; there is no
argument/environment input to overflow. Future argument support must add
bounded string/pointer sizing and a documented oversize error before it is
enabled.

Stage 14I-C validation completed with `make -j4 kernel elf-test`,
`make phase12-test`, `make process-test vfs-smoke-test`,
`make private-map-qemu-test private-map-lifecycle-qemu-test
private-map-exhaustion-qemu-test`,
`make shm-test shm-failure-qemu-test shm-lifecycle-qemu-test
shm-fork-failure-qemu-test`, `make private-protection-test
private-protection-qemu-test`, and `make phase13b-test`. These passed,
including the QEMU startup probes for initial launch, fork, failed exec, and
successful exec. The private-mapping exhaustion target reported free pages
unchanged across failed allocation (19717 before and after), then returned
to 19722 after teardown. The Stage 14I-C diff also passed `git diff --check`.

Stage 14I-C does not implement PT_TLS or FS-base. Therefore the stack and
auxv are musl-startup prerequisites only; a static musl executable that
requires CRT TLS initialization is not claimed to run until Stage 14I-D
implements and tests the ADR-023/024 TLS and FS-base contract. Dynamic
linking, PIE/ASLR, stack growth, entropy, and argument-capable exec remain
deferred.

---

ADR-031 — Native x86-64 TLS and FS-base Lifecycle

Status: Implemented for Stage 14I-D; validation recorded below

Evidence and ownership

This decision is based on the pinned musl 1.2.6 source in ADR-024, especially
`src/env/__init_tls.c`, `src/internal/pthread_impl.h`,
`arch/x86_64/pthread_arch.h`, and
`src/thread/x86_64/__set_thread_area.s`. The default x86-64 build uses TLS
Variant II: static TLS is addressed below TP, FS base equals TP, and
`__get_tp()` reads `%fs:0`. `__copy_tls()` copies each module image and
`__init_tp()` sets the `struct pthread.self` pointer, DTV, and bookkeeping.
The kernel must not construct or depend on musl's private `struct pthread`
layout.

Musl owns initial TLS allocation, template copy, zero-fill, DTV, and TCB
initialization in user memory. It uses its builtin TLS storage when sufficient
and otherwise calls anonymous `mmap`; providing that larger allocation path
remains a separate native allocator/mapping prerequisite. The kernel owns
ELF metadata validation and the process FS-base value and privilege-transition
lifecycle. The kernel does not allocate a second TLS block or claim TLS is
initialized before musl calls its setter. A process starts with saved FS base
zero, including an image with PT_TLS; musl initializes its own TCB and then
sets FS base. Musl also constructs a TCB when no PT_TLS segment exists.

PT_TLS validation

The loader accepts at most one PT_TLS in static x86-64 ET_EXEC. It requires
`p_filesz <= p_memsz`, an overflow-safe file range for the template, and an
overflow-safe user virtual range for `p_vaddr + p_memsz`. Nonempty template
bytes must have exactly one readable PT_LOAD file-backed translation with
both matching file offset and runtime virtual address; the loaded bytes at
`p_vaddr` are the source musl reads through its program-header scan. The
range must be below the user limit and must not intersect the private mapping
arena, shared-memory arena, or user stack. The corresponding readable
PT_LOAD overlap is intentional: PT_TLS describes a template within the
executable image, not a separate kernel mapping. Duplicate, ambiguous,
unbacked, non-readable, overflowing, or out-of-range metadata is rejected.
The kernel stores presence, source virtual address, file size, memory size,
and validated alignment in the load plan; it keeps no pointer into the
temporary ELF input. The source address remains valid while the loaded image
is alive. The process also owns a compact copy of this metadata so it remains
available after fork even though a child has a cloned address space rather
than ownership of the parent's `elf_loaded_image` object. Successful exec
replaces that copy at the image commit point; failed exec leaves it unchanged.

`p_align == 0` is rejected. Although generic ELF permits zero to mean no
additional alignment, musl 1.2.6 uses `align - 1` in static TLS layout and
does not safely normalize zero. `p_align == 1` is accepted; larger values
must be powers of two and `p_vaddr`/`p_offset` must be congruent modulo the
alignment. PT_TLS sizes are bounded by checked arithmetic and the supported
user virtual range; TLS allocation itself remains user-owned. PT_INTERP,
PT_DYNAMIC, PIE, and general relocations remain rejected.

An empty PT_TLS emitted by the project linker script (`p_filesz == 0` and
`p_memsz == 0`) is accepted as equivalent to no TLS image, provided its
alignment and flags are valid. It does not set `tls_present` or require a
user address for its zero-sized template. A zero-sized PT_TLS with nonzero
file size is rejected.

Native setter ABI

Append syscall 20, `set_fs_base(base)`. `RDI` contains the requested base;
`RAX` contains 20. Success returns zero with CF clear. Errors return positive
errno in RAX with CF set: `EINVAL` for noncanonical, kernel, out-of-range, or
otherwise invalid address values; `EFAULT` when a nonzero base does not point
to at least one mapped writable user pointer-sized TCB word. Nonzero bases
must be 8-byte aligned so the TCB self pointer is naturally aligned. Base zero
is accepted to clear/disable the thread pointer. A process without PT_TLS starts
at zero and may later set a valid TCB base; PT_TLS presence is not a syscall
precondition because musl creates a TCB even without a TLS image. This is an
OpenSillicon-native operation, not Linux `arch_prctl`; no existing syscall is
renumbered or changed, and ABI version remains 1.

Process and privilege-transition lifecycle

The process stores one `user_fs_base` value. Kernel FS base is zero; the
kernel does not use FS for per-CPU state, and this change does not alter GS,
`swapgs`, or enable FSGSBASE. Boot clears IA32_FS_BASE before normal kernel
operation. The common interrupt entry checks the saved CS privilege level.
For a Ring 3 frame, assembly reads IA32_FS_BASE and saves the value to that
process before calling the interrupt dispatcher, then writes zero before
kernel C dispatch. Kernel-originated nested entries do not capture or modify
the process value. The captured value is held in the entry's saved-register
context until it is associated with the current process, so nested kernel
interrupts cannot overwrite a global scratch value. Kernel C/assembly uses
no FS-relative state.

After dispatch selects a frame, the common return path restores the selected
process's saved FS base only when the selected frame returns to Ring 3. IDT
entries are interrupt gates, so maskable interrupts remain disabled from the
restore through the final register pop and `iretq`. No kernel C code runs
after the restore. Kernel-mode interrupt returns leave FS at zero. The direct
initial `user_enter()` path starts with kernel FS already zero and each new
process's saved value zero. The syscall path uses the same common entry and
return code; syscall 20 updates only the current process's saved value, which
is loaded by that return path.

Fork copies the parent's saved FS-base virtual address and validated TLS
metadata after the syscall entry captured it. Existing eager private-page
cloning copies the TCB/TLS pages to the same virtual addresses with independent
physical backing. The child receives the inherited pointer and private bytes; child writes cannot
affect the parent. Successful exec builds the new image and stack first,
activates it, commits the process address-space pointer and resets saved FS
base to zero, then releases the inactive old image. This leaves CRT startup
responsible for constructing new TLS. Failed
exec changes neither the old saved base nor old TLS. Exit runs with kernel FS
zero; a zombie's address space and FS metadata remain until reap, and reap
destroys only an inactive address space. Multiple threads and multiple TLS
modules are excluded.

Validation and limits

Host ELF tests cover PT_TLS acceptance, duplicate/malformed headers, source
translation, file bounds, size/alignment rules, zero alignment rejection,
overflow, no-PT_TLS compatibility, and empty PT_TLS. Ring 3 fixtures use
FS-relative TLS variables and syscall 20 to test initialization, zero-fill,
syscall round trips, timer preemption, process-specific state, fork
isolation, failed exec preservation, and successful exec reset. Invalid
base tests cover zero, noncanonical/kernel, unaligned, unmapped, read-only,
and valid writable addresses. The dedicated `make fs-base-qemu-test` checks
the reset at successful exec commit while the old process has nonzero FS.
`make -j4 kernel elf-test` passed. The dedicated `make fs-base-qemu-test`
passed, including the test-only successful-exec reset assertion. The full
`make phase13b-test` aggregate passed with exit status zero. It covered the
existing process, interrupt, signal, ELF,
VMM, IPC, VFS, and terminal regressions. `make shm-test`, all three shared
memory QEMU failure/lifecycle targets, all three private mapping QEMU targets,
and both private protection tests passed. Source audit confirms CPL0 entries
do not capture/overwrite the saved user value and interrupt gates prevent
nested maskable IRQs during C dispatch. No dedicated NMI injection test is
available; the kernel has no FS-relative state, but NMI transition-window
behavior remains an untested architecture limitation. The system remains
single-CPU; no SMP TLB or per-CPU FS scheme is introduced.

The FS tests use hand-written freestanding Ring 3 TLS probes that implement
the minimal Variant-II template copy, zero-fill, self pointer, and setter
operation. They are not a musl CRT test and do not demonstrate that musl
startup or its full TCB/DTV works. The test-only exec assertion is compiled
only under `KERNEL_TEST_FS_BASE` in a separate `build/fs-base` tree. An
`objdump` audit of `build/kernel.elf` found no FS-segment memory operands;
kernel execution has no FS-relative data accesses. The source audit confirms
all IDT stubs reach `isr_common`, but NMI delivery in the tiny assembly
transition windows was not hardware-injected.

---

ADR-032 — Native musl 1.2.6 Build and First Static Executable

Status: Implemented for the Stage 14J bounded first-executable profile

Pinned inputs

The build consumes only the ADR-024 musl 1.2.6 official release archive
(`https://musl.libc.org/releases/musl-1.2.6.tar.gz`, SHA-256
`d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`),
the verified `COPYRIGHT` hash
`b870108ec5e7790e9f9919064f1b9421d62d5f9b0e6c230c6adf7ea2da62e97b`,
and these confirmed patches, in order:

- `0001-regcomp-overflow.patch`, SHA-256
  `0cf0090ab433e8f9ed08ae03a0c93aff557c1d3e347260f62a61a10281f5333b`.
- `0002-iconv-gb18030.patch`, SHA-256
  `444fa70e52ca158fb7d4bad560637790bbf8f72e80b82fff840dd66fa83091e3`.
- `0003-qsort-hardening.patch`, SHA-256
  `1ee29f64f9ca8e8ad7c349779d661ff6b52126a27575d3586981357a52c406fb`.

The separate `regexec` candidate has SHA-256
`6cfe87c3794562841a80fee8d1cd1b99d5ec67d4565f459b3812760912fe97ff`;
it remains unaccepted and is not applied. The release signature was verified in the earlier source review
against key fingerprint `836489290BB6B70F99FFDA0556BCDB593020450F`, whose
identity is not certified by the local GPG trust database. Stage 14J's local
verification reran archive, COPYRIGHT, patch hashes, and clean patch-series
application; it skipped signature verification because no signature/key pair
was supplied. This development baseline is not security-qualified for
distribution.

The pristine extraction remains under `build/musl-bootstrap/source/upstream`;
the three-confirmed-patch tree remains under `source/patched`. The build
script copies that tree to `build/musl-port/source/musl-1.2.6` and overlays
only the files in `third_party/musl/opensillicon/`. It never edits the pinned
trees. Generated source, configuration, logs, libraries, and sysroot remain
under ignored `build/musl-port/`. The output script replaces only that
dedicated directory when its ownership marker matches; it refuses an
unmarked or symlink output path. Upstream `COPYRIGHT` is preserved in both
the generated source and `sysroot/usr/share/licenses/musl/COPYRIGHT`.

Target build and syscall boundary

The experimentally validated compiler is Clang 22.1.8, target
`x86_64-unknown-none-elf`, with LLD 22.1.8, LLVM ar/ranlib, musl's existing
x86_64 architecture configuration, and `--disable-shared --disable-wrapper`.
The freestanding C flags are `-O2 -pipe -ffreestanding -fno-builtin
-fno-stack-protector -fno-pic -fno-pie -mno-red-zone
-fno-asynchronous-unwind-tables -fno-unwind-tables`. Link probing uses
`-fuse-ld=lld -nostdlib -static`; `LIBCC` is intentionally empty. The musl
library build and first executable link use no host libc, startup objects, or
compiler runtime. `build-info.json`, `configure.log`, `build.log`, and
`install.log` record the selected build inputs and commands under the ignored
output directory. The candidate target triple selects x86_64 code generation;
it does not itself provide an OpenSillicon SDK or syscall ABI.

The adapter takes musl's Linux syscall constants only as private selectors and
never passes them to the kernel. `syscall_arch.h` routes ordinary calls to
`__opensillicon_syscall`; the cancellation-point assembly calls that same
adapter, and the x86_64 thread-area assembly routes `ARCH_SET_FS` to native
syscall 20. The adapter uses `int 0x80`, RAX, RDI/RSI/RDX/R10/R8/R9, samples
CF, and returns negative errno to musl. It translates the kernel's ENOSYS 78
to musl ENOSYS 38. It strips Linux `O_LARGEFILE` on open and maps only
`AT_FDCWD` openat. `set_tid_address` returns the process ID as the single
thread's ID; it does not register clear-child-tid state. The supported
selectors for this test are write, read, open/openat, close, getpid,
set_tid_address, exit/exit_group, and ARCH_SET_FS. Other selectors return
ENOSYS. This is deliberately not a general musl syscall implementation.

Operation | musl path | OpenSillicon support/action
--- | --- | ---
`write` | `src/unistd/write.c` cancellation-point path | Adapter maps to syscall 2; tested on stdout.
`openat` / `open` | `src/fcntl/open.c`, `src/internal/syscall.h` | `AT_FDCWD` maps to syscall 7; Linux `O_LARGEFILE` is masked; missing-file errno tested.
`read`, `close` | `src/unistd/read.c`, `close.c` | Mapped to syscalls 9 and 8; not exercised by the hello test.
`exit_group`, `exit` | `src/exit/_Exit.c` | Both map to syscall 1; normal return from `main` tested.
`getpid`, `set_tid_address` | `src/unistd/getpid.c`, `src/env/__init_tls.c` | `getpid` maps to 3; single-thread `set_tid_address` is emulated by the same result.
`arch_prctl(ARCH_SET_FS)` | `src/thread/x86_64/__set_thread_area.s` | Dedicated overlay calls syscall 20; real TLS startup tested.
`mmap`, `mprotect`, `munmap`, `brk` | mallocng and oversized static TLS paths | Unsupported; return ENOSYS. The small test TLS fits musl's builtin TLS storage; malloc is not tested.
`writev`, `ioctl`, `fstat`, `clock_gettime`, `getrandom` | stdio, metadata, time, entropy | Not needed by this direct `write` program; unsupported and not claimed.

The current public mapping ABI is unresolved under ADR-027. No private
internal VMM function is exposed to userspace, and shared-memory syscalls 18/19
are not repurposed. `printf`, `puts`, malloc, file metadata, time, random,
threads, signals beyond what the kernel already offers, and broader libc
behavior remain outside this milestone.

Static CRT, TLS, and ELF contract

The executable uses musl's real x86_64 `crt1.o`, `crti.o`, `crtn.o`, and
`libc.a`. `_start` consumes the Stage 14I-C initial stack and passes argc,
argv, and envp to `__libc_start_main`. Musl reads AT_PHDR/AT_PHENT/AT_PHNUM
and scans PT_TLS, builds its own Variant-II TLS block, TCB, and DTV, then
sets FS through syscall 20. The kernel only validates PT_TLS, supplies the
standard stack/auxv, and stores/restores per-process FS base. This fixture has
an 8-byte initialized PT_TLS image plus 8 bytes of zero-filled PT_TLS memory
(`p_filesz=8`, `p_memsz=16`, `p_align=8`) and no larger-TLS mmap fallback. It
contains two PT_LOADs and PT_TLS, is ET_EXEC at entry 0x400000, and has no
PT_INTERP, PT_DYNAMIC, or relocation records. A weak unresolved `_DYNAMIC`
symbol remains at value zero as allowed by this static musl CRT; there are no
unresolved strong symbols.

The C fixture checks `argc == 0`, null argv/envp, an initialized and a
zero-initialized thread-local variable, ENOENT from a missing-file `open`,
and preservation of TLS across syscalls. It prints
`OpenSillicon: native musl write, errno, and TLS passed` through libc
`write()` and returns zero through musl's normal exit path. The dedicated
`make native-musl-qemu-test` boots the real kernel with the ELF on the
SilliconFS root; QEMU reaches the expected user output and kernel success
marker. QEMU itself is stopped by the target's 35-second timeout after the
kernel proceeds to its normal interactive boot, so the host QEMU status is
124 by design; the userspace process status is zero.

This proves one static program using libc startup, a small PT_TLS image,
FS-base setup, `open` errno handling, `write`, and process exit. It does not
establish general musl or POSIX compatibility. In particular, the public
anonymous mapping ABI, sufficiently large musl TLS, mallocng, stdio/writev,
argument-bearing exec, thread lifecycle, FPU/XSTATE state, time, entropy,
signals adaptation, and remaining file operations are still prerequisites
for a useful general userspace.

This ADR does not claim a working musl port. In particular, musl's large TLS
fallback still needs a supported user anonymous-mapping allocator path, and
the native libc syscall adaptation, thread-ID setup, FPU/XSTATE policy,
threads, dynamic linking, PIE/ASLR, and broad Linux compatibility remain
deferred.

Stage 14J validation record

- `make native-musl-verify`: PASS for the pinned archive/version/COPYRIGHT,
  confirmed patch hashes and clean application, and separate candidate hash.
  Detached signature verification was SKIPPED in this invocation because no
  signature/key pair was supplied; the earlier Stage 14C signature review and
  its untrusted-key caveat remain as recorded above.
- `make native-musl-build`: PASS; musl 1.2.6 and the OpenSillicon adapter
  compiled and installed into the generated sysroot with Clang/LLD.
- `make native-musl-inspect`: PASS; static ELF64 x86-64 ET_EXEC, valid PT_TLS,
  no PT_INTERP, PT_DYNAMIC, relocations, or unresolved strong symbols.
- Two consecutive clean builds of the final fixture produced the same ELF
  SHA-256:
  `932ce5b34f063a524d524468cd58baa7e2ac7b969455b73653a069f705933455`.
- The zero-initialized TLS variable is volatile so the compiler retains a
  real `.tbss` access; `readelf` reports PT_TLS `p_filesz=8`, `p_memsz=16`,
  and `p_align=8`.
- `make native-musl-qemu-test`: PASS; the real CRT program checked initial
  argc/argv/envp, initialized and zero TLS, ENOENT-to-errno translation,
  libc `write()`, TLS across syscalls, and normal exit. QEMU was stopped by the
  target timeout after the kernel success markers; the tested process status
  was zero.
- `make -j4 kernel elf-test`, `make phase12-test`, `make process-test
  vfs-smoke-test`, and `make phase13b-test`: PASS.
- `make private-map-qemu-test`, `make private-map-lifecycle-qemu-test`,
  `make private-map-exhaustion-qemu-test`, `make private-protection-test`,
  `make private-protection-qemu-test`, `make shm-test`,
  `make shm-failure-qemu-test`, `make shm-lifecycle-qemu-test`,
  `make shm-fork-failure-qemu-test`, and `make fs-base-qemu-test`: PASS.
- `git diff --check`: PASS. No files were staged or committed.

## ADR-033 — Native Private Anonymous Mapping ABI

Status: Implemented for the Stage 14K-A static musl allocator profile

### Context

The native musl 1.2.6 mallocng allocator obtains metadata areas and large
allocations with anonymous private `mmap`, changes selected metadata pages
from `PROT_NONE` to read/write with `mprotect`, and releases complete mapping
extents with `munmap`. Its initial `brk` attempt is optional: failure switches
to mapping. Large `realloc` attempts `mremap`, then falls back to allocate,
copy, and free. `MADV_FREE` is disabled by this musl build. These paths were
verified in `src/malloc/mallocng/{malloc.c,realloc.c,free.c}` from pinned
musl 1.2.6.

### Decision

Append native syscalls 21–23; syscalls 0–20 retain their numbers and
semantics. The kernel ABI uses `int 0x80`, number/result in RAX, arguments in
RDI, RSI, RDX, R10, R8, R9, and reports an error as positive errno in RAX
with CF set.

| Number | Operation | Arguments | Success |
| --- | --- | --- | --- |
| 21 | `mmap` | RDI=size in bytes, RSI=protection | RAX=page-aligned base |
| 22 | `munmap` | RDI=base, RSI=length | RAX=0 |
| 23 | `mprotect` | RDI=base, RSI=length, RDX=protection | RAX=0 |

The syscall 21 kernel interface is intrinsically private and anonymous; it
does not accept an address hint, descriptor, offset, or mapping flags. The
musl adapter accepts the musl internal Linux-shaped selector only when the
hint is null, flags equal `MAP_PRIVATE|MAP_ANONYMOUS`, descriptor is -1, and
offset is zero. Unsupported combinations fail with `EINVAL`. Executable
protection returns `EOPNOTSUPP`; unknown protection bits return `EINVAL`.
Supported protections are `PROT_NONE`, `PROT_READ`, `PROT_WRITE`, and
`PROT_READ|PROT_WRITE`. All mappings remain user-accessible when enabled,
supervisor-inaccessible, and NX. `PROT_NONE` retains its physical backing.
New memory is zero-filled.

Zero length is invalid. Size rounding and address arithmetic are checked for
overflow. The mapping allocator uses the existing usable private arena
`[0x0000600000001000, 0x00006ffffffff000)`, first-fit selection, and its
per-address-space monotonic cursor. Unmapped virtual ranges are not reused
during that address space's lifetime. A failed allocation rolls back PTEs,
frames, metadata, and cursor state.

`munmap` releases one complete mapping owned by the calling address space;
partial unmapping is rejected. The requested length may be unrounded or
page-rounded if both round to the complete mapping extent. This accommodates
mallocng's stored page-count length without freeing a partial or adjacent
mapping. Mapping metadata and physical frames are released once. Fork gets
private deep copies with identical protection metadata; exec replacement,
exit, reap, and address-space destruction follow ADR-028/029 ownership rules.

The musl adapter maps its internal `mmap`, `munmap`, and `mprotect` selectors
to these native calls and converts CF plus positive errno into musl's negative
kernel-return convention. Kernel ENOSYS value 78 is translated to musl's
ENOSYS value 38. No Linux syscall numbers enter the kernel ABI.

### Validation and limits

The Stage 14K-A fixture uses the real musl CRT and allocator. It covers small
and large `malloc`, `calloc`, `realloc`, `free`, mapping protection and
release, invalid mapping arguments, zero-fill, TLS across allocation/syscall
use, output, and normal exit under QEMU. mallocng's six integer-only C
objects are compiled without MMX/SSE because this kernel does not yet save
FPU/XSTATE across user execution; the remainder of musl uses its standard
x86_64 ABI flags. The build verifies those objects contain no SIMD-register
instructions. This is a bounded allocator build choice, not an FPU context
switch implementation.

Not implemented: file-backed or shared mappings, fixed/address-hint
mappings, executable mappings, partial unmap, `mremap`, `brk`, `madvise`,
copy-on-write, dynamic linking, threads, and general Linux VM compatibility.
Large realloc succeeds through mallocng's documented allocate-copy-free
fallback. This decision does not imply general POSIX or full musl support.

### Stage 14K-A validation record

- Pinned archive, COPYRIGHT, and confirmed patch verification: PASS; local
  `regexec` candidate remains separate and unapplied. Detached signature
  verification was skipped because no signature/key pair was provided to the
  command.
- Native musl build and ELF inspection: PASS; static x86-64 ET_EXEC with
  PT_TLS, no PT_INTERP/PT_DYNAMIC, no relocations, and no unresolved symbols.
- The final allocation fixture ELF SHA-256 was
  `f40091f858d6606b574c8439d52dac337e8b5bc54b30547157ceb416d470cb44`; the
  generated `libc.a` SHA-256 was
  `6791df2f7b1b53af1c35d02b734fd32d54176eea56fb4965cdb657b20a81cd93`.
- `make native-musl-alloc-qemu-test`: PASS; mallocng small/large allocation,
  mapping, protection, TLS, output, and process exit markers observed.
- The allocator fixture also checks invalid flags, hints, descriptors,
  offsets, protection bits, zero lengths, range overflow, complete versus
  partial unmap, and the kernel's size-rounding overflow path. musl's public
  `mmap()` wrapper rejects lengths at or above `PTRDIFF_MAX` first with
  `ENOMEM`; the fixture calls musl's generic `syscall()` adapter with the
  internal `SYS_mmap` selector to verify the kernel-side overflow returns
  `EINVAL`.
- `make -j4 kernel elf-test`: PASS.
- `make phase12-test process-test vfs-smoke-test`: PASS.
- `make phase13b-test`: PASS, including prior phase host and QEMU regressions.
- `make private-map-abi-test private-protection-test private-map-qemu-test
  private-map-lifecycle-qemu-test private-map-exhaustion-qemu-test
  private-protection-qemu-test shm-test shm-failure-qemu-test
  shm-lifecycle-qemu-test shm-fork-failure-qemu-test fs-base-qemu-test`:
  PASS. The PMM exhaustion test returned its available-page count to the
  pre-failure value after rollback.
- `git diff --check`: PASS.
- No regression target failed. The detached musl signature check was skipped
  because no signature/key pair was supplied; the recorded upstream key
  verification and trust caveat remain unchanged.

## ADR-034 — Per-Process x87/SSE Context Management

**Status:** Accepted and implemented for the single-CPU x86-64 baseline.

### Context

Native musl 1.2.6 stdio executes SSE instructions such as `xorps`. The kernel
previously neither enabled the architectural SSE state nor preserved it when
the process scheduler selected another process. Compiling libc's stdio code
without SSE would hide that kernel defect and is not an acceptable workaround.

### Decision

At boot, `arch_fpu_init()` checks CPUID leaf 1 for FXSR, SSE, and SSE2. Missing
any required bit stops boot with a diagnostic. The kernel sets CR0.MP and
CR0.NE, clears CR0.EM and CR0.TS, and sets CR4.OSFXSR and CR4.OSXMMEXCPT.
It does not enable CR4.OSXSAVE and does not change XCR0.

The supported state is the legacy 512-byte FXSAVE image, aligned to 16 bytes:
x87/MMX state, XMM0–XMM15, MXCSR, and the architectural FXSAVE metadata. The
initial image is established by `FNINIT`, MXCSR `0x1f80`, and clearing all
XMM registers. Every process owns an inline, 16-byte-aligned image in its
process object. Kernel heap payloads are 16-byte aligned by the heap's
16-byte allocation rounding and page-aligned heap base.

`schedule_from()` saves the outgoing process image before waiting or changing
the selected process, then restores the selected process after activating its
address space. Selecting the same process is safe: its current state is saved
and immediately restored. When the scheduler waits in Ring 0 or returns to
the kernel after the root process exits, it restores a separate clean kernel
image. First user entry restores the new process image. Successful exec
replaces the image only after loading succeeds, resets the saved state to the
clean template, and restores that state before returning to Ring 3. Failed
exec does not touch either saved or live FPU state. Fork saves the parent at
the syscall boundary and copies that image into the child's independent
process object. Process teardown frees the inline image with the process;
zombie lifetime therefore retains it until reap, exactly like other process
state.

### Kernel policy and interrupt behavior

All kernel C compilation uses `-mno-mmx -mno-sse -mno-sse2 -msoft-float`.
The only kernel code that deliberately executes SSE instructions is
`kernel/arch/x86_64/fpu.c`, where the initial XMM state is cleared. Interrupt
entry and return assembly does not save FPU state on every interrupt. It does
not execute FPU instructions, and only the scheduler performs process
FXSAVE/FXRSTOR. A nested kernel interrupt therefore cannot replace the
process-owned saved image. This policy assumes the kernel and linked kernel
libraries remain compiled with the no-FPU flags; disassembly is audited for
SIMD/x87 register instructions with the architecture FPU helper as the sole
allowed exception.

### Validation and limits

The `fpu-context-qemu-test` Ring 3 fixture checks clean x87 control word,
MXCSR, and XMM startup state; fork inheritance; different parent/child XMM
patterns and MXCSR rounding modes; timer preemption while both processes are
runnable; syscall preservation; failed-exec preservation; and successful
exec reset. The `native-musl-stdio-qemu-test` also runs the actual pinned musl
stdio implementation, including the SSE instructions that previously faulted
with #UD. Kernel, ELF, process, Stage 13B, private mapping/protection,
shared-memory, signal, and FS-base regressions are recorded in the Stage
14K-F validation record below.

Validation record:

- `make -B kernel fpu-context-qemu-test`: PASS. The Ring 3 fixture observed
  clean initial x87/MXCSR/XMM state, fork inheritance and isolation, distinct
  parent/child XMM and MXCSR values under timer preemption, syscall
  preservation, failed-exec preservation, and clean state after successful
  exec.
- `make native-musl-stdio-qemu-test`: PASS. Pinned musl stdio ran under QEMU,
  printed the expected fixture output, and exited with status zero. This
  confirms the prior `xorps` #UD is resolved; it does not complete Stage
  14K-B.
- `make -j4 kernel elf-test`: PASS; ELF validation/load/cleanup tests passed.
- `make phase12-test process-test vfs-smoke-test`: PASS.
- `make phase13b-test`: PASS.
- `make private-map-abi-test private-protection-test private-map-qemu-test
  private-map-lifecycle-qemu-test private-map-exhaustion-qemu-test
  private-protection-qemu-test shm-test shm-failure-qemu-test
  shm-lifecycle-qemu-test shm-fork-failure-qemu-test fs-base-qemu-test`:
  PASS.
- `make native-musl-alloc-qemu-test`: PASS.
- Kernel disassembly found FPU/SIMD instructions only in the intentional
  `kernel/arch/x86_64/fpu.c` initializer; other kernel C objects were built
  with general-register-only, no-MMX/no-SSE, and soft-float flags.
- `git diff --check` and `git diff --cached --check`: PASS. No changes were
  staged or committed.

Untested edges include booting on hardware missing a required CPUID feature,
an NMI arriving at every instruction boundary around a scheduler switch,
modified x87 data-stack isolation (the fixture checks x87 initialization and
MXCSR isolation), and multi-CPU execution. These remain outside the current
single-CPU test environment.

AVX, AVX-512, XSAVE/XRSTOR, OSXSAVE, and XCR0 management are unsupported.
FSGSBASE, multiple CPUs, simultaneous multithreading, kernel FPU use, and
multithreaded processes are not covered. AVX support requires feature checks,
XSAVE area sizing, XCR0 policy, and expanded per-process state before user
AVX instructions may be enabled.

## ADR-035 — Native musl Stdio and Terminal I/O ABI

**Status:** Accepted and implemented as a bounded OpenSillicon interface.

### Context and decision

The pinned musl 1.2.6 source uses `SYS_writev` in
`src/stdio/__stdio_write.c`, `SYS_readv` in `src/stdio/__stdio_read.c`, and
`SYS_ioctl(TIOCGWINSZ)` in `src/stdio/__stdout_write.c`,
`src/stdio/__fdopen.c`, and `src/unistd/isatty.c`. These operations are
implemented as native OpenSillicon syscalls, not Linux syscall ABI support.
This decision supersedes earlier ABI gap tables that recorded `writev` and
`ioctl` as missing at the time those tables were written.
The existing syscall numbers 0–23 are unchanged; 24–26 are appended:

| Number | Operation | Arguments |
| --- | --- | --- |
| 24 | `writev(fd, iov, iovcnt)` | RDI fd, RSI user iovec address, RDX count |
| 25 | `readv(fd, iov, iovcnt)` | RDI fd, RSI user iovec address, RDX count |
| 26 | `ioctl(fd, request, data)` | RDI fd, RSI request, RDX user data address |

The iovec ABI is two 64-bit fields in order: base address and byte length
(16 bytes per entry). A call permits at most 16 vectors and 64 KiB aggregate
length. The entire descriptor array and every nonempty buffer are validated
before I/O; zero-length vectors do not require a valid base pointer. Address
and aggregate arithmetic is checked. `writev` validates readable buffers;
`readv` validates writable buffers. Invalid requests therefore do not emit
output or consume input. Validation is bounded; output is copied through a
512-byte kernel transfer buffer rather than a user-sized stack allocation.

Both operations route through the existing process FD table and descriptor
operations. Pipes use their existing staged read and nonblocking write
helpers; regular VFS objects and the console use their FD operations. A
short transfer or an error after progress returns the bytes already
transferred. If no bytes transferred, the operation returns the underlying
error. A TTY read may block before progress; after returning input it stops
instead of waiting for another iovec. A pipe read validates every destination
before peeking and commits consumption only after the copy succeeds. The
musl x86-64 overlay translates its internal `SYS_writev`, `SYS_readv`, and
`SYS_ioctl` selectors to these native numbers. It converts carry-flag plus
positive errno to musl's negative errno result; kernel `ENOTTY` is 25, and
the historical kernel `ENOSYS` value 78 is translated to musl's 38.

### Terminal query

Syscall 26 supports only request `TIOCGWINSZ` (`0x5413`, matching the pinned
musl target header). Its eight-byte result is four 16-bit fields: rows,
columns, horizontal pixels, and vertical pixels. Rows and columns come from
`tty_dimensions`; pixel fields are zero. The user result must be writable.
The descriptor's `is_terminal` operation identifies a terminal; descriptor
numbers are not special-cased. Non-terminal descriptors and unsupported
requests return `ENOTTY`; nonexistent descriptors return `EBADF`; invalid
output memory returns `EFAULT`.

The three standard descriptors are separate immortal FD objects with the
same console read/write and terminal-identity operations. TTY dimensions are
calculated from validated framebuffer pixel dimensions and the renderer's
font cell size, then clamped to terminal limits. If framebuffer text setup is
unavailable, TTY initialization uses the existing 80-column by 25-row
fallback. No ioctl changes terminal state. `isatty()` in musl is implemented
by issuing `TIOCGWINSZ` and translating success to true.

### Validation and limits

`tests/musl_stdio.c` is linked against the pinned static musl 1.2.6 and run
through the real kernel/TTY/VFS in QEMU. It exercises `printf`, stdout and
stderr `fprintf`, `puts`, `putchar`, `fwrite`, `fflush`, all three `isatty`
checks, winsize query, unsupported request and invalid-pointer errors,
non-terminal and invalid descriptors, `fgets` from VFS, direct `read`,
multi-vector `readv` across buffers and EOF, malformed `writev`/`readv`
requests including empty vectors, excessive counts, and aggregate overflow,
malloc/free, TLS, and process exit. The fixture verifies an EOF-short `readv`
across two buffers but does not force a short write through a full pipe. No
deterministic keyboard input is injected; input tests use deterministic VFS
contents. This depends on ADR-034's
per-process FXSAVE/SSE policy: musl's formatter uses SSE and previously
faulted with #UD. The successful QEMU fixture is evidence for these paths,
not general POSIX or complete musl compatibility.

Validation record:

- `make native-musl-verify`: PASS for pinned archive/COPYRIGHT and confirmed
  patch hashes/series; detached signature verification was skipped because
  no signature/key pair was supplied. The local regex candidate remained
  separate and unapplied.
- `make native-musl-build`: PASS; the pinned musl source and native adapter
  rebuilt into the generated OpenSillicon sysroot.
- `make native-musl-inspect`: PASS; static x86-64 ET_EXEC with PT_TLS and no
  PT_INTERP, PT_DYNAMIC, relocations, or unresolved symbols.
- `make native-musl-qemu-test`: PASS; CRT/TLS/errno/write/exit fixture
  markers observed.
- `make native-musl-alloc-qemu-test`: PASS; real mallocng small/large
  allocation and memory mapping fixture markers observed.
- `make native-musl-stdio-qemu-test`: PASS; expected `printf`, stdout and
  stderr `fprintf`, `puts`, `putchar`, `fwrite`, and `writev` output observed;
  kernel harness confirmed exit status zero. The fixture also checked all
  three `isatty` results, winsize and unsupported ioctl errors, invalid
  ioctl/iovec pointers and descriptor, VFS `fgets`, direct `read`, multi-
  vector `readv` across buffers and EOF, allocator use, and TLS.
- `make -j4 kernel elf-test`, `make phase12-test process-test vfs-smoke-test`,
  `make phase13b-test`, `make fpu-context-qemu-test`, and
  `make fs-base-qemu-test`: PASS.
- `make private-map-abi-test`, `make private-protection-test`,
  `make private-map-qemu-test`, `make private-map-lifecycle-qemu-test`,
  `make private-map-exhaustion-qemu-test`,
  `make private-protection-qemu-test`, `make shm-test`,
  `make shm-failure-qemu-test`, `make shm-lifecycle-qemu-test`, and
  `make shm-fork-failure-qemu-test`: PASS.
- One initial run of the expanded stdio fixture failed with fixture status
  27 because its new partial-read expected string omitted the second passwd
  record. The fixture expectation was corrected to match the actual VFS file;
  the rerun passed. No kernel I/O defect was observed.
- `git diff --check` and `git diff --cached --check`: PASS; nothing was staged
  or committed.

Full termios, `TCGETS`/`TCSETS`, job control, PTYs, terminal-generated
signals, blocking semantics beyond the current TTY/pipe behavior, and
arbitrary ioctl requests remain unsupported.

## ADR-037 — DMA Boot Contract, Descriptor Rings, and QEMU e1000 Profile

Status: Accepted for the initial x86_64 DMA profile; no e1000 datapath is
implemented.

### Limine contract and fail-closed policy

The kernel requests Limine base revision 5. At entry it requires a valid
loaded-revision field and at least revision 4 for the existing HHDM/ACPI
contracts. Identity-address DMA is enabled only when
`LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)` is true (the request's
third word was changed to zero by the bootloader) and the reported loaded
revision is at least 5. A revision-4 boot can therefore continue through
non-DMA initialization, but it does not acquire DMA capability.

This kernel does not declare `LIMINE_X86_64_KEEP_IOMMU_REQUEST_ID`. Limine's
base-revision-5 contract disables Intel VT-d DMA translation and AMD-Vi,
unless that feature is requested. The runtime DMA state is explicitly either
`DMA_ADDRESSING_PHYSICAL_IDENTITY` or `DMA_ADDRESSING_UNAVAILABLE`; an
unavailable state rejects allocation and device handoff. `dma_init()` may
successfully initialize the subsystem in the unavailable state so unrelated
boot code can continue. AHCI checks the capability before allocating or
starting DMA; the current boot requires the persistent disk root and therefore
halts with an explicit AHCI-disabled diagnostic if the contract is absent.

Revision 5 is a necessary boot contract for this identity-address profile,
not IOMMU translation support. The implementation does not discover or
program an IOMMU. It also does not provide cache maintenance for
non-coherent devices; those platforms are unsupported. The x86 `sfence`/
`mfence` instructions provide ordering for the assumed coherent profile, not
cache synchronization. Device-visible addresses currently equal allocated
physical addresses. The guarantee is limited to the supported x86_64
PC-compatible coherent, no-IOMMU profile; allocator tests do not prove a
physical device's DMA behavior.

The source declares the Keep-IOMMU request structure in the bundled
`limine/limine.h` but does not instantiate a request. Local QEMU boot
configuration does not configure an IOMMU. Protocol semantics are documented
in the [Limine protocol](https://github.com/limine-bootloader/limine-protocol/blob/trunk/PROTOCOL.md),
Base Revision 5 and the x86_64 Keep IOMMU feature.

### DMA allocation and ring lifetime

`dma_buffer_info` distinguishes `cpu_virtual_address` (HHDM pointer),
`physical_address` (PMM address), and `device_address` (currently the same
physical value under this policy). PMM supplies contiguous, page-backed
storage; `dma_alloc()` applies power-of-two alignment, requested device
address-width, 16 MiB maximum, zero-fill, and overflow checks. A DMA buffer
cannot be freed while whole-buffer device-owned. The older whole-allocation
CPU/device handoff is for independent packet/data buffers; it is not the
ownership model for a concurrently shared descriptor ring.

`dma_descriptor_ring` tracks ownership per descriptor using separate bounded
software state. The ring backing is a generation-tagged DMA allocation. The
ring pins that allocation on start; `dma_free()` and whole-buffer handoff are
rejected while pinned. For each descriptor:

1. CPU owns and prepares descriptor contents and its device-visible ownership
   field.
2. `dma_ring_publish()` performs `sfence` before the device tail/doorbell
   update and transitions the slot to device-owned.
3. The driver observes the hardware completion/ownership bit. Only then may
   `dma_ring_reclaim(..., true)` perform `mfence` and return the slot to CPU
   ownership; packet data/status may be consumed after reclaim.
4. RX rearm follows the same CPU-prepare then publish sequence. TX reuse waits
   for completion.

The caller must serialize ring API calls in the current single-CPU kernel.
`dma_ring_quiesce(..., true)` is a caller assertion that device-specific stop
or reset has been verified; it orders completion, returns slots to CPU state,
and unpins the backing. Passing false leaves the ring and allocation pinned.
No ring code currently performs device-specific reset or proves the stop
condition. Thus no one may free ring memory on timeout/error until the future
driver has independently established device quiescence. The API does not add
SMP synchronization, IOMMU revocation, cache maintenance, or a live e1000
driver.

### QEMU 82540EM profile

The deterministic model command used for PCI/QMP profile inspection is:

```sh
qemu-system-x86_64 -machine q35 -m 128M -nodefaults -nic none \
  -netdev user,id=net0 -device e1000,netdev=net0 -display none -S \
  -qmp unix:/tmp/opensillicon-e1000.sock,server=on,wait=off
```

On QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), QMP `query-pci` reported bus 0,
slot 1, vendor/device `8086:100e`; BAR0 is 128 KiB non-prefetchable 32-bit
MMIO, BAR1 is 64-byte I/O, and expansion ROM BAR6 is 256 KiB and unassigned.
Because the VM was paused before firmware assignment, QMP showed BAR addresses
as unassigned. This is QEMU model metadata, not guest BAR probing or generic
PCI behavior. The QEMU 10.2.2 e1000 model defines these region sizes; the
Intel 8254x manual gives a 128 KiB register space. See the [QEMU e1000 model](https://github.com/qemu/qemu/blob/v10.2.2/hw/net/e1000.c)
and [Intel 8254x Software Developer's Manual](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf).

`e1000_qemu_profile_validate()` accepts only the exact `8086:100e` Ethernet
profile with assigned BAR0 as aligned, non-prefetchable 32-bit memory and BAR1
as the expected aligned I/O BAR; unexpected extra BARs and e1000e (`8086:10d3`)
are rejected. It supplies a profile aperture to the existing MMIO lease, which
still checks address range and Limine memory-map safety. It does not size a
live BAR, establish device ownership, or map the BAR by itself. The profile is
compiled and host-tested but not yet connected to PCI enumeration or an e1000
driver.

### Validation and remaining limits

`make kernel pci-test pci-resource-test dma-test pmm-contiguous-test
network-foundation-test dma-ring-test e1000-profile-test` passed. DMA contract
tests cover honored/rejected revisions and Keep-IOMMU rejection; ring tests
cover per-slot transitions, refused early free/handoff, and quiesce pin release;
profile tests reject wrong IDs and BAR layouts. QMP confirmed only the QEMU
model ID and region sizes. No guest e1000 device was mapped or initialized;
no hardware DMA, TX, or RX test was run. Generic PCI BAR aperture discovery
remains unresolved and no live BAR sizing was attempted.

The bundled EFI loader identifies itself as Limine 12.9.6; the repository's
bundled `limine/limine.h` exposes the standard base-revision support and loaded
revision macros. `make phase12-qemu-test` passed after the kernel began
requesting revision 5, and a separate QEMU boot with the explicit e1000 model
reached DMA policy initialization, AHCI initialization, and the persistent
SilliconFS root. This exercises the enabled path on the tested QEMU/firmware
profile; it is not a negative bootloader test or e1000 DMA test. A host test
exercises rejected/unhonored/Keep-IOMMU contract inputs.

The broader `make phase12-test` and `make phase13b-test` aggregate did not
complete: the host `acpi-test` target fails to link because modified
`kernel/arch/x86_64/acpi.c` references `vmm_vmalloc_device_start`, which its
current test link command does not provide. Independent process, VFS, signal,
FPU, FS-base, private mapping/protection, shared-memory, and native musl
regressions were run separately; this unrelated ACPI test-link issue was not
changed here.

## ADR-038 — Guest e1000 Profile Discovery, BAR Mapping, and Reset

Status: accepted and implemented for reset-only initialization; packet I/O is
not implemented.

### Context

ADR-037 established a fail-closed Limine Base Revision 5+ identity-DMA
contract, descriptor backing pinning, and a bounded QEMU 82540EM profile. The
profile validator was previously host-only. This decision connects it to guest
PCI enumeration and maps the device registers without starting DMA.

### Decision

The kernel recognizes only PCI vendor/device `8086:100e`, class Ethernet,
header type 0, and the existing strict profile: BAR0 is a 128 KiB aligned,
non-prefetchable 32-bit MMIO resource; BAR1 is an aligned 64-byte I/O resource;
BAR2–BAR5 must be zero. No live BAR sizing is performed. Other Intel network
IDs are diagnosed and skipped; duplicate matching devices fail the singleton
driver rather than selecting one ambiguously.

Before mapping, the driver sets PCI memory decode and clears bus mastering,
then reads back the command register and requires memory decode on and bus
master off. BAR0 is mapped only with `pci_mmio_map()` and its uncached lease;
physical BAR addresses are never cast to pointers. Register accesses are
aligned 32-bit volatile MMIO operations bounded to the validated BAR aperture,
with compiler and `mfence` ordering. The driver reads CTRL, rejects floating
all-ones values, sets CTRL.RST (bit 26), and polls at most
`E1000_RESET_POLL_LIMIT` (1,000,000) times with `pause`. It then requires reset
to be deasserted and STATUS not to read all ones. It does not write interrupt,
ring, receive, or transmit registers.

The lifecycle states are absent, discovered, resources validated, mapped,
reset in progress, initialized, failed, and stopped. A repeated initialize
after success is idempotent. Failures are recorded with a bounded reason enum.
Where PCI configuration permits, failure cleanup clears memory decode and bus
mastering. The MMIO lease is released only after reset failure and verified
decode disable; otherwise it is retained to avoid a stale mapping or possible
device access after unmap. No packet DMA was armed, and the successful path
keeps the BAR lease for the kernel lifetime. The Limine DMA capability remains
a prerequisite; no identity-DMA assumption is bypassed.

### MMIO safety adjustment

QEMU assigns the profile's BAR0 at physical `0x80040000`, which is not listed
as a Limine memory-map `RESERVED` entry. The old lease safety predicate
therefore rejected the device. VMM now permits MMIO ranges that are either
fully covered by `RESERVED` memory or wholly absent from all Limine memory-map
entries, and rejects ranges that overlap any described RAM/boot range or are
only partially described. It also bounds addresses to the x86-64 page-table
physical-address field. This permits an assigned PCI aperture in a firmware
map hole without treating any HHDM RAM as MMIO. It is not generic BAR sizing;
the caller still needs a trusted resource aperture. The driver supplies only
the fixed QEMU profile aperture.

### Verification

QEMU 10.2.2 was launched with q35, `-nodefaults -nic none`, explicit
`-netdev user,id=net0 -device e1000,netdev=net0`, OVMF, and the project root
disk. The guest logged PCI enumeration, BAR0 base `2147745792` (`0x80040000`),
aperture `131072`, PCI command `3` (memory decode enabled, bus master disabled),
CTRL `1311328`, STATUS `2148009859`, and reset completion. The kernel then
continued through AHCI and reached the shell. This demonstrates guest discovery,
MMIO mapping, reset, and register readback for this QEMU configuration only.

Host tests pass for profile accept/reject cases and reset completion, bounded
timeout, floating CTRL/STATUS, and invalid poll count. Kernel/ELF and PCI,
resource, DMA, and ring tests pass. No actual device DMA, link state, TX, RX,
interrupt behavior, or physical 82540EM hardware has been tested. The reset
poll bound is an iteration bound, not a calibrated wall-clock timeout.

### Consequences

This milestone prepares the first QEMU device but does not complete Phase 15.2
or remove the need for a separately reviewed TX/RX implementation. Descriptor
rings remain unused. Generic BAR sizing, device reset on physical hardware,
IOMMU translation, and non-coherent DMA remain unsupported.

## ADR-039 — Bounded e1000 DMA Rings and Ethernet Smoke Path

Status: accepted and implemented for the QEMU 82540EM profile; this is a raw
Ethernet driver proof, not a network protocol implementation.

### Context

ADR-037 establishes the fail-closed Limine Base Revision 5+ identity-DMA
policy, and ADR-038 establishes guest discovery, the bounded `8086:100e`
profile, BAR0 mapping, and reset. This milestone proves that the guest can use
that profile for bounded descriptor DMA and exchange frames with a test peer.
The driver remains polling-only and does not parse higher-level protocols.

### Decision

The supported profile uses the legacy 82540EM descriptors. TX and RX each
have eight 16-byte descriptors in their own 128-byte DMA allocation. Ring
bases must be 128-byte aligned; the ring length is exactly 128 bytes. The host
side validates descriptor size, count, base alignment, completion bits,
descriptor errors, and received frame bounds. Per-descriptor software owner
states complement the device's descriptor status fields. A CPU-published
descriptor is ordered before the tail-register doorbell with the DMA layer's
coherent-x86 ordering fence. Completion is fenced before descriptor or packet
memory is read by the CPU.

Each slot owns a separate contiguous 2048-byte DMA data buffer. The current
DMA address is the PMM physical address only when the validated Limine
Base-Revision-5-or-newer identity-DMA capability is available and
Keep-IOMMU was not requested. The driver verifies the queried device address
matches that address and requests 64-bit device addressability, supported by
this selected e1000 profile. No IOMMU translation or non-coherent cache
maintenance is provided. Descriptor and buffer allocations stay pinned or
device-owned while hardware can access them.

Initialization order is: discover exact device/profile; validate identity-DMA
capability; enable PCI memory decode while clearing bus mastering; acquire the
uncached BAR0 mapping; reset and validate CTRL/STATUS; allocate and zero DMA
rings and buffers; pin ring backing; publish initialized RX descriptors; mask
interrupts and program T/R ring bases, lengths, head/tail, and TIPG with both
engines disabled; set and read back PCI Bus Mastering; enable TX/RX engines.
Bus mastering is never enabled on the reset-only or failed-allocation paths.

TX accepts Ethernet frames from 14 through 1518 bytes, copies them into a
driver-owned DMA slot, publishes one descriptor, and advances TDT. It retains
the packet reference until TX DD is observed, then returns ownership to the
caller. The bounded smoke test waits for completion. RX polls the next ring
slot, observes DD after a completion fence, and accepts only descriptors with
DD and EOP, no descriptor errors, and lengths from 14 through 1518 bytes. It
copies accepted bytes into the fixed core packet pool, then rearms the DMA
buffer and returns RDT ownership. Invalid frames are dropped and safely
rearmed. There is no protocol parsing, software receive queue, or IRQ path.

Shutdown disables RCTL/TCTL, performs a bounded reset and validates reset
completion, then disables and reads back PCI memory decode and bus mastering.
Only after this stop condition does it return/release in-flight packet state,
quiesce and unpin rings, free the DMA allocations, and release BAR0. A polling
timeout alone does not prove hardware stop. If reset or PCI readback cannot
establish quiescence, the driver retains device-visible allocations and the
MMIO lease rather than risking DMA use-after-free.

### QEMU test profile and evidence

`make e1000-dma-qemu-test` builds an isolated test kernel with
`KERNEL_TEST_E1000_SMOKE` and launches QEMU 10.2.2 with q35, 256 MiB, OVMF,
`-nodefaults -nic none`, an explicit `e1000` device with ID `8086:100e`, and
`-netdev socket` connected to a loopback host-side peer. QEMU's socket backend
uses a four-byte big-endian frame length followed by the frame bytes. The host
peer requires the exact 60-byte guest TX frame before injecting a deterministic
60-byte RX frame. It then requires guest serial markers for TX descriptor 0
DD, RX descriptor 0 DD/EOP, successful byte comparison, and clean shutdown.

Verified output:

```text
host peer observed guest Ethernet TX: 60 bytes, exact frame match
host peer injected deterministic Ethernet RX: 60 bytes
guest confirmed TX/RX descriptor completion and safe DMA shutdown
QEMU e1000 DMA Ethernet smoke passed
```

Guest serial also reported eight descriptors per direction, link up, TX
descriptor 0 completion at length 60, RX descriptor 0 completion at length
60, Bus Mastering disabled, and rings released. QEMU is actual guest-side DMA
evidence for this profile; it is not evidence for physical 82540EM devices,
other BAR layouts, IOMMU translation, non-coherent DMA, or arbitrary hardware.

Host checks passed for TX ring slot availability/full-ring rejection and
reuse, TX descriptor initialization/address/length validation and DD
completion, 128-byte ring-base geometry, and RX DD/EOP/error/length validation.
The existing DMA ring host tests pass owner transition, duplicate publication,
completion gating, quiesce gating, and pinned-allocation behavior. The guest
smoke test verifies the successful hardware path and clean shutdown. The
injected TX/RX timeout and PCI-reset failure branches have not been exercised
against a hardware device; the existing isolated reset helper covers bounded
reset timeout behavior, but that is not equivalent to proving a stuck device
has stopped DMA. On any such failure, the driver retains DMA memory and its
MMIO lease.

The kernel/ELF, PCI/resource, DMA, PMM-contiguous, and network-foundation host
tests passed. Process/VFS, FPU/SSE, FS-base, private mapping/protection, shared
memory, and pinned native-musl regression targets also passed. The aggregate
`phase12-test` and `phase13b-test` stop at the existing host `acpi-test` link
failure (`vmm_vmalloc_device_start` is undefined in that test link). The
separate `phase12-qemu-test` was attempted but the installed QEMU rejected its
QMP key event for underscore (`Parameter 'data' does not accept value
'underscore'`); this is a harness/key-code limitation, not a passing test.

### Consequences and exclusions

The milestone establishes bounded raw-frame TX/RX only. Interrupts are masked;
all completion handling is bounded polling. ARP, IPv4, ICMP, UDP, TCP,
routing, sockets, user access to DMA buffers, generic NIC profiles, IOMMU
support, non-coherent cache synchronization, and SMP synchronization remain
out of scope. Phase 15 remains incomplete.

## ADR-040 — Bounded Ethernet, ARP, IPv4, and ICMP Echo

Status: accepted and implemented as a bounded protocol smoke stack over the
QEMU e1000 profile; not a general-purpose network service.

### Context

ADR-036 defines protocol boundaries, and ADR-039 provides the polled e1000
Ethernet device and packet ownership contract. This milestone validates a
real guest IPv4 exchange without introducing routing, transports, sockets,
or an unbounded receive worker.

### Decision

The protocol module lives in `kernel/net/netstack.c`. It consumes borrowed
bytes from a CORE-owned packet for the duration of one call, returns a
bounded event and optionally writes a response into caller-owned storage.
It retains no packet references. The caller remains responsible for releasing
received pool references and for allocating/transmitting any generated reply.
The test poller copies generated bytes into a pool packet, calls the existing
device TX operation, waits for descriptor completion, then releases the
returned CORE reference.

The supported profile is untagged Ethernet II only. Frames shorter than the
14-byte header or larger than 1518 bytes are ignored; only EtherTypes ARP
`0x0806` and IPv4 `0x0800` are dispatched. VLAN and other EtherTypes are
ignored. The IPv4 MTU is 1500 bytes. IPv4 requires version 4, IHL 5, a valid
20-byte header checksum, a total length from 20 through 1500 fully contained
in the received Ethernet payload, nonzero TTL, on-link unicast source,
configured local destination, and no reserved/MF/fragment-offset bits. DF is
allowed. IP options, fragmentation/reassembly, forwarding, and protocols
other than ICMP are unsupported. Outbound IPv4 uses TTL 64, a valid header
checksum, and no fragmentation.

ARP accepts only Ethernet/IPv4 packets with hardware type 1, protocol type
`0x0800`, address lengths 6 and 4, request/reply operation, consistent source
Ethernet and ARP MAC addresses, on-link unicast sender IP, and target protocol
address equal to the configured local IPv4 address. Replies must target the
local MAC. Valid requests receive a padded 60-byte ARP reply; valid replies
update the cache. The cache holds eight entries, expires entries after 6000
timer ticks, reuses expired/free entries first, then replaces entries in
round-robin order. No automatic retransmission worker is implemented. The
QEMU resolution test sends one request and has a five-second bounded polling
deadline.

ICMP supports only code-zero Echo Request and Echo Reply with valid checksum
and at least the eight-byte ICMP header. Echo replies to local Echo Requests
preserve identifier, sequence, and payload while changing type to zero and
recomputing the checksum and IPv4 header. Echo Replies are reported as an
event; matching identifier/payload policy is checked by the test caller.
Other ICMP types and codes are ignored. Output is bounded by the 1500-byte IP
MTU and the 1518-byte frame limit.

The explicit test addresses are guest `10.15.3.1/24`, MAC
`52:54:00:15:03:01`, and peer `10.15.3.2/24`, MAC
`52:54:00:15:03:02`. QEMU uses q35, `-nodefaults -nic none`, an explicit e1000
`8086:100e`, and a loopback socket backend. The peer validates the guest's
ARP request, IPv4 and ICMP checksums, Echo identifier `0x153a`, sequence 1 and
payload `OS-PHASE15.3A-ECHO`, then injects an ARP reply and matching Echo
Reply. Guest serial confirms descriptor TX/RX completion, ARP resolution,
Echo transmission/reception, and safe e1000 shutdown. No Ethernet/IP bytes
are delivered to userspace.

### Verification and consequences

`make netstack-test` passes host tests for ARP request/reply construction,
cache expiry and replacement, malformed ARP, IPv4 checksum and length
validation, unsupported options/protocols/fragments, Echo reply construction,
Echo Reply recognition, and bounded outbound builders. `make kernel` passes.
`make e1000-icmp-qemu-test` passes with host peer evidence for both guest
transmit frames and injected responses. This is QEMU-only proof of the bounded
profile, not physical NIC support or a continuously serviced network stack.

UDP, TCP, sockets, route selection, gateways, DHCP, IPv6, VLAN, IP
fragmentation/reassembly, IP forwarding, asynchronous retries, interrupt
processing, SMP synchronization, and broad RFC/POSIX compatibility remain
deferred. IOMMU translation and non-coherent DMA remain unsupported under
ADR-037. Phase 15 is incomplete.

## ADR-041 — Bounded UDP and Project-Native Socket Descriptors

Status: accepted and implemented for the current static QEMU e1000 profile;
not a general-purpose or POSIX-compatible socket service.

### Context

ADR-040 supplies bounded Ethernet II, ARP, IPv4 and ICMP processing over the
polled e1000 device. Native UDP userspace needs datagram boundaries and
endpoint metadata, while the kernel already routes file I/O through
reference-counted `fd_object` instances. This decision reuses those objects
and keeps packet processing out of interrupt context.

### Protocol profile

The existing netstack owns UDP parsing and construction. Incoming IPv4 still
requires version 4, IHL 5, valid IPv4 header checksum, no fragments, nonzero
TTL, an on-link unicast source and the configured local destination. UDP must
have at least its eight-byte header, a length of at least eight exactly equal
to the remaining IPv4 payload, and nonzero source and destination ports.
Malformed, truncated, oversized and unmatched datagrams are dropped. A
nonzero UDP checksum is validated over the IPv4 pseudoheader and complete UDP
datagram. A zero checksum is accepted for IPv4. Outbound UDP checksums are
computed; a computed zero is serialized as `0xffff`.

The current bounded profile is MTU 1500, Ethernet frame maximum 1518 and UDP
payload maximum 512 bytes. The QEMU guest configuration is `10.15.3.1/24` at
the explicitly configured e1000 MAC `52:54:00:15:03:01`; the deterministic test peer is
`10.15.3.2`, MAC `52:54:00:15:03:02`. The only currently supported NIC is the
explicit QEMU 82540EM profile `8086:100e`; DMA still requires the validated
Limine Base Revision 5 identity-DMA capability in ADR-037.

### Socket and endpoint model

UDP sockets are ordinary reference-counted `fd_object`s. Fork copies FD
references to the same socket endpoint and receive queue; closing a descriptor
drops one reference, and the final close clears the socket and queued data.
Process exit uses the existing FD-table destruction path. Exec follows the
existing process descriptor inheritance policy. This API is a project-native
datagram facility and does not claim Linux or BSD socket compatibility.

Limits are eight allocated socket objects, four queued datagrams per socket,
512 payload bytes per datagram, and one synchronous send operation per socket
on the single-CPU path. Queues contain kernel copies; DMA pool buffers are
released after parsing and never escape to userspace. Queue overflow drops the
newest packet. Receive returns `EMSGSIZE` without dequeuing when the caller's
buffer is too small. There is no background receive worker: `recvfrom` polls
the device in bounded syscall context for at most 500 timer ticks (five
seconds), then returns `ETIMEDOUT`. There is no signal interruption or
nonblocking flag. It validates all user output ranges before polling and
consumes a queued datagram only after copying payload and source endpoint.

Endpoints are exactly six bytes: four IPv4 octets followed by a host-order
`uint16_t` port. `0.0.0.0` is the wildcard local address; only wildcard or the
configured local address can be bound. Wildcard conflicts with any binding on
the same port; explicit local bindings conflict with the same explicit
address/port. Binding port zero chooses a deterministic ephemeral port from
49152 through 65535. No `getsockname` operation exists, so callers requiring a
known reply port should bind a nonzero port. Unbound senders are assigned an
ephemeral wildcard endpoint; the current API does not report that assignment.

### Syscall ABI

Syscalls 0–27 are unchanged. New calls use `int 0x80`, with the existing
success convention (RAX result, CF clear) and error convention (positive
errno in RAX, CF set):

| Number | Operation | Arguments |
| --- | --- | --- |
| 28 | `udp_socket` | RDI family (`2` IPv4), RSI type (`2` datagram), RDX protocol (`0` or `17`) |
| 29 | `udp_bind` | RDI descriptor, RSI user endpoint pointer, RDX exact endpoint size (`6`) |
| 30 | `udp_sendto` | RDI descriptor, RSI payload pointer, RDX payload length, R10 destination endpoint pointer |
| 31 | `udp_recvfrom` | RDI descriptor, RSI writable payload buffer, RDX capacity, R10 writable source endpoint pointer |

The native user adapter translates CF plus positive errno into a negative
return. Invalid descriptor numbers return `EBADF`; non-socket descriptors
return `ENOTSOCK`. Unsupported family returns `EAFNOSUPPORT`, unsupported
type/protocol returns `EPROTONOSUPPORT`, duplicate bindings return
`EADDRINUSE`, nonlocal bind addresses return `EADDRNOTAVAIL`, oversize data
returns `EMSGSIZE`, and unavailable link/device returns `ENETDOWN`. The
remaining errors use the kernel's existing errno numbers (`EFAULT`, `EINVAL`,
`ENOMEM`, `EMFILE`, `ENOBUFS`, `EHOSTUNREACH`, `ETIMEDOUT`, and `EIO`).
Arguments are copied or validated using the current VMM user-memory helpers
before protocol side effects. There are no flags, connected state, socket
options, ancillary data or stream operations.

### QEMU verification

`make e1000-udp-qemu-test` runs a dedicated QEMU q35 guest with 256 MiB, OVMF,
`-nodefaults -nic none`, an explicit socket Ethernet backend, and
`-device e1000` with MAC `52:54:00:15:03:01`. The host peer validates the guest
ARP request, responds for `10.15.3.2`, verifies a UDP frame from port 45000 to
45001 containing `OS-UDP-REQ-15.3B` with valid IPv4 and UDP checksums, then
sends `OS-UDP-REPLY-15.3B` from 45001 to 45000 with a valid checksum. Guest
serial confirms the user process verified source IP/port and payload, closed
the socket, exited with status zero, and the e1000 stopped with bus mastering
disabled. Host netstack tests cover UDP frame construction, checksum
validation, IPv4 checksum omission rules, payload boundaries, and malformed
UDP lengths/checksums.

### Consequences and limitations

The initial service is initialized with the static `10.15.3.1/24` profile
whenever the supported e1000 device starts. It polls receive only while a
send resolves ARP or a process calls `recvfrom`; there is no asynchronous
worker, so unsolicited packets are not serviced until a poll occurs. A
`recvfrom` poll runs synchronously in the current syscall for up to five
seconds and can occupy the single CPU. Short shared socket-table and netstack
state transitions mask local interrupts so timer preemption cannot interleave
those updates; this does not provide SMP locking. The fixed ephemeral
assignment is not queryable. Binding and queue behavior are exercised by the QEMU user fixture;
comprehensive socket-table exhaustion, queue-overflow, fork-sharing, and
timeout tests remain to be added. TCP, DHCP, IPv6, VLAN, IP forwarding,
fragmentation, routing/gateways, retransmission policy, socket options,
multithreading, SMP synchronization, and broad POSIX/BSD/Linux compatibility
remain out of scope. Phase 15 is incomplete.
