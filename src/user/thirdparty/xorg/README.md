# Xorg userland

- `tinyx/`: TinyX server and the OpenSillicon backend.
- `twm/`: window manager.
- `xterm/`: terminal emulator (upstream xterm 412).
- `xeyes/`: Xeyes mouse-following demo.
- `feh/`: image viewer (PNG, JPEG, PNM, BMP, QOI, XPM, XBM, TGA and ARGB).
- `deps/libXi-1.8.4/` and `deps/libXfixes-6.0.2/`: X Input client libraries used by xeyes.
- `deps/`: vendored X client libraries, image codecs, font libraries, zlib and XCB protocol definitions.
- `deps/x11-dependencies.json`: dependency archive URLs and checksums.

Build and install the server and clients with `make root` from the
repository root. The session launcher is in `src/user/thirdparty/xorg/startx/`.

The TinyX backend polls input every 10 ms, coalesces queued mouse-motion events,
and presents damaged framebuffer rectangles instead of copying the whole screen
after each small update. feh uses the bundled static image loaders; caption-file
editing and terminal stdin controls are unavailable on this OS.

Presentation keeps disjoint damage rectangles separate and skips the shadow
copy when the screen pixmap already uses the presentation buffer. The kernel
copies validated user RAM in 64-bit blocks without SIMD, and writes aligned
framebuffer pixels in pairs, with fallbacks for odd widths and unusual pitches.

`startx` disables framebuffer rendering of text-console output while the X
session owns the display. Kernel diagnostics still go to the serial console;
the text framebuffer is restored when `startx` exits or is terminated.

The ordinary shell and X clients run as UID 1000. TinyX explicitly claims
console input through `TIOCSINPUT` on its inherited console descriptor. The
claim is exclusive and available only to the graphics-session owner or a
direct child of that owner. It authorizes the claiming PID to poll raw
keyboard and mouse events, without granting that access to other processes
of the same user or to the server's children. It is revoked when TinyX exits,
when the session owner exits, or when the text framebuffer is restored.
TinyX reports input initialization or polling failures instead of leaving
an apparently frozen desktop. Root retains its existing input-polling access.
