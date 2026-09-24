"""PSP-Forge glTF 2.0 / GLB 3D Skeletal Animation Cooker.

Dual-Mode Cooker:
1. Engine Mode (default):
   - Model: .p3d (Magic: P3D2, Version: 2, <= 96 bones)
   - Automatic Bone Reduction & Compounding if source skeleton has > 96 bones
   - Material Atlas Fusion (single 512x512 master texture .tex) + UV remapping
   - Auto-Chroma Keying for cutouts
   - Hardware chunking: <= 8 local bones per sub-mesh chunk

2. Agnostic Mode (--no-engine):
   - Model: .p3dx (Magic: P3DX, Version: 1, unlimited bones)
   - Multi-Material: individual textures (.tex) with authentic alpha
   - Original UVs preserved intact (no atlas remapping)
   - Chunks partitioned primarily by Material ID + <= 8 local bones
   - Material Name Table in header for external loaders
"""

import io
import json
import math
import os
import struct
from typing import Dict, List, Optional, Set, Tuple

from PIL import Image
from .texture import cook_texture

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

def quat_multiply(q1: Tuple[float, float, float, float],
                  q2: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return (
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2
    )

def quat_rotate_vec3(q: Tuple[float, float, float, float],
                     v: Tuple[float, float, float]) -> Tuple[float, float, float]:
    x, y, z, w = normalize_quat(q)
    vx, vy, vz = v
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (
        vx + w * tx + (y * tz - z * ty),
        vy + w * ty + (z * tx - x * tz),
        vz + w * tz + (x * ty - y * tx)
    )

def compound_transform(parent_pos: Tuple[float, float, float],
                       parent_rot: Tuple[float, float, float, float],
                       child_pos: Tuple[float, float, float],
                       child_rot: Tuple[float, float, float, float]) -> Tuple[Tuple[float, float, float], Tuple[float, float, float, float]]:
    rot = normalize_quat(quat_multiply(parent_rot, child_rot))
    rot_c_pos = quat_rotate_vec3(parent_rot, child_pos)
    pos = (
        parent_pos[0] + rot_c_pos[0],
        parent_pos[1] + rot_c_pos[1],
        parent_pos[2] + rot_c_pos[2]
    )
    return pos, rot


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


class ParsedPrimitive:
    def __init__(self, positions: list, normals: list, uvs: list, indices: list,
                 material_idx: Optional[int], joints_attr: Optional[list] = None,
                 weights_attr: Optional[list] = None):
        self.positions = positions
        self.normals = normals
        self.uvs = uvs
        self.indices = indices
        self.material_idx = material_idx
        self.has_skin = (joints_attr is not None and weights_attr is not None)
        self.influences: List[List[List[float]]] = []  # per vertex: list of [joint_idx, weight]

        if self.has_skin:
            for vi in range(len(positions)):
                infl = []
                for jb, wb in zip(joints_attr[vi], weights_attr[vi]):
                    if wb > 0.0001:
                        infl.append([int(jb), float(wb)])
                # Normalize initially
                tot = sum(e[1] for e in infl)
                if tot > 1e-6:
                    for e in infl:
                        e[1] /= tot
                self.influences.append(infl)


def sample_channel(times: list, values: list, t: float, is_quat: bool):
    """Interpolates sampler values at time t."""
    if not times:
        return (0.0, 0.0, 0.0, 1.0) if is_quat else (0.0, 0.0, 0.0)
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]

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


# =============================================================================
# Bone Reduction & Compounding Engine (Mode 1 / Engine Mode)
# =============================================================================

