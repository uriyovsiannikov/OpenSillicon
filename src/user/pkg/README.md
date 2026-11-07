# OpenSillicon packages, version 1

The native package manager lives in `src/user/pkg/`. It is a static libos
program installed as `/bin/pkg`. The archive parser, SHA-256 implementation,
and client also build on Linux for integration tests.

## Build and run

From the repository root:

```sh
make root
make src/build/pkg/pkg-host src/build/pkg/pkg-server \
  src/tools/pkg/repo/_pkges/hello-1.0.0.spkg
src/build/pkg/pkg-server --repo src/tools/pkg/repo
```

The last command runs the C repository server in the foreground on
`127.0.0.1:8080`. QEMU user networking exposes the host at `10.15.3.2` with
this project's VM configuration. The normal guest shell runs as UID 1000;
use a writable installation prefix there:

```sh
pkg -r /tmp/packages update
pkg -r /tmp/packages search hello
pkg -r /tmp/packages install hello
/tmp/packages/bin/hello
pkg -r /tmp/packages list
pkg -r /tmp/packages info hello
pkg -r /tmp/packages verify hello
```

Without `-r`, package paths are rooted at `/` and write operations require
root privileges. The manager does not elevate the ordinary shell. With
`-r`, payloads, cache, configuration and the package database all live under
that prefix; use the same prefix for subsequent commands. `/tmp` is stored
on the current SilliconFS disk and is not cleared by the package manager.

`pkg update` refreshes the repository **index**, not installed applications.
The default repository is `http://10.15.3.2:8080`. Set another address with
`pkg -R http://host:port update` and the same option on `install`, or put that
URL alone on the first and only line of `/etc/pkg.conf`. An optional final
newline is accepted. Hostnames resolve through the guest's IPv4 DNS API.
`pkg add /path/to/file.spkg` installs a local archive without a server.

To run the server with another repository or listen address:

```sh
src/build/pkg/pkg-server --repo src/tools/pkg/repo --bind 127.0.0.1 --port 8080
```

It serves `GET /index` and `GET /packages/<name>-<version>.spkg`. Source
files are never served. The index is generated from the current archives on
each request; restart is not needed after adding a package. Keep one version
per package name in `_pkges/`; duplicates or malformed archives produce an
explicit repository error. Publish host archives with an atomic rename, as
`pkg-build` does, so readers see a complete old or new file.

## Repository layout and building a package

```text
src/tools/pkg/
  build.c                   host archive builder
  server/server.c           host HTTP server
  repo/
    _pkges/                 ready-to-install .spkg archives
    _src/
      hello/
        hello.c             native program source
        Makefile            package build entry point
```

`make -C src/tools/pkg/repo/_src/hello` builds the example for OpenSillicon and places
`hello-1.0.0.spkg` in `_pkges/`. The generated archives are ignored by Git;
keep package sources and recipes in `_src/<name>/`. The package recipe exposes the generated archive as its file target.

For another package, compile its program against libos and place the desired
filesystem tree in a staging directory. The builder accepts regular files
under `bin/`, `usr/`, and `opt/`; directories are implicit. Programs intended
for shell lookup belong in `bin/`.

```sh
src/build/pkg/pkg-build example 1.0 /absolute/path/to/stage \
  src/tools/pkg/repo/_pkges/example-1.0.spkg 'Example application'
```

The builder sorts paths for deterministic output. It accepts no symlinks,
devices, special mode bits, install scripts, or
absolute/parent-traversal paths. Hardlinked regular input files are copied
as independent regular files. Files normalize to mode `0755` if any execute
bit is set, otherwise `0644`. Packages target native x86-64 OpenSillicon;
the builder does not cross-compile arbitrary Linux binaries.

## Installation and database

Before writing payloads, the client validates the whole archive, all file
hashes, the destination paths, and existing file conflicts. Remote downloads
also have to match the index's archive size, SHA-256, name, and version.
Existing destination files are never overwritten. VFS now supports
`O_CREAT | O_EXCL`, which the client uses for records and payload files.

Records live at `/var/db/pkg/<name>.db`. A complete manifest with state
`incomplete` is written before copying files; the state changes to `installed`
only after all writes and permission changes succeed. `pkg list`, `info`,
and `verify` expose incomplete state. Verification checks file length,
SHA-256, and permission bits. Filesystem writes currently have no userland
`fsync` API, so this is not a power-loss-atomic transaction.

Version 1 installs new packages only. It has no dependency resolution,
package replacement, removal, rollback, or post-install hooks. The kernel's
`unlink`/`rename` operations are not available yet. An interrupted installation
keeps its record and any written files rather than silently claiming success;
a repeat install refuses that name. Recovery currently requires restoring a
known-good disk image or offline filesystem maintenance. Old JSON records
from the previous placeholder tool are not imported.

The transport is plain HTTP for a trusted development host/LAN. SHA-256
checks detect damaged or inconsistent downloads; this version has no signed
repository metadata or TLS. Archives are bounded to 16 MiB and 128 files;
indexes to 256 packages and 256 KiB. The HTTP client uses a 30-second request
deadline, fixed-length responses, and handles short/nonblocking I/O. The
host server handles one connection at a time.

## Tests

```sh
make src/build/pkg/pkg-host src/build/pkg/pkg-build src/build/pkg/pkg-server \
  src/build/pkg/pkg-vfs-test
src/build/pkg/pkg-vfs-test
python3 src/tests/pkg_test.py --build-dir src/build/pkg
```

Host tests exercise deterministic archives and independent SHA-256 vectors,
HTTP installation, installed-file verification, incomplete records, conflicts,
path traversal, symlinks, duplicate versions, and broken HTTP responses.
The standalone harness `src/tests/pkg_qemu_test.py` creates a fresh temporary
SilliconFS disk and checks the native client against the C server. Run it
with `--help` for its artifact arguments. It boots eight CPUs, fetches the
index, installs `hello`, verifies it and executes it without altering
`src/build/root.img`.

For Linux-side manual tests, the same client can install into an isolated
root directory:

```sh
mkdir -p /tmp/pkg-root
src/build/pkg/pkg-host -r /tmp/pkg-root -R http://127.0.0.1:8080 update
src/build/pkg/pkg-host -r /tmp/pkg-root -R http://127.0.0.1:8080 install hello
```

## Wire format

All integers are unsigned little-endian 32-bit values. All text fields are
NUL-terminated within their fixed width. No host structs are serialized.

Archive header (304 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | ASCII `OSPKG001` |
| 8 | 4 | Complete archive length |
| 12 | 4 | File count |
| 16 | 64 | Package name |
| 80 | 32 | Version |
| 112 | 192 | Printable description |

Each file follows as a 232-byte header and exactly `size` payload bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 192 | Relative installation path |
| 192 | 4 | Mode (`0644` or `0755`) |
| 196 | 4 | Payload size |
| 200 | 32 | Binary SHA-256 of payload |

A database record uses magic `OSDB0001`, replaces the archive length with
installation state (`0` pending, `1` installed), and retains the package and
file headers without payload bytes. Trailing bytes and conflicting paths
are rejected in both formats.

The text index starts with `OSPKG-INDEX-1\n`. Each subsequent line has six
TAB-separated fields and ends with LF:

```text
name<TAB>version<TAB>archive-size<TAB>sha256-hex<TAB>filename<TAB>description
```

Names and versions use ASCII letters, digits, `+`, `-`, `_`, and `.`, and
cannot begin with `.` or `-`. The archive filename must be exactly
`<name>-<version>.spkg`. Descriptions cannot contain tabs or newlines.
