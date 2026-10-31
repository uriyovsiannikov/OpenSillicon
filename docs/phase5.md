# Phase 5: Timer and Scheduler

## Implemented

- Calibrated the LAPIC periodic timer against PIT channel 2, using divisor 16
  and a 10 ms interval. Both legacy PICs are masked before the timer starts.
- Registered interrupt handlers for external vectors. The timer handler advances
  a monotonic 100 Hz tick counter and sends LAPIC EOI; it never switches threads
  in interrupt context.
- Added x86_64 context switching for callee-saved general registers and a
  Round Robin kernel-thread queue with private 16 KiB stacks and cooperative
  `thread_yield()`.
- Added per-CPU scheduler state behind the scheduler interface and a minimal
  process scheduling record. This phase runs one kernel process and one CPU.
- Added QEMU checks that two threads interleave in Round Robin order and that
  the LAPIC delivers at least five timer ticks.

HPET discovery, TSC timekeeping, wall clock, sleep queues, and user preemption
remain deferred. User preemption begins in Phase 6. Scheduler queue changes
disable interrupts on the single active CPU; general locks, SMP run queues,
and FPU/SSE thread state remain unimplemented. `thread_yield()` is a kernel
thread API and must be called with interrupts enabled.

## Verification

```sh
make clean all iso test fault-test interrupt-test vmm-protection-test timer-test
```

- Full project build: PASS with GCC `-Wall -Wextra -Werror`.
- Host boot-info/PMM tests: PASS.
- Page-fault, software-interrupt, and VMM protection QEMU tests: PASS.
- `timer-test`: PASS; QEMU serial reports `Round Robin scheduler passed` and a
  timer tick count of 5 or greater.
- Normal UEFI QEMU boot completes the scheduler and timer checks, reports
  initialization over serial, and halts. The test harness stops QEMU by timeout.