def pick_bone_to_prune(joints: List[int], parent_map: Dict[int, int],
                       model: GLTFModel, weight_sums: Dict[int, float]) -> Optional[int]:
    """
    Selects the best candidate bone index to prune based on priorities in Section 4.A.
    """
    FACE_KEYWORDS = [
        "eye", "eyelid", "jaw", "lip", "tongue", "brow", "nose", "cheek",
        "chin", "teeth", "mouth", "facial", "viseme"
    ]
    FINGER_KEYWORDS = [
        "finger", "thumb", "index", "middle", "ring", "pinky", "little"
    ]
    DISTAL_KEYWORDS = [
        "distal", "intermediate", "mid", "02", "03", "_02", "_03", ".02", ".03",
        "_2", "_3", ".2", ".3", "tip", "end"
    ]
    TWIST_KEYWORDS = [
        "twist", "roll", "aux", "helper", "bend", "corrective"
    ]

    joint_set = set(joints)
    has_child_in_joints = set()
    for j_node in joints:
        p_node = parent_map.get(j_node)
        if p_node in joint_set:
            has_child_in_joints.add(p_node)

    candidates = []

    for j_idx, node_id in enumerate(joints):
        p_node = parent_map.get(node_id)
        if p_node not in joint_set:
            # Cannot prune root bone without parent
            continue

        node = model.gltf["nodes"][node_id]
        name = (node.get("name") or "").lower()
        w_sum = weight_sums.get(j_idx, 0.0)
        is_leaf = (node_id not in has_child_in_joints)

        # 1. Zero-weight terminal bone
        if is_leaf and w_sum < 1e-4:
            priority = 1
        # 2. Face detail
        elif any(k in name for k in FACE_KEYWORDS):
            priority = 2
        # 3. Finger intermediate / distal phalanx
        elif any(k in name for k in FINGER_KEYWORDS) and any(d in name for d in DISTAL_KEYWORDS):
            priority = 3
        # 4. Twist bone
        elif any(k in name for k in TWIST_KEYWORDS):
            priority = 4
        # 5. General fallback
        else:
            priority = 5

        # Sort order: priority (1..5), leaf first (0=leaf, 1=non-leaf), smallest weight sum
        candidates.append((priority, 0 if is_leaf else 1, w_sum, j_idx))

    if not candidates:
        return None

    candidates.sort()
    return candidates[0][3]


def reduce_skeleton_bones(model: GLTFModel, parsed_prims: List[ParsedPrimitive],
                          inv_bind_matrices: List, max_bones: int = 96) -> Tuple[List[int], Dict[int, List[int]]]:
    """
    Deterministically reduces bones down to max_bones (<=96) using Weight Collapsing
    and Transform Compounding as specified in Section 4.
    """
    skins = model.gltf.get("skins", [])
    if not skins or "joints" not in skins[0]:
        return [], {}

    joints = list(skins[0]["joints"])
    if len(joints) <= max_bones:
        return joints, {}

    print(f"  [!] Skeleton has {len(joints)} bones (> {max_bones}). Applying Bone Reduction & Compounding...")

    parent_map: Dict[int, int] = {}
    for p_id, node in enumerate(model.gltf.get("nodes", [])):
        for ch in node.get("children", []):
            parent_map[ch] = p_id

    compounding_chain: Dict[int, List[int]] = {}

    while len(joints) > max_bones:
        weight_sums = {j: 0.0 for j in range(len(joints))}
        for prim in parsed_prims:
            if prim.has_skin:
                for infl in prim.influences:
                    for jb, wb in infl:
                        if jb in weight_sums:
                            weight_sums[jb] += wb

        cand_idx = pick_bone_to_prune(joints, parent_map, model, weight_sums)
        if cand_idx is None:
            print(f"  [!] Warning: Could not find pruneable bone with parent. Stopping reduction at {len(joints)} bones.")
            break

        b_node = joints[cand_idx]
        p_node = parent_map.get(b_node)
        p_idx = joints.index(p_node)

        # 1. Compounding transforms for any children of b_node
        b_node_dict = model.gltf["nodes"][b_node]
        b_pos = tuple(b_node_dict.get("translation", [0.0, 0.0, 0.0]))
        b_rot = tuple(b_node_dict.get("rotation", [0.0, 0.0, 0.0, 1.0]))

        b_children = b_node_dict.get("children", [])
        for ch_id in b_children:
            ch_node = model.gltf["nodes"][ch_id]
            parent_map[ch_id] = p_node
            ch_pos = tuple(ch_node.get("translation", [0.0, 0.0, 0.0]))
            ch_rot = tuple(ch_node.get("rotation", [0.0, 0.0, 0.0, 1.0]))
            new_pos, new_rot = compound_transform(b_pos, b_rot, ch_pos, ch_rot)
            ch_node["translation"] = list(new_pos)
            ch_node["rotation"] = list(new_rot)

            compounding_chain[ch_id] = compounding_chain.get(b_node, []) + [b_node]

        p_node_dict = model.gltf["nodes"][p_node]
        if "children" in p_node_dict:
            if b_node in p_node_dict["children"]:
                p_node_dict["children"].remove(b_node)
            for ch_id in b_children:
                if ch_id not in p_node_dict["children"]:
                    p_node_dict["children"].append(ch_id)

        # 2. Collapse vertex weights into parent bone
        for prim in parsed_prims:
            if prim.has_skin:
                for infl in prim.influences:
                    b_weight = 0.0
                    p_entry = None
                    new_infl = []
                    for entry in infl:
                        if entry[0] == cand_idx:
                            b_weight += entry[1]
                        elif entry[0] == p_idx:
                            p_entry = entry
                            new_infl.append(entry)
                        else:
                            new_infl.append(entry)
                    if b_weight > 0.0:
                        if p_entry is not None:
                            p_entry[1] += b_weight
                        else:
                            new_infl.append([p_idx, b_weight])

                    tot = sum(e[1] for e in new_infl)
                    if tot > 1e-6:
                        for e in new_infl:
                            e[1] /= tot
                    else:
                        new_infl = [[p_idx, 1.0]]

                    # Remap indices after cand_idx removal
                    for e in new_infl:
                        if e[0] > cand_idx:
                            e[0] -= 1
                    infl.clear()
                    infl.extend(new_infl)

        # 3. Remove cand_idx from joints and inverse bind matrices
        joints.pop(cand_idx)
        if inv_bind_matrices and cand_idx < len(inv_bind_matrices):
            inv_bind_matrices.pop(cand_idx)

    print(f"  [+] Skeleton reduced to {len(joints)} bones.")
    return joints, compounding_chain


