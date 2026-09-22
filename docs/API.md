# PSP-Forge C99 Runtime API Reference (`libpspforge`)

The primary header file to include in any project is `#include <psp_forge.h>`.

---

## Constants & Defines

```c
#define FORGE_VERSION_MAJOR 1
#define FORGE_VERSION_MINOR 1
#define FORGE_VERSION_PATCH 0

#define FORGE_SCREEN_WIDTH  480
#define FORGE_SCREEN_HEIGHT 272
#define FORGE_BUF_WIDTH     512

/* Flags for forge_init */
#define FORGE_INIT_DEFAULT          0x00
#define FORGE_INIT_ERROR_HANDLER    0x01

#define FORGE_MAX_VIRTUAL_LIGHTS    16
```

---

## 1. Core & Frame Lifecycle

### `void forge_init(uint32_t flags)`
Initializes the Graphics Utility (GU), system callbacks for the HOME button, game controller polling, and high-resolution hardware timers. Disables Allegrex FPU exceptions via `pspFpuSetEnable(0)`.
* **Parameters:** `flags` bitmask (`FORGE_INIT_DEFAULT` or `FORGE_INIT_ERROR_HANDLER`). Note: `FORGE_INIT_ERROR_HANDLER` is handled safely in user mode. Audio subsystem threads are initialized lazily upon first playback.

### `void forge_shutdown(void)`
Shuts down the display pipeline, cleanly terminates the background audio thread and hardware channels (`forge_audio_shutdown`), resets the active scene (`forge_scene_reset`), and resets VRAM scratchpad allocators.

### `int forge_is_running(void)`
Returns `1` if the application is running, or `0` if the user requested exit via the PSP HOME button.

### `void forge_set_base_path(const char* path_or_argv0)`
Sets the application base directory from `argv[0]` or an explicit path, ensuring relative asset paths resolve correctly whether running from Memory Stick (`ms0:/PSP/GAME/...`) or emulator.

### `const char* forge_get_base_path(void)`
Returns the configured base directory path.

### `SceUID forge_io_open(const char* path)`
Opens a file relative to the base directory path using `sceIoOpen(..., PSP_O_RDONLY, 0777)`.

### `void forge_begin_frame(void)`
Opens a new Display List to begin recording drawing commands for the current frame.

### `void forge_clear(uint32_t color_rgba8888)`
Clears the color buffer and depth buffer (Z-buffer).
* **Example:** `forge_clear(0xFF2E1C12);`

### `void forge_end_frame(void)`
Finalizes the Display List, waits for vertical blank synchronization (`VBlank`), executes buffer swapping, and computes `delta_time` and framerate statistics.

### `float forge_get_delta_time(void)`
Returns elapsed time (in seconds) since the previous frame (useful for framerate-independent physics and movement).

### `float forge_get_fps(void)`
Returns the current average frames per second.

---

## 2. Static VRAM Allocator (2 MB eDRAM)

The 2 MB on-chip VRAM is partitioned deterministically without buffer overlap (stride pitch: 512, height: 272):
* `0x00000000` - `0x00088000` (544 KiB): Draw Buffer Base (RGBA8888)
* `0x00088000` - `0x00110000` (544 KiB): Display Buffer Base (RGBA8888)
* `0x00110000` - `0x00154000` (272 KiB): Depth Buffer (16-bit Z)
* `0x00154000` - `0x00200000` (688 KiB): Ultra-fast Texture Scratchpad

### `void* forge_vram_get_draw_buffer(void)`
Returns the fixed relative VRAM offset to the draw buffer base (`0x00000000`).

### `void* forge_vram_get_disp_buffer(void)`
Returns the fixed relative VRAM offset to the display buffer base (`0x00088000`).

### `void* forge_vram_get_depth_buffer(void)`
Returns the relative VRAM pointer to the active 272 KiB 16-bit depth buffer (`0x00110000`).

### `void* forge_vram_get_scratchpad(void)`
Returns the current allocation pointer inside the 688 KiB texture scratchpad.

### `void* forge_vram_alloc(uint32_t size)`
Allocates 64-byte aligned linear memory in the VRAM scratchpad. Returns a relative GPU offset, or `NULL` if VRAM scratchpad space is exhausted.

