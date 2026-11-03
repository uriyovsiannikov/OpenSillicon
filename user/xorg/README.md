# Xorg userland

- `tinyx/`: TinyX server and the OpenSillicon backend.
- `twm/`: window manager.
- `xterm/`: terminal emulator (upstream xterm 412).
- `xeyes/`: Xeyes mouse-following demo.
- `feh/`: image viewer (PNG, JPEG, PNM, BMP, QOI, XPM, XBM, TGA and ARGB).
- `deps/libXi-1.8.4/` and `deps/libXfixes-6.0.2/`: X Input client libraries used by xeyes.
- `deps/`: vendored X client libraries, image codecs, font libraries, zlib and XCB protocol definitions.
- `deps/x11-dependencies.json`: dependency archive URLs and checksums.

Build and install the server and clients with `make build/root.img` from the
repository root. The session launcher remains in `user/xorg/startx/`.

The TinyX backend polls input every 10 ms, coalesces queued mouse-motion events,
and presents damaged framebuffer rectangles instead of copying the whole screen
after each small update. feh uses the bundled static image loaders; caption-file
editing and terminal stdin controls are unavailable on this OS.

`startx` disables framebuffer rendering of text-console output while the X
session owns the display. Kernel diagnostics still go to the serial console;
the text framebuffer is restored when `startx` exits or is terminated.
