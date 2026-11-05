# OpenSillicon libfontenc port

This is the libfontenc 1.1.8 source release from X.Org. It supplies the
built-in encoding maps and the encoding-file parser required by libXfont.
Build it with `make libfontenc-build`; the target archive is
`build/libfontenc/libfontenc.a`.

The parser uses OpenSillicon's `stdio` VFS calls and reads plain encoding
files. Gzip-compressed encoding files are not supported because libos does
not currently provide zlib's `gz*` file interface. The default encoding
directory is `/usr/share/fonts/encodings`; `FONT_ENCODINGS_DIRECTORY` can
override it at runtime. The path must contain regular, uncompressed X.Org
encoding files. Built-in mappings do not require those files.

This is a static target library; link it after libXfont and before libos.
The imported license is in `COPYING`.
