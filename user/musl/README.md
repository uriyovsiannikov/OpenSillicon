# musl source bootstrap

This directory pins source provenance and reviewed patches for the OpenSillicon
native musl port. It does not contain musl source. The baseline is for
development and porting research only; it is not security-qualified for
distribution.

## Source and notices

The source is the official musl 1.2.6 release archive at
`https://musl.libc.org/releases/musl-1.2.6.tar.gz`, SHA-256
`d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`.
`manifest/source.json` is the machine-readable source lock. The archive's
`COPYRIGHT` SHA-256 is
`b870108ec5e7790e9f9919064f1b9421d62d5f9b0e6c230c6adf7ea2da62e97b`.
musl's main license is MIT; retain `COPYRIGHT` and all applicable per-file
copyright and license notices in generated source and any later distribution.
The upstream release publishes a detached signature alongside the archive.
It verifies with primary key fingerprint
`8364 8929 0BB6 B70F 99FF DA05 56BC DB59 3020 450F`; this key
was not certified by the local GPG trust database. A valid signature proves
the signature matches that key, not the real-world identity of the key owner.
Archive SHA-256 verification is mandatory regardless of signature status.

`manifest/patches.json` pins the exact patch bytes and their order. The three
patches under `patches/upstream/` are reviewed upstream fixes. The patch under
`patches/candidates/` is an unaccepted local candidate and is never applied to
the default confirmed-patched tree. OpenSillicon patch series, if later
approved, must be tracked separately from both sets. Patch files are
identified as OpenSillicon bootstrap inputs; they do not replace upstream
notices or rewrite upstream license files.

## Generated output

Generated archives, source trees, and experiments stay under the ignored
`build/musl-bootstrap/` directory by default, never under this tracked source
directory. Override the location with `OPENSILLICON_MUSL_OUT`. The bootstrap
does not configure, compile, or install musl or modify the system installation.

The output layout is:

```text
build/musl-bootstrap/
  downloads/musl-1.2.6.tar.gz
  source/upstream/musl-1.2.6/       # pristine archive extraction
  source/patched/musl-1.2.6/        # three confirmed patches only
  source/experimental-regexec/musl-1.2.6/  # only when explicitly requested
```

Existing output is never overwritten. Re-running verifies existing output
against a fresh extraction and patch application; a mismatch fails with an
instruction to choose a new output directory. The script does not delete
unrelated output.

## Commands

Verify an existing archive and all patch artifacts without compiling musl:

```sh
python3 user/musl/scripts/verify.py \
  --archive /path/to/musl-1.2.6.tar.gz
```

Optionally verify a detached signature against an explicitly supplied public
key (the fingerprint is pinned and checked; GPG trust is not inferred):

```sh
python3 user/musl/scripts/verify.py \
  --archive /path/to/musl-1.2.6.tar.gz \
  --signature /path/to/musl-1.2.6.tar.gz.asc \
  --key /path/to/musl-release-key.asc
```

Bootstrap from an existing archive, or omit `--archive` to download the
official archive when it is not cached:

```sh
OPENSILLICON_MUSL_OUT="$PWD/build/musl-bootstrap" \
  python3 user/musl/scripts/bootstrap.py \
  --archive /path/to/musl-1.2.6.tar.gz
```

Re-run verification against the bootstrap cache and inspect the separate
pristine and patched trees:

```sh
python3 user/musl/scripts/verify.py \
  --output "$PWD/build/musl-bootstrap"
diff -ru build/musl-bootstrap/source/upstream/musl-1.2.6 \
  build/musl-bootstrap/source/patched/musl-1.2.6
```

To create an isolated experimental tree with the local candidate, first
bootstrap the confirmed tree, then copy it to a new path and apply only the
candidate there. Do not target either default source tree:

```sh
repo_root=$(git rev-parse --show-toplevel)
out_root=${OPENSILLICON_MUSL_OUT:-"$repo_root/build/musl-bootstrap"}
candidate_tree="$out_root/source/experimental-regexec/musl-1.2.6"
test ! -e "$candidate_tree" || { echo "candidate tree already exists" >&2; exit 1; }
mkdir -p "$(dirname "$candidate_tree")"
cp -a "$out_root/source/patched/musl-1.2.6" "$candidate_tree"
(cd "$candidate_tree" && patch --batch --forward --fuzz=0 -p1 \
  -i "$repo_root/user/musl/patches/candidates/regexec-backref-candidate.patch")
```

This opt-in procedure does not change the default bootstrap or promote the
candidate to upstream status. Keep experimental results separate and report
the candidate's explicit status.

