"""PSP-Forge Texture Cooker.

Handles:
- Power-of-Two (POT) padding (up to 512x512)
- Swizzling (16x8 byte hardware block ordering)
- Color format conversions:
    - GU_PSM_8888 (32-bit RGBA)
    - GU_PSM_5551 (16-bit RGBA 1-bit alpha)
    - GU_PSM_4444 (16-bit RGBA 4-bit alpha)
    - GU_PSM_5650 (16-bit RGB)
    - GU_PSM_T8   (8-bit indexed CLUT, 256 colors)
    - GU_PSM_T4   (4-bit indexed CLUT, 16 colors)
"""

import os
import struct
from PIL import Image

# PSP GPU Pixel Storage Modes (GU_PSM_* from pspgu.h)
# IMPORTANT: These MUST match pspgu.h exactly — they are written directly into
# the .tex header and passed verbatim to sceGuTexMode() at runtime.
PSM_5650 = 0  # GU_PSM_5650: 16-bit RGB  (5:6:5)
PSM_5551 = 1  # GU_PSM_5551: 16-bit RGBA (5:5:5:1)
PSM_4444 = 2  # GU_PSM_4444: 16-bit RGBA (4:4:4:4)
PSM_8888 = 3  # GU_PSM_8888: 32-bit RGBA
PSM_T4   = 4  # GU_PSM_T4:   4-bit indexed
PSM_T8   = 5  # GU_PSM_T8:   8-bit indexed

FORMAT_NAMES = {
    "8888": PSM_8888,
    "rgba8888": PSM_8888,
    "5551": PSM_5551,
    "rgba5551": PSM_5551,
    "4444": PSM_4444,
    "rgba4444": PSM_4444,
    "5650": PSM_5650,
    "rgb5650": PSM_5650,
    "t8": PSM_T8,
    "clut8": PSM_T8,
    "t4": PSM_T4,
    "clut4": PSM_T4,
}


def next_power_of_two(n: int, max_val: int = 512) -> int:
    """Returns the smallest power of 2 >= n, capped at max_val."""
    if n > max_val:
        raise ValueError(f"Texture dimension {n} exceeds maximum allowed size ({max_val})")
    p = 1
    while p < n:
        p <<= 1
    return p


def swizzle_linear_data(src: bytes, width: int, height: int, bytes_per_pixel: int) -> bytes:
    """Swizzles linear texture data into PSP hardware block ordering (16x8 byte blocks)."""
    block_w = 16 // bytes_per_pixel
    block_h = 8
    width_blocks = width // block_w
    height_blocks = height // block_h

    dst = bytearray(len(src))
    dst_idx = 0

    for by in range(0, height, block_h):
        for bx in range(0, width, block_w):
            for y in range(block_h):
                for x in range(block_w):
                    src_idx = ((by + y) * width + (bx + x)) * bytes_per_pixel
                    for b in range(bytes_per_pixel):
                        dst[dst_idx] = src[src_idx + b]
                        dst_idx += 1

    return bytes(dst)


def swizzle_4bit_data(src: bytes, width: int, height: int) -> bytes:
    """Swizzles 4-bit (2 pixels per byte) indexed texture data."""
    block_w = 32  # 32 pixels = 16 bytes
    block_h = 8
    dst = bytearray(len(src))
    dst_idx = 0

    for by in range(0, height, block_h):
        for bx in range(0, width, block_w):
            for y in range(block_h):
                for x in range(0, block_w, 2):
                    pixel_x = bx + x
                    pixel_y = by + y
                    src_byte_idx = (pixel_y * width + pixel_x) // 2
                    dst[dst_idx] = src[src_byte_idx]
                    dst_idx += 1

    return bytes(dst)


