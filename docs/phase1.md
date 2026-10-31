# Phase 1: Boot Information and Early Memory

## Implemented

- Requested Limine firmware type, HHDM offset, memory map, and framebuffer.
- Required EFI64 firmware and a nonzero page-aligned HHDM offset.
- Validated memory map responses, entry pointers, nonempty ranges, arithmetic
  overflow, and overlapping entries. Only `LIMINE_MEMMAP_USABLE` regions are
  eligible for allocation; unknown and reserved types remain unavailable.
- Validated the first framebuffer's RGB format, dimensions, depth, address, and
  minimum pitch. A missing framebuffer is accepted for headless operation.
- Added a page-independent, aligned bump allocator using HHDM virtual addresses.
  Invalid requests and exhausted usable regions return `NULL`.

The allocator is intentionally bootstrap-only: it does not free allocations or
manage pages after initialization.

## Verification

```sh
make test
make clean all iso
timeout 12s qemu-system-x86_64 \
  -machine q35 -m 256M \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -cdrom build/opensillicon.iso \
  -serial file:build/serial.log -display none -no-reboot -no-shutdown
```

Host tests cover missing responses, non-UEFI firmware, invalid HHDM alignment,
empty, overlapping, and overflowing memory maps, malformed and absent
framebuffers, aligned allocations, invalid allocation requests, exhaustion,
and HHDM arithmetic overflow. QEMU serial output confirms EFI64 boot info,
33 memory map entries, a 1280x800 framebuffer, and a successful bootstrap
allocation.

Early kernel code disables MMX/SSE code generation because the bootloader does
not enable OS support for SIMD state; CPU/FPU initialization is outside this
milestone.
