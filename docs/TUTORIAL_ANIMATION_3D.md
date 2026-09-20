# Tutorial: Procedural & Hierarchical 3D Animation 💎⚔️

This tutorial explains how to implement 3D animations on the Sony PSP leveraging the hardware matrix stack (`pspgum`), procedural kinematics, and high-performance mathematics—guaranteeing 60 FPS without overloading the 333 MHz MIPS CPU.

Two complete showcase demos are included in PSP-Forge:
1. **Procedural Floating Gem**: `demos/demo_anim_3d/` (Harmonic oscillations, tilt, and continuous rotation)
2. **Hierarchical Humanoid Knight**: `demos/demo_anim_3d_v2/` (Full articulated humanoid rig with walk cycle, jump, attack, and arena)

---

## 1. Why Hierarchical & Procedural Animation on PSP?

The Sony PSP features a Vector Floating Point Unit (VFPU) and a dedicated Graphics Engine (GE) with a hardware $4 \times 4$ transformation matrix stack (`pspgum`).

Processing skeletal deformation with per-vertex weights on the CPU (*software vertex skinning*) requires multiplying dozens of matrices for thousands of vertices every frame. On the PSP's 333 MHz MIPS R4000-based CPU with limited cache lines, this causes severe frame drops.

Commercial PSP titles (and classic PS1/N64 era games) solve this elegantly with two techniques:
1. **Procedural Transforms**: Harmonic sinusoidal functions ($\sin$, $\cos$) for bobbing, rolling, floating, and breathing.
2. **Hierarchical Matrix Cascades**: Splitting a character into modular rigid meshes (torso, head, upper arm, forearm, thigh, shin, weapon) connected via nested `sceGumPushMatrix()` and `sceGumPopMatrix()` calls. The PSP's hardware transformation engine does the matrix multiplication natively in eDRAM pipelines!

---

## 2. Part 1: Procedural Floating Object (`demo_anim_3d`)

### A. Mathematical Foundations
- **Vertical oscillation (bobbing)**:
  $$Y(t) = Y_0 + \sin(t \cdot \omega_{\text{bob}}) \cdot A_{\text{bob}}$$
- **Dynamic harmonic tilt**:
  $$\theta_x(t) = \cos(t \cdot \omega_{\text{tilt}}) \cdot A_{\text{tilt}}$$
- **Continuous spin**:
  $$R_y(t) = t \cdot \text{speed}$$

### B. Implementation in Game Loop
```c
float dt = forge_get_delta_time();
time_acc += dt;

// 1. Compute procedural trajectory
float gem_bob_y = sinf(time_acc * 2.5f) * 0.35f;
float gem_rot_y = time_acc * 2.0f;
float gem_tilt  = cosf(time_acc * 1.5f) * 0.15f;

// 2. Setup orbiting camera
float cam_x = sinf(cam_angle) * cam_dist;
float cam_z = -cosf(cam_angle) * cam_dist;
forge_set_camera(cam_x, cam_height, cam_z, 0.0f, 0.0f, 0.0f, 60.0f);

// 3. Render base and animated crystal
forge_begin_frame();
forge_clear(0xFF140F0A);

forge_draw_mesh(ped_mesh, ped_tex, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
forge_draw_mesh(gem_mesh, gem_tex,
    0.0f, 0.5f + gem_bob_y, 0.0f,
    gem_tilt, gem_rot_y, 0.0f,
    1.0f, 1.0f, 1.0f
);

forge_end_frame();
```

---

## 3. Part 2: Articulated Humanoid Rig (`demo_anim_3d_v2`)

In `demo_anim_3d_v2`, we construct an interactive humanoid cyber-knight walking inside a 3D circular arena.

### A. Modular Mesh Design & Pivot Points
To allow limbs to rotate naturally around joints (shoulders, elbows, hips, knees), modular meshes must be modeled with their **pivot point at $(0, 0, 0)$**:

