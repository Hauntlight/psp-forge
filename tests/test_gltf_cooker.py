import json
import math
import os
import struct
import tempfile
import unittest

from cli.cookers.gltf import (
    cook_gltf,
    lerp_vec3,
    normalize_quat,
    slerp_quat,
    quat_multiply,
    quat_rotate_vec3,
    compound_transform,
)


def create_synthetic_skinned_glb(num_bones=104) -> bytes:
    """Creates a valid, rigged and animated GLB binary with num_bones."""
    nodes = []
    joints = []
    # Node 0 is root
    nodes.append({"name": "Root", "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "children": []})
    joints.append(0)

    for i in range(1, num_bones):
        p = (i - 1) // 2
        name = f"Bone_{i}"
        if i >= 95:
            name = f"Bone_Nub_{i}"
        elif i >= 85:
            name = f"DEF-eye_{i}"
        elif i >= 75:
            name = f"DEF-finger_distal_{i}"
        elif i >= 65:
            name = f"DEF-arm_twist_{i}"
        nodes.append({"name": name, "translation": [0.0, 0.1, 0.0], "rotation": [0, 0, 0, 1], "children": []})
        joints.append(i)
        nodes[p]["children"].append(i)

    # Geometry: simple quad with 4 vertices
    positions = [
        -0.5, 0.0, 0.0,
         0.5, 0.0, 0.0,
         0.5, 1.0, 0.0,
        -0.5, 1.0, 0.0,
    ]
    normals = [
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
    ]
    uvs = [
        0.0, 0.0,
        1.0, 0.0,
        1.0, 1.0,
        0.0, 1.0,
    ]
    joints_0 = [
        0, 1, 80, 100,
        0, 1, 80, 100,
        0, 1, 80, 100,
        0, 1, 80, 100,
    ]
    weights_0 = [
        0.4, 0.3, 0.2, 0.1,
        0.4, 0.3, 0.2, 0.1,
        0.4, 0.3, 0.2, 0.1,
        0.4, 0.3, 0.2, 0.1,
    ]
    indices = [0, 1, 2, 0, 2, 3]

    bin_data = bytearray()
    def add_buf(data, fmt):
        nonlocal bin_data
        off = len(bin_data)
        b = struct.pack(fmt, *data)
        bin_data.extend(b)
        while len(bin_data) % 4 != 0:
            bin_data.append(0)
        return off, len(b)

    pos_off, pos_len = add_buf(positions, f"<{len(positions)}f")
    norm_off, norm_len = add_buf(normals, f"<{len(normals)}f")
    uv_off, uv_len = add_buf(uvs, f"<{len(uvs)}f")
    j_off, j_len = add_buf(joints_0, f"<{len(joints_0)}H")
    w_off, w_len = add_buf(weights_0, f"<{len(weights_0)}f")
    ind_off, ind_len = add_buf(indices, f"<{len(indices)}H")

    ibms = []
    for _ in range(num_bones):
        ibms.extend([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    ibm_off, ibm_len = add_buf(ibms, f"<{len(ibms)}f")

    anim_times = [0.0, 1.0]
    anim_rots = [0.0, 0.0, 0.0, 1.0, 0.0, 0.70710678, 0.0, 0.70710678]
    t_off, t_len = add_buf(anim_times, "<2f")
    r_off, r_len = add_buf(anim_rots, "<8f")

    bufferViews = [
        {"buffer": 0, "byteOffset": pos_off, "byteLength": pos_len},
        {"buffer": 0, "byteOffset": norm_off, "byteLength": norm_len},
        {"buffer": 0, "byteOffset": uv_off, "byteLength": uv_len},
        {"buffer": 0, "byteOffset": j_off, "byteLength": j_len},
        {"buffer": 0, "byteOffset": w_off, "byteLength": w_len},
        {"buffer": 0, "byteOffset": ind_off, "byteLength": ind_len},
        {"buffer": 0, "byteOffset": ibm_off, "byteLength": ibm_len},
        {"buffer": 0, "byteOffset": t_off, "byteLength": t_len},
        {"buffer": 0, "byteOffset": r_off, "byteLength": r_len},
    ]

    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5123, "count": 4, "type": "VEC4"},
        {"bufferView": 4, "componentType": 5126, "count": 4, "type": "VEC4"},
        {"bufferView": 5, "componentType": 5123, "count": 6, "type": "SCALAR"},
        {"bufferView": 6, "componentType": 5126, "count": num_bones, "type": "MAT4"},
        {"bufferView": 7, "componentType": 5126, "count": 2, "type": "SCALAR"},
        {"bufferView": 8, "componentType": 5126, "count": 2, "type": "VEC4"},
    ]

    gltf_dict = {
        "asset": {"version": "2.0"},
        "nodes": nodes,
        "skins": [{"joints": joints, "inverseBindMatrices": 6}],
        "meshes": [{
            "primitives": [{
                "attributes": {
                    "POSITION": 0,
                    "NORMAL": 1,
                    "TEXCOORD_0": 2,
                    "JOINTS_0": 3,
                    "WEIGHTS_0": 4
                },
                "indices": 5
            }]
        }],
        "animations": [{
            "name": "walk",
            "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}],
            "samplers": [{"input": 7, "output": 8}]
        }],
        "buffers": [{"byteLength": len(bin_data)}],
        "bufferViews": bufferViews,
        "accessors": accessors
    }

    nodes[0]["mesh"] = 0
    json_bytes = json.dumps(gltf_dict).encode("utf-8")
    while len(json_bytes) % 4 != 0:
        json_bytes += b" "

    total_len = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    glb_bytes = bytearray()
    glb_bytes.extend(struct.pack("<4sII", b"glTF", 2, total_len))
    glb_bytes.extend(struct.pack("<I4s", len(json_bytes), b"JSON"))
    glb_bytes.extend(json_bytes)
    glb_bytes.extend(struct.pack("<I4s", len(bin_data), b"BIN\x00"))
    glb_bytes.extend(bin_data)
    return bytes(glb_bytes)


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

    def test_compound_transform(self):
        p_pos = (1.0, 2.0, 3.0)
        p_rot = (0.0, 0.0, 0.0, 1.0)
        c_pos = (0.5, 0.0, 0.0)
        c_rot = (0.0, 0.0, 0.0, 1.0)
        pos, rot = compound_transform(p_pos, p_rot, c_pos, c_rot)
        self.assertEqual(pos, (1.5, 2.0, 3.0))
        self.assertEqual(rot, (0.0, 0.0, 0.0, 1.0))

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

                expected_samples_size = bone_count * frame_count * 16
                remaining = f.read()
                self.assertEqual(len(remaining), expected_samples_size)

    def test_default_engine_mode_bone_reduction(self):
        """Test 1: Model > 96 bones in Default Mode -> reduced to <= 96 bones, single atlas .tex."""
        with tempfile.TemporaryDirectory() as tmpdir:
            glb_path = os.path.join(tmpdir, "model_104.glb")
            with open(glb_path, "wb") as f:
                f.write(create_synthetic_skinned_glb(104))

            res = cook_gltf(glb_path, tmpdir, no_engine=False)
            p3d_file = res["model"][0]
            self.assertTrue(p3d_file.endswith(".p3d"))
            self.assertTrue(os.path.exists(p3d_file))

            with open(p3d_file, "rb") as f:
                magic, ver, bones, chunks = struct.unpack("<4sHHH", f.read(10))
                self.assertEqual(magic, b"P3D2")
                self.assertEqual(ver, 2)
                self.assertEqual(bones, 96)
                self.assertGreater(chunks, 0)

            self.assertEqual(len(res["textures"]), 1)
            self.assertTrue(res["textures"][0].endswith(".tex"))

            panm_file = res["animations"][0]
            with open(panm_file, "rb") as f:
                magic, ver, bone_count, frame_count, fps, dur, pos_scale, _ = struct.unpack("<4sHHIfff8s", f.read(32))
                self.assertEqual(magic, b"PANM")
                self.assertEqual(bone_count, 96)

    def test_agnostic_mode_no_engine(self):
        """Test 2: Model > 96 bones in Agnostic Mode (--no-engine) -> output .p3dx, all bones intact."""
        with tempfile.TemporaryDirectory() as tmpdir:
            glb_path = os.path.join(tmpdir, "model_104.glb")
            with open(glb_path, "wb") as f:
                f.write(create_synthetic_skinned_glb(104))

            res = cook_gltf(glb_path, tmpdir, no_engine=True)
            p3dx_file = res["model"][0]
            self.assertTrue(p3dx_file.endswith(".p3dx"))
            self.assertTrue(os.path.exists(p3dx_file))

            with open(p3dx_file, "rb") as f:
                magic, ver, bones, chunks, mats = struct.unpack("<4sHHHH", f.read(12))
                self.assertEqual(magic, b"P3DX")
                self.assertEqual(ver, 1)
                self.assertEqual(bones, 104)  # All 104 bones preserved!
                self.assertGreater(chunks, 0)
                self.assertGreaterEqual(mats, 1)

            panm_file = res["animations"][0]
            with open(panm_file, "rb") as f:
                magic, ver, bone_count, frame_count, fps, dur, pos_scale, _ = struct.unpack("<4sHHIfff8s", f.read(32))
                self.assertEqual(magic, b"PANM")
                self.assertEqual(bone_count, 104)

    def test_animation_quaternion_integrity(self):
        """Test 3: Verify all .panm samples with compounding maintain ||q|| = 1.0 invariant."""
        with tempfile.TemporaryDirectory() as tmpdir:
            glb_path = os.path.join(tmpdir, "model_104.glb")
            with open(glb_path, "wb") as f:
                f.write(create_synthetic_skinned_glb(104))

            res = cook_gltf(glb_path, tmpdir, no_engine=False)
            panm_file = res["animations"][0]

            with open(panm_file, "rb") as f:
                header = f.read(32)
                magic, ver, bone_count, frame_count, fps, dur, pos_scale, _ = struct.unpack("<4sHHIfff8s", header)
                total_samples = bone_count * frame_count
                for _ in range(total_samples):
                    sample_bytes = f.read(16)
                    qx, qy, qz, qw, px, py, pz, _ = struct.unpack("<4h3hh", sample_bytes)
                    norm = math.sqrt((qx / 32767.0)**2 + (qy / 32767.0)**2 + (qz / 32767.0)**2 + (qw / 32767.0)**2)
                    self.assertAlmostEqual(norm, 1.0, delta=0.01)

    def test_real_model_agnostic_multi_material(self):
        """Test multi-material extraction on real model (knightpr.glb has 11 materials)."""
        test_glb = "/home/hauntlight/psp_game_dev/3d_toolset_and_engine_material/test_models/glb/knightpr.glb"
        if not os.path.exists(test_glb):
            self.skipTest(f"Test model {test_glb} not found")

        with tempfile.TemporaryDirectory() as tmpdir:
            res = cook_gltf(test_glb, tmpdir, no_engine=True)
            self.assertEqual(len(res["textures"]), 11)
            p3dx_file = res["model"][0]
            with open(p3dx_file, "rb") as f:
                magic, ver, bones, chunks, mats = struct.unpack("<4sHHHH", f.read(12))
                self.assertEqual(magic, b"P3DX")
                self.assertEqual(ver, 1)
                self.assertEqual(bones, 78)
                self.assertEqual(mats, 11)
                f.seek(16)
                # Read 11 material names of 32 bytes each
                mat_names = [f.read(32).decode("utf-8").strip("\x00") for _ in range(11)]
                self.assertEqual(len(mat_names), 11)
                self.assertTrue(all(name.endswith(".tex") for name in mat_names))


if __name__ == "__main__":
    unittest.main()
