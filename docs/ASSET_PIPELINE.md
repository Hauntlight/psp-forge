# Official Asset Pipeline Guide (`The Cooker`) ⚔️

The GPU and CPU of the Sony PSP (MIPS R4000 Allegrex) have distinct architectural constraints:
- **On-chip eDRAM of only 2 MB** with ultra-high bandwidth.
- **Costly cache misses**: linear texture sampling from standard memory degrades fillrate.
- **Mandatory 16-byte alignment** for vertices dispatched to the Graphics Engine (GE) via DMA.
- **Native hardware audio requirements** of 44100 Hz signed 16-bit little-endian PCM in chunks of 64 samples.

The **PSP-Forge** suite includes the asset compiler (`psp-forge cook`) to automatically convert standard art, modeling, and audio files (PNG, JPG, OBJ, WAV, MP3) into binary formats ready for direct hardware consumption:

| Source Format | Cooked PSP Format | Cooker Module | Hardware Optimizations Performed |
|---|---|---|---|
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga` | **`.tex`** (PSP Texture) | `cli/cookers/texture.py` | $16 \times 8$ byte block swizzling, POT Padding ($2^n \le 512$), hardware PSM alignment |
| Wavefront `.obj` | **`.p3d`** (PSP 3D Mesh) | `cli/cookers/mesh.py` | Normals, flipped V UVs, triangulation, AABB Bounding Box, 16-byte DMA alignment |
| `.wav`, `.mp3`, `.ogg`, `.flac`, `.m4a` | **`.snd`** (PSP Sound) | `cli/cookers/audio.py` | Linear resampling to 44.1kHz, PCM S16-LE, alignment to 64-sample multiples |

---

## 1. Textures: From PNG / JPG to `.tex`

### What is Texture Swizzling?
In standard linear images, pixels are stored row by row from left to right. When the PSP GPU samples textures on angled or 3D surfaces, it must constantly jump between distant memory rows, causing severe cache misses.  
**Swizzling** reorganizes the byte sequence into rectangular blocks of **$16 \times 8$ bytes**: pixels that are close together in 2D space become contiguous in physical memory, dramatically improving texture cache hit rate.

### How Conversion Works:
1. **Power-of-Two (POT) Dimensions**: The PSP requires texture dimensions to be powers of two ($8, 16, 32, 64, 128, 256, 512$; note that `clut4` requires a minimum width of 32 for block alignment). The cooker calculates the next power of two and applies transparent zero-padding automatically.
2. **Supported Pixel Storage Modes (PSM)**:
   - `8888` / `rgba8888` (`GU_PSM_8888 = 3`): 32-bit RGBA (maximum fidelity).
   - `5551` / `rgba5551` (`GU_PSM_5551 = 1`): 16-bit RGBA (1-bit alpha on/off).
   - `4444` / `rgba4444` (`GU_PSM_4444 = 2`): 16-bit RGBA (16 levels of alpha).
   - `5650` / `rgb5650`  (`GU_PSM_5650 = 0`): 16-bit RGB with no alpha (ideal for skyboxes and backgrounds).
   - `clut8` (`GU_PSM_T8 = 5`): 8-bit indexed palette (256 RGBA colors) with full alpha preservation.
   - `clut4` (`GU_PSM_T4 = 4`): 4-bit indexed palette (16 RGBA colors) with full alpha preservation.

### Command-Line Usage:
```bash
# Cook all project assets:
psp-forge cook

# Or cook individual assets via the CLI
psp-forge cook
```

### Loading and Rendering in C:
```c
ForgeTexture* tex = forge_texture_load("assets/hero.tex");

// Immediate 2D quad drawing (texel pixel coordinates):
forge_draw_sprite(tex, screen_x, screen_y, width, height, tex_u, tex_v, tex_w, tex_h);