| Mesh Asset | Description | Pivot Position |
|---|---|---|
| `torso.obj` | Chest and spine | Center of chest $(0, 0, 0)$ |
| `head.obj` | Helmet with glowing visor | Base of neck $(0, 0, 0)$ |
| `limb.obj` | Articulated capsule / beveled segment | Top joint $(0, 0, 0)$ extending downward along $-Y$ |
| `sword.obj` | Energy blade and crossguard | Center of the hilt |
| `arena.obj` | Circular arena floor disc | Center of the arena $(0, 0, 0)$ |
| `shadow_disc.obj` | Ground contact shadow polygon | Center of the ground contact |

> [!TIP]
> **Reusing Meshes**: The single `limb.obj` asset is reused for all 8 limb segments: Left/Right Upper Arms, Left/Right Forearms, Left/Right Thighs, and Left/Right Shins. Different joint proportions are created simply by passing different scale factors (`sx, sy, sz`) into `forge_draw_mesh_node()`.

### B. Texture Budgeting
- `knight_bot.png` ($256 \times 256$, 256 KB): UV atlas for armor, visor, and sword.
- `arena.png` ($128 \times 128$, 64 KB): Radial stone pattern with glowing cyan circuit runes.
- `shadow.png` ($32 \times 32$, 4 KB): Soft radial alpha vignette.
Total texture memory: ~324 KB, staying comfortably inside the 512 KB eDRAM texture scratchpad!

---

## 4. The Hierarchical Matrix Tree

By chaining matrix transformations, child nodes automatically inherit the world position, rotation, and jump height of their parents:

```text
World Space
 ├── Arena Floor (static at 0, 0, 0)
 └── Knight Root (Pos X, Jump Y, Pos Z, Facing Yaw)
      ├── Contact Shadow Disc (clamped at Y = 0.02, scale shrinks on jump)
      └── Torso (breathing Y bob + forward lean during run)
           ├── Head (neck offset + look angle)
           ├── Left Shoulder -> Left Upper Arm -> Left Forearm
           ├── Right Shoulder -> Right Upper Arm -> Right Forearm -> Hand (Sword)
           ├── Left Hip -> Left Thigh -> Left Shin
           └── Right Hip -> Right Thigh -> Right Shin
```

### C99 Hierarchical Drawing API
`libpspforge` provides two dedicated functions for hierarchical rendering:
1. `forge_draw_mesh_current(mesh, tex)`: Draws a mesh using the current transformation on the Gum matrix stack without resetting it.
2. `forge_draw_mesh_node(...)`: Pushes a new matrix, applies local translation/rotation/scaling, renders the mesh, and pops back to the parent frame:

```c
// Example: Attaching Head and Left Arm to Torso
sceGumPushMatrix();
{
    // 1. Move to Character Root and apply facing rotation
    ScePspFVector3 root_pos = { char_x, char_y, char_z };
    ScePspFVector3 root_rot = { 0.0f, char_yaw, 0.0f };
    sceGumTranslate(&root_pos);
    sceGumRotateXYZ(&root_rot);

    // 2. Draw Torso (with subtle breathing oscillation)
    sceGumPushMatrix();
    {
        ScePspFVector3 torso_pos = { 0.0f, 1.15f + idle_bob, 0.0f };
        ScePspFVector3 torso_rot = { run_lean, 0.0f, 0.0f };
        sceGumTranslate(&torso_pos);
        sceGumRotateXYZ(&torso_rot);

        // Draw chest mesh
        forge_draw_mesh_current(torso_mesh, bot_tex);

        // Child: Head (relative to Torso)
        forge_draw_mesh_node(head_mesh, bot_tex,
            0.0f, 0.38f, 0.0f,    // neck offset
            0.0f, head_yaw, 0.0f, // look angle
            0.85f, 0.85f, 0.85f
        );

        // Child: Left Upper Arm
        sceGumPushMatrix();
        {
            ScePspFVector3 l_shldr = { -0.36f, 0.22f, 0.0f };
            ScePspFVector3 l_arm_rot = { l_arm_pitch, 0.0f, 0.1f };
            sceGumTranslate(&l_shldr);
            sceGumRotateXYZ(&l_arm_rot);

            // Draw upper arm
            forge_draw_mesh_node(limb_mesh, bot_tex, 0, 0, 0, 0, 0, 0, 0.7f, 0.7f, 0.7f);

            // Grandchild: Left Forearm (relative to elbow)
            forge_draw_mesh_node(limb_mesh, bot_tex,
                0.0f, -0.32f, 0.0f,  // elbow offset
                l_forearm_pitch, 0, 0,
                0.6f, 0.65f, 0.6f
            );
        }
        sceGumPopMatrix();

        // (Draw Right Arm + Sword, Left Leg, Right Leg...)
    }
    sceGumPopMatrix();
}
sceGumPopMatrix();
```

