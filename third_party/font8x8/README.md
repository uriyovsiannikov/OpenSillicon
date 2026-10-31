OpenSillicon uses the Basic Latin 8-by-8 bitmap table from Daniel Hepper's
`font8x8` project, pinned at commit
`8e279d2d864e79128e96188a6b9526cfa3fbfef9`:

https://github.com/dhepper/font8x8

SHA-256 of the unmodified upstream `font8x8_basic.h` at that commit:
`49d8df366296b203ca3211bc0672cf2a762135bf12710735b6292756b19dffd5`.

The upstream header identifies this font data as public domain and credits
the original IBM public-domain VGA font. The vendored header retains that
provenance. OpenSillicon doubles each glyph row to render an 8-by-16 cell.