### `void forge_vram_reset(void)`
Resets the scratchpad allocation pointer (enabling VRAM memory recycling between scenes or levels).

### `void* forge_vram_to_uncached_cpu(void* vram_rel)`
Translates a relative VRAM offset into an uncached absolute MIPS CPU address (`0x44000000 + offset`) for direct CPU write access without L1 cache pollution.

---

## 3. Input Subsystem

### `void forge_input_poll(ForgeInput* input)`
Polls the PSP controller non-blockingly (`sceCtrlPeekBufferPositive`) and computes instantaneous discrete state:
```c
typedef struct {
    uint32_t held;      // Buttons currently held down
    uint32_t pressed;   // Buttons pressed on this frame
    uint32_t released;  // Buttons released on this frame
    float    analog_x;  // Analog stick horizontal axis [-1.0 .. 1.0] (deadzone filtered)
    float    analog_y;  // Analog stick vertical axis   [-1.0 .. 1.0] (deadzone filtered)
} ForgeInput;
```

### Quick Helper Functions:
* `bool forge_input_is_pressed(const ForgeInput* in, uint32_t btn);`
* `bool forge_input_is_held(const ForgeInput* in, uint32_t btn);`
* `bool forge_input_is_released(const ForgeInput* in, uint32_t btn);`

---

## 4. 2D & Texture Pipeline

### `ForgeTexture` Struct Definition
```c
typedef struct {
    void*    data;
    void*    palette;
    uint16_t width;
    uint16_t height;
    uint16_t pwr2_w;
    uint16_t pwr2_h;
    uint8_t  format;         /* GU_PSM_8888, GU_PSM_5551, GU_PSM_T8, etc. */
    uint8_t  is_swizzled;
    uint8_t  has_palette;
    uint16_t palette_count;
    bool     in_vram;
} ForgeTexture;
```

### `ForgeTexture* forge_texture_load(const char* path)`
Loads a binary `.tex` texture (cooked by `psp-forge cook`) into 16-byte aligned main RAM with D-Cache writeback.

### `ForgeTexture* forge_texture_load_vram(const char* path)`
Loads a `.tex` texture directly into the fast VRAM scratchpad (maximum fillrate performance). Automatically falls back to main RAM if scratchpad space is insufficient.

### `void forge_texture_free(ForgeTexture* tex)`
Releases memory allocated by the texture.

### `void forge_draw_sprite(const ForgeTexture* tex, float sx, float sy, float sw, float sh, float tx, float ty, float tw, float th)`
Renders a 2D textured quad:
* `sx, sy`: Screen destination coordinates (in pixels, $0$ to $480 \times 272$).
* `sw, sh`: Screen destination dimensions (width and height in pixels).
* `tx, ty`: Source texture coordinates in texel pixels (e.g. $0.0\text{f}$ to `width`).
* `tw, th`: Source crop dimensions in texel pixels (e.g. $32.0\text{f}$, not normalized UV).

---

## 5. 3D Pipeline, Meshes & Lighting

### `void forge_set_alpha_test(bool enable, uint8_t ref_value)`
Enables or disables the hardware Graphics Engine alpha test (`sceGuAlphaFunc(GU_GREATER, ref_value, 0xFF)` / `sceGuEnable(GU_ALPHA_TEST)`).
* **Parameters:** `enable` (`true` or `false`), `ref_value` (threshold, e.g. `128` for 50% opacity cutouts).
* **Hardware & Architectural Justification**:
  * Unlike software alpha blending (`GU_BLEND`), which requires reading the destination pixel from eDRAM, blending in the rasterizer, and writing back (incurring a heavy read-modify-write bandwidth penalty), **alpha testing** discards transparent fragments *before* they are written to the depth buffer (Z-buffer).
  * **Zero CPU sorting needed**: Because discarded fragments never update the Z-buffer, cutouts like foliage, eyes, eyelashes, and decals do not require sorting polygons back-to-front on the Allegrex CPU.
  * Essential for skeletal models with transparent textures (eyebrows, hair strands) to prevent transparent quads from occluding opaque geometry behind them.


