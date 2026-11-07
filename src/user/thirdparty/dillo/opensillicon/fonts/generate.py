#!/usr/bin/env python3
"""Regenerate Sillicon Sans BDFs from the vendored OFL Noto Sans font.

Requires Pillow and fc-scan on the development host. The generated BDFs
are checked in, so ordinary system builds do not need these Python packages.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from subprocess import check_output

here = Path(__file__).resolve().parent
source = here.parents[2] / "xorg/feh/share/fonts/NotoSans-Medium.ttf"
charset = check_output(["fc-scan", "--format", "%{charset}", str(source)], text=True)
cmap = set()
for span in charset.split():
    bounds = [int(n, 16) for n in span.split("-")]
    cmap.update(range(bounds[0], bounds[-1] + 1))
# Latin, Greek, Cyrillic and BMP punctuation/symbols supported by this font.
points = sorted(cp for cp in cmap if
                0x20 <= cp <= 0xFF or 0x370 <= cp <= 0x52F or
                0x2000 <= cp <= 0x206F or cp == 0x20AC)
for size in (16, 20, 24):
    font = ImageFont.truetype(str(source), size)
    ascent, descent = font.getmetrics()
    glyphs = []
    for cp in points:
        char = chr(cp)
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        width, height = right - left, bottom - top
        bitmap = Image.new("L", (max(width, 1), max(height, 1)))
        ImageDraw.Draw(bitmap).text((-left, -top), char, font=font,
                                   anchor="ls", fill=255)
        rows = []
        for y in range(height):
            value = 0
            for x in range(width):
                value = (value << 1) | (bitmap.getpixel((x, y)) >= 128)
            value <<= (-width) % 8
            rows.append(f"{value:0{((width + 7) // 8) * 2}X}")
        glyphs.append((cp, round(font.getlength(char)), left, -bottom,
                       width, height, rows))
    ascent = max(ascent, max(g[3] + g[5] for g in glyphs))
    descent = max(descent, -min(g[3] for g in glyphs))
    left_edge = min(g[2] for g in glyphs)
    right_edge = max(g[2] + g[4] for g in glyphs)
    name = f"-opensillicon-silliconsans-medium-r-normal--{size}-{size*10}-75-75-p-0-iso10646-1"
    lines = [
        "STARTFONT 2.1",
        "COMMENT Derived from NotoSans-Medium.ttf; SIL Open Font License 1.1.",
        "COMMENT Original copyright and license: OFL.txt in this directory.",
        f"FONT {name}", f"SIZE {size} 75 75",
        f"FONTBOUNDINGBOX {right_edge-left_edge} {ascent+descent} {left_edge} {-descent}",
        "STARTPROPERTIES 13", 'FOUNDRY "OpenSillicon"',
        'FAMILY_NAME "SilliconSans"', 'WEIGHT_NAME "Medium"', 'SLANT "R"',
        'SETWIDTH_NAME "Normal"', f"PIXEL_SIZE {size}", f"POINT_SIZE {size*10}",
        'SPACING "P"', 'CHARSET_REGISTRY "ISO10646"', 'CHARSET_ENCODING "1"',
        f"FONT_ASCENT {ascent}", f"FONT_DESCENT {descent}", "DEFAULT_CHAR 63",
        "ENDPROPERTIES", f"CHARS {len(glyphs)}",
    ]
    for cp, advance, x, y, width, height, rows in glyphs:
        lines.extend([f"STARTCHAR uni{cp:04X}", f"ENCODING {cp}",
                      f"SWIDTH {round(advance*1000/size)} 0",
                      f"DWIDTH {advance} 0",
                      f"BBX {width} {height} {x} {y}", "BITMAP", *rows,
                      "ENDCHAR"])
    lines.append("ENDFONT")
    (here / f"silliconsans-{size}.bdf").write_text("\n".join(lines)+"\n")