---

## 5. Locomotion State Machine

`demo_anim_3d_v2` includes a complete 4-state locomotion controller:

### A. Idle State
- **Breathing**: Sinusoidal chest bobbing $Y = \sin(t \cdot 3.0) \cdot 0.025$.
- **Arm Rest**: Arms hang relaxed at sides with gentle counter-oscillation.

### B. Walk / Run Gait
- **Leg Swing (Forward Kinematics)**:
  $$\theta_{\text{thigh\_left}} = \sin(\text{walk\_phase}) \cdot 0.55\text{ rad}$$
  $$\theta_{\text{thigh\_right}} = -\theta_{\text{thigh\_left}}$$
- **Knee Bend**:
  $$\theta_{\text{shin}} = |\sin(\text{walk\_phase} - 0.4)| \cdot 0.5\text{ rad}$$
- **Arm Swing**: Arms swing reciprocally in opposition to the legs to balance momentum.

### C. Ballistic Jump
- Triggered with Cross ($\times$):
  $$V_y \mathrel{+}= 5.2\text{ m/s}$$
  $$Y(t) \mathrel{+}= V_y \cdot dt - \frac{1}{2} g \cdot dt^2$$
- Legs tuck upward while in the air ($\theta_{\text{thigh}} = -0.4\text{ rad}$, $\theta_{\text{knee}} = 0.6\text{ rad}$).
- Ground shadow shrinks dynamically in proportion to altitude ($S = 1.0 - \frac{Y}{2.5}$).

### D. Sword Slash Attack
- Triggered with Square ($\square$):
  - Right arm raises and executes a rapid $1.8\text{ rad}$ downward slash arc.
  - Utilizes smooth ease-out interpolation for snappy, impactful combat feel.

---

## 6. Hardware Rasterizer & Frustum Clipping Tip

> [!WARNING]
> **PSP Near-Plane Geometry Discard ($W \le 0$)**:  
> The PSP's hardware rasterizer discards any triangle whose vertices extend behind the camera's near clipping plane ($Z_{\text{near}} \approx 0.5$).
> If a large arena floor extends past the camera's eye position, triangular wedges will visibly pop out of view.
> 
> **Solution**: Keep ground arenas sized appropriately (e.g. radius $R \approx 4.5$ with camera distance $D \approx 5.8$ to $6.2$), or subdivide large ground floors into smaller tiles so the GPU can clip individual small polygons cleanly.

---

## 7. Building and Running the Demos

### Run Demo 1 (Floating Gem):
```bash
cd demos/demo_anim_3d
psp-forge cook
psp-forge build
psp-forge run
```

### Run Demo 2 (Humanoid Cyber-Knight):
```bash
cd demos/demo_anim_3d_v2
psp-forge cook
psp-forge build
psp-forge run
```

### Interactive Controls (Demo V2):
| Button | Action |
|---|---|
| **Analog Stick / D-Pad** | Walk & Run across the 3D arena |
| **Cross ($\times$)** | Ballistic Jump with dynamic shadow scaling |
| **Square ($\square$)** | Energy Sword Slash combo |
| **L / R Triggers** | Smooth 360° Orbiting Camera |
| **Start** | Reset position to center |