### 3D Struct Definitions
```c
typedef struct __attribute__((aligned(16))) {
    float u, v;        /* Texture UV */
    float nx, ny, nz;  /* Normal */
    float x, y, z;     /* Position */
} ForgeVertex3D;

typedef struct {
    void*    vertices;
    uint32_t count;
    uint32_t vertex_format;
    uint16_t vertex_stride;
    float    aabb_min[3];
    float    aabb_max[3];
    float    center[3];
    float    radius;
} ForgeMesh;

typedef struct {
    float    pos[3];
    uint32_t color;
    float    intensity;
    bool     active;
} ForgeLight;
```

### `ForgeMesh* forge_mesh_load(const char* path)`
Loads a precompiled 3D binary `.p3d` mesh. Contains 16-byte aligned vertices, normals, texture coordinates, and AABB bounding box values.

### `void forge_mesh_free(ForgeMesh* mesh)`
Releases mesh memory.

### `void forge_set_camera(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z, float fov_degrees)`
Sets up view and perspective projection matrices with a $16:9$ aspect ratio.

### `void forge_set_light(uint8_t id, float x, float y, float z, uint32_t color_rgba, float intensity)`
Registers a virtual scene light (up to 16 simultaneous lights supported, IDs 0 to 15).

### `void forge_clear_lights(void)`
Clears all registered virtual lights, disables `GU_LIGHTING`, and resets hardware lights `GU_LIGHT0..3`.

### `void forge_cull_and_apply_lights(float obj_x, float obj_y, float obj_z)`
Finds the 4 nearest active virtual lights relative to the specified world coordinates `(obj_x, obj_y, obj_z)`, binds them to hardware light registers `GU_LIGHT0..3`, and enables `GU_LIGHTING`. Call this prior to rendering meshes that require dynamic illumination.

### `void forge_draw_mesh(const ForgeMesh* mesh, const ForgeTexture* tex, float x, float y, float z, float rx_rad, float ry_rad, float rz_rad, float sx, float sy, float sz)`
Applies model transformations (translation, rotation in **radians**, scale), and renders mesh primitives. Note: To apply lighting, invoke `forge_cull_and_apply_lights(x, y, z)` prior to this call; otherwise the mesh is rendered with existing lighting or unlit mode (`GU_TFX_REPLACE`).

### `void forge_draw_mesh_current(const ForgeMesh* mesh, const ForgeTexture* tex)`
Renders a mesh using the current transformation matrix on the active `GU_MODEL` Gum stack without modifying or resetting it. Ideal for custom hierarchical matrix operations. Automatically handles lighting modulation when scene lights are enabled.

### `void forge_draw_mesh_node(const ForgeMesh* mesh, const ForgeTexture* tex, float x, float y, float z, float rx_rad, float ry_rad, float rz_rad, float sx, float sy, float sz)`
Convenience helper for hierarchical articulated rigs: pushes a matrix onto the `pspgum` stack (`sceGumPushMatrix()`), applies relative translation, rotation (in **radians**), and scale, draws the mesh via `forge_draw_mesh_current`, and pops the matrix (`sceGumPopMatrix()`).

---

## 6. Multithreaded Audio Subsystem

### `ForgeSound` Struct Definition
```c
typedef struct {
    int16_t* pcm_data;
    uint32_t sample_count; /* Per channel */
    uint32_t sample_rate;
    uint8_t  channels;
} ForgeSound;
```

### `ForgeSound* forge_sound_load(const char* path)`
Loads a `.snd` audio file transcoded to signed 16-bit 44100 Hz PCM into main RAM with D-Cache writeback.

### `void forge_sound_free(ForgeSound* snd)`
Releases audio buffer memory.

### `void forge_sound_play(const ForgeSound* snd, uint8_t loop)`
Dispatches audio playback to the dedicated high-priority thread (`0x12`), which transfers 512-sample stereo PCM chunks to the hardware audio channel via `sceAudioOutputBlocking`. When `loop` is `1`, the audio loops continuously (ideal for background music). Spawns the audio thread lazily if not already running.

### `void forge_sound_stop(void)`
Stops active audio playback.

### `bool forge_sound_is_playing(void)`
Returns `true` if audio is currently playing, or `false` otherwise.

