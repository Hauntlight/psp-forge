"""Tests for PSP-Forge Texture Cooker."""
import os
import struct
import tempfile
import unittest
from PIL import Image

import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from cli.cookers.texture import cook_texture, next_power_of_two, swizzle_linear_data


class TestTextureCooker(unittest.TestCase):
    def test_next_power_of_two(self):
        self.assertEqual(next_power_of_two(1), 1)
        self.assertEqual(next_power_of_two(10), 16)
        self.assertEqual(next_power_of_two(32), 32)
        self.assertEqual(next_power_of_two(100), 128)
        self.assertEqual(next_power_of_two(300), 512)
        with self.assertRaises(ValueError):
            next_power_of_two(513)

    def test_cook_rgba8888(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src_png = os.path.join(tmpdir, "test.png")
            out_tex = os.path.join(tmpdir, "test.tex")

            # Create 32x20 RGBA test image
            img = Image.new("RGBA", (32, 20), (255, 0, 0, 255))
            img.save(src_png)

            info = cook_texture(src_png, out_tex, format_type="8888", swizzle=True)
            self.assertTrue(os.path.exists(out_tex))
            self.assertEqual(info["pwr2_size"], (32, 32))

            # Verify header
            with open(out_tex, "rb") as f:
                header = f.read(32)
                magic, ver, psm, orig_w, orig_h, p2w, p2h, swz, has_pal, pal_cnt = struct.unpack("<4sHHHHHHBBH", header[:20])
                self.assertEqual(magic, b"PTEX")
                self.assertEqual(ver, 1)
                self.assertEqual(psm, 0) # 8888
                self.assertEqual(orig_w, 32)
                self.assertEqual(orig_h, 20)
                self.assertEqual(p2w, 32)
                self.assertEqual(p2h, 32)
                self.assertEqual(swz, 1)

    def test_cook_clut8(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src_png = os.path.join(tmpdir, "clut.png")
            out_tex = os.path.join(tmpdir, "clut.tex")

            img = Image.new("RGBA", (16, 16), (0, 255, 0, 255))
            img.save(src_png)

            info = cook_texture(src_png, out_tex, format_type="t8", swizzle=True)
            self.assertTrue(os.path.exists(out_tex))
            with open(out_tex, "rb") as f:
                header = f.read(32)
                magic, _, psm, _, _, _, _, _, has_pal, pal_cnt = struct.unpack("<4sHHHHHHBBH", header[:20])
                self.assertEqual(magic, b"PTEX")
                self.assertEqual(psm, 5) # T8
                self.assertEqual(has_pal, 1)
                self.assertEqual(pal_cnt, 256)


if __name__ == "__main__":
    unittest.main()
