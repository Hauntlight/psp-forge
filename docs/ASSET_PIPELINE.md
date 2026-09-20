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
| `.wav`, `.mp3`, `.ogg`, `.flac` | **`.snd`** (PSP Sound) | `cli/cookers/audio.py` | Linear resampling to 44.1kHz, PCM S16-LE, alignment to 64-sample multiples |

---

## 1. Textures: From PNG / JPG to `.tex`

### What is Texture Swizzling?
In standard linear images, pixels are stored row by row from left to right. When the PSP GPU samples textures on angled or 3D surfaces, it must constantly jump between distant memory rows, causing severe cache misses.  
**Swizzling** reorganizes the byte sequence into rectangular blocks of **$16 \times 8$ bytes**: pixels that are close together in 2D space become contiguous in physical memory, dramatically improving texture cache hit rate.

### How Conversion Works:
1. **Power-of-Two (POT) Dimensions**: The PSP requires texture dimensions to be powers of two ($16, 32, 64, 128, 256, 512$). The cooker calculates the next power of two and applies transparent zero-padding automatically.
2. **Supported Pixel Storage Modes (PSM)**:
   - `8888` / `rgba8888` (`GU_PSM_8888 = 3`): 32-bit RGBA (maximum fidelity).
   - `5551` / `rgba5551` (`GU_PSM_5551 = 1`): 16-bit RGBA (1-bit alpha on/off).
   - `4444` / `rgba4444` (`GU_PSM_4444 = 2`): 16-bit RGBA (16 levels of alpha).
   - `5650` / `rgb5650`  (`GU_PSM_5650 = 0`): 16-bit RGB with no alpha (ideal for skyboxes and backgrounds).

### Command-Line Usage:
```bash
# Cook all project assets:
psp-forge cook

# Or cook a single image:
python3 -c "
from cli.cookers.texture import cook_texture
cook_texture('assets/hero.png', 'build/assets/hero.tex', format_type='8888', swizzle=True)
"
```

### Loading and Rendering in C:
```c
ForgeTexture* tex = forge_texture_load("assets/hero.tex");

// Immediate 2D quad drawing (pixel coordinates):
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
2. Computes smooth face normals via cross products if the model lacks normals.
3. Flips the vertical UV axis ($1.0 - V$) to align with the GE standard.
4. Triangulates faces (quads and N-gons) using triangle fans.
5. Computes the **AABB Bounding Box** (`aabb_min`, `aabb_max`, bounding center, and culling radius).
6. Writes packed vertices with a 32-byte stride, strictly aligned to 16 bytes for hardware DMA.

### Exporting from Blender / Maya:
When exporting `.obj` from Blender:
- Select **Triangulate Faces** (or let the cooker triangulate automatically).
- Check **Write Normals** and **Include UVs**.
- Coordinate axes: Forward `-Z`, Up `+Y`.

### Command-Line Usage:
```bash
python3 -c "
from cli.cookers.mesh import cook_mesh
cook_mesh('assets/track.obj', 'build/assets/track.p3d')
"
```

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
The PSP audio hardware operates on **44.1 kHz signed 16-bit PCM** with buffers that must be multiples of **64 samples**.

The `audio.py` cooker:
- Supports uncompressed `.wav` files natively.
- Supports `.mp3`, `.ogg`, `.flac`, `.m4a` when `ffmpeg` is available on the system.
- Applies linear interpolation resampling if the source rate differs from 44100 Hz.
- Normalizes and aligns final sample length to the nearest multiple of 64 samples with zero padding (silence).

### Loading and Playing in C:
```c
ForgeSound* sound = forge_sound_load("assets/jump.snd");

// Immediate dispatch to dedicated audio thread:
forge_sound_play(sound, 0); // second parameter: 0 = one-shot, 1 = loop

// Cleanup:
forge_sound_free(sound);
```

---

## 4. Executable Packaging & Hardware Compatibility (`EBOOT.PBP`)

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