## Native OpenSillicon build (Stage 14J)

`user/musl/opensillicon/` contains the target-specific syscall and
x86_64 thread-area adapter overlays. They are applied only to a generated
copy of the confirmed-patched tree. The pristine extraction and confirmed
patch output under `build/musl-bootstrap/` are not edited.

The validated LLVM toolchain is **22.1.8**: Clang, LLD, `llvm-ar`,
`llvm-ranlib`, `llvm-objdump`, and `llvm-nm` must all report that exact
release. This is the toolchain version recorded by the existing native build
metadata and used by the clean verification recorded below. The build checks
all required tools and versions before creating or replacing its output. If
`clang` resolves through ccache, it selects the corresponding versioned Clang
binary (for example `clang-22`) directly, avoiding dependence on a writable
ccache. GNU Make is required but its version is recorded rather than pinned.
Fixture links read the compiler and linker paths from the build metadata.
Clang delegates this target's link step through the detected host GCC driver;
the driver path and version are recorded, and the validated LLD directory is
put first in `PATH` for both musl and fixture links. GCC is recorded rather
than version-pinned because the validated output depends on the LLVM tools and
selected LLD. QEMU is selected by `QEMU`; record its resolved path and version
with the test results. The verified environment used GNU Make 4.4.1, GCC
16.2.1, and `/usr/bin/qemu-system-x86_64` 10.2.2.

The target is `x86_64-unknown-none-elf`; this triple selects x86-64 code
generation but does not supply an OS SDK. Configuration builds static musl
only, with no compiler runtime. The mallocng integer-only rebuild remains
required because the kernel does not preserve SIMD state for those allocator
objects. The sysroot, logs, and actual executable paths/versions are recorded
under `build/musl-port/` by default. An output directory is replaced on
rebuild only if its OpenSillicon ownership marker matches. Use a new output
path to preserve existing generated artifacts.

Commands from the repository root:

```sh
make native-musl-verify
make native-musl-build
make native-musl-hello
make native-musl-inspect
make native-musl-qemu-test
make native-musl-alloc-qemu-test
make native-musl-stdio-qemu-test
```

All generated paths can be isolated. For example, after copying the pinned
archive into a new temporary bootstrap directory, set `MUSL_BOOTSTRAP_DIR`,
`MUSL_BUILD_DIR`, and the three `MUSL_*_QEMU_BUILD_DIR` variables to fresh
directories. `native-musl-verify`, `native-musl-build`, the fixture link
targets, `native-musl-inspect`, and all QEMU recipes honor those locations.
The Make targets do not clear the configured QEMU directories before use;
choose empty paths for a clean run.

`native-musl-inspect` checks all four fixtures for static ELF output, no
`PT_INTERP`/`PT_DYNAMIC`, and no unresolved strong symbols. Each QEMU target
keeps the guest running after its musl harness reports success, so the host
`timeout` is expected to return 124. The target then requires the guest
success markers from the serial log. The kernel marker is emitted only after
the musl fixture returns status zero; timeout alone is not a pass.

`native-musl-qemu-test` builds a static C fixture with musl CRT, a small TLS
image, `open` errno handling, libc `write()`, and normal exit, then boots it
on the persistent SilliconFS root under QEMU. The current adapter is a narrow
The native adapter remains a bounded profile, not general musl/POSIX support.
Unsupported operations return ENOSYS. Before Stage 14K-A, `mmap`,
`mprotect`, and `munmap` were unsupported; they now have only the limited
private anonymous semantics described below. `brk`, `mremap`, threads, and
dynamic linking remain unsupported. Bounded stdio `writev`, `readv`, and
`TIOCGWINSZ` are provided by the Stage 14K-B adapter described below. The local
`regexec` candidate remains unapplied by default and is unrelated to the
native adapter overlays.

## Stage 14K-A private mappings

The native kernel ABI appends syscall 21 `mmap(size, protection)`, syscall
22 `munmap(base, length)`, and syscall 23 `mprotect(base, length,
protection)`. The musl overlay accepts only null-hint,
`MAP_PRIVATE|MAP_ANONYMOUS`, fd -1, offset-zero mapping requests and
translates them to this OpenSillicon ABI. Supported protection modes are
`PROT_NONE`, read, write, and read/write; executable and unknown modes are
rejected. `munmap` releases a complete mapping only. Its length may be rounded
to the original extent, but partial unmap is not supported. Mappings are
zero-filled; requested read/write/no-access protections are enforced while
all states remain NX. Mappings are owned by one address space and allocated
from the private arena using its monotonic no-reuse cursor. See ADR-033.

