# Tutorial: 2D Animation with Spritesheets & Flipbook (`ForgeSpriteAnim`) 🏃

This tutorial explains how to achieve smooth 60 FPS 2D animations on the PlayStation Portable, from building a single multi-frame spritesheet to hardware-accelerated rendering using PSP-Forge's `ForgeSpriteAnim` component.

The complete working demo associated with this guide is located in:
`demos/demo_anim_2d/`

---

## 1. How 2D Animations Work on the PSP

Rather than loading individual image files for every frame of an animation (which would cause repeated RAM allocations and texture state switches via `sceGuTexImage`, degrading rendering performance), 2D games on the PSP use a **Spritesheet** (a single texture containing all animation frames arranged in a horizontal or vertical grid).

### Architectural Requirements:
1. **Power of Two (POT) Dimensions**: The overall texture must have power-of-two width and height ($16, 32, 64, 128, 256, 512$). For example, for 4 frames of $32 \times 32$, total dimensions are $128 \times 32$ (both powers of two!).
2. **Automatic Swizzling**: Running `psp-forge cook` swizzles the image into hardware-aligned $16 \times 8$ byte blocks, maximizing the GPU fillrate.
3. **UV Coordinate Computation**: The engine calculates texture UV coordinates on the fly for each frame:
   $$tx = (\text{frame} \pmod{\text{columns}}) \times \text{frame\_w}$$
   $$ty = (\lfloor\text{frame} / \text{columns}\rfloor) \times \text{frame\_h}$$

---

## 2. The `ForgeSpriteAnim` API

PSP-Forge includes native support for flipbook sprite animations in `psp_forge.h`:

```c
typedef struct {
    const ForgeTexture* texture;       // Loaded spritesheet texture
    int   frame_w;                     // Single frame width (e.g. 32)
    int   frame_h;                     // Single frame height (e.g. 32)
    int   num_frames;                  // Total number of frames (e.g. 4)
    int   columns;                     // Number of frame columns in texture
    float fps;                         // Playback speed (e.g. 8.0f FPS)
    float timer;                       // Internal accumulator timer
    int   current_frame;               // Current active frame index
    bool  loop;                        // Continuous loop or stop at final frame
    bool  is_playing;                  // Playback state flag
} ForgeSpriteAnim;
```

### Available Functions:
- `forge_anim2d_init(...)`: Initializes animation settings with dimensions, framerate, and looping options.
- `forge_anim2d_update(&anim, dt)`: Advances the animation timer using the frame delta time.
- `forge_anim2d_draw(&anim, x, y, w, h)`: Renders the active frame at the specified screen coordinates with arbitrary scaling.
- `forge_anim2d_set_frame(&anim, frame_index)`: Directly sets an explicit frame (e.g. frame 0 for Idle state when character stops).

---

## 3. Step-by-Step Implementation

### A. Loading the Spritesheet
```c
ForgeTexture* sheet_tex = forge_texture_load("assets/walker_sheet.tex");

ForgeSpriteAnim walk_anim;
// 4 frames of 32x32 pixels at 8 frames per second with looping enabled
forge_anim2d_init(&walk_anim, sheet_tex, 32, 32, 4, 8.0f, true);
```

### B. In the Game Loop: Dynamic Updates
```c
bool is_moving = false;

if (forge_input_is_held(&input, PSP_CTRL_LEFT))  { pos_x -= speed * dt; is_moving = true; }
if (forge_input_is_held(&input, PSP_CTRL_RIGHT)) { pos_x += speed * dt; is_moving = true; }

if (is_moving) {
    // Character is moving: update walk animation
    walk_anim.fps = 10.0f;
    forge_anim2d_update(&walk_anim, dt);
} else {
    // Character is stationary: reset to frame 0 (Idle)
    forge_anim2d_set_frame(&walk_anim, 0);
}
```

### C. In the Render Block
```c
forge_begin_frame();
forge_clear(0xFF1E2818);

// Draw animated sprite scaled to 48x48 pixels on screen
forge_anim2d_draw(&walk_anim, pos_x, pos_y, 48.0f, 48.0f);

forge_end_frame();
```

---

## 4. Testing the Included Demo

You can build and launch the demo from the repository:

```bash
cd demos/demo_anim_2d
psp-forge build
psp-forge run
```
