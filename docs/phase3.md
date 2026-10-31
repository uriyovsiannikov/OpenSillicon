# Phase 3: Physical Memory Manager

## Implemented

- Added a bitmap PMM indexed by physical frame number, through the highest
  usable memory-map range. Gaps and non-usable map entries remain unavailable.
- Allocated the bitmap from the bootstrap allocator and reserved every physical
  page it occupies before exposing free pages.
- Allocated and released aligned 4 KiB physical pages.
- Rejected unaligned addresses, non-usable or metadata pages, double frees,
  invalid allocation outputs, and exhaustion.
- Added free-page and frame-slot diagnostics.

One bitmap is sufficient because `pmm_free_page` also checks that the full page
lies in a Limine-usable region and rejects the PMM metadata range. The bootstrap
allocator is not used again after PMM initialization.

## Verification

```sh
make clean all iso test
timeout 8s qemu-system-x86_64 \
  -machine q35 -m 256M \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -cdrom build/opensillicon.iso \
  -serial file:build/serial.log -display none -no-reboot -no-shutdown
```

Host tests cover metadata-page reservation, allocation from usable pages,
reserved-region exclusion, aligned and invalid frees, double-free rejection,
and exhaustion, including a map with no pages remaining after bitmap
reservation. QEMU boots through UEFI, initializes the PMM, allocates and frees a
page, and reports 53,491 free pages from 57,860 physical frame slots in the
256 MiB test VM.
