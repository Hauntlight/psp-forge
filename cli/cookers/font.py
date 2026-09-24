"""PSP-Forge Font Cooker.

Rasters a TrueType / OpenType font (.ttf, .otf) into:
1. .fnt: Compact binary font metric descriptor (ForgeFontHeader + ForgeGlyphDef array)
2. .tex: PSP hardware-swizzled texture atlas containing glyphs
"""

import math
import os
import struct
from typing import Dict, List, Tuple
from PIL import Image, ImageDraw, ImageFont

from .texture import cook_texture


def cook_font(
    input_path: str,
    output_dir: str,
    font_size: int = 24,
    prefix: str = None,
    format_type: str = "8888"
) -> Dict[str, str]:
    """Cooks a TrueType font into <prefix>.fnt and <prefix>.tex."""
    if prefix is None:
        prefix = os.path.splitext(os.path.basename(input_path))[0].lower()
        prefix = "".join(c if c.isalnum() or c in "._-" else "_" for c in prefix)

    fnt_output = os.path.join(output_dir, f"{prefix}.fnt")
    tex_output = os.path.join(output_dir, f"{prefix}.tex")
    tmp_png = os.path.join(output_dir, f"{prefix}_tmp_atlas.png")

    try:
        pil_font = ImageFont.truetype(input_path, font_size)
    except Exception as e:
        raise RuntimeError(f"Failed to load TrueType font '{input_path}': {e}")

    # Printable ASCII range: 32 (space) to 126 (~)
    chars = [chr(c) for c in range(32, 127)]

    # Measure each glyph
    glyph_data = []
    max_h = 0
    total_area = 0

    for ch in chars:
        bbox = pil_font.getbbox(ch)  # (left, top, right, bottom)
        if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
            # Space or invisible
            advance = pil_font.getlength(ch) if hasattr(pil_font, "getlength") else font_size // 2
            glyph_data.append({
                "char": ch,
                "code": ord(ch),
                "w": 0,
                "h": 0,
                "xoff": 0,
                "yoff": 0,
                "xadvance": int(round(advance)),
                "img": None
            })
            continue

        left, top, right, bottom = bbox
        gw = right - left
        gh = bottom - top
        advance = pil_font.getlength(ch) if hasattr(pil_font, "getlength") else gw

        # Render glyph into a single image with antialiasing
        # White text with transparency
        gimg = Image.new("RGBA", (gw, gh), (0, 0, 0, 0))
        gdraw = ImageDraw.Draw(gimg)
        gdraw.text((-left, -top), ch, font=pil_font, fill=(255, 255, 255, 255))

        max_h = max(max_h, gh)
        total_area += (gw + 2) * (gh + 2)

        glyph_data.append({
            "char": ch,
            "code": ord(ch),
            "w": gw,
            "h": gh,
            "xoff": left,
            "yoff": top,
            "xadvance": int(round(advance)),
            "img": gimg
        })

    # Determine atlas dimensions (POT: 256x256 or 512x256 or 512x512)
    atlas_w = 256
    atlas_h = 256
    if total_area > 240 * 240:
        atlas_w = 512
        atlas_h = 256
    if total_area > 480 * 240:
        atlas_w = 512
        atlas_h = 512

    # Shelf packing
    atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))
    cur_x = 1
    cur_y = 1
    row_h = 0

    packed_glyphs = []
    for g in glyph_data:
        gw = g["w"]
        gh = g["h"]
        if g["img"] is None or gw == 0 or gh == 0:
            packed_glyphs.append({
                "code": g["code"],
                "x": 0, "y": 0, "w": 0, "h": 0,
                "xoff": g["xoff"], "yoff": g["yoff"],
                "xadvance": g["xadvance"],
                "u0": 0.0, "v0": 0.0, "u1": 0.0, "v1": 0.0
            })
            continue

        if cur_x + gw + 1 >= atlas_w:
            # New row
            cur_x = 1
            cur_y += row_h + 2
            row_h = 0

        if cur_y + gh + 1 >= atlas_h:
            # Need larger atlas
            atlas_h = min(512, atlas_h * 2)
            new_atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))
            new_atlas.paste(atlas, (0, 0))
            atlas = new_atlas

        pos_x = cur_x
        pos_y = cur_y
        atlas.paste(g["img"], (pos_x, pos_y))

        cur_x += gw + 2
        row_h = max(row_h, gh)

        u0 = pos_x / float(atlas_w)
        v0 = pos_y / float(atlas_h)
        u1 = (pos_x + gw) / float(atlas_w)
        v1 = (pos_y + gh) / float(atlas_h)

        packed_glyphs.append({
            "code": g["code"],
            "x": pos_x,
            "y": pos_y,
            "w": gw,
            "h": gh,
            "xoff": g["xoff"],
            "yoff": g["yoff"],
            "xadvance": g["xadvance"],
            "u0": u0, "v0": v0, "u1": u1, "v1": v1
        })

    # Save temporary PNG and cook to .tex
    atlas.save(tmp_png, "PNG")
    cook_texture(tmp_png, tex_output, format_type=format_type, swizzle=True)
    if os.path.exists(tmp_png):
        os.remove(tmp_png)

    # Line metrics
    ascent, descent = pil_font.getmetrics() if hasattr(pil_font, "getmetrics") else (font_size, font_size // 4)
    line_height = ascent + descent

    # Write .fnt binary
    # Header: 32 bytes (<4s7H14s)
    # Magic: "FFNT", Version: 1
    hdr_bytes = struct.pack(
        "<4s7H14s",
        b"FFNT",
        1,                     # version
        font_size,             # font_size
        line_height,           # line_height
        ascent,                # base_line
        atlas_w,               # tex_w
        atlas_h,               # tex_h
        len(packed_glyphs),    # glyph_count
        b"\x00" * 14           # reserved
    )

    with open(fnt_output, "wb") as f:
        f.write(hdr_bytes)
        for pg in packed_glyphs:
            # ForgeGlyphDef: 32 bytes
            # <BBHHHHhhhffff
            # char_code, reserved, x, y, w, h, xoff, yoff, xadvance, u0, v0, u1, v1
            g_bytes = struct.pack(
                "<BBHHHHhhhffff",
                pg["code"],
                0,
                pg["x"],
                pg["y"],
                pg["w"],
                pg["h"],
                pg["xoff"],
                pg["yoff"],
                pg["xadvance"],
                pg["u0"],
                pg["v0"],
                pg["u1"],
                pg["v1"]
            )
            f.write(g_bytes)

    print(f"  [+] Font cooked: {os.path.basename(fnt_output)} ({len(packed_glyphs)} glyphs, atlas {atlas_w}x{atlas_h})")
    return {
        "fnt": fnt_output,
        "tex": tex_output
    }
