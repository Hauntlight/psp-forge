"""PSP-Forge 3D Geometry Cooker.

Converts Wavefront .obj files into aligned PSP-ready binary mesh files (.p3d).

Vertex Layout (32 bytes, 16-byte aligned):
    float u, v;       // 8 bytes (UV texture coordinates)
    float nx, ny, nz; // 12 bytes (surface normals)
    float x, y, z;    // 12 bytes (local coordinates)

PSP GU Vertex Format:
    GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D
    = (3 << 0) | (3 << 5) | (3 << 7) | (0 << 23) = 483 (0x01E3)
"""

import math
import os
import struct
from typing import List, Tuple

# GU Flags
GU_TEXTURE_32BITF = 3 << 0
GU_NORMAL_32BITF  = 3 << 5
GU_VERTEX_32BITF  = 3 << 7
GU_TRANSFORM_3D   = 0 << 23
VERTEX_FORMAT_FLAGS = GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D
VERTEX_STRIDE = 32  # bytes


def normalize_vector(v: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Normalizes a 3D vector. Returns (0, 1, 0) if magnitude is zero."""
    x, y, z = v
    mag = math.sqrt(x * x + y * y + z * z)
    if mag > 1e-6:
        return (x / mag, y / mag, z / mag)
    return (0.0, 1.0, 0.0)


def compute_face_normal(
    p0: Tuple[float, float, float],
    p1: Tuple[float, float, float],
    p2: Tuple[float, float, float]
) -> Tuple[float, float, float]:
    """Computes face normal via cross product: (p1 - p0) x (p2 - p0)."""
    ax, ay, az = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    bx, by, bz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
    cx = ay * bz - az * by
    cy = az * bx - ax * bz
    cz = ax * by - ay * bx
    return normalize_vector((cx, cy, cz))


def cook_mesh(input_path: str, output_path: str) -> dict:
    """Parses a Wavefront .obj file and produces a .p3d binary file."""
    positions: List[Tuple[float, float, float]] = []
    texcoords: List[Tuple[float, float]] = []
    normals: List[Tuple[float, float, float]] = []
    vertices: List[Tuple[float, float, float, float, float, float, float, float]] = []

    with open(input_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split()
            cmd = parts[0]

            if cmd == "v":
                # Position: x, y, z
                positions.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif cmd == "vt":
                # Texture: u, v (invert v for PSP/OpenGL standard where 0 is top)
                u = float(parts[1])
                v = float(parts[2]) if len(parts) > 2 else 0.0
                texcoords.append((u, 1.0 - v))
            elif cmd == "vn":
                # Normal: nx, ny, nz
                normals.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif cmd == "f":
                # Face: list of v/vt/vn
                face_tokens = parts[1:]
                # Triangulate face using triangle fan: (0, i, i+1)
                for i in range(1, len(face_tokens) - 1):
                    tri_tokens = [face_tokens[0], face_tokens[i], face_tokens[i + 1]]
                    tri_points = []
                    tri_uvs = []
                    tri_normals = []

                    for tok in tri_tokens:
                        sub = tok.split("/")
                        v_idx = int(sub[0]) - 1 if sub[0] else -1
                        vt_idx = int(sub[1]) - 1 if len(sub) > 1 and sub[1] else -1
                        vn_idx = int(sub[2]) - 1 if len(sub) > 2 and sub[2] else -1

                        pos = positions[v_idx] if 0 <= v_idx < len(positions) else (0.0, 0.0, 0.0)
                        uv = texcoords[vt_idx] if 0 <= vt_idx < len(texcoords) else (0.0, 0.0)
                        norm = normals[vn_idx] if 0 <= vn_idx < len(normals) else None

                        tri_points.append(pos)
                        tri_uvs.append(uv)
                        tri_normals.append(norm)

                    # Compute geometric normal if not provided in OBJ
                    has_missing_normal = any(n is None for n in tri_normals)
                    if has_missing_normal:
                        computed_normal = compute_face_normal(tri_points[0], tri_points[1], tri_points[2])
                        tri_normals = [computed_normal, computed_normal, computed_normal]

                    for p, uv, n in zip(tri_points, tri_uvs, tri_normals):
                        # Layout: u, v, nx, ny, nz, x, y, z
                        vertices.append((uv[0], uv[1], n[0], n[1], n[2], p[0], p[1], p[2]))

    vertex_count = len(vertices)
    if vertex_count == 0:
        raise ValueError(f"No valid geometric faces found in OBJ file: {input_path}")

    # Compute AABB and Bounding Sphere
    min_x = min(v[5] for v in vertices)
    max_x = max(v[5] for v in vertices)
    min_y = min(v[6] for v in vertices)
    max_y = max(v[6] for v in vertices)
    min_z = min(v[7] for v in vertices)
    max_z = max(v[7] for v in vertices)

    cx = (min_x + max_x) * 0.5
    cy = (min_y + max_y) * 0.5
    cz = (min_z + max_z) * 0.5

    max_dist_sq = 0.0
    for v in vertices:
        dx = v[5] - cx
        dy = v[6] - cy
        dz = v[7] - cz
        d2 = dx * dx + dy * dy + dz * dz
        if d2 > max_dist_sq:
            max_dist_sq = d2
    radius = math.sqrt(max_dist_sq)

    # Pack 64-byte Header:
    # Magic (4s), Version (H), VtxFormat (I), VtxStride (H), VtxCount (I),
    # AABB Min (3f), AABB Max (3f), Center (3f), Radius (f), Reserved (4s)
    header = struct.pack(
        "<4sHIHI3f3f3ff4s",
        b"PM3D",
        1,
        VERTEX_FORMAT_FLAGS,
        VERTEX_STRIDE,
        vertex_count,
        min_x, min_y, min_z,
        max_x, max_y, max_z,
        cx, cy, cz,
        radius,
        b"\x00" * 4
    )

    # Pack Vertices
    vtx_data = bytearray()
    for v in vertices:
        # u, v, nx, ny, nz, x, y, z
        vtx_data.extend(struct.pack("<8f", *v))

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(header)
        f.write(vtx_data)

    return {
        "output_path": output_path,
        "vertex_count": vertex_count,
        "triangle_count": vertex_count // 3,
        "aabb_min": (min_x, min_y, min_z),
        "aabb_max": (max_x, max_y, max_z),
        "center": (cx, cy, cz),
        "radius": radius,
        "file_size": len(header) + len(vtx_data)
    }
