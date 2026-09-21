# PSP-Forge C99 Runtime API Reference (`libpspforge`)

The primary header file to include in any project is `#include <psp_forge.h>`.

---

## 1. Core & Frame Lifecycle

### `void forge_init(uint32_t flags)`
Initializes the Graphics Utility (GU), audio subsystem, system callbacks for the HOME button, game controller polling, and high-resolution hardware timers.
* **Parameters:** `flags` reserved (pass `0`).

### `void forge_shutdown(void)`
Shuts down the display pipeline, cleanly terminates the background audio thread and hardware channels (`forge_audio_shutdown`), resets the active scene (`forge_scene_reset`), and resets VRAM scratchpad allocators.

### `int forge_is_running(void)`
Returns `1` if the application is running, or `0` if the user requested exit via the PSP HOME button.

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
* `0x00000000` - `0x00088000` (544 KiB): Draw Buffer (RGBA8888)
* `0x00088000` - `0x00110000` (544 KiB): Display Buffer (RGBA8888)
* `0x00110000` - `0x00154000` (272 KiB): Depth Buffer (16-bit Z)
* `0x00154000` - `0x00200000` (688 KiB): Ultra-fast Texture Scratchpad

### `void* forge_vram_get_draw_buffer(void)`
Returns the relative VRAM pointer to the active 544 KiB draw buffer.

### `void* forge_vram_get_disp_buffer(void)`
Returns the relative VRAM pointer to the active 544 KiB display buffer.

### `void* forge_vram_get_depth_buffer(void)`
Returns the relative VRAM pointer to the active 272 KiB 16-bit depth buffer.

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
Polls the PSP controller and computes instantaneous discrete state:
```c
typedef struct {
    uint32_t held;      // Buttons currently held down
    uint32_t pressed;   // Buttons pressed on this frame
    uint32_t released;  // Buttons released on this frame
    float    analog_x;  // Analog stick horizontal axis [-1.0 .. 1.0]
    float    analog_y;  // Analog stick vertical axis   [-1.0 .. 1.0]
} ForgeInput;
```

### Quick Helper Functions:
* `bool forge_input_is_pressed(const ForgeInput* in, uint32_t btn);`
* `bool forge_input_is_held(const ForgeInput* in, uint32_t btn);`
* `bool forge_input_is_released(const ForgeInput* in, uint32_t btn);`

---

## 4. 2D & Texture Pipeline

### `ForgeTexture* forge_texture_load(const char* path)`
Loads a binary `.tex` texture (cooked by `psp-forge cook`) into main RAM.

### `ForgeTexture* forge_texture_load_vram(const char* path)`
Loads a `.tex` texture directly into the fast VRAM scratchpad (maximum fillrate performance). Automatically falls back to main RAM if scratchpad space is insufficient.

### `void forge_texture_free(ForgeTexture* tex)`
Releases memory allocated by the texture.

### `void forge_draw_sprite(const ForgeTexture* tex, float sx, float sy, float sw, float sh, float tx, float ty, float tw, float th)`
Renders a 2D textured quad:
* `sx, sy`: Screen coordinates (in pixels, $0$ to $480 \times 272$).
* `sw, sh`: Screen dimensions (width and height).
* `tx, ty`: Source UV offset on the texture.
* `tw, th`: Source crop width and height on the texture.

---

## 5. 3D Pipeline, Meshes & Lighting

### `ForgeMesh* forge_mesh_load(const char* path)`
Loads a precompiled 3D binary `.p3d` mesh. Contains 16-byte aligned vertices, normals, texture coordinates, and AABB bounding box values.

### `void forge_mesh_free(ForgeMesh* mesh)`
Releases mesh memory.

### `void forge_set_camera(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z, float fov_degrees)`
Sets up view and perspective projection matrices with a $16:9$ aspect ratio.

### `void forge_set_light(uint8_t id, float x, float y, float z, uint32_t color_rgba, float intensity)`
Registers a virtual scene light (up to 16 simultaneous lights supported). Nearest 4 lights are dynamically sorted and mapped to hardware registers `GU_LIGHT0..3` via `forge_cull_and_apply_lights()`.

### `void forge_draw_mesh(const ForgeMesh* mesh, const ForgeTexture* tex, float x, float y, float z, float rx, float ry, float rz, float sx, float sy, float sz)`
Applies model transformations (translation, rotation, scale), performs light culling and binding for the 4 nearest lights, and renders mesh primitives. Note: `forge_draw_mesh_current()` dynamically uses `GU_TFX_MODULATE` with hardware lighting when lights are registered, or `GU_TFX_REPLACE` (unlit mode) when no lights are active for maximum fillrate.

### `void forge_draw_mesh_current(const ForgeMesh* mesh, const ForgeTexture* tex)`
Renders a mesh using the current transformation matrix on the active `GU_MODEL` Gum stack without modifying or resetting it. Ideal for custom hierarchical matrix operations. Automatically handles lighting modulation when scene lights are present.

### `void forge_draw_mesh_node(const ForgeMesh* mesh, const ForgeTexture* tex, float x, float y, float z, float rx, float ry, float rz, float sx, float sy, float sz)`
Convenience helper for hierarchical articulated rigs: pushes a matrix onto the `pspgum` stack (`sceGumPushMatrix()`), applies relative translation, rotation, and scale, draws the mesh via `forge_draw_mesh_current`, and pops the matrix (`sceGumPopMatrix()`).

---

## 6. Multithreaded Audio Subsystem

### `ForgeSound* forge_sound_load(const char* path)`
Loads a `.snd` audio file transcoded to signed 16-bit 44100 Hz PCM into main RAM with D-Cache writeback.

### `void forge_sound_free(ForgeSound* snd)`
Releases audio buffer memory.

### `void forge_sound_play(const ForgeSound* snd, uint8_t loop)`
Dispatches audio playback to the dedicated high-priority thread (`0x12`), which transfers 512-sample stereo PCM chunks to the hardware audio channel via `sceAudioChReserve`. When `loop` is `1`, the audio loops continuously (ideal for background music).

### `void forge_sound_stop(void)`
Stops active audio playback.

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
    int   frame_w, frame_h;
    int   num_frames, columns;
    float fps, timer;
    int   current_frame;
    bool  loop, is_playing;
} ForgeSpriteAnim;
```

* `void forge_anim2d_init(ForgeSpriteAnim* anim, const ForgeTexture* tex, int fw, int fh, int frames, float fps, bool loop);`: Initializes animation, automatically computing grid columns.
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
