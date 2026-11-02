<div align="center">
  <img src="other/logo.png" alt="OpenSillicon logo" width="180">
  <h1>OpenSillicon</h1>
  <p><strong>A small x86_64 operating system, built from the ground up.</strong></p>
  <p>Kernel · framebuffer terminal · shell · userspace</p>
</div>

<p align="center">
  <img src="other/screenshot1.png" alt="OpenSillicon running its shell" width="800">
</p>

## Features

### Kernel and processes

- Limine boot on x86_64 UEFI systems, with ACPI discovery and interrupt-controller support.
- Ring 3 user processes with ELF loading, `fork`/`exec`/`wait`, preemptive scheduling, and process inspection.
- Virtual and physical memory management, per-process address spaces, memory mapping and protection, shared memory, and DMA buffers.
- System calls for process, filesystem, IPC, networking, time, and framebuffer operations.
- Pipes and signal handling for basic inter-process communication and process control.

### Storage and filesystems

- VFS with initramfs, RAMFS, `devfs`, and `procfs`.
- SilliconFS with block-device support and an AHCI storage driver.
- File and directory operations, file descriptors, metadata, and vectored I/O.

### Console and userspace

- Framebuffer text terminal, PS/2 keyboard input, and a command-line shell.
- Shell conveniences including command history, path handling, and executable discovery/completion.
- A small C runtime and libc-style `libos` for user programs.
- Core utilities such as `ls`, `cat`, `pwd`, `ps`, `stat`, `uname`, `wc`, `sleep`, and `ping`.

### Networking and hardware

- IPv4 networking with ARP, ICMP Echo, UDP sockets, and TCP protocol/runtime components.
- Intel e1000 network adapter driver, PCI enumeration, and DMA-backed device rings.
- Entropy sources including CPU RDSEED, TPM 2.0, and virtio RNG support.

OpenSillicon is an experimental operating system, and hardware support is evolving.
For a quick start, run `make run` on a Linux host with the required build tools,
QEMU, OVMF, and ISO utilities installed.

## License

Project code is provided under the [BSD 2-Clause License](LICENSE). Third-party components retain their respective licenses.
