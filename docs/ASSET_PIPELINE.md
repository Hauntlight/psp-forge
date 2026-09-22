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

## 4. Skeletal 3D Models & Animations: glTF / GLB to `.p3d` & `.panm`

The PSP Graphics Engine supports hardware vertex skinning for up to **8 bone matrices** (`GU_WEIGHTS(n)`, `sceGuBoneMatrix(0..7)`). For modern 3D models (which typically have 20–80 bones), PSP-Forge introduces an automated **Mesh Chunking Pipeline**, **Material Atlas Fusion**, and a forward kinematics clip format (`.panm`).

| Source Format | Cooked PSP Format | Cooker Module | Optimizations Performed |
|---|---|---|---|
| `.gltf`, `.glb` | **`.p3d` (P3D2 Multi-Chunk)** | `cli/cookers/gltf.py` | Triangle clustering into $\le 8$ bone chunks, local bone index remapping, vertex weight normalization |
| `.gltf`, `.glb` (Animations) | **`.panm`** (Skeletal Animation) | `cli/cookers/gltf.py` | Keyframe baking at 30 FPS, quaternion SLERP, 16-byte fixed samples |
| Embedded Textures | **`.tex`** (PSP Texture) | `cli/cookers/gltf.py` | Material Fusion (atlas packing + UV remapping), auto chroma-keying of neutral background mats, POT padding |

### Two Architectural Modes Supported:
1. **Mode A: Hierarchical Rigid Meshes (Tekken 1–3 Style)**:
   - For articulated models without continuous skinning (mechas, segmented armor, robots).
   - Each limb or section is an independent mesh attached to a bone node (`node_index >= 0`).
   - Evaluated using `pspgum` matrix stack operations (`sceGumPushMatrix()` / `sceGumPopMatrix()`).
   - Completely bypasses the 8-bone hardware limit since each draw call uses only the active node matrix.
2. **Mode B: Continuous Skinning with Mesh Chunking**:
   - For organic, smooth-skinned characters (up to **96 bones** total in the skeleton hierarchy).
   - The cooker partitions triangles so that **each sub-mesh chunk references at most 8 unique bones**.
   - Generates local bone palettes (`bone_palette[8]`) and remaps vertex bone indices (`0..7`).
   - At runtime, `forge_model3d_draw()` binds the active chunk's skinning matrices to hardware bone registers (`sceGuBoneMatrix(0..7)`) and dispatches native hardware-blended draw calls (`GU_WEIGHTS(1..8) | GU_WEIGHT_32BITF | GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D`).

### Automatic Material Fusion & Chroma Keying:
Real-world glTF characters frequently come with multiple separate materials (e.g. skin, clothes, eyes, hair). On the PSP:
- Switching textures per draw call (`sceGuTexImage`) causes heavy pipeline stalls and Display List bloat.
- The `gltf.py` cooker automatically packs all textures referenced by the model into a single **$512 \times 512$ master texture atlas** (`<model>.tex`), automatically recalculating and remapping the $(U, V)$ coordinates for every vertex.
- **Auto-Chroma Keying**: When eye or eyebrow textures are painted over a solid neutral matte background (e.g. RGB 128, 128, 128), the cooker automatically detects and converts the background to transparent alpha (`A = 0`), preventing solid opaque boxes from hiding the character's face.

### Loading and Animating in C:
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
- **Skeleton Limit**: Maximum 96 bones. *Rationale*: `ForgeAnimator` maintains statically sized matrix arrays (`world_matrices[96]`, `skin_matrices[96]`), consuming only $12.5\text{ KiB}$ to keep RAM footprint negligible on the 24 MB PSP.
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