The Stage 14K-A build compiles mallocng's six integer-only C implementation
objects with MMX/SSE disabled because the kernel does not yet preserve FPU/
XSTATE across user execution. Other musl objects retain the normal x86_64
flags. The build checks those allocator objects for SIMD register
instructions. The QEMU allocator fixture exercises small and large
`malloc`/`free`, `calloc`, `realloc`, protection changes, TLS, output, and
normal exit. `brk` failure and `mremap` failure use mallocng's fallback
paths; file-backed mappings, executable mappings, partial unmap, threads,
and broad compatibility remain unsupported.

## Stage 14K-B stdio and terminal I/O

The native adapter maps musl's `SYS_writev`, `SYS_readv`, and `SYS_ioctl` to
OpenSillicon syscalls 24, 25, and 26. The kernel accepts at most 16 iovecs
and 64 KiB total per vectored operation, validates the complete iovec list
and all nonempty user ranges before I/O, and transfers through the process
descriptor table. Partial transfers return the completed byte count. The
only supported ioctl is `TIOCGWINSZ`; unsupported requests and non-terminal
descriptors return `ENOTTY`, while invalid descriptors return `EBADF`.
Terminal identity is an operation on the descriptor object, not a descriptor
number check. Dimensions come from framebuffer-derived TTY geometry when
available; without a usable framebuffer the current TTY initializes to its
documented 80-by-25 fallback. See ADR-035.

`make native-musl-stdio-qemu-test` runs `tests/musl_stdio.c` against this
adapter and the real kernel. It covers stdio output, `isatty`, winsize and
unsupported ioctl errors, VFS `fgets`/`readv`, allocator use, TLS, and normal
exit. This is a bounded native interface, not full POSIX termios or general
Linux syscall compatibility.

### `CLOCK_MONOTONIC` support

OpenSillicon syscall 34 implements the bounded `clock_gettime` operation.
The musl adapter maps its Linux-shaped `SYS_clock_gettime` selector to native
arguments `(clock_id, timespec_pointer)` using the existing `int 0x80` ABI.
Only `CLOCK_MONOTONIC` is accepted; other IDs, including `CLOCK_REALTIME`,
return `EINVAL`. A null or invalid output range returns `EFAULT`.

The source is the kernel's 100 Hz periodic LAPIC timer. Its initial count is
calibrated against one PIT channel-2 interval programmed from the PIT's
1,193,182 Hz input clock. Each timer interrupt advances a saturating 64-bit
tick counter; the counter is initialized once with the timer and is not adjusted
by wall-clock changes. The syscall converts each tick to 10,000,000 ns using
integer quotient/remainder arithmetic, so `tv_nsec` is always in
`[0, 999999999]`. The `timespec` unit is nanoseconds, but the clock resolution
is one nominal 10 ms timer tick, with accuracy limited by PIT/LAPIC calibration
and interrupt delivery. This is elapsed tick time, not an epoch or a claim of
nanosecond accuracy. Saturation prevents counter wrap and backward movement.

`make native-musl-clock-qemu-test` builds and runs `tests/musl_clock.c`. It
checks valid reads, nondecreasing values, advancement after sleeping for two
kernel timer ticks, unsupported-clock `EINVAL`, and invalid-pointer `EFAULT`.
The fixture uses the existing sleep-ticks syscall only to wait for known timer
events; musl `nanosleep` is not implemented. As with the other QEMU fixtures,
the test target expects timeout status 124 after checking the kernel's success
marker; the guest fixture itself must exit with status zero for that marker to
appear.

Other time interfaces, including `CLOCK_REALTIME`, `gettimeofday`,
`nanosleep`, and POSIX timers, remain unsupported.

### Clean verification procedure

The commands below preserve the default `build/musl-bootstrap/`,
`build/musl-port/`, and existing QEMU build directories. Supply the already
pinned archive as the bootstrap input; no network download is needed.

