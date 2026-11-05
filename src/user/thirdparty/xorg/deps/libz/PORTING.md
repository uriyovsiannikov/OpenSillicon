# OpenSillicon zlib subset

These files are from the upstream zlib `v1.3.2` release. The source archive
used was GitHub's `madler/zlib` tag archive, SHA-256
`b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1`.
The upstream license is in `LICENSE`.

OpenSillicon builds the inflate, checksum, and utility sources into the static
`build/libz/libz.a` archive with `make libz-build`. This subset provides the
`inflateInit2`, `inflate`, and `inflateEnd` API used by libXfont's builtin PCF
font decompressor. It uses libos `malloc/calloc/free` for zlib's default
allocator. `DYNAMIC_CRC_TABLE` keeps the archive small; its one-time CRC table
initialization is not thread-safe, which matches libos's current single-thread
runtime. It does not include compression or the `gz*` file API; libXfont's
upstream configure check for `gzopen` must be adapted when its target build is
integrated.

The selected upstream source files are kept unmodified. `zconf.h` is the
upstream release configuration header; the target compiler and libos headers
provide the platform types and limits.
