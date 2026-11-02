<div align="center">
  <img src="other/logo.png" alt="OpenSillicon logo" width="88">
  <h1>OpenSillicon</h1>
  <p>a small x86_64 operating system built from the ground up.</p>
  <img src="other/screenshot1.png" alt="OpenSillicon shell" width="620">
</div>

OpenSillicon is an experimental Unix-like operating system with a modular
monolithic kernel and a native userspace.

## What works

- Ring 3 processes, elf loading, `fork`/`exec`/`wait`, preemptive scheduling,
  virtual mem, shared mem, signals, pipes, and file descriptors.
- VFS with RAMFS, initramfs, devfs, procfs, and the native SilliconFS.
- F/b terminal, ps/2 keyboard, shell, and core utilities.
- Native `libos` runtime.
- TinyX session with `twm`, `xterm`, and `xeyes`.
- Network stack components for ethernet, arp, ipv4, icmp, udp, and tcp.

## Build and run

On a Linux x86_64 host, install GCC, GNU binutils, GNU Make, Python 3, QEMU,
OVMF, and `xorriso`, then run:

```sh
make
make run       # UEFI QEMU, serial console
make run-gui   # UEFI QEMU, graphical display
```

## Source map

- `kernel/` — kernel, architecture code, drivers, filesystems, and networking.
- `user/` — init, shell, utilities, `libos`, and X.Org components.
- `include/` — kernel and userspace ABI headers.
- `tests/` — host and QEMU test harnesses.
- `tools/` — image and filesystem build tools.
- `docs/` — architecture, design decisions, security assessments, and roadmap.

See [Architecture and Design](docs/DESIGN.md), [Development Roadmap](docs/ROADMAP.md),
[Open ADRs](docs/OPEN_ADRS.md), and the relevant subsystem README files for
implementation details and current limitations.

## License

Project code is available under the [BSD 2-Clause License](LICENSE). Vendored
and third-party components retain their respective licenses.
