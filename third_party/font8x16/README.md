OpenSillicon uses the 8-by-16 bitmap data from Modern DOS in the `pcface`
project, pinned to commit `8629ba46b73f58ac88cacbe8e77c6e9975dd221c`:

https://github.com/susam/pcface/tree/8629ba46b73f58ac88cacbe8e77c6e9975dd221c/out/moderndos-8x16

The font contains 256 CP437 glyphs, with 16 one-byte rows per glyph. The data
is available under CC0 1.0 Universal. The downloaded upstream
`fontlist.js` SHA-256 is
`3785ce92b2187e18f520f43bf66262c975e1e67f4d602b6fccc80682322dd250`.
The vendored C header stores the same rows in `font8x16_basic`.
