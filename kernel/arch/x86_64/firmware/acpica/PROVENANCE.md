# ACPICA provenance

- Upstream: <https://github.com/open-acpica/acpica>
- Release tag: `20260930`
- Commit: `e85aa3bebaae81ed1e6d163511ac7e1eafe732cf`
- Release source archive: `acpica-unix-20260930.tar.gz`
- SHA-256: `aa18901b92e30749be0edc3081c8d550c61fce4fa37546fc6a65d367a4ae71a5`
- Selected license: BSD-3-Clause; see `LICENSE.BSD-3-Clause`.

The archive hash was independently checked before extraction. The vendored
subset includes upstream `source/include` and the `dispatcher`, `events`,
`executer`, `hardware`, `namespace`, `parser`, `resources`, `tables`, and
`utilities` component directories. ACPICA's public initialization and
shutdown objects have link-time dependencies on event and hardware symbols;
those units are linked, but Phase 10 does not call ACPI mode enablement, event
initialization, or power-management methods. It excludes host OSL implementations,
command-line tools, compiler, debugger, disassembler, and test suites. The
kernel supplies its own ACPICA OSL. `platform/acenv.h` is patched only to
select local `acopensillicon.h` configuration when `OPEN_SILLICON_KERNEL` is
defined; all other upstream platform selection is unchanged.
The kernel build applies `-Wno-unused-parameter` only to ACPICA source
translation units, because upstream callback and compatibility interfaces
intentionally leave parameters unused. ACPICA headers are treated as system
headers; the OpenSillicon OSL adapter is built with the normal `-Werror`
settings and no warning exception. A minimal local `ctype.h` avoids importing
host libc headers; required string and character helpers are implemented in
the kernel's freestanding string module.
The debug-only `components/resources/rsdump.c` translation unit is omitted;
no host utilities or standalone ACPICA applications are built.

The release is dual licensed under BSD-3-Clause or GPL-2.0-only. OpenSillicon
selects and retains the BSD-3-Clause component license and does not select or
include the GPL-only license option.
