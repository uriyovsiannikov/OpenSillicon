# Phase 0: Toolchain and Boot Skeleton

## Implemented

- Freestanding x86_64 C11 kernel entry point with minimal COM1 serial logging.
- Limine v12.9.6 protocol request markers and base revision 3 request.
- ELF64 kernel linker layout with isolated executable, request, and read-only segments.
- Make targets for building the kernel, creating a UEFI ISO, and launching QEMU.
- Pinned Limine x86_64 UEFI executable and UEFI CD image, including provenance,
  artifact hashes, BSD-2-Clause license, and required third-party notices.

## Build and boot

The approved toolchain is the installed system GCC and GNU binutils. The
measured versions are GCC 16.2.1 and GNU ld 2.46.1. GCC builds freestanding
x86_64 code with `-ffreestanding`, `-mno-red-zone`, disabled stack protection,
and warnings as errors. GNU ld links directly with `-nostdlib -static`; the
kernel has no dynamic section or unresolved symbols. No Linux CRT, libc, or
other userspace runtime is linked. Kernel and userspace flags are not shared.

Commands used successfully:

```sh
make clean all
make iso
timeout 12s qemu-system-x86_64 \
  -machine q35 -m 256M \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -cdrom build/opensillicon.iso \
  -serial file:build/serial.log -display none -no-reboot -no-shutdown
```

The kernel build completed without warnings. The ELF was confirmed to be
x86_64, with no dynamic section and no undefined symbols. The ISO was booted
under OVMF in QEMU; the QEMU process was stopped by the 12-second timeout after
the kernel halted. `build/serial.log` contained `OpenSillicon: booted`.

## Bootloader provenance

Limine v12.9.6 is pinned to commit `89a9512c623a4779cc73e3869383876d54264b28`.
The official binary archive checksum was verified against the published
SHA-256. The protocol header is pinned to commit
`6e85ddd6f928b5b512015c4d47d1cfce2fd28079`. Full artifact hashes and license
details are in [PROVENANCE.md](../limine/PROVENANCE.md). The UEFI image follows
the official Limine UEFI CD image layout; BIOS remains deferred.