### `void forge_audio_shutdown(void)`
Stops audio playback, signals the background audio thread to terminate, waits for thread exit (`sceKernelWaitThreadEnd`), deletes thread resources, and releases the hardware audio channel (`sceAudioChRelease`). Automatically invoked by `forge_shutdown()`.

---

## 7. 2D & 3D Collisions

### Supported Collision Shapes:
```c
typedef struct { float x, y, w, h; } ForgeRect;
typedef struct { float x, y, radius; } ForgeCircle;
typedef struct { ScePspFVector3 min; ScePspFVector3 max; } ForgeAABB;
typedef struct { ScePspFVector3 center; float radius; } ForgeSphere;
```

### Collision Query Functions:
* `bool forge_collide_rect_rect(ForgeRect a, ForgeRect b);`: 2D Rectangle-Rectangle (AABB) intersection.
* `bool forge_collide_rect_circle(ForgeRect r, ForgeCircle c);`: 2D Rectangle-Circle intersection.
* `bool forge_collide_point_rect(float px, float py, ForgeRect r);`: 2D Point-Rectangle containment.
* `bool forge_collide_aabb_aabb(ForgeAABB a, ForgeAABB b);`: 3D Box-Box (AABB) intersection.
* `bool forge_collide_sphere_sphere(ForgeSphere a, ForgeSphere b);`: 3D Sphere-Sphere intersection.
* `bool forge_collide_aabb_sphere(ForgeAABB b, ForgeSphere s);`: 3D Box-Sphere intersection.
* `ForgeAABB forge_mesh_get_transformed_aabb(const ForgeMesh* mesh, float x, float y, float z, float sx, float sy, float sz);`: Computes the transformed world-space AABB bounding box for a mesh.

---

## 8. 2D Sprite Animation (`ForgeSpriteAnim`)

Flipbook spritesheet animation for dynamic characters and effects:

```c
typedef struct {
    const ForgeTexture* texture;
    int   frame_w;
    int   frame_h;
    int   num_frames;
    int   columns;
    float fps;
    float timer;
    int   current_frame;
    bool  loop;
    bool  is_playing;
} ForgeSpriteAnim;
```

* `void forge_anim2d_init(ForgeSpriteAnim* anim, const ForgeTexture* tex, int frame_w, int frame_h, int num_frames, float fps, bool loop);`: Initializes animation, automatically computing grid columns.
* `void forge_anim2d_update(ForgeSpriteAnim* anim, float dt);`: Advances animation timer based on frame duration.
* `void forge_anim2d_draw(const ForgeSpriteAnim* anim, float x, float y, float w, float h);`: Draws the current animated frame to the screen.
* `void forge_anim2d_set_frame(ForgeSpriteAnim* anim, int frame);`: Directly jumps to a specific frame (e.g. frame 0 for Idle).

---

## 9. Scene Management (`ForgeScene`)

State machine architecture for multi-screen games (Title, Gameplay, Game Over) with isolated memory allocation and cleanup:

```c
typedef struct ForgeScene ForgeScene;
typedef void (*ForgeSceneCallback)(ForgeScene* scene, float dt);

struct ForgeScene {
    const char*        name;
    void*              user_data;
    ForgeSceneCallback on_init;     // Called on activation (load scene assets)
    ForgeSceneCallback on_update;   // Called each frame (game logic & input)
    ForgeSceneCallback on_draw;     // Called each frame (video rendering)
    ForgeSceneCallback on_destroy;  // Called on exit (unload scene assets)
};
```

* `void forge_scene_set(ForgeScene* scene);`: Schedules a transition to a new scene (executed at the start of the next frame).
* `ForgeScene* forge_scene_get_current(void);`: Returns pointer to active scene.
* `void forge_scene_update_and_draw(float dt);`: Executes update and draw sequence for active scene.
* `void forge_scene_reset(void);`: Destroys active scene (invoking its `on_destroy` callback if registered) and resets scene state. Automatically invoked by `forge_shutdown()`.

---

## 10. 3D Skeletal Animation & Multi-Chunk Models

PSP-Forge includes a complete 3D skeletal mesh and keyframe animation runtime supporting Forward Kinematics (FK) and hardware vertex skinning:

