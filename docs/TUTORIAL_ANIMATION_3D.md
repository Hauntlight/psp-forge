# Tutorial: Procedural & Hierarchical 3D Animation 💎

This tutorial explains how to implement 3D animations on the Sony PSP leveraging the hardware matrix stack (`pspgum`) and high-performance mathematics, maintaining 60 FPS without overloading the MIPS CPU.

The complete working demo associated with this guide is located in:
`demos/demo_anim_3d/`

---

## 1. Why Procedural & Hierarchical 3D Animation on the PSP?

The PSP hardware features a Vector Floating Point Unit (VFPU) and a Graphics Engine with native $4 \times 4$ transformation matrix pipelines.
Processing complex skeletal deformation with per-vertex weights on the CPU (*software vertex skinning*) quickly saturates MIPS clock cycles. The optimal and widely used approach in commercial PSP titles combines:

1. **Procedural Animations with Harmonic Functions**:
   - Vertical floating/bobbing: $Y(t) = Y_0 + \sin(t \cdot \omega) \cdot A$
   - Dynamic roll and tilt (e.g. boats, aircraft, floating gems): $\theta(t) = \cos(t \cdot \omega) \cdot \alpha$
   - Continuous rotations (wheels, propellers, collectibles): $R_y(t) = t \cdot \text{speed}$
2. **Hierarchical Node Animation**:
   - Instead of per-vertex deformation, models are broken into submeshes connected via matrix cascades using `sceGumPushMatrix()` / `sceGumPopMatrix()`.

---

## 2. Preparing 3D Models (`.obj` $\rightarrow$ `.p3d`)

In our example, we have two distinct entities:
- `pedestal.obj`: Static base pedestal.
- `gem.obj`: Faceted floating crystal centered at the origin $(0, 0, 0)$.

Both models, when processed by `psp-forge cook`, are compiled into the compact `.p3d` binary format with 16-byte aligned vertices for GPU DMA and precomputed AABB bounding boxes.

---

## 3. C99 Implementation

### A. Computing Trajectories in the Game Loop
```c
float dt = forge_get_delta_time();
time_acc += dt;

// 1. Vertical oscillation (frequency 2.5 rad/s, amplitude 0.35 units)
float gem_bob_y = sinf(time_acc * 2.5f) * 0.35f;

// 2. Continuous rotation around Y axis
float gem_rot_y = time_acc * 2.0f;

// 3. Harmonic tilt on X axis
float gem_tilt  = cosf(time_acc * 1.5f) * 0.15f;
```

### B. Mobile Orbiting Camera
We can set up a camera in polar coordinates that smoothly orbits the scene controlled by the D-Pad or Analog Stick:
```c
float cam_x = sinf(cam_angle) * cam_dist;
float cam_z = -cosf(cam_angle) * cam_dist;

forge_set_camera(
    cam_x, cam_height, cam_z, // Camera position (Eye)
    0.0f, 0.0f, 0.0f,         // Focus target (Center of scene)
    60.0f                     // Field of view (FOV in degrees)
);
```

### C. Rendering with `forge_draw_mesh`
Pass computed parameters directly into the GPU transformation pipeline:
```c
forge_begin_frame();
forge_clear(0xFF140F0A);

// Draw static base pedestal
forge_draw_mesh(ped_mesh, ped_tex, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

// Draw procedurally animated crystal
forge_draw_mesh(
    gem_mesh, gem_tex,
    0.0f, 0.5f + gem_bob_y, 0.0f,  // Harmonically bobbing Y position
    gem_tilt, gem_rot_y, 0.0f,      // Y spin + X tilt
    1.0f, 1.0f, 1.0f               // Scale
);

forge_end_frame();
```

---

## 4. Building and Running the Demo

```bash
cd demos/demo_anim_3d
psp-forge build
psp-forge run
```
