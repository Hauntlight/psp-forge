import os
import struct
import tempfile
import unittest

from cli.cookers.gltf import (
    cook_gltf,
    lerp_vec3,
    normalize_quat,
    slerp_quat,
)

class TestGLTFCooker(unittest.TestCase):
    def test_quat_normalize(self):
        q = (0.0, 3.0, 4.0, 0.0)
        nq = normalize_quat(q)
        mag = sum(x * x for x in nq)
        self.assertAlmostEqual(mag, 1.0, places=5)
        self.assertAlmostEqual(nq[1], 0.6, places=5)
        self.assertAlmostEqual(nq[2], 0.8, places=5)

    def test_slerp_endpoints(self):
        q0 = (0.0, 0.0, 0.0, 1.0)
        q1 = (0.0, 0.70710678, 0.0, 0.70710678)
        s0 = slerp_quat(q0, q1, 0.0)
        s1 = slerp_quat(q0, q1, 1.0)
        self.assertAlmostEqual(s0[3], 1.0, places=4)
        self.assertAlmostEqual(s1[1], 0.7071, places=4)

    def test_slerp_midpoint(self):
        q0 = (0.0, 0.0, 0.0, 1.0)
        q1 = (0.0, 0.70710678, 0.0, 0.70710678)
        mid = slerp_quat(q0, q1, 0.5)
        mag = sum(x * x for x in mid)
        self.assertAlmostEqual(mag, 1.0, places=5)

    def test_cook_real_glb_model(self):
        test_glb = "/home/hauntlight/psp_game_dev/3d_toolset_and_engine_material/test_models/glb/armorhelmet.glb"
        if not os.path.exists(test_glb):
            self.skipTest(f"Test model {test_glb} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            res = cook_gltf(test_glb, tmpdir)
            self.assertTrue(len(res["model"]) > 0)
            p3d_file = res["model"][0]
            self.assertTrue(os.path.exists(p3d_file))
            with open(p3d_file, "rb") as f:
                magic, ver, bones, chunks = struct.unpack("<4sHHH", f.read(10))
                self.assertEqual(magic, b"P3D2")
                self.assertEqual(ver, 2)
                self.assertTrue(chunks > 0)

    def test_cook_real_glb_animated_skin(self):
        test_glb = "/home/hauntlight/psp_game_dev/3d_toolset_and_engine_material/test_models/glb/basemeshpr.glb"
        if not os.path.exists(test_glb):
            self.skipTest(f"Test model {test_glb} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            res = cook_gltf(test_glb, tmpdir)
            self.assertTrue(len(res["model"]) > 0)
            self.assertTrue(len(res["animations"]) > 0)

            # Check a .panm file
            panm_file = res["animations"][0]
            self.assertTrue(os.path.exists(panm_file))
            with open(panm_file, "rb") as f:
                header_bytes = f.read(32)
                magic, ver, bone_count, frame_count, fps, duration, pos_scale, _ = struct.unpack("<4sHHIfff8s", header_bytes)
                self.assertEqual(magic, b"PANM")
                self.assertEqual(ver, 1)
                self.assertTrue(bone_count > 0)
                self.assertTrue(frame_count > 0)
                self.assertGreater(fps, 0.0)
                self.assertGreater(duration, 0.0)

                # Check sample size alignment
                expected_samples_size = bone_count * frame_count * 16
                remaining = f.read()
                self.assertEqual(len(remaining), expected_samples_size)

if __name__ == "__main__":
    unittest.main()