```sh
tmp=$(mktemp -d /tmp/opensillicon-musl.XXXXXX)
mkdir -p "$tmp/bootstrap/downloads"
cp build/musl-bootstrap/downloads/musl-1.2.6.tar.gz \
  "$tmp/bootstrap/downloads/musl-1.2.6.tar.gz"
make MUSL_BOOTSTRAP_DIR="$tmp/bootstrap" native-musl-verify
make MUSL_BOOTSTRAP_DIR="$tmp/bootstrap" MUSL_BUILD_DIR="$tmp/port" \
  native-musl-build native-musl-inspect
make MUSL_BOOTSTRAP_DIR="$tmp/bootstrap" MUSL_BUILD_DIR="$tmp/port" \
  MUSL_QEMU_BUILD_DIR="$tmp/qemu-hello" native-musl-qemu-test
make MUSL_BOOTSTRAP_DIR="$tmp/bootstrap" MUSL_BUILD_DIR="$tmp/port" \
  MUSL_ALLOC_QEMU_BUILD_DIR="$tmp/qemu-alloc" native-musl-alloc-qemu-test
make MUSL_BOOTSTRAP_DIR="$tmp/bootstrap" MUSL_BUILD_DIR="$tmp/port" \
  MUSL_STDIO_QEMU_BUILD_DIR="$tmp/qemu-stdio" native-musl-stdio-qemu-test
make MUSL_BOOTSTRAP_DIR="$tmp/bootstrap" MUSL_BUILD_DIR="$tmp/port" \
  MUSL_CLOCK_QEMU_BUILD_DIR="$tmp/qemu-clock" native-musl-clock-qemu-test
```

The native build target bootstraps pristine and confirmed-patched source in
`$tmp/bootstrap`, then builds a copied, overlaid source tree and sysroot in
`$tmp/port`. The four QEMU builds and their serial logs are isolated in the
specified directories. Keep the output and serial logs with the verification
record when actual command exit statuses and markers need to be audited.

### Monotonic clock verification — 2026-11-02

Source verification and the fresh native build used an isolated bootstrap
directory containing only the pinned archive and a sysroot at
`/tmp/opensillicon-musl-repro.dcO2Hy/port-clock`. This verified that
`native-musl-build` creates pristine and patched source from an archive-only
bootstrap directory. Build and fixture inspection returned status 0. Hello,
allocator, stdio, and clock fixtures were linked against this sysroot; all
four were static, had no `PT_INTERP`/`PT_DYNAMIC`, and had no unresolved
strong symbols.

The three existing QEMU targets and `native-musl-clock-qemu-test` each
returned status 0 after the expected markers appeared. The clock marker was
`OpenSillicon: native musl CLOCK_MONOTONIC and error paths passed`; the fixture
checked nondecreasing reads, advancement after a two-tick kernel sleep,
unsupported-clock and invalid-pointer errors, and retained TLS. QEMU is
intentionally terminated by the host timeout after the guest returns zero.

The existing `timer-test` target's 8-second timeout expired before its timer
markers on this host. The same isolated image was rerun with a 30-second
timeout; that invocation returned status 0 after expected timeout status 124
and logged `OpenSillicon: Round Robin scheduler passed` and
`OpenSillicon: timer tick count 73`.

### Verification record — 2026-11-01

The commands above were run from the pinned local musl archive in isolated
`/tmp/opensillicon-musl-repro.dcO2Hy/` outputs. All listed Make/Python commands
exited **0**. Source verification checked archive SHA-256
`d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`, version
1.2.6, COPYRIGHT hash, three confirmed patch hashes, and clean patch
application. Detached signature verification was skipped because no
signature/key pair was supplied.

The final `native-musl-build` invocation used a fresh
`/tmp/opensillicon-musl-repro.dcO2Hy/port-final2` output. It configured,
compiled, rebuilt the six mallocng objects with SIMD disabled, installed the
sysroot, and recorded all LLVM 22.1.8 tool paths plus the actual GCC link driver
in `build-info.json`. Two independent clean outputs produced identical installed archive
hashes: `libc.a` SHA-256
`9001d4ef4324644d1c5c270cd1411b0f57f201607b9e93de44dc0d2a7321a64f`. The
`native-musl-inspect` linked with the recorded compiler and selected LLD, then
checked hello, allocator, and stdio
fixtures; each was static, had no `PT_INTERP`/`PT_DYNAMIC`, and no unresolved
strong symbols.

The three QEMU Make targets each exited 0 after their expected `timeout`
child returned 124. The timeout is intentional: QEMU remains running after the
guest fixture returns. The hello run logged
`OpenSillicon: native musl 1.2.6 CRT, TLS, errno, write, and exit passed`; the
allocator run logged `OpenSillicon: native musl malloc, mmap, mprotect,
munmap, TLS, and exit passed`; and the stdio run logged
`OpenSillicon: native musl stdio QEMU fixture exited with status zero` plus
the expected printf/fprintf/puts/fwrite/writev output. The kernel emits those
completion messages only after each guest fixture returns status zero. The
test runtime was `/usr/bin/qemu-system-x86_64`, QEMU 10.2.2. Its per-run
serial logs were retained under the isolated QEMU output directories.
