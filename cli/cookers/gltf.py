"""PSP-Forge glTF 2.0 / GLB 3D Skeletal Animation Cooker.

Parses rigged and animated glTF / GLB models and outputs:
1. .panm: 16-byte aligned, quantized animation clips for libpspforge
2. .p3d (v2): Skeletal multi-chunk model with bone hierarchy and <=8 bones per sub-mesh chunk
"""

import json
import math
import os
import struct
from typing import Dict, List, Optional, Set, Tuple

# GU Vertex format flags matching PSPSDK
GU_TEXTURE_32BITF = 3 << 0
GU_NORMAL_32BITF  = 3 << 5
GU_VERTEX_32BITF  = 3 << 7
GU_WEIGHT_32BITF  = 3 << 9
GU_TRANSFORM_3D   = 0 << 23

def GU_WEIGHTS(n: int) -> int:
    return (((n) - 1) & 7) << 14

def normalize_quat(q: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    x, y, z, w = q
    mag_sq = x * x + y * y + z * z + w * w
    if mag_sq > 1e-8:
        inv = 1.0 / math.sqrt(mag_sq)
        return (x * inv, y * inv, z * inv, w * inv)
    return (0.0, 0.0, 0.0, 1.0)

def slerp_quat(q0: Tuple[float, float, float, float],
               q1: Tuple[float, float, float, float],
               t: float) -> Tuple[float, float, float, float]:
    cos_half = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3]
    target = q1
    if cos_half < 0.0:
        target = (-q1[0], -q1[1], -q1[2], -q1[3])
        cos_half = -cos_half
    
    if cos_half > 0.9995:
        # NLERP fallback
        x = q0[0] + t * (target[0] - q0[0])
        y = q0[1] + t * (target[1] - q0[1])
        z = q0[2] + t * (target[2] - q0[2])
        w = q0[3] + t * (target[3] - q0[3])
        return normalize_quat((x, y, z, w))
    
    half_theta = math.acos(cos_half)
    sin_half = math.sqrt(1.0 - cos_half * cos_half)
    if abs(sin_half) < 1e-5:
        return q0
    
    ratio_a = math.sin((1.0 - t) * half_theta) / sin_half
    ratio_b = math.sin(t * half_theta) / sin_half
    x = q0[0] * ratio_a + target[0] * ratio_b
    y = q0[1] * ratio_a + target[1] * ratio_b
    z = q0[2] * ratio_a + target[2] * ratio_b
    w = q0[3] * ratio_a + target[3] * ratio_b
    return normalize_quat((x, y, z, w))

def lerp_vec3(v0: Tuple[float, float, float],
              v1: Tuple[float, float, float],
              t: float) -> Tuple[float, float, float]:
    return (
        v0[0] + t * (v1[0] - v0[0]),
        v0[1] + t * (v1[1] - v0[1]),
        v0[2] + t * (v1[2] - v0[2])
    )


class GLTFModel:
    def __init__(self, filepath: str):
        self.filepath = filepath
        self.gltf = {}
        self.bin_data = b""
        self._load()

    def _load(self):
        with open(self.filepath, "rb") as f:
            magic = f.read(4)
            if magic == b"glTF":
                version, length = struct.unpack("<II", f.read(8))
                chunk0_len, chunk0_type = struct.unpack("<I4s", f.read(8))
                if chunk0_type != b"JSON":
                    raise ValueError(f"Invalid GLB chunk 0 type: {chunk0_type}")
                self.gltf = json.loads(f.read(chunk0_len).decode("utf-8"))
                if f.tell() < length:
                    chunk1_len, chunk1_type = struct.unpack("<I4s", f.read(8))
                    if chunk1_type == b"BIN\x00":
                        self.bin_data = f.read(chunk1_len)
            else:
                f.seek(0)
                self.gltf = json.loads(f.read().decode("utf-8"))
                # If separate .bin buffer
                if "buffers" in self.gltf and len(self.gltf["buffers"]) > 0:
                    uri = self.gltf["buffers"][0].get("uri", "")
                    if uri and not uri.startswith("data:"):
                        bin_path = os.path.join(os.path.dirname(self.filepath), uri)
                        if os.path.exists(bin_path):
                            with open(bin_path, "rb") as bf:
                                self.bin_data = bf.read()

    def read_accessor(self, acc_idx: int) -> list:
        acc = self.gltf["accessors"][acc_idx]
        bv = self.gltf["bufferViews"][acc["bufferView"]]
        offset = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
        count = acc["count"]
        ctype = acc["componentType"]
        atype = acc["type"]
        num_comp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[atype]
        fmt_char = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}[ctype]
        comp_size = struct.calcsize(fmt_char)
        stride = bv.get("byteStride", num_comp * comp_size)

        data = []
        for i in range(count):
            pos = offset + i * stride
            vals = struct.unpack_from("<" + fmt_char * num_comp, self.bin_data, pos)
            data.append(vals if num_comp > 1 else vals[0])
        return data


