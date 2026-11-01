<div align="center">
  <img src="other/logo.png" alt="OpenSillicon logo" width="180">
  <h1>OpenSillicon</h1>
  <p><strong>A small x86_64 operating system, built from the ground up.</strong></p>
  <p>Kernel · framebuffer terminal · shell · userspace</p>
</div>

<p align="center">
  <img src="other/screenshot1.png" alt="OpenSillicon running its shell" width="800">
</p>

## Build and run

On a Linux development host with the project’s toolchain, QEMU, OVMF, and ISO utilities installed:

```sh
make -j4 kernel
make run
```

## License

Project code is provided under the [BSD 2-Clause License](LICENSE). Third-party components retain their respective licenses.