```c
#define FORGE_MAX_BONES    96
#define FORGE_MAX_HW_BONES 8

typedef struct {
    float x, y, z, w;
} ForgeQuat;

typedef struct {
    char     name[24];             /* Identifier (e.g. "DEF-spine", "head") */
    uint8_t  parent_index;         /* Parent node index (0xFF = root) */
    uint8_t  reserved[3];
    float    local_pos[3];         /* Neutral rest position */
    float    local_rot[4];         /* Neutral rest quaternion (x,y,z,w) */
    float    inv_bind_matrix[16];  /* 4x4 inverse bind matrix for skinning */
} ForgeBoneDef;

typedef struct __attribute__((packed)) {
    char     magic[4];             /* "PANM" */
    uint16_t version;              /* 1 */
    uint16_t bone_count;
    uint32_t frame_count;
    float    framerate;
    float    duration;
    float    pos_scale;            /* Scale factor for compressed positions (e.g. 0.001f) */
    uint8_t  reserved[8];
} PanmHeader;

typedef struct __attribute__((packed)) {
    int16_t rot_quat[4];           /* Quantized x,y,z,w (* 32767) */
    int16_t pos[3];                /* Quantized x,y,z: pos = raw * pos_scale */
    int16_t reserved;
} ForgeBoneSample;

typedef struct {
    PanmHeader       header;
    ForgeBoneSample* samples;      /* frame_count * bone_count samples */
} ForgeAnimClip;

typedef struct {
    ForgeMesh* mesh;               /* Geometry chunk */
    int16_t    node_index;         /* Mode A: attached bone index (-1 if skinned) */
    uint8_t    num_local_bones;    /* Mode B: number of active local bones (<= 8) */
    uint8_t    bone_palette[8];    /* Mode B: maps local bone index (0..7) to global bone */
} ForgeModelChunk;

typedef struct {
    uint16_t         bone_count;
    ForgeBoneDef*    bones;
    uint16_t         chunk_count;
    ForgeModelChunk* chunks;
} ForgeModel3D;

typedef struct {
    const ForgeAnimClip* clip;
    float                time;
    float                speed;
    bool                 loop;
    bool                 is_playing;
    bool                 finished;
    ScePspFMatrix4       world_matrices[FORGE_MAX_BONES]; /* Computed world transforms */
    ScePspFMatrix4       skin_matrices[FORGE_MAX_BONES];  /* world * inv_bind_matrix */
} ForgeAnimator;
```

### Quaternion Math Functions:
* `void forge_quat_identity(ForgeQuat* q);`: Sets `q` to the identity quaternion `(0, 0, 0, 1)`.
* `void forge_quat_normalize(ForgeQuat* q);`: Normalizes quaternion `q` in-place.
* `void forge_quat_slerp(ForgeQuat* out, const ForgeQuat* a, const ForgeQuat* b, float t);`: Computes spherical linear interpolation along the shortest arc ($t \in [0.0, 1.0]$) and stores the result in `out`.
* `void forge_quat_to_matrix(ScePspFMatrix4* m, const ForgeQuat* q, const float pos[3]);`: Converts quaternion `q` and translation vector `pos` into a 16-element column-major $4 \times 4$ transformation matrix `m`.

### Model3D Functions:
* `ForgeModel3D* forge_model3d_load(const char* path);`: Loads a multi-chunk binary model file (`.p3d` with P3D2 header) containing skeletal bones, chunks, and sub-mesh vertex data.
* `void forge_model3d_free(ForgeModel3D* model);`: Frees all chunks, vertex buffers, and bone hierarchies.
* `void forge_model3d_draw(const ForgeModel3D* model, const ForgeAnimator* animator, const ForgeTexture* tex);`: Renders the model using the current model matrix on the Gum stack. Evaluates Mode A (hierarchical node positioning) or Mode B (hardware vertex skinning via `sceGuBoneMatrix(0..7)` and `GU_WEIGHTS`).