def sample_channel(times: list, values: list, t: float, is_quat: bool):
    """Interpolates sampler values at time t."""
    if not times:
        return (0.0, 0.0, 0.0, 1.0) if is_quat else (0.0, 0.0, 0.0)
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]

    # Binary search keyframe interval
    low, high = 0, len(times) - 1
    while low <= high:
        mid = (low + high) // 2
        if times[mid] <= t:
            low = mid + 1
        else:
            high = mid - 1
    idx = max(0, high)
    next_idx = min(len(times) - 1, idx + 1)
    if idx == next_idx:
        return values[idx]

    dt = times[next_idx] - times[idx]
    factor = (t - times[idx]) / dt if dt > 1e-6 else 0.0
    factor = max(0.0, min(1.0, factor))

    if is_quat:
        return slerp_quat(values[idx], values[next_idx], factor)
    else:
        return lerp_vec3(values[idx], values[next_idx], factor)


def cook_gltf_animations(model: GLTFModel, output_dir: str, prefix: str) -> List[str]:
    """Extracts all animation clips from glTF and saves .panm binary files."""
    if "animations" not in model.gltf or not model.gltf["animations"]:
        return []

    skins = model.gltf.get("skins", [])
    if not skins:
        # No skin attached: collect nodes animated
        joints = []
        for a in model.gltf["animations"]:
            for ch in a.get("channels", []):
                nid = ch["target"]["node"]
                if nid not in joints:
                    joints.append(nid)
    else:
        joints = skins[0].get("joints", [])

    if not joints:
        return []

    # Limit to maximum 64 bones supported by runtime
    if len(joints) > 64:
        print(f"  [!] Note: Rig has {len(joints)} joints, clamping tracks to 64 bones for PSP.")
        joints = joints[:64]

    output_files = []
    fps = 30.0
    pos_scale = 0.001  # 1 mm precision

    for anim_idx, anim in enumerate(model.gltf["animations"]):
        anim_name = anim.get("name", f"anim_{anim_idx}")
        # Sanitize filename
        safe_name = "".join(c if c.isalnum() or c in "._-" else "_" for c in anim_name).strip()
        if not safe_name:
            safe_name = f"anim_{anim_idx}"

        # Find max duration
        max_duration = 0.0
        samplers_data = []
        for s in anim.get("samplers", []):
            t_acc = model.read_accessor(s["input"])
            v_acc = model.read_accessor(s["output"])
            if t_acc:
                max_duration = max(max_duration, t_acc[-1])
            samplers_data.append((t_acc, v_acc))

        if max_duration <= 0.0:
            max_duration = 0.033

        # Map channel per joint: joint_idx -> {'rotation': s_idx, 'translation': s_idx}
        channel_map: Dict[int, Dict[str, int]] = {}
        for ch in anim.get("channels", []):
            node_idx = ch["target"]["node"]
            path = ch["target"]["path"]
            if node_idx in joints:
                j_idx = joints.index(node_idx)
                if j_idx not in channel_map:
                    channel_map[j_idx] = {}
                channel_map[j_idx][path] = ch["sampler"]

        frame_count = max(1, int(math.ceil(max_duration * fps)))
        samples_data = bytearray()

        for f_idx in range(frame_count):
            t = (f_idx / fps)
            if t > max_duration:
                t = max_duration

            for j_idx, node_id in enumerate(joints):
                node = model.gltf["nodes"][node_id]

                # Sample rotation
                if j_idx in channel_map and "rotation" in channel_map[j_idx]:
                    s_idx = channel_map[j_idx]["rotation"]
                    times, vals = samplers_data[s_idx]
                    q = sample_channel(times, vals, t, is_quat=True)
                else:
                    q = tuple(node.get("rotation", [0.0, 0.0, 0.0, 1.0]))
                q = normalize_quat(q)

                # Sample translation
                if j_idx in channel_map and "translation" in channel_map[j_idx]:
                    s_idx = channel_map[j_idx]["translation"]
                    times, vals = samplers_data[s_idx]
                    pos = sample_channel(times, vals, t, is_quat=False)
                else:
                    pos = tuple(node.get("translation", [0.0, 0.0, 0.0]))

                # Quantize rotation quaternion (-1.0 to 1.0 -> -32767 to 32767)
                qx = max(-32767, min(32767, int(round(q[0] * 32767.0))))
                qy = max(-32767, min(32767, int(round(q[1] * 32767.0))))
                qz = max(-32767, min(32767, int(round(q[2] * 32767.0))))
                qw = max(-32767, min(32767, int(round(q[3] * 32767.0))))

                # Quantize translation (pos / pos_scale)
                px = max(-32767, min(32767, int(round(pos[0] / pos_scale))))
                py = max(-32767, min(32767, int(round(pos[1] / pos_scale))))
                pz = max(-32767, min(32767, int(round(pos[2] / pos_scale))))

                # Pack ForgeBoneSample (16 bytes)
                samples_data.extend(struct.pack("<4h3hh", qx, qy, qz, qw, px, py, pz, 0))

        # Pack PanmHeader (32 bytes)
        header = struct.pack(
            "<4sHHIfff8s",
            b"PANM",
            1,
            len(joints),
            frame_count,
            fps,
            max_duration,
            pos_scale,
            b"\x00" * 8
        )

        out_name = f"{prefix}_{safe_name}.panm"
        out_path = os.path.join(output_dir, out_name)
        with open(out_path, "wb") as out_f:
            out_f.write(header)
            out_f.write(samples_data)

        output_files.append(out_path)
        print(f"  [+] Saved clip: {out_name} ({frame_count} frames, {len(joints)} bones, {len(header) + len(samples_data)} bytes)")

    return output_files


