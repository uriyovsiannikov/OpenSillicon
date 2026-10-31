# Phase 2: CPU, Exceptions and Interrupts

## Implemented

- Replaced the bootloader GDT with kernel and user code/data descriptors.
- Installed a 64-bit TSS with a dedicated kernel stack for privilege changes.
- Installed a 256-entry IDT with vector-specific assembly stubs.
- Added common register-save/restore and interrupt-frame handling.
- Added fatal exception diagnostics for vector, error code, and RIP, followed by
  a safe halt.
- Added serial logging and panic helpers used by CPU exception paths.

LAPIC setup is scheduled for Phase 4 after VMM MMIO mapping. QEMU demonstrated
that Limine's HHDM does not map the LAPIC MMIO page. IOAPIC setup is scheduled
for Phase 10 after ACPI MADT support through ACPICA. This dependency adjustment
is recorded in `ROADMAP.md` and `DESIGN.md`.

## Verification

```sh
make clean all iso test fault-test interrupt-test
timeout 10s qemu-system-x86_64 \
  -machine q35 -m 256M \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -cdrom build/opensillicon.iso \
  -serial file:build/serial.log -display none -no-reboot -no-shutdown
```

- `make test`: PASS; Phase 1 boot-info and allocator regression tests pass.
- `make fault-test`: PASS; QEMU serial reports page-fault vector 14, error 2.
- `make interrupt-test`: PASS; software interrupt vector 128 reaches the
  expected unexpected-interrupt panic path with its actual vector.
- Normal QEMU UEFI boot reaches `bootstrap allocator ready` without an
  exception or panic.

The test kernels halt after their expected diagnostics, so QEMU is stopped by
the test timeout. The normal kernel also halts after reporting initialization.
