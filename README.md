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

## Build layout

The root `Makefile` is the canonical build entry point. It loads shared defaults
from `mk/common.mk` and the target rules owned by `kernel/Makefile`,
`user/Makefile`, and `tests/Makefile`. Existing target names remain available at
the repository root.

The subsystem Makefiles can also be invoked directly; they re-enter the root
build graph so cross-subsystem dependencies and repository-relative source paths
are handled consistently:

```sh
make -C kernel kernel
make -C user libos-build
make -C tests test elf-test vfs-test silliconfs-test
```

Common overrides are passed on the command line and forwarded to nested Make
invocations. The main build, user image, and VFS test targets honor `BUILD_DIR`.
A few older specialized QEMU test recipes still contain fixed `build/<fixture>`
paths. For example, use a separate output tree for these representative targets
without touching `build/`:

```sh
make -j4 kernel BUILD_DIR=/tmp/opensillicon-build
make -C user libos-build BUILD_DIR=/tmp/opensillicon-build \
  LIBOS_BUILD_DIR=/tmp/opensillicon-build/libos
make -C tests vfs-test BUILD_DIR=/tmp/opensillicon-build
```

`CC`, `LD`, `QEMU`, `OVMF_CODE`, `XORRISO`, and the existing subsystem flags
can be overridden the same way. `make clean` removes the selected `BUILD_DIR`;
when omitted, it is `build`.

## License

Project code is provided under the [BSD 2-Clause License](LICENSE). Third-party components retain their respective licenses.
