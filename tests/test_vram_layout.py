import unittest

class TestVRAMLayout(unittest.TestCase):
    """Verifies that the PSP-Forge VRAM layout is mathematically sound,
    does not overlap buffers, adheres to hardware alignment constraints,
    and fits exactly within 2 MB eDRAM.
    """

    def setUp(self):
        self.total_vram = 2 * 1024 * 1024  # 2048 KiB = 2,097,152 bytes
        self.buf_width = 512              # Stride / pitch in pixels
        self.height = 272                 # PSP screen height in pixels

        # RGBA8888 = 4 bytes per pixel
        self.draw_size = self.buf_width * self.height * 4   # 557,056 bytes = 544 KiB
        self.disp_size = self.buf_width * self.height * 4   # 557,056 bytes = 544 KiB

        # 16-bit Depth = 2 bytes per pixel
        self.depth_size = self.buf_width * self.height * 2  # 278,528 bytes = 272 KiB

        self.draw_offset = 0x00000000
        self.disp_offset = self.draw_offset + self.draw_size      # 0x00088000
        self.depth_offset = self.disp_offset + self.disp_size     # 0x00110000
        self.scratch_start = self.depth_offset + self.depth_size   # 0x00154000
        self.scratch_size = self.total_vram - self.scratch_start   # 704,512 bytes = 688 KiB

    def test_buffer_sizes(self):
        self.assertEqual(self.draw_size, 544 * 1024)
        self.assertEqual(self.disp_size, 544 * 1024)
        self.assertEqual(self.depth_size, 272 * 1024)
        self.assertEqual(self.scratch_size, 688 * 1024)

    def test_offsets_and_no_overlap(self):
        self.assertEqual(self.draw_offset, 0x00000000)
        self.assertEqual(self.disp_offset, 0x00088000)
        self.assertEqual(self.depth_offset, 0x00110000)
        self.assertEqual(self.scratch_start, 0x00154000)

        # Invariant: Each buffer must end before or at the start of the next
        self.assertLessEqual(self.draw_offset + self.draw_size, self.disp_offset)
        self.assertLessEqual(self.disp_offset + self.disp_size, self.depth_offset)
        self.assertLessEqual(self.depth_offset + self.depth_size, self.scratch_start)
        self.assertLessEqual(self.scratch_start + self.scratch_size, self.total_vram)

    def test_alignment(self):
        # PSP DMA and GE cache bursts require 64-byte alignment
        for name, offset in [
            ("draw", self.draw_offset),
            ("disp", self.disp_offset),
            ("depth", self.depth_offset),
            ("scratch", self.scratch_start)
        ]:
            self.assertEqual(offset % 64, 0, f"{name} offset {hex(offset)} is not 64-byte aligned")

if __name__ == "__main__":
    unittest.main()