// Cleanup on shutdown:
forge_texture_free(tex);
```

---

## 2. 3D Models: From Wavefront `.obj` to `.p3d`

### Vertex Structure Required by the PSP:
The PSP Graphics Engine expects vertex components in strict sequential order:
Texture (U, V) -> Color -> Normals (NX, NY, NZ) -> Position (X, Y, Z)

The `mesh.py` cooker:
1. Reads vertex positions `v`, texture coordinates `vt`, and surface normals `vn`.
2. Computes face normals via triangle cross products if the model lacks normals (flat shading).
3. Flips the vertical UV axis ($1.0 - V$) to align with the GE standard.
4. Triangulates faces (quads and N-gons) using triangle fans.
5. Computes the **AABB Bounding Box** (`aabb_min`, `aabb_max`, bounding center, and culling radius).
6. Writes packed vertices with a 32-byte stride (aligned to 16 bytes for hardware DMA when loaded into RAM via `forge_mesh_load`).

### Exporting from Blender / Maya:
When exporting `.obj` from Blender:
- Select **Triangulate Faces** (or let the cooker triangulate automatically).
- Check **Write Normals** and **Include UVs**.
- Coordinate axes: Forward `-Z`, Up `+Y`.

### Loading and Rendering in C:
```c
ForgeMesh* mesh = forge_mesh_load("assets/track.p3d");
ForgeTexture* tex = forge_texture_load("assets/track.tex");

