# Xorg userland

- `tinyx/`: TinyX server and the OpenSillicon backend.
- `twm/`: window manager.
- `xterm/`: terminal emulator (upstream xterm 412).
- `xeyes-1.3.1/`: Xeyes mouse-following demo.
- `deps/libXi-1.8.4/` and `deps/libXfixes-6.0.2/`: X Input client libraries used by xeyes.
- `deps/`: vendored X client libraries, font libraries, zlib and XCB protocol definitions.
- `deps/x11-dependencies.json`: dependency archive URLs and checksums.

Build and install the server and clients with `make build/root.img` from the
repository root. The session launcher remains in `user/xorg/startx/`.