# =============================================================================
# Texture Cooking Pipelines (Dual Mode)
# =============================================================================

def cook_gltf_textures(model: GLTFModel, output_dir: str, prefix: str) -> Tuple[List[str], Dict[int, Tuple[float, float, float, float]]]:
    """
    Engine Mode: Fuses multiple materials into a single 512x512 master texture atlas.
    """
    def _create_default_atlas():
        def_img = Image.new("RGBA", (16, 16), (255, 255, 255, 255))
        tmp_png = os.path.join(output_dir, f"{prefix}_atlas_tmp.png")
        def_img.save(tmp_png, "PNG")
        dst_tex = os.path.join(output_dir, f"{prefix}.tex")
        cook_texture(tmp_png, dst_tex, format_type="8888", swizzle=True)
        if os.path.exists(tmp_png):
            os.remove(tmp_png)
        print(f"  [+] Material Fusion: generated default texture atlas {os.path.basename(dst_tex)}")
        return [dst_tex], {0: (0.0, 0.0, 1.0, 1.0)}

    images = model.gltf.get("images", [])
    if not images:
        return _create_default_atlas()

    pil_images: List[Optional[Image.Image]] = []
    for idx, img in enumerate(images):
        if "bufferView" not in img:
            pil_images.append(None)
            continue
        bv = model.gltf["bufferViews"][img["bufferView"]]
        offset = bv.get("byteOffset", 0)
        length = bv["byteLength"]
        raw_bytes = model.bin_data[offset:offset + length]
        try:
            im = Image.open(io.BytesIO(raw_bytes)).convert("RGBA")
            # Auto-detect neutral solid background (Auto-Chroma Keying)
            alpha_chan = im.getchannel("A")
            alpha_data = getattr(alpha_chan, "get_flattened_data", alpha_chan.getdata)()
            min_a = min(alpha_data)
            if min_a > 180:
                corners = [im.getpixel((0, 0)), im.getpixel((im.width - 1, 0)),
                           im.getpixel((0, im.height - 1)), im.getpixel((im.width - 1, im.height - 1))]
                c0 = corners[0][:3]
                if all(max(abs(c[i] - c0[i]) for i in range(3)) <= 3 for c in corners):
                    if c0 == (95, 95, 95) or (abs(c0[0] - c0[1]) <= 2 and abs(c0[1] - c0[2]) <= 2 and 40 <= c0[0] <= 210):
                        pix = im.load()
                        for py in range(im.height):
                            for px in range(im.width):
                                r, g, b, a = pix[px, py]
                                if max(abs(r - c0[0]), abs(g - c0[1]), abs(b - c0[2])) <= 8:
                                    pix[px, py] = (0, 0, 0, 0)
                        print(f"  [+] Auto keyed-out solid matte background {c0} for image {idx} ({img.get('name')})")
            pil_images.append(im)
        except Exception as e:
            print(f"  [!] Warning: Failed to decode image {idx}: {e}")
            pil_images.append(None)

    valid_images = [img for img in pil_images if img is not None]
    if not valid_images:
        return _create_default_atlas()

    uv_transforms: Dict[int, Tuple[float, float, float, float]] = {}
    atlas_w, atlas_h = 512, 512
    atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))

    if len(valid_images) == 1:
        single_idx = next(i for i, img in enumerate(pil_images) if img is not None)
        im = pil_images[single_idx]
        resample = getattr(Image, "Resampling", Image).LANCZOS
        im_resized = im.resize((atlas_w, atlas_h), resample)
        atlas.paste(im_resized, (0, 0))
        uv_transforms[single_idx] = (0.0, 0.0, 1.0, 1.0)
    else:
        num_imgs = len(valid_images)
        if num_imgs <= 2:
            cols, rows = 2, 1
        elif num_imgs <= 4:
            cols, rows = 2, 2
        elif num_imgs <= 6:
            cols, rows = 3, 2
        else:
            cols, rows = 4, 2

        tile_w = atlas_w // cols
        tile_h = atlas_h // rows
        resample = getattr(Image, "Resampling", Image).LANCZOS

        valid_count = 0
        for idx, im in enumerate(pil_images):
            if im is None:
                continue
            col = valid_count % cols
            row = valid_count // cols
            valid_count += 1

            im_tile = im.resize((tile_w, tile_h), resample)
            pos_x = col * tile_w
            pos_y = row * tile_h
            atlas.paste(im_tile, (pos_x, pos_y))

            u_off = pos_x / float(atlas_w)
            v_off = pos_y / float(atlas_h)
            u_scale = tile_w / float(atlas_w)
            v_scale = tile_h / float(atlas_h)
            uv_transforms[idx] = (u_off, v_off, u_scale, v_scale)

    tmp_png = os.path.join(output_dir, f"{prefix}_atlas_tmp.png")
    atlas.save(tmp_png, "PNG")

    dst_tex = os.path.join(output_dir, f"{prefix}.tex")
    cook_texture(tmp_png, dst_tex, format_type="8888", swizzle=True)
    if os.path.exists(tmp_png):
        os.remove(tmp_png)

    print(f"  [+] Material Fusion: generated single texture atlas {os.path.basename(dst_tex)} ({len(valid_images)} textures fused)")
    return [dst_tex], uv_transforms


