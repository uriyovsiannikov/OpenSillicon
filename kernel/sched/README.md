The scheduler maintains CPU-local cooperative kernel-thread queues. Threads can be pinned to a logical CPU; queue insertion is synchronized and wakes a sleeping AP with an IPI. AP scheduling starts after the heap and BSP scheduler are ready. User processes remain scheduled by the process subsystem on the BSP. AP heap allocations can extend the kernel heap through synchronized kernel page-table updates and TLB shootdowns; other VMM mapping changes remain BSP-only.

`make smp-scheduler-qemu-test` builds a separate kernel with
`KERNEL_TEST_SMP_SCHEDULER`. It runs two pinned threads on each online CPU,
checks their CPU and address space through repeated yields, exercises heap
allocation on APs, and verifies that exiting threads leave only the bootstrap
thread and no unreaped stacks. Invalid and offline CPU targets are rejected.
The production kernel does not run this fixture during normal startup.

Each test thread also submits a child to another online AP (or its own CPU
when no other AP exists). Children check CPU affinity, yield, allocate and free
heap memory, and exit before the fixture checks queue cleanup. This exercises
concurrent producers on remote queues. `make smp-scheduler-partial-qemu-test`
injects a startup failure on logical CPU 1 and verifies that submissions skip
that CPU while surviving queues continue to run.
