The framebuffer console uses Sun Gallant Demi, a 12x22 bitmap typeface.

Upstream: https://github.com/dim13/gallant
Source file: `gallant.go`, SHA-256
`f733c4bf442146a9ded38fbad5cbc48de0892661c55c58ec02cefacaf626f85e`.
The upstream Go data is retained here; `../font.h` packs each row into a
12-bit value for the framebuffer renderer. Copyright and license notices are
in `GALLANT-LICENSE.txt`.

The source includes ASCII, Latin-1 and the replacement glyph. Bytes outside
those ranges are drawn using the replacement glyph.