def cook_gltf_textures_agnostic(model: GLTFModel, output_dir: str, prefix: str) -> Tuple[List[str], List[str]]:
    """
    Agnostic Mode (--no-engine): Exports each material/texture as a separate .tex file.
    Preserves original dimensions, authentic transparency, and no atlas UV alteration.
    Returns:
        (output_tex_paths, material_names)
    """
    materials = model.gltf.get("materials", [])
    images = model.gltf.get("images", [])
    textures = model.gltf.get("textures", [])

    output_texs = []
    material_names = []

    if not materials:
        # Default single material
        def_img = Image.new("RGBA", (16, 16), (255, 255, 255, 255))
        tmp_png = os.path.join(output_dir, f"{prefix}_default.png")
        def_img.save(tmp_png, "PNG")
        dst_tex = os.path.join(output_dir, f"{prefix}_default.tex")
        cook_texture(tmp_png, dst_tex, format_type="8888", swizzle=True)
        if os.path.exists(tmp_png):
            os.remove(tmp_png)
        return [dst_tex], [os.path.basename(dst_tex)]

    for m_idx, mat in enumerate(materials):
        raw_mat_name = mat.get("name") or f"mat_{m_idx}"
        safe_mat_name = "".join(c if c.isalnum() or c in "._-" else "_" for c in raw_mat_name).strip() or f"mat_{m_idx}"
        tex_filename = f"{prefix}_{safe_mat_name}.tex"
        dst_tex = os.path.join(output_dir, tex_filename)

        if tex_filename in material_names:
            tex_filename = f"{prefix}_{safe_mat_name}_{m_idx}.tex"
            dst_tex = os.path.join(output_dir, tex_filename)

        pbr = mat.get("pbrMetallicRoughness", {})
        bct = pbr.get("baseColorTexture", {})
        tex_idx = bct.get("index")

        img_found = False
        if tex_idx is not None and tex_idx < len(textures):
            img_idx = textures[tex_idx].get("source")
            if img_idx is not None and img_idx < len(images):
                img_obj = images[img_idx]
                if "bufferView" in img_obj:
                    bv = model.gltf["bufferViews"][img_obj["bufferView"]]
                    offset = bv.get("byteOffset", 0)
                    length = bv["byteLength"]
                    raw_bytes = model.bin_data[offset:offset + length]
                    try:
                        im = Image.open(io.BytesIO(raw_bytes)).convert("RGBA")
                        if im.width > 512 or im.height > 512:
                            resample = getattr(Image, "Resampling", Image).LANCZOS
                            im.thumbnail((512, 512), resample)
                        tmp_png = os.path.join(output_dir, f"{prefix}_{safe_mat_name}_tmp.png")
                        im.save(tmp_png, "PNG")
                        cook_texture(tmp_png, dst_tex, format_type="8888", swizzle=True)
                        if os.path.exists(tmp_png):
                            os.remove(tmp_png)
                        img_found = True
                    except Exception as e:
                        print(f"  [!] Warning: Failed to decode image for material {m_idx}: {e}")

        if not img_found:
            bcf = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
            rgba = tuple(int(round(c * 255.0)) for c in bcf)
            solid_im = Image.new("RGBA", (16, 16), rgba)
            tmp_png = os.path.join(output_dir, f"{prefix}_{safe_mat_name}_tmp.png")
            solid_im.save(tmp_png, "PNG")
            cook_texture(tmp_png, dst_tex, format_type="8888", swizzle=True)
            if os.path.exists(tmp_png):
                os.remove(tmp_png)

        output_texs.append(dst_tex)
        material_names.append(os.path.basename(dst_tex))

    print(f"  [+] Multi-Material: exported {len(material_names)} individual textures (.tex)")
    return output_texs, material_names