### Skeletal Animation Functions:
* `ForgeAnimClip* forge_anim3d_load(const char* path);`: Loads a binary `.panm` animation clip file into 16-byte aligned memory and performs D-Cache writeback.
* `void forge_anim3d_free(ForgeAnimClip* clip);`: Releases memory associated with the animation clip.
* `void forge_anim3d_init(ForgeAnimator* animator);`: Initializes the animator state, setting identity transforms and zero time.
* `void forge_anim3d_play(ForgeAnimator* animator, const ForgeAnimClip* clip, bool loop);`: Binds `clip` to `animator`, resets playback time to 0, and starts playback.
* `void forge_anim3d_stop(ForgeAnimator* animator);`: Stops playback.
* `void forge_anim3d_set_speed(ForgeAnimator* animator, float speed);`: Sets playback rate multiplier (default `1.0f`).
* `void forge_anim3d_update(ForgeAnimator* animator, const ForgeModel3D* model, float dt);`: Advances playback time, samples and SLERPs bone transforms from keyframes, and computes the Forward Kinematics cascade and skinning matrices.

---

## 11. Hardware & Engine Constraints & Architectural Rationale

To write high-performance 60 FPS homebrew on the Sony PSP, developers must respect the physical constraints of the hardware. The table below outlines these rules, their limits, and the exact architectural reason for each:

| Subsystem / Feature | Constraint / Limit | Hardware & Architectural Rationale |
|---|---|---|
| **Max Global Bones** | `FORGE_MAX_BONES = 96` | Skeletons are solved on the Allegrex CPU using Forward Kinematics (FK). `ForgeAnimator` statically allocates matrices (`world_matrices[96]`, `skin_matrices[96]`), consuming exactly $12.5\text{ KiB}$ of BSS memory per animator, avoiding heap fragmentation in the $24\text{ MB}$ user RAM. |
| **Hardware Bones per Chunk** | `FORGE_MAX_HW_BONES = 8` (`GU_WEIGHTS(1..8)`) | The PSP Graphics Engine (GE) hardware only has 8 skinning matrix registers (`sceGuBoneMatrix(0..7)`). The GE vertex format applies matrix $i$ directly to weight $i$ with no bone index attribute per-vertex. Therefore, the asset cooker splits meshes into sub-chunks referencing $\le 8$ local bones. |
| **Texture Dimensions** | Power-of-Two ($2^n$), Max $512 \times 512$ | The PSP GE texture sampling unit only supports texture wrapping, mipmapping, and hardware filtering on power-of-two dimensions $\le 512$. Non-POT dimensions cause texture wrapping artifacts or display distortion. |
| **Material Atlas Fusion** | Single $512 \times 512$ texture per skinned model | State changes (`sceGuTexImage`, display list flushes) on the PSP GE stall the command pipeline. Fusing multiple materials into an atlas allows the micro-engine to render the entire character in a single unified draw sequence at 60 FPS. |
| **Alpha Testing vs Blending** | `forge_set_alpha_test(true, 128)` for cutouts | Software alpha blending (`GU_BLEND`) triggers an expensive read-modify-write cycle against the 2 MB eDRAM framebuffer and requires sorting quads back-to-front on the CPU. Alpha test discards fragments immediately without touching the depth buffer, requiring zero CPU sorting. |
| **Vertex Memory Alignment** | 16-byte boundary (`aligned(16)`) | The PSP DMA controller transfers vertices directly from main RAM to the Graphics Engine. Unaligned memory addresses cause hardware bus exceptions or severe cache stalls on the Allegrex CPU/GE interface. |
| **VRAM Scratchpad Budget** | Max $688\text{ KiB}$ | Total on-chip eDRAM is exactly 2048 KiB. Draw ($544\text{ KiB}$), Display ($544\text{ KiB}$), and 16-bit Z-buffer ($272\text{ KiB}$) occupy $1360\text{ KiB}$, leaving exactly $688\text{ KiB}$ for the ultra-fast scratchpad. Textures that do not fit in scratchpad must remain in main RAM. |
| **Virtual Light Culling** | Max 16 virtual lights, 4 active (`GU_LIGHT0..3`) | The PSP GE provides only 4 hardware directional/point light registers. `forge_cull_and_apply_lights()` culls up to 16 virtual scene lights by 3D distance and uploads the 4 closest to hardware registers each draw call. |
| **Audio Chunk Alignment** | Multiples of 64 samples (128 bytes, 44.1 kHz PCM) | The PSP hardware audio DMAC processes DMA transfers in fixed 64-sample blocks. Buffer misalignment causes audio clicking, buffer underruns, or hardware channel lockups. |


