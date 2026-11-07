# Dillo for OpenSillicon

Vendored upstream releases (licenses remain in their original source trees):

| Component | Version | Source | SHA-256 |
| --- | --- | --- | --- |
| Dillo | 3.3.0 | https://dillo-browser.org/release/3.3.0/dillo-3.3.0.tar.gz | db1863261d5efbd27b090e430c88064082b891cea1edf7a14e234cca51754f60 |
| FLTK | 1.3.11 | https://github.com/fltk/fltk/releases/download/release-1.3.11/fltk-1.3.11-source.tar.gz | 92805abc84505e3e7e27aec775ab0754ecb4182fe2d8ff2a9d568ccdcb2811ac |

Dillo is GPL-3.0-or-later; FLTK uses LGPL-2.0-or-later with the FLTK
static linking exception. See ../COPYING and ../deps/fltk/COPYING.

## Build and launch

Run `make root` from the repository root. The internal builder
`src/tools/builders/build_dillo.sh` builds FLTK and Dillo against the native static
libos, X11, PNG, JPEG and zlib libraries. Generated files stay under
`src/build/dillo/`; input fingerprints and a lock protect incremental builds.

The root image contains `/bin/dillo` and `/etc/dillo/dillorc`.
After `startx`, select Dillo in the twm desktop menu, or run
`dillo http://10.15.3.2:8000/` in xterm to visit an HTTP server on the host.

## Port details

* Core X11 rendering, single-threaded DNS, HTTP, HTML, CSS, PNG, JPEG, GIF.
* Sillicon Sans BDF fonts (16/20/24 pixels) derived from the existing
  OFL Noto Sans asset, covering Latin, Greek, Cyrillic and punctuation.
  The font generator is optional; BDFs and the OFL license are included.
  The image includes the font license alongside its BDFs.
* Native C++ allocation and constructor startup; exceptions, RTTI and the
  host C++ runtime are unnecessary. The private linker script retains
  constructor arrays, and the native entry to Dillo calls them before use.
* The private iconv implementation supports UTF-8, UTF-16LE/BE, ASCII,
  ISO-8859-1, Windows-1251 and Windows-1252, including invalid/incomplete
  input and output-buffer exhaustion errors. Other encodings fail explicitly.
* HOME defaults to /tmp because native exec does not pass an environment yet.
  Dillo stores its per-user files in /tmp/.dillo.
* TLS, IPv6, threaded DNS, XEmbed, WebP, SVG and Brotli are disabled.
  HTTPS is unavailable in this build.
* DPI helper programs are not installed. File URLs, downloads, bookmarks and
  cookies requiring those helpers are unavailable; cookies are disabled.
  The built-in about:splash page and HTTP work without DPI.

Local upstream edits are confined to constructor startup and native signal
flags in src/dillo.cc, positioning the window before mapping in src/uicmd.cc, using libos strndup
in dw/selection.cc, and avoiding setsid (which the native
kernel does not provide) in FLTK's optional external-program launcher.
The remaining port code and configuration live in this directory.