def cook_gltf_model(model: GLTFModel, output_path: str) -> bool:
    """Extracts skeleton and mesh chunks and saves a .p3d v2 model file."""
    skins = model.gltf.get("skins", [])
    joints = skins[0].get("joints", []) if skins else []
    if len(joints) > 64:
        joints = joints[:64]

    # Map parent of each node
    parent_map: Dict[int, int] = {}
    for p_id, node in enumerate(model.gltf.get("nodes", [])):
        for ch in node.get("children", []):
            parent_map[ch] = p_id

    # Inverse bind matrices
    inv_bind_matrices = []
    if skins and "inverseBindMatrices" in skins[0]:
        ibm_data = model.read_accessor(skins[0]["inverseBindMatrices"])
        inv_bind_matrices = ibm_data

    # Pack ForgeBoneDef array (120 bytes per bone)
    bone_defs = bytearray()
    for idx, j_node_id in enumerate(joints):
        j_node = model.gltf["nodes"][j_node_id]
        name = j_node.get("name", f"bone_{idx}")[:23].encode("utf-8")
        name_padded = name.ljust(24, b"\x00")

        # Parent index relative to joints array
        raw_p = parent_map.get(j_node_id, None)
        p_idx = joints.index(raw_p) if (raw_p in joints) else 0xFF

        local_pos = j_node.get("translation", [0.0, 0.0, 0.0])
        local_rot = j_node.get("rotation", [0.0, 0.0, 0.0, 1.0])

        if idx < len(inv_bind_matrices):
            ibm = inv_bind_matrices[idx]
        else:
            # Identity 4x4
            ibm = [
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0
            ]

        bone_defs.extend(name_padded)
        bone_defs.extend(struct.pack("<B3s3f4f16f",
            p_idx,
            b"\x00" * 3,
            local_pos[0], local_pos[1], local_pos[2],
            local_rot[0], local_rot[1], local_rot[2], local_rot[3],
            *ibm
        ))

    # Collect meshes and chunk them if skinned
    chunks_data = bytearray()
    chunk_count = 0

    for m_idx, mesh in enumerate(model.gltf.get("meshes", [])):
        for p_idx, prim in enumerate(mesh.get("primitives", [])):
            if "POSITION" not in prim["attributes"]:
                continue

            pos_list = model.read_accessor(prim["attributes"]["POSITION"])
            norm_list = model.read_accessor(prim["attributes"]["NORMAL"]) if "NORMAL" in prim["attributes"] else [(0.0, 1.0, 0.0)] * len(pos_list)
            uv_list = model.read_accessor(prim["attributes"]["TEXCOORD_0"]) if "TEXCOORD_0" in prim["attributes"] else [(0.0, 0.0)] * len(pos_list)

            indices = model.read_accessor(prim["indices"]) if "indices" in prim else list(range(len(pos_list)))

            # Check if skinned
            has_skin = ("JOINTS_0" in prim["attributes"] and "WEIGHTS_0" in prim["attributes"])
            if has_skin:
                joint_attr = model.read_accessor(prim["attributes"]["JOINTS_0"])
                weight_attr = model.read_accessor(prim["attributes"]["WEIGHTS_0"])

                # Partition triangles into sub-chunks of <= 8 bones
                num_tris = len(indices) // 3
                raw_chunks: List[Dict] = []

                for t in range(num_tris):
                    i0, i1, i2 = indices[t * 3], indices[t * 3 + 1], indices[t * 3 + 2]
                    tri_bones: Set[int] = set()
                    for vi in (i0, i1, i2):
                        for jb, wb in zip(joint_attr[vi], weight_attr[vi]):
                            if wb > 0.001 and jb < len(joints):
                                tri_bones.add(jb)

                    placed = False
                    for c in raw_chunks:
                        if len(c["bones"].union(tri_bones)) <= 8:
                            c["bones"].update(tri_bones)
                            c["triangles"].append((i0, i1, i2))
                            placed = True
                            break
                    if not placed:
                        raw_chunks.append({"bones": set(tri_bones), "triangles": [(i0, i1, i2)]})

                # Build each skinned chunk
                for c in raw_chunks:
                    palette = sorted(list(c["bones"]))
                    # Pad palette to 8
                    palette_map = {global_b: local_idx for local_idx, global_b in enumerate(palette)}
                    palette_padded = palette + [0] * (8 - len(palette))

                    vtx_bytes = bytearray()
                    min_x, min_y, min_z = float("inf"), float("inf"), float("inf")
                    max_x, max_y, max_z = float("-inf"), float("-inf"), float("-inf")

                    # We support 2 weights hardware blending (GU_WEIGHTS(2))
                    vtx_format = GU_WEIGHTS(2) | GU_WEIGHT_32BITF | GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D
                    vtx_stride = 40  # 2*4 + 2*4 + 3*4 + 3*4 = 40 bytes (multiple of 4)

                    vtx_count = len(c["triangles"]) * 3
                    for tri in c["triangles"]:
                        for vi in tri:
                            px, py, pz = pos_list[vi]
                            nx, ny, nz = norm_list[vi]
                            u, v = uv_list[vi]

                            min_x, min_y, min_z = min(min_x, px), min(min_y, py), min(min_z, pz)
                            max_x, max_y, max_z = max(max_x, px), max(max_y, py), max(max_z, pz)

                            # Extract top 2 local weights
                            j_pairs = []
                            for jb, wb in zip(joint_attr[vi], weight_attr[vi]):
                                if jb in palette_map:
                                    j_pairs.append((palette_map[jb], wb))
                            j_pairs.sort(key=lambda x: x[1], reverse=True)
                            w0 = j_pairs[0][1] if len(j_pairs) > 0 else 1.0
                            w1 = j_pairs[1][1] if len(j_pairs) > 1 else 0.0
                            tot_w = w0 + w1
                            if tot_w > 1e-6:
                                w0 /= tot_w
                                w1 /= tot_w
                            else:
                                w0, w1 = 1.0, 0.0

                            # PSPSDK vertex layout order: Weights -> Texture UV -> Normal -> Position
                            vtx_bytes.extend(struct.pack(
                                "<2f2f3f3f",
                                w0, w1,
                                u, 1.0 - v,
                                nx, ny, nz,
                                px, py, pz
                            ))

                    cx = (min_x + max_x) * 0.5
                    cy = (min_y + max_y) * 0.5
                    cz = (min_z + max_z) * 0.5
                    rad = math.sqrt((max_x - cx)**2 + (max_y - cy)**2 + (max_z - cz)**2)

                    # Pack P3d2ChunkHeader
                    chunk_hdr = struct.pack(
                        "<hB8sIHI3f3f3ff4s",
                        -1,  # node_index = -1 (skinned)
                        len(palette),
                        bytes(palette_padded[:8]),
                        vtx_format,
                        vtx_stride,
                        vtx_count,
                        min_x, min_y, min_z,
                        max_x, max_y, max_z,
                        cx, cy, cz,
                        rad,
                        b"\x00" * 4
                    )
                    chunks_data.extend(chunk_hdr)
                    chunks_data.extend(vtx_bytes)
                    chunk_count += 1
            else:
                # Rigid mesh (Mode A)
                vtx_format = GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D
                vtx_stride = 32
                vtx_bytes = bytearray()
                min_x, min_y, min_z = float("inf"), float("inf"), float("inf")
                max_x, max_y, max_z = float("-inf"), float("-inf"), float("-inf")

                vtx_count = len(indices)
                for vi in indices:
                    px, py, pz = pos_list[vi]
                    nx, ny, nz = norm_list[vi]
                    u, v = uv_list[vi]

                    min_x, min_y, min_z = min(min_x, px), min(min_y, py), min(min_z, pz)
                    max_x, max_y, max_z = max(max_x, px), max(max_y, py), max(max_z, pz)

                    vtx_bytes.extend(struct.pack("<2f3f3f", u, 1.0 - v, nx, ny, nz, px, py, pz))

                cx = (min_x + max_x) * 0.5
                cy = (min_y + max_y) * 0.5
                cz = (min_z + max_z) * 0.5
                rad = math.sqrt((max_x - cx)**2 + (max_y - cy)**2 + (max_z - cz)**2)

                chunk_hdr = struct.pack(
                    "<hB8sIHI3f3f3ff4s",
                    0,  # root node
                    0,
                    b"\x00" * 8,
                    vtx_format,
                    vtx_stride,
                    vtx_count,
                    min_x, min_y, min_z,
                    max_x, max_y, max_z,
                    cx, cy, cz,
                    rad,
                    b"\x00" * 4
                )
                chunks_data.extend(chunk_hdr)
                chunks_data.extend(vtx_bytes)
                chunk_count += 1

    # Write out P3D2 file
    hdr = struct.pack("<4sHHH8s", b"P3D2", 2, len(joints), chunk_count, b"\x00" * 8)
    with open(output_path, "wb") as f:
        f.write(hdr)
        f.write(bone_defs)
        f.write(chunks_data)

    print(f"  [+] Saved model: {os.path.basename(output_path)} ({len(joints)} bones, {chunk_count} chunks)")
    return True


