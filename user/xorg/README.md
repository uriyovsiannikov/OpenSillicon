# Xorg userland

- `tinyx/`: TinyX server and the OpenSillicon backend.
- `twm/`: window manager.
- `xterm/`: terminal emulator (upstream xterm 412).
- `deps/`: vendored X client libraries, font libraries, zlib and XCB protocol definitions.
- `deps/x11-dependencies.json`: dependency archive URLs and checksums.

Build and install the server and clients with `make build/root.img` from the
repository root. The session launcher remains in `user/xorg/startx/`.
