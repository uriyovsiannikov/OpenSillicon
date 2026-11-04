# Kernel modules

`make modules` builds `build/modules/hello.ko`. The root image installs it in
`/boot/modules/hello.ko` and installs `kldload`, `kldstat`, and `kldunload` in
`/bin`. Rebuild the disk with `make build/root.img` and the kernel with `make iso`.

From a process with effective UID 0:

```sh
kldload /boot/modules/hello.ko
kldstat
kldunload hello
```

The unprivileged interactive shell cannot load or unload kernel code. Reading
the module list does not require root. Module names come from the filename with
the `.ko` suffix removed. Loading an existing name fails; unloading a pinned
module fails with `EBUSY`.

## e1000 boot driver

The e1000 driver is built only into `build/modules/e1000.ko`, including its
profile, ring, reset, and module entry code. The disk image installs it in
`/boot/modules`; the kernel loads it after mounting SilliconFS. A missing or
invalid module allows boot to continue without networking.

The module probes exactly one QEMU 8086:100e Ethernet device, checks the driver
API version, starts the device, and attaches it to the existing static network
profile (10.15.3.1/24, gateway 10.15.3.2, MAC 52:54:00:15:03:01).
The driver code is absent from the core kernel ELF.

e1000 currently refuses unloading with `EBUSY`: the network services retain its
callbacks for the entire boot. It also remains resident when hardware startup
or network attachment fails after initialization begins, since releasing code
after a failed DMA stop would be unsafe. Inspect `/tmp/btlog.log` for those
failures; a loaded module does not by itself guarantee a working link.

## Writing a module

Place a C source in `kernel/module/examples/NAME.c`, then run
`make build/modules/NAME.ko`. Follow `hello.c`: provide `int module_init(void)`
returning zero on success, and optionally `void module_fini(void)`. Initialization
failure releases mappings without calling `module_fini`; the initializer must
undo any external resources it created before returning an error.

Modules may also provide `int module_prepare_unload(void)`. It runs after the
loader has blocked new references and established that no references remain.
Return zero to allow `module_fini` and unmapping, or a nonzero value to refuse
with `EBUSY`. A refusal must leave the module usable; the loader makes it
available for new references again. Callbacks run without the module lock.

Use the module build flags: freestanding x86-64 ELF ET_REL, kernel code model,
no PIC, red zone, SIMD, unwind tables, or debug relocations. Modules can use
explicitly exported kernel symbols. The complete export list and registration
live in `kernel/module/driver_api.c`; include `kernel/driver_api.h` for PCI,
MMIO, DMA, packet buffers, and network-device interfaces. Logging and heap
allocation are also exported. There is no libc. TLS, common symbols, module dependencies,
and automatic exports from modules are not supported yet.

Allocatable sections are relocated while writable, then mapped with separate
write/execute permissions before initialization. References acquired by kernel
clients prevent unloading retained callbacks. Modules run with kernel privileges;
the ELF checks do not isolate faulty module code.

To install another module in the root image, add it to `MODULE_OBJECTS` and add
`--module build/modules/NAME.ko` to the `populate_root` invocation before
`--coreutils`. That option can be repeated.
