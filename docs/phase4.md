# Phase 4: Virtual Memory Manager

## Implemented

- Added x86_64 four-level 4 KiB page mapping and unmapping with write, user,
  execute, and uncached flags. NX is enabled, write protection is enabled, and
  writable-executable mappings are rejected.
- Adopted Limine's active page-table root and retained its kernel and HHDM
  mappings. Newly added kernel mappings are shared with all registered address
  spaces.
- Added isolated lower-half address-space roots. Create/destroy manages page
  table pages; mapped leaf frames remain owned by the caller. Address-space
  activation loads CR3.
- Added virtual-range validation for the HHDM, kernel image, vmalloc and heap
  regions defined in ADR-002.
- Mapped the local APIC MMIO page uncached, enabled xAPIC mode, masked its LVT
  sources, and installed its spurious-vector behavior. IOAPIC remains deferred
  to Phase 10 and x2APIC is unsupported.
- Added a coalescing kernel heap with multi-page allocations and free-list
  reuse. Returned pages stay mapped for reuse.
- Kept page faults fatal with the existing vector, error-code, RIP, and CR2
  diagnostics. Demand paging remains deferred.

The allocator and VMM are single-core and have no locking; scheduling and SMP
are not enabled yet. Limine's boot page tables are extended in place rather
than rebuilt. Unmapping does not reclaim empty kernel page-table levels.

## Verification

```sh
make clean all iso test fault-test interrupt-test vmm-protection-test
timeout 8s qemu-system-x86_64 \
  -machine q35 -m 256M \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -cdrom build/opensillicon.iso \
  -serial file:build/serial.log -display none -no-reboot -no-shutdown
```

- Host boot-info and PMM tests passed.
- QEMU page-fault regression reported vector 14, error 2, CR2 0.
- QEMU software-interrupt regression routed vector 128 to the unexpected
  interrupt panic.
- QEMU VMM protection test wrote to a read-only page and reported vector 14,
  error 3, CR2 `0x400000`.
- Normal QEMU UEFI boot initialized the VMM, LAPIC (ID 0), heap, and address
  spaces; heap allocation/coalescing and user-page map/write/unmap smoke tests
  completed. The serial log reported 53,473 free physical pages from 57,860
  frame slots. QEMU is stopped by the timeout after the kernel halts.