# =============================================================================
# Model Cooking (.p3d v2 & .p3dx v1)
# =============================================================================

def cook_gltf_model(
    model: GLTFModel,
    parsed_prims: List[ParsedPrimitive],
    joints: List[int],
    inv_bind_matrices: List,
    output_path: str,
    uv_transforms: Optional[Dict[int, Tuple[float, float, float, float]]] = None,
    material_names: Optional[List[str]] = None,
    no_engine: bool = False
) -> bool:
    """
    Builds the binary model file:
    - If no_engine == False: generates .p3d (P3D2 v2) with atlas UV transforms
    - If no_engine == True:  generates .p3dx (P3DX v1) with material_id per chunk & original UVs
    """
    if uv_transforms is None:
        uv_transforms = {}
    if material_names is None:
        material_names = []

    parent_map: Dict[int, int] = {}
    for p_id, node in enumerate(model.gltf.get("nodes", [])):
        for ch in node.get("children", []):
            parent_map[ch] = p_id

    # Build ForgeBoneDef array (120 bytes per bone)
    bone_defs = bytearray()
    for idx, j_node_id in enumerate(joints):
        j_node = model.gltf["nodes"][j_node_id]
        name = j_node.get("name", f"bone_{idx}")[:23].encode("utf-8")
        name_padded = name.ljust(24, b"\x00")

        raw_p = parent_map.get(j_node_id, None)
        p_idx = joints.index(raw_p) if (raw_p in joints) else 0xFF

        local_pos = j_node.get("translation", [0.0, 0.0, 0.0])
        local_rot = j_node.get("rotation", [0.0, 0.0, 0.0, 1.0])

        if idx < len(inv_bind_matrices):
            ibm = inv_bind_matrices[idx]
        else:
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

    def get_primitive_uv_transform(mat_idx: Optional[int]) -> Tuple[float, float, float, float]:
        if mat_idx is not None and "materials" in model.gltf and mat_idx < len(model.gltf["materials"]):
            mat = model.gltf["materials"][mat_idx]
            pbr = mat.get("pbrMetallicRoughness", {})
            bct = pbr.get("baseColorTexture", {})
            tex_idx = bct.get("index")
            if tex_idx is not None and "textures" in model.gltf and tex_idx < len(model.gltf["textures"]):
                texture_obj = model.gltf["textures"][tex_idx]
                img_source = texture_obj.get("source")
                if img_source is not None and img_source in uv_transforms:
                    return uv_transforms[img_source]
        if 0 in uv_transforms:
            return uv_transforms[0]
        return (0.0, 0.0, 1.0, 1.0)

    chunks_data = bytearray()
    chunk_count = 0

    for prim in parsed_prims:
        pos_list = prim.positions
        norm_list = prim.normals
        uv_list = prim.uvs
        indices = prim.indices

        if not no_engine:
            u_off, v_off, u_scale, v_scale = get_primitive_uv_transform(prim.material_idx)
            chunk_mat_id = 0
        else:
            u_off, v_off, u_scale, v_scale = 0.0, 0.0, 1.0, 1.0
            chunk_mat_id = prim.material_idx if (prim.material_idx is not None and prim.material_idx < len(material_names)) else 0

        if prim.has_skin and len(joints) > 0:
            num_tris = len(indices) // 3
            raw_chunks: List[Dict] = []

            for t in range(num_tris):
                i0, i1, i2 = indices[t * 3], indices[t * 3 + 1], indices[t * 3 + 2]
                tri_bones: Set[int] = set()
                for vi in (i0, i1, i2):
                    for jb, wb in prim.influences[vi]:
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

            for c in raw_chunks:
                palette = sorted(list(c["bones"]))
                num_local_bones = len(palette)
                if num_local_bones == 0:
                    palette = [0]
                    num_local_bones = 1

                palette_map = {global_b: local_idx for local_idx, global_b in enumerate(palette)}
                palette_padded = palette + [0] * (8 - len(palette))

                vtx_bytes = bytearray()
                min_x, min_y, min_z = float("inf"), float("inf"), float("inf")
                max_x, max_y, max_z = float("-inf"), float("-inf"), float("-inf")

                vtx_format = GU_WEIGHTS(num_local_bones) | GU_WEIGHT_32BITF | GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D
                vtx_stride = num_local_bones * 4 + 2 * 4 + 3 * 4 + 3 * 4

                vtx_count = len(c["triangles"]) * 3
                for tri in c["triangles"]:
                    for vi in tri:
                        px, py, pz = pos_list[vi]
                        nx, ny, nz = norm_list[vi]
                        raw_u, raw_v = uv_list[vi]

                        u = raw_u * u_scale + u_off
                        v = raw_v * v_scale + v_off

                        min_x, min_y, min_z = min(min_x, px), min(min_y, py), min(min_z, pz)
                        max_x, max_y, max_z = max(max_x, px), max(max_y, py), max(max_z, pz)

                        slot_weights = [0.0] * num_local_bones
                        for jb, wb in prim.influences[vi]:
                            if wb > 0.001 and jb in palette_map:
                                slot_weights[palette_map[jb]] += wb

                        tot_w = sum(slot_weights)
                        if tot_w > 1e-6:
                            slot_weights = [w / tot_w for w in slot_weights]
                        else:
                            slot_weights[0] = 1.0

                        vtx_bytes.extend(struct.pack(
                            f"<{num_local_bones}f2f3f3f",
                            *slot_weights,
                            u, v,
                            nx, ny, nz,
                            px, py, pz
                        ))

                cx = (min_x + max_x) * 0.5
                cy = (min_y + max_y) * 0.5
                cz = (min_z + max_z) * 0.5
                rad = math.sqrt((max_x - cx)**2 + (max_y - cy)**2 + (max_z - cz)**2)

                if not no_engine:
                    # P3d2ChunkHeader (65 bytes)
                    chunk_hdr = struct.pack(
                        "<hB8sIHI3f3f3ff4s",
                        -1,
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
                else:
                    # P3dxChunkHeader (65 bytes, material_id: uint16, reserved: 2s)
                    chunk_hdr = struct.pack(
                        "<hB8sHIHI3f3f3ff2s",
                        -1,
                        len(palette),
                        bytes(palette_padded[:8]),
                        chunk_mat_id,
                        vtx_format,
                        vtx_stride,
                        vtx_count,
                        min_x, min_y, min_z,
                        max_x, max_y, max_z,
                        cx, cy, cz,
                        rad,
                        b"\x00" * 2
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
                raw_u, raw_v = uv_list[vi]

                u = raw_u * u_scale + u_off
                v = raw_v * v_scale + v_off

                min_x, min_y, min_z = min(min_x, px), min(min_y, py), min(min_z, pz)
                max_x, max_y, max_z = max(max_x, px), max(max_y, py), max(max_z, pz)

                vtx_bytes.extend(struct.pack("<2f3f3f", u, v, nx, ny, nz, px, py, pz))

            cx = (min_x + max_x) * 0.5
            cy = (min_y + max_y) * 0.5
            cz = (min_z + max_z) * 0.5
            rad = math.sqrt((max_x - cx)**2 + (max_y - cy)**2 + (max_z - cz)**2)

            if not no_engine:
                chunk_hdr = struct.pack(
                    "<hB8sIHI3f3f3ff4s",
                    0,
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
            else:
                chunk_hdr = struct.pack(
                    "<hB8sHIHI3f3f3ff2s",
                    0,
                    0,
                    b"\x00" * 8,
                    chunk_mat_id,
                    vtx_format,
                    vtx_stride,
                    vtx_count,
                    min_x, min_y, min_z,
                    max_x, max_y, max_z,
                    cx, cy, cz,
                    rad,
                    b"\x00" * 2
                )

            chunks_data.extend(chunk_hdr)
            chunks_data.extend(vtx_bytes)
            chunk_count += 1

    with open(output_path, "wb") as f:
        if not no_engine:
            # P3d2Header (16 bytes)
            hdr = struct.pack("<4sHHH8s", b"P3D2", 2, len(joints), chunk_count, b"\x00" * 8)
            f.write(hdr)
            f.write(bone_defs)
            f.write(chunks_data)
        else:
            # P3dxHeader (16 bytes)
            hdr = struct.pack("<4sHHHH6s", b"P3DX", 1, len(joints), chunk_count, len(material_names), b"\x00" * 6)
            f.write(hdr)
            # Material Name Table (material_count * 32 bytes)
            for mat_name in material_names:
                name_bytes = mat_name.encode("utf-8")[:31].ljust(32, b"\x00")
                f.write(name_bytes)
            f.write(bone_defs)
            f.write(chunks_data)

    print(f"  [+] Saved model: {os.path.basename(output_path)} ({len(joints)} bones, {chunk_count} chunks)")
    return True


# =============================================================================
# Skeletal Animation Player Baking (.panm)
# =============================================================================

def cook_gltf_animations(
    model: GLTFModel,
    output_dir: str,
    prefix: str,
    joints: List[int],
    compounding_chain: Optional[Dict[int, List[int]]] = None,
    no_engine: bool = False
) -> List[str]:
    """
    Extracts all animation clips from glTF and saves .panm binary files at 30 FPS.
    Applies compounding transform chains for collapsed bones.
    """
    if "animations" not in model.gltf or not model.gltf["animations"] or not joints:
        return []

    if compounding_chain is None:
        compounding_chain = {}

    output_files = []
    fps = 30.0
    pos_scale = 0.001  # 1 mm precision

    for anim_idx, anim in enumerate(model.gltf["animations"]):
        anim_name = anim.get("name", f"anim_{anim_idx}")
        safe_name = "".join(c if c.isalnum() or c in "._-" else "_" for c in anim_name).strip() or f"anim_{anim_idx}"

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

        # Map channel per node_id: node_id -> {'rotation': s_idx, 'translation': s_idx}
        node_channel_map: Dict[int, Dict[str, int]] = {}
        for ch in anim.get("channels", []):
            node_idx = ch["target"]["node"]
            path = ch["target"]["path"]
            if node_idx not in node_channel_map:
                node_channel_map[node_idx] = {}
            node_channel_map[node_idx][path] = ch["sampler"]

        def sample_node(n_id: int, t_sec: float) -> Tuple[Tuple[float, float, float], Tuple[float, float, float, float]]:
            node_dict = model.gltf["nodes"][n_id]
            if n_id in node_channel_map and "translation" in node_channel_map[n_id]:
                s_idx = node_channel_map[n_id]["translation"]
                times, vals = samplers_data[s_idx]
                pos = sample_channel(times, vals, t_sec, is_quat=False)
            else:
                pos = tuple(node_dict.get("translation", [0.0, 0.0, 0.0]))

            if n_id in node_channel_map and "rotation" in node_channel_map[n_id]:
                s_idx = node_channel_map[n_id]["rotation"]
                times, vals = samplers_data[s_idx]
                rot = sample_channel(times, vals, t_sec, is_quat=True)
            else:
                rot = tuple(node_dict.get("rotation", [0.0, 0.0, 0.0, 1.0]))

            return pos, normalize_quat(rot)

        frame_count = max(1, int(math.ceil(max_duration * fps)))
        samples_data = bytearray()

        for f_idx in range(frame_count):
            t = f_idx / fps
            if t > max_duration:
                t = max_duration

            for j_node_id in joints:
                pos, q = sample_node(j_node_id, t)

                # If this bone was compounded, multiply down through pruned ancestor chain
                if j_node_id in compounding_chain:
                    for anc_id in reversed(compounding_chain[j_node_id]):
                        anc_pos, anc_rot = sample_node(anc_id, t)
                        pos, q = compound_transform(anc_pos, anc_rot, pos, q)

                q = normalize_quat(q)

                qx = max(-32767, min(32767, int(round(q[0] * 32767.0))))
                qy = max(-32767, min(32767, int(round(q[1] * 32767.0))))
                qz = max(-32767, min(32767, int(round(q[2] * 32767.0))))
                qw = max(-32767, min(32767, int(round(q[3] * 32767.0))))

                px = max(-32767, min(32767, int(round(pos[0] / pos_scale))))
                py = max(-32767, min(32767, int(round(pos[1] / pos_scale))))
                pz = max(-32767, min(32767, int(round(pos[2] / pos_scale))))

                samples_data.extend(struct.pack("<4h3hh", qx, qy, qz, qw, px, py, pz, 0))

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


# =============================================================================
# Main Entry Point
# =============================================================================

def cook_gltf(input_path: str, output_dir: str, no_engine: bool = False) -> Dict[str, list]:
    """
    Top-level cooker entry point for glTF / GLB assets.
    Parameters:
        input_path: Source glTF or GLB file path
        output_dir: Destination directory for cooked assets
        no_engine: If False, optimizes for libpspforge (.p3d, <=96 bones, fused atlas).
                   If True, agnostic mode (.p3dx, unlimited bones, multi-material .tex).
    """
    os.makedirs(output_dir, exist_ok=True)
    base_name = os.path.splitext(os.path.basename(input_path))[0]
    model = GLTFModel(input_path)

    # 1. Parse all primitives
    parsed_prims: List[ParsedPrimitive] = []
    for m_idx, mesh in enumerate(model.gltf.get("meshes", [])):
        for p_idx, prim in enumerate(mesh.get("primitives", [])):
            if "POSITION" not in prim["attributes"]:
                continue
            pos_list = model.read_accessor(prim["attributes"]["POSITION"])
            norm_list = model.read_accessor(prim["attributes"]["NORMAL"]) if "NORMAL" in prim["attributes"] else [(0.0, 1.0, 0.0)] * len(pos_list)
            uv_list = model.read_accessor(prim["attributes"]["TEXCOORD_0"]) if "TEXCOORD_0" in prim["attributes"] else [(0.0, 0.0)] * len(pos_list)
            indices = model.read_accessor(prim["indices"]) if "indices" in prim else list(range(len(pos_list)))
            mat_idx = prim.get("material")

            has_skin = ("JOINTS_0" in prim["attributes"] and "WEIGHTS_0" in prim["attributes"])
            joints_attr = model.read_accessor(prim["attributes"]["JOINTS_0"]) if has_skin else None
            weights_attr = model.read_accessor(prim["attributes"]["WEIGHTS_0"]) if has_skin else None

            parsed_prims.append(ParsedPrimitive(pos_list, norm_list, uv_list, indices, mat_idx, joints_attr, weights_attr))

    # 2. Extract skeleton joints and inverse bind matrices
    skins = model.gltf.get("skins", [])
    if skins and "joints" in skins[0]:
        joints = list(skins[0]["joints"])
        if "inverseBindMatrices" in skins[0]:
            inv_bind_matrices = list(model.read_accessor(skins[0]["inverseBindMatrices"]))
        else:
            inv_bind_matrices = []
    else:
        # Collect nodes animated as fallback
        joints = []
        for a in model.gltf.get("animations", []):
            for ch in a.get("channels", []):
                nid = ch["target"]["node"]
                if nid not in joints:
                    joints.append(nid)
        inv_bind_matrices = []

    compounding_chain: Dict[int, List[int]] = {}

    # 3. Process according to mode
    if not no_engine:
        # Mode 1: libpspforge Engine Mode
        if len(joints) > 96:
            joints, compounding_chain = reduce_skeleton_bones(model, parsed_prims, inv_bind_matrices, max_bones=96)

        texs_output, uv_transforms = cook_gltf_textures(model, output_dir, prefix=base_name)
        model_output = os.path.join(output_dir, f"{base_name}.p3d")
        cook_gltf_model(
            model=model,
            parsed_prims=parsed_prims,
            joints=joints,
            inv_bind_matrices=inv_bind_matrices,
            output_path=model_output,
            uv_transforms=uv_transforms,
            no_engine=False
        )
    else:
        # Mode 2: Agnostic Toolchain Mode (--no-engine)
        texs_output, material_names = cook_gltf_textures_agnostic(model, output_dir, prefix=base_name)
        model_output = os.path.join(output_dir, f"{base_name}.p3dx")
        cook_gltf_model(
            model=model,
            parsed_prims=parsed_prims,
            joints=joints,
            inv_bind_matrices=inv_bind_matrices,
            output_path=model_output,
            material_names=material_names,
            no_engine=True
        )

    anims_output = cook_gltf_animations(
        model=model,
        output_dir=output_dir,
        prefix=base_name,
        joints=joints,
        compounding_chain=compounding_chain,
        no_engine=no_engine
    )

    return {
        "model": [model_output],
        "animations": anims_output,
        "textures": texs_output
    }