// Render in 3D space (position x, y, z; rotation in radians; scale):
forge_draw_mesh(mesh, tex, 0.0f, 0.0f, pos_z, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

// Cleanup:
forge_mesh_free(mesh);
```

---

## 3. Audio: From WAV / MP3 / OGG to `.snd`

### Hardware Audio Constraints:
The PSP audio hardware operates on **44.1 kHz stereo signed 16-bit PCM** with buffers that must be multiples of **64 samples**.

The `audio.py` cooker:
- Supports uncompressed `.wav` files natively.
- Supports `.mp3`, `.ogg`, `.flac`, `.m4a` when `ffmpeg` is available on the system.
- Converts audio to 2-channel stereo (duplicating mono channels) for hardware compatibility.
- Applies linear interpolation resampling if the source rate differs from 44100 Hz.
- Pads output PCM to the nearest multiple of 64 samples (128 bytes).

### Loading and Playing in C:
```c
ForgeSound* sound = forge_sound_load("assets/jump.snd");

// Immediate dispatch to dedicated audio thread:
forge_sound_play(sound, 0); // second parameter: 0 = one-shot, 1 = loop

// Cleanup:
forge_sound_free(sound);
```

---

## 4. Skeletal 3D Models & Animations: glTF / GLB to `.p3d` / `.p3dx` & `.panm`

The PSP Graphics Engine supports hardware vertex skinning for up to **8 bone matrices** (`GU_WEIGHTS(n)`, `sceGuBoneMatrix(0..7)`). For modern 3D models (which typically have 20–120+ bones), PSP-Forge provides a **Dual-Mode Cooker** (`cli/cookers/gltf.py`):

```
                              [File 3D glTF / GLB]
                                       │
              ┌────────────────────────┴────────────────────────┐
              ▼                                                 ▼
  [Default: psp-forge cook]                        [Flag: --no-engine]
  Modalità "libpspforge Engine"                    Modalità "Toolchain Agnostica"
  ─────────────────────────────                    ──────────────────────────────
  • Formato: .p3d (Magic: P3D2)                    • Formato: .p3dx (Magic: P3DX)
  • Max 96 ossa (Bone Reduction se > 96)           • Ossa illimitate (Nessuna riduzione)
  • Material Atlas Fusion (1 sola texture 512x512) • Multi-Materiale (texture separate .tex)
  • UV rimappate su griglia atlante                • UV originali preservate intatte
  • Auto-Chroma Keying per cutouts                 • Nessun Chroma Keying forzato
  • Chunks divisi solo per <= 8 ossa               • Chunks divisi per Material ID + <= 8 ossa
```

| Mode | Command Flag | Output Model | Textures | Max Bones | Primary Target |
|---|---|---|---|---|---|
| **Engine Mode** | *(default)* | `.p3d` (`P3D2`) | Single `$512 \times 512$` Atlas (`.tex`) | **96** (Reduced) | `libpspforge` Micro-Engine |
| **Agnostic Mode** | `--no-engine` | `.p3dx` (`P3DX`) | Separate `.tex` per material | **Unlimited** | Raylib-PSP, SDL, OSLib, Custom C/C++ Engines |

---

### Pipeline 1: Default (`psp-forge cook`) — Optimized for `libpspforge`

The default mode packages assets precisely as expected by the `libpspforge` runtime, eliminating runtime GPU state changes and fitting comfortably within the PSP's 24 MB RAM budget.

#### A. Hardware Constraints & Automatic Optimizations
1. **Bone Limit & Reduction:** Maximum 96 bones (`FORGE_MAX_BONES = 96`). If a model exceeds 96 bones, the cooker automatically executes **Bone Reduction & Compounding** (see below).
2. **Material Atlas Fusion:** All textures are packed into a single $512 \times 512$ master texture atlas (`<model>.tex`), and vertex UV coordinates are automatically remapped to the atlas sub-rectangles. If no textures are present, a clean $16 \times 16$ white default atlas is emitted.
3. **Auto-Chroma Keying:** Neutral matte background pixels on cutout textures (e.g. solid grey behind eyelashes/hair) are automatically keyed out to full transparency (`alpha = 0`).
4. **Hardware Mesh Chunking:** Polygons are partitioned into sub-mesh chunks referencing at most 8 unique bones, with local bone palettes (`bone_palette[8]`).

#### B. Bone Reduction & Compounding Engine
When a skeleton exceeds 96 bones, the cooker deterministically prunes low-impact bones down to 96 while preserving geometric fidelity:

* **Pruning Priority (Order of elimination):**
  1. *Zero-weight terminal bones:* IK helpers, nub bones, leaf nodes with zero vertex weights.
  2. *Facial bone details:* Eyelids, jaw, lips, tongue, brow collapsed into the `Head` parent bone.
  3. *Distal & intermediate finger phalanges:* Intermediate and fingertip bones collapsed into proximal finger joints or the `Hand` palm bone.
  4. *Twist & auxiliary bones:* Segment twist bones (e.g. `arm_twist`) collapsed into the segment parent bone (`upper_arm`).
  5. *Leaf bones with smallest weight sum:* Smallest total weight contributors merged into their respective parents.

* **Weight Collapsing (Vertex Influence Fusion):**
  For each bone $B$ pruned into parent $P$, any vertex referencing $B$ with weight $w_B$:
  - If $P$ is already an influence of the vertex with weight $w_P$, update $w_P \leftarrow w_P + w_B$.
  - If $P$ is not present, replace the bone reference $B \rightarrow P$ with weight $w_B$.
  - Renormalize all vertex weights: $w_i = \frac{w_i}{\sum_k w_k}$.

* **Transform Compounding (Animation Curves):**
  If pruned bone $B$ has remaining children $C$, the relative motion of $B$ is compounded into $C$ for every sampled 30 FPS animation frame in `.panm`:
  $$M_{C \to P}(t) = M_{B \to P}(t) \times M_{C \to B}(t)$$
  The compounded rotation quaternion is normalized ($\|q\| = 1.0$) and relative translation is updated, ensuring zero visual disruption.

#### C. Binary Layout: `.p3d` (P3D2)
```c
typedef struct __attribute__((packed)) {
    char     magic[4];       /* "P3D2" */
    uint16_t version;        /* 2 */
    uint16_t bone_count;     /* <= 96 */
    uint16_t chunk_count;    /* Number of sub-mesh chunks */
    uint8_t  reserved[8];
} P3d2Header;

typedef struct __attribute__((packed)) {
    int16_t  node_index;      /* -1 = skinned, >= 0 = rigid bone */
    uint8_t  num_local_bones; /* 1..8 */
    uint8_t  bone_palette[8]; /* Maps local index (0..7) to global bone */
    uint32_t vertex_format;   /* GU_WEIGHTS(n) | GU_TEXTURE_32BITF | ... */
    uint16_t vertex_stride;
    uint32_t vertex_count;
    float    aabb_min[3];
    float    aabb_max[3];
    float    center[3];
    float    radius;
    uint8_t  reserved[4];
} P3d2ChunkHeader;
```

---

### Pipeline 2: Agnostic Mode (`psp-forge cook --no-engine`)

For developers building homebrew with external frameworks (Raylib-PSP, SDL, OSLib, or custom engines), the `--no-engine` flag transforms `psp-forge` into an open Swiss Army Knife asset compiler.

```bash
# Cook glTF model in Agnostic Mode:
psp-forge cook --no-engine

# Or build the project using agnostic cooking:
psp-forge build --no-engine
```

#### A. Key Features
1. **Unlimited Bones (No Bone Reduction):** Skeletons are exported in full (104, 150, 200+ bones).
2. **Multi-Material Texture Export:** Each material is exported as an independent `.tex` file (`<model>_<material>.tex`), automatically downsampled to $\le 512 \times 512$ if needed for PSP hardware limits, with authentic alpha channels (no auto-chroma keying).
3. **Original UV Coordinates Preserved:** Vertex UV coordinates are never modified or remapped to an atlas grid.
4. **Chunk Partitioning by Material ID:** Chunks are grouped primarily by `material_id` and secondarily partitioned into $\le 8$ local bones for developers who wish to utilize hardware skinning.

#### B. Binary Layout: `.p3dx` (P3DX v1)
```c
typedef struct __attribute__((packed)) {
    char     magic[4];       /* "P3DX" */
    uint16_t version;        /* 1 */
    uint16_t bone_count;     /* Any number (no bone reduction) */
    uint16_t chunk_count;
    uint16_t material_count; /* Number of associated materials/textures */
    uint8_t  reserved[6];
} P3dxHeader;

/* Followed immediately by:
   char material_names[material_count][32]; // Null-padded string table
*/

typedef struct __attribute__((packed)) {
    int16_t  node_index;      /* -1 = skinned, >= 0 = rigid bone */
    uint8_t  num_local_bones; /* 0..8 */
    uint8_t  bone_palette[8];
    uint16_t material_id;     /* Index into Material Name Table */
    uint32_t vertex_format;
    uint16_t vertex_stride;
    uint32_t vertex_count;
    float    aabb_min[3];
    float    aabb_max[3];
    float    center[3];
    float    radius;
    uint8_t  reserved[2];
} P3dxChunkHeader;
```

---

### Loading and Animating in C (`libpspforge`):
```c
// 1. Load multi-chunk skinned model and shared atlas texture
ForgeModel3D* model = forge_model3d_load("assets/character.p3d");
ForgeTexture* tex   = forge_texture_load("assets/character.tex");
ForgeAnimClip* clip = forge_anim3d_load("assets/character_walk.panm");

// 2. Initialize animator and play animation
ForgeAnimator anim;
forge_anim3d_init(&anim);
forge_anim3d_play(&anim, clip, true);

// 3. Enable hardware alpha test for eye/hair cutouts
forge_set_alpha_test(true, 128);

// 4. Update & render in frame loop
forge_anim3d_update(&anim, model, forge_get_delta_time());

// Setup model matrix and draw all chunks
sceGumPushMatrix();
{
    ScePspFVector3 pos = { char_x, char_y, char_z };
    ScePspFVector3 rot = { 0.0f, facing_angle, 0.0f };
    sceGumTranslate(&pos);
    sceGumRotateXYZ(&rot);

    forge_model3d_draw(model, &anim, tex);
}
sceGumPopMatrix();

// 5. Cleanup
forge_anim3d_free(clip);
forge_model3d_free(model);
forge_texture_free(tex);
```

### Skeletal Model Asset Constraints & Rationale:
- **Skeleton Limit (Engine Mode)**: Maximum 96 bones. *Rationale*: `ForgeAnimator` maintains statically sized matrix arrays (`world_matrices[96]`, `skin_matrices[96]`), consuming only $12.5\text{ KiB}$ to keep RAM footprint negligible on the 24 MB PSP. If greater fidelity or more bones are required for custom engines, use `--no-engine` (`.p3dx`).
- **Max Bones Per Vertex**: At most 4 non-zero weights per vertex in glTF. *Rationale*: Standard glTF attribute `JOINTS_0` / `WEIGHTS_0` supports 4 influences, which the cooker normalizes before assigning to the chunk's 8-bone palette.
- **Max Unique Bones Per Chunk**: $\le 8$ bones. *Rationale*: The PSP Graphics Engine has exactly 8 hardware bone registers (`GU_WEIGHTS(1..8)`). Any mesh part with more than 8 bones is automatically split into multiple sub-mesh chunks by the cooker.


---

## 5. Executable Packaging & Hardware Compatibility (`EBOOT.PBP`)

To ensure the compiled game runs seamlessly **both in the PPSSPP emulator and on real PSP consoles**, make sure `CMakeLists.txt` includes the `BUILD_PRX` directive in `create_pbp_file()`:

```cmake
create_pbp_file(
    TARGET my_game
    TITLE "My PSP Game"
    BUILD_PRX
    ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png"
    BACKGROUND_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/pic1.png"
)
```

In addition, at the beginning of `main()` in C:
```c
int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }
    forge_init(0);
    ...
```
This enables the engine to resolve whether the application is running from `ms0:/PSP/GAME/<folder>` or a local development environment, resolving asset paths transparently and reliably.
