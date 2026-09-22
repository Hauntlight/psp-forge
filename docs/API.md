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

Support for rigged characters, forward kinematics animation sampling, and hardware vertex blending:

```c
#define FORGE_MAX_BONES    64
#define FORGE_MAX_HW_BONES 8

typedef struct { float x, y, z, w; } ForgeQuat;

typedef struct {
    char            name[32];
    int32_t         parent_index;
    ScePspFVector3  rest_pos;
    ForgeQuat       rest_rot;
    ScePspFVector3  rest_scale;
    ScePspFMatrix4  inv_bind_matrix;
} ForgeBoneDef;

typedef struct {
    uint32_t        num_chunks;
    ForgeModelChunk* chunks;
    uint32_t        num_bones;
    ForgeBoneDef*   bones;
    ForgeTexture*   texture;
} ForgeModel3D;

typedef struct {
    const ForgeModel3D*   model;
    const ForgeAnimClip*  current_clip;
    const ForgeAnimClip*  blend_clip;
    float                 current_time;
    float                 blend_time;
    float                 crossfade_duration;
    float                 crossfade_timer;
    float                 playback_speed;
    bool                  is_looping;
    bool                  is_playing;
    ScePspFMatrix4        bone_world_matrices[FORGE_MAX_BONES];
    ScePspFMatrix4        bone_skin_matrices[FORGE_MAX_BONES];
} ForgeAnimator;
```

### Quaternion Math & Interpolation:
* `ForgeQuat forge_quat_identity(void);`: Returns the identity quaternion `(0, 0, 0, 1)`.
* `ForgeQuat forge_quat_normalize(ForgeQuat q);`: Normalizes a quaternion.
* `ForgeQuat forge_quat_slerp(ForgeQuat a, ForgeQuat b, float t);`: Spherical linear interpolation between two quaternions along the shortest arc.
* `void forge_quat_to_matrix(ForgeQuat q, ScePspFMatrix4* out);`: Converts a unit quaternion to a $4 \times 4$ rotation matrix.

### Animation Clips (`.panm`):
* `ForgeAnimClip* forge_anim3d_clip_load(const char* path);`: Loads a binary `.panm` animation clip into 16-byte aligned memory and flushes D-Cache.
* `void forge_anim3d_clip_free(ForgeAnimClip* clip);`: Releases memory associated with an animation clip.

### Animator State Machine:
* `void forge_anim3d_init(ForgeAnimator* anim, const ForgeModel3D* model);`: Initializes animator with rest pose transforms.
* `void forge_anim3d_play(ForgeAnimator* anim, const ForgeAnimClip* clip, bool loop);`: Starts playing a clip immediately.
* `void forge_anim3d_crossfade(ForgeAnimator* anim, const ForgeAnimClip* clip, float duration, bool loop);`: Starts a smooth crossfade blend to a new animation clip over `duration` seconds.
* `void forge_anim3d_update(ForgeAnimator* anim, float dt);`: Advances animation timing, computes joint transformations, and evaluates forward kinematics hierarchy.

### Multi-Chunk Model Loading & Rendering:
* `ForgeModel3D* forge_model3d_load(const char* path);`: Loads a multi-chunk P3D2 model file and its bone hierarchy.
* `void forge_model3d_free(ForgeModel3D* model);`: Releases all sub-mesh chunks, bone definitions, and vertex arrays.
* `void forge_model3d_set_texture(ForgeModel3D* model, ForgeTexture* tex);`: Binds a shared texture to the model.
* `void forge_model3d_draw(const ForgeModel3D* model, const ForgeAnimator* anim, float x, float y, float z, float rx, float ry, float rz, float scale);`: Dispatches multi-chunk geometry to the hardware Graphics Engine, binding chunk skinning palettes to `sceGuBoneMatrix(0..7)` for continuous skinning (Mode B) or evaluating node transforms (Mode A).