def cook_texture(
    input_path: str,
    output_path: str,
    format_type: str = "8888",
    swizzle: bool = True
) -> dict:
    """Cooks a source image (PNG, etc.) into a PSP-ready binary texture (.tex).

    Header format (32 bytes):
      - Magic: 'PTEX' (4 bytes)
      - Version: uint16 (1)
      - Format: uint16 (PSM_*)
      - Original Width: uint16
      - Original Height: uint16
      - Pwr2 Width: uint16
      - Pwr2 Height: uint16
      - Is Swizzled: uint8 (1 or 0)
      - Has Palette: uint8 (1 or 0)
      - Palette Count: uint16 (number of color entries)
      - Padding/Reserved: 12 bytes
    Followed by:
      - Palette data (if indexed, RGBA8888 * Palette Count bytes)
      - Pixel data
    """
    fmt_key = format_type.lower()
    if fmt_key not in FORMAT_NAMES:
        raise ValueError(f"Unsupported texture format '{format_type}'. Choices: {list(FORMAT_NAMES.keys())}")
    psm = FORMAT_NAMES[fmt_key]

    img = Image.open(input_path).convert("RGBA")
    orig_w, orig_h = img.size

    # Hard hardware limit: PSP GE cannot sample textures larger than 512x512
    if orig_w > 512 or orig_h > 512:
        print(f"  [!] Auto-downscaling '{os.path.basename(input_path)}' ({orig_w}x{orig_h}) to fit PSP 512x512 hardware limit...")
        resample = getattr(Image, "Resampling", Image).LANCZOS
        img.thumbnail((512, 512), resample)
        orig_w, orig_h = img.size

    pwr2_w = next_power_of_two(orig_w)
    pwr2_h = next_power_of_two(orig_h)

    # Minimum size for 16x8 byte hardware block alignment
    min_w = 16 if psm in (PSM_8888, PSM_5551, PSM_4444, PSM_5650, PSM_T8) else 32
    min_h = 8
    pwr2_w = max(pwr2_w, min_w)
    pwr2_h = max(pwr2_h, min_h)

    # Estimate VRAM usage
    bpp = 4 if psm == PSM_8888 else (2 if psm in (PSM_5551, PSM_4444, PSM_5650) else 1)
    vram_bytes = pwr2_w * pwr2_h * bpp
    if vram_bytes > 512 * 1024:
        print(f"  [!] WARNING (PSP VRAM): Texture '{os.path.basename(input_path)}' padded to {pwr2_w}x{pwr2_h} ({vram_bytes / 1024:.0f} KB in {format_type}).")
        print(f"      Fast eDRAM texture scratchpad is only 688 KiB. Consider using RGBA5551/5650 (16-bit) or indexed CLUT8.")

    # Pad image to power of two
    padded_img = Image.new("RGBA", (pwr2_w, pwr2_h), (0, 0, 0, 0))
    padded_img.paste(img, (0, 0))

    has_palette = 0
    palette_count = 0
    palette_bytes = b""
    raw_pixel_bytes = bytearray()

    if psm == PSM_8888:
        # 32-bit RGBA: 4 bytes per pixel
        raw_pixel_bytes = bytearray(padded_img.tobytes())
        bpp = 4
        if swizzle:
            pixel_data = swizzle_linear_data(raw_pixel_bytes, pwr2_w, pwr2_h, bpp)
        else:
            pixel_data = bytes(raw_pixel_bytes)

    elif psm == PSM_5551:
        # 16-bit RGBA (5:5:5:1)
        pixels = list(padded_img.getdata())
        bpp = 2
        for r, g, b, a in pixels:
            r5 = (r * 31 + 127) // 255
            g5 = (g * 31 + 127) // 255
            b5 = (b * 31 + 127) // 255
            a1 = 1 if a > 127 else 0
            val = r5 | (g5 << 5) | (b5 << 10) | (a1 << 15)
            raw_pixel_bytes.extend(struct.pack("<H", val))
        if swizzle:
            pixel_data = swizzle_linear_data(bytes(raw_pixel_bytes), pwr2_w, pwr2_h, bpp)
        else:
            pixel_data = bytes(raw_pixel_bytes)

    elif psm == PSM_4444:
        # 16-bit RGBA (4:4:4:4)
        pixels = list(padded_img.getdata())
        bpp = 2
        for r, g, b, a in pixels:
            r4 = r >> 4
            g4 = g >> 4
            b4 = b >> 4
            a4 = a >> 4
            val = r4 | (g4 << 4) | (b4 << 8) | (a4 << 12)
            raw_pixel_bytes.extend(struct.pack("<H", val))
        if swizzle:
            pixel_data = swizzle_linear_data(bytes(raw_pixel_bytes), pwr2_w, pwr2_h, bpp)
        else:
            pixel_data = bytes(raw_pixel_bytes)

    elif psm == PSM_5650:
        # 16-bit RGB (5:6:5)
        pixels = list(padded_img.getdata())
        bpp = 2
        for r, g, b, _ in pixels:
            r5 = (r * 31 + 127) // 255
            g6 = (g * 63 + 127) // 255
            b5 = (b * 31 + 127) // 255
            val = r5 | (g6 << 5) | (b5 << 11)
            raw_pixel_bytes.extend(struct.pack("<H", val))
        if swizzle:
            pixel_data = swizzle_linear_data(bytes(raw_pixel_bytes), pwr2_w, pwr2_h, bpp)
        else:
            pixel_data = bytes(raw_pixel_bytes)

    elif psm == PSM_T8:
        # 8-bit CLUT (256 colors)
        quantized = padded_img.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
        pal = quantized.getpalette()  # RGB list
        has_palette = 1
        palette_count = 256

        raw_indices = quantized.tobytes()

        # Compute per-palette-index alpha from original image if RGBA
        if "A" in padded_img.getbands():
            a_bytes = padded_img.getchannel("A").tobytes()
            alpha_accum = [0] * 256
            alpha_counts = [0] * 256
            for a, idx in zip(a_bytes, raw_indices):
                alpha_accum[idx] += a
                alpha_counts[idx] += 1
            pal_alpha = [
                int(round(alpha_accum[i] / alpha_counts[i])) if alpha_counts[i] > 0 else 255
                for i in range(256)
            ]
        else:
            pal_alpha = [255] * 256

        # Generate RGBA8888 palette entries
        pal_buf = bytearray()
        for i in range(256):
            if i * 3 + 2 < len(pal):
                pr, pg, pb = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
                pa = pal_alpha[i]
            else:
                pr, pg, pb, pa = 0, 0, 0, 0
            pal_buf.extend(struct.pack("<BBBB", pr, pg, pb, pa))
        palette_bytes = bytes(pal_buf)

        bpp = 1
        if swizzle:
            pixel_data = swizzle_linear_data(raw_indices, pwr2_w, pwr2_h, bpp)
        else:
            pixel_data = raw_indices

    elif psm == PSM_T4:
        # 4-bit CLUT (16 colors)
        quantized = padded_img.quantize(colors=16, method=Image.Quantize.FASTOCTREE)
        pal = quantized.getpalette()
        has_palette = 1
        palette_count = 16

        indices = quantized.tobytes()

        # Compute per-palette-index alpha from original image if RGBA
        if "A" in padded_img.getbands():
            a_bytes = padded_img.getchannel("A").tobytes()
            alpha_accum = [0] * 16
            alpha_counts = [0] * 16
            for a, idx in zip(a_bytes, indices):
                alpha_accum[idx] += a
                alpha_counts[idx] += 1
            pal_alpha = [
                int(round(alpha_accum[i] / alpha_counts[i])) if alpha_counts[i] > 0 else 255
                for i in range(16)
            ]
        else:
            pal_alpha = [255] * 16

        pal_buf = bytearray()
        for i in range(16):
            if i * 3 + 2 < len(pal):
                pr, pg, pb = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
                pa = pal_alpha[i]
            else:
                pr, pg, pb, pa = 0, 0, 0, 0
            pal_buf.extend(struct.pack("<BBBB", pr, pg, pb, pa))
        palette_bytes = bytes(pal_buf)

        indices = quantized.tobytes()
        # Pack 2 pixels per byte (lower nibble first, then upper nibble)
        packed_bytes = bytearray()
        for i in range(0, len(indices), 2):
            p0 = indices[i] & 0x0F
            p1 = (indices[i + 1] & 0x0F) if (i + 1 < len(indices)) else 0
            packed_bytes.append(p0 | (p1 << 4))
        if swizzle:
            pixel_data = swizzle_4bit_data(bytes(packed_bytes), pwr2_w, pwr2_h)
        else:
            pixel_data = bytes(packed_bytes)

    # 32-byte header
    # Magic (4s), Version (H), Format (H), OrigW (H), OrigH (H), Pwr2W (H), Pwr2H (H),
    # Swizzled (B), HasPalette (B), PaletteCount (H), Reserved (12s)
    header = struct.pack(
        "<4sHHHHHHBBH12s",
        b"PTEX",
        1,
        psm,
        orig_w,
        orig_h,
        pwr2_w,
        pwr2_h,
        1 if swizzle else 0,
        has_palette,
        palette_count,
        b"\x00" * 12
    )

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(header)
        if palette_bytes:
            f.write(palette_bytes)
        f.write(pixel_data)

    return {
        "output_path": output_path,
        "format": format_type,
        "orig_size": (orig_w, orig_h),
        "pwr2_size": (pwr2_w, pwr2_h),
        "swizzled": swizzle,
        "total_bytes": len(header) + len(palette_bytes) + len(pixel_data)
    }
