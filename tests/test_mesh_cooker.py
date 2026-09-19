"""Tests for PSP-Forge 3D Mesh Cooker."""
import os
import struct
import tempfile
import unittest

import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from cli.cookers.mesh import cook_mesh


class TestMeshCooker(unittest.TestCase):
    def test_cook_cube_obj(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src_obj = os.path.join(tmpdir, "cube.obj")
            out_p3d = os.path.join(tmpdir, "cube.p3d")

            # Simple triangle quad OBJ
            cube_obj_content = """# Simple Triangle
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 0.0 1.0
vn 0.0 0.0 1.0
f 1/1/1 2/2/1 3/3/1
"""
            with open(src_obj, "w") as f:
                f.write(cube_obj_content)

            info = cook_mesh(src_obj, out_p3d)
            self.assertTrue(os.path.exists(out_p3d))
            self.assertEqual(info["vertex_count"], 3)
            self.assertEqual(info["triangle_count"], 1)

            # Check header
            with open(out_p3d, "rb") as f:
                header = f.read(64)
                magic, ver, vflags, vstride, vcount = struct.unpack("<4sHIHI", header[:16])
                self.assertEqual(magic, b"PM3D")
                self.assertEqual(ver, 1)
                self.assertEqual(vcount, 3)
                self.assertEqual(vstride, 32)


if __name__ == "__main__":
    unittest.main()
