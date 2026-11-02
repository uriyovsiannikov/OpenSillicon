# OpenSillicon libXfont port

This is libXfont 1.5.4, required by this TinyX revision's legacy
`FontFileRegisterFpeFunctions` API. FreeType and the X Font Services Protocol
are disabled for OpenSillicon. Bitmap PCF and the built-in `fixed` and `cursor`
fonts remain enabled. The font-file backend uses libos VFS and stdio support.

OpenSillicon's zlib subset implements the streaming inflate API required by
`gunzip.c`, but not zlib's `gz*` file API. The configure check therefore tests
`inflateInit2_`, the API the code actually uses. The optional `catalogue:` FPE
is omitted when the target has no `readlink` function.

The target must link libXfont, libz, libfontenc, libos, and TinyX's server
objects into one static ELF. The target libfontenc 1.1.8 archive is built with
`make libfontenc-build`; it supports built-in mappings and plain encoding
files. No host libXfont binary is used.
