This code prepares x86_64 processors for kernel execution. It sets up the Global Descriptor Table, manages floating-point state, discovers processor topology, and starts secondary CPUs for multiprocessor operation.

Online APs handle TLB shootdowns, synchronous kernel-work broadcasts from the BSP, and CPU-local cooperative kernel threads. A broadcast callback runs concurrently on the BSP and every online AP; it must split shared work by logical CPU, remain non-blocking, and leave address-space mappings unchanged. User processes are still scheduled only on the BSP.