def cook_gltf_textures(model: GLTFModel, output_dir: str, prefix: str) -> List[str]:
    """Extracts embedded glTF textures, resizes if exceeding 512x512, and cooks to .tex."""
    from .texture import cook_texture
    import io
    from PIL import Image

    output_texs = []
    for idx, img in enumerate(model.gltf.get("images", [])):
        if "bufferView" not in img:
            continue
        bv = model.gltf["bufferViews"][img["bufferView"]]
        offset = bv.get("byteOffset", 0)
        length = bv["byteLength"]
        raw_bytes = model.bin_data[offset:offset + length]

        img_name = img.get("name", f"tex_{idx}")
        safe_name = "".join(c if c.isalnum() or c in "._-" else "_" for c in img_name).strip()
        if not safe_name:
            safe_name = f"tex_{idx}"

        try:
            im = Image.open(io.BytesIO(raw_bytes))
            # Auto-downsample to PSP hardware limit 512x512 if necessary
            w, h = im.size
            if w > 512 or h > 512:
                scale = min(512.0 / w, 512.0 / h)
                new_w = max(8, min(512, int(w * scale)))
                new_h = max(8, min(512, int(h * scale)))
                resample = getattr(Image, "Resampling", Image).LANCZOS
                im = im.resize((new_w, new_h), resample)

            # Save temporary PNG
            tmp_png = os.path.join(output_dir, f"{prefix}_{safe_name}_tmp.png")
            im.save(tmp_png, "PNG")

            dst_tex = os.path.join(output_dir, f"{prefix}_{safe_name}.tex")
            cook_texture(tmp_png, dst_tex, format_type="8888", swizzle=True)
            if os.path.exists(tmp_png):
                os.remove(tmp_png)
            output_texs.append(dst_tex)
            print(f"  [+] Extracted & cooked texture: {os.path.basename(dst_tex)}")
        except Exception as e:
            print(f"  [!] Warning: Failed to extract texture {img_name}: {e}")

    return output_texs


def cook_gltf(input_path: str, output_dir: str) -> Dict[str, list]:
    """Top-level cooker entry point for glTF / GLB assets."""
    os.makedirs(output_dir, exist_ok=True)
    base_name = os.path.splitext(os.path.basename(input_path))[0]

    model = GLTFModel(input_path)
    model_output = os.path.join(output_dir, f"{base_name}.p3d")
    cook_gltf_model(model, model_output)
    anims_output = cook_gltf_animations(model, output_dir, prefix=base_name)
    texs_output = cook_gltf_textures(model, output_dir, prefix=base_name)

    return {
        "model": [model_output],
        "animations": anims_output,
        "textures": texs_output
    }
