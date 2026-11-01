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

The native build uses installed Clang/LLD and LLVM ar/ranlib for
`x86_64-unknown-none-elf`; this triple selects the architecture code
generation but does not supply an OS SDK. The script configures and builds
static musl only, with no compiler runtime, and installs to the generated
sysroot at `build/musl-port/sysroot/`. The output also contains detailed
logs and `build-info.json`. Only the dedicated generated output directory is
replaced on rebuild, and only when its ownership marker matches.

Commands from the repository root:

```sh
make native-musl-verify
make native-musl-build
make native-musl-hello
make native-musl-inspect
make native-musl-qemu-test
```

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
