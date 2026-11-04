<div align="center">
  <img src="other/logo.png" alt="OpenSillicon logo" width="88">
  <h1>OpenSillicon</h1>
  <p>a small x86_64 operating system built from the ground up.</p>
  <img src="other/screenshot2.png" alt="OpenSillicon shell" width="620">
</div>

opensillicon is an experimental Unix-like operating system with a modular
monolithic kernel and a native userspace.

## what works

- ring 3 processes, elf loading, `fork`/`exec`/`wait`, preemptive scheduling,
  virtual mem, shared mem, signals, pipes, and file descriptors.
- vfs with ramfs, initramfs, devfs, procfs, and the native SilliconFS.
- f/b terminal, ps/2 keyboard, shell, and core utilities.
- native `libos` runtime.
- GNU nano text editor (`nano /tmp/example.txt`).
- tinyx session with `twm`, `xterm`, and `xeyes`.
- network stack components for ethernet, arp, ipv4, icmp, udp, and tcp.

## build and run

on a linux x86_64 host, install gcc, gnu binutils, gnu make, py3, qemu-x86 and `xorriso`, then run:

```sh
make
make run       # UEFI QEMU, serial console
make run-gui   # UEFI QEMU, graphical display
```

## source map

- `kernel/` - kernel, architecture code, drivers, filesystems, and networking.
- `user/` - init, shell, utilities, `libos`, and xorg components.
- `include/` - kernel and userspace ABI headers.
- `tests/` - host and qemu test harnesses.
- `tools/` - image and filesystem build tools.

## license

project code is available under the [BSD 2-Clause License](LICENSE). Vendored
and third-party components retain their respective licenses.
