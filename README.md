# PSP-Forge ⚔️
> Modern Dev Suite & Micro-Engine for Sony PlayStation Portable (PSP)

PSP-Forge is a complete toolkit designed to modernize, accelerate, and simplify homebrew and game development for Sony PSP on Linux and cross-platform systems.

The framework unifies project orchestration, an automated asset compilation pipeline (Asset Cooker), and a C99 micro-runtime optimized for the MIPS Allegrex architecture and console eDRAM into a single command-line interface.

---

## 🚀 Key Features

* **CLI Orchestrator (`psp-forge`):**
  * `init`: Instantly generates a project skeleton (2D or 3D) with CMake configuration and IDE support (automatic include paths for VSCode/Clangd).
  * `cook`: Automatically compiles and optimizes textures, 3D models, and audio tracks.
  * `build`: Fast native MIPS cross-compilation and `EBOOT.PBP` packaging.
  * `run`: Direct launch on PPSSPP emulator or executable path reporting.
  * `clean`: Cleans build caches and intermediate compilation artifacts.

* **Asset Pipeline ("The Cooker"):**
  * **Texture Swizzling:** Automatic $16 \times 8$ byte block pixel reordering to eliminate PSP GPU cache misses.
  * **POT Padding & Formats:** Normalization to powers of two ($2^n \le 512$), support for RGBA8888, RGBA5551, RGBA4444, and CLUT quantization to 8-bit (256 colors) and 4-bit (16 colors).
  * **3D Geometry Packer:** Conversion of Wavefront `.obj` models into compact `.p3d` binary format with 16-byte aligned vertices and AABB bounding box calculation.
  * **Audio Transcoder:** Automatic conversion into signed 16-bit PCM stereo/mono at 44100 Hz with 64-sample aligned buffers.

* **C99 Micro-Runtime (`libpspforge`):**
  * **Zero Allocation at Runtime:** Deterministic static 2 MB VRAM allocator (Draw buffer, Display buffer, Z-buffer, and Texture scratchpad).
  * **Transparent Hardware Management:** 16-byte aligned Display Lists and automatic D-Cache flush (`sceKernelDcacheWritebackRange`).
  * **2D & 3D Pipeline:** Wrappers for sprite batching and `pspgum` matrices, with dynamic distance-based culling to assign the nearest lights to the 4 hardware slots (`GU_LIGHT0..3`).
  * **Input with Edge Detection:** Discrete detection for button pressed (*pressed*), released (*released*), and held (*held*), with analog stick normalized to $[-1.0, 1.0]$.
  * **Multithreaded Audio:** Dedicated high-priority audio thread (`0x12`) with dual 2048-sample PCM buffers to prevent degrading graphics framerate.

---

## 📦 Project Structure

```text
psp-forge/
├── bin/
│   └── psp-forge              # Terminal executable launcher
├── cli/
│   ├── psp_forge.py           # Core CLI Orchestrator
│   ├── config.py              # psp.toml parser
│   ├── cookers/               # Asset compilation modules
│   │   ├── texture.py         # Swizzler, POT padding, CLUT
│   │   ├── mesh.py            # OBJ parser -> .p3d vertex buffer
│   │   └── audio.py           # Audio WAV PCM transcoder
│   └── templates/             # Starter templates for psp-forge init
│       ├── 2d_starter/
│       └── 3d_runner/
├── runtime/                   # libpspforge (C99 Micro-Engine)
│   ├── include/
│   │   └── psp_forge.h        # Public API header
│   ├── src/
│   │   ├── core.c             # GU Init, DisplayList loop, HOME Callback
│   │   ├── vram.c             # 2MB VRAM layout
│   │   ├── video2d.c          # Sprites and swizzled textures
│   │   ├── video3d.c          # 3D Meshes, matrices, and 4 HW lights
│   │   ├── input.c            # Differential controller polling
│   │   ├── physics.c          # 2D & 3D collision detection
│   │   ├── anim.c             # 2D flipbook sprite animation
│   │   ├── scene.c            # Multi-scene state machine
│   │   └── audio.c            # Priority PCM thread
│   └── CMakeLists.txt
├── docs/                      # Technical documentation and API guides
└── tests/                     # Unit test suite
```

---

## 🛠️ Prerequisites & Setup

### 1. PSPSDK Toolchain
Ensure `PSPDEV` is set and available in your `PATH`:
```bash
export PSPDEV="/usr/local/pspdev"
export PATH="$PATH:$PSPDEV/bin"
```
Verify with:
```bash
psp-config --pspsdk-path
```

### 2. Python Dependencies
* Python 3.11+ (standard library `tomllib`)
* `Pillow` for image processing (`pip install Pillow`)

---

## ⚡ Quickstart

### 1. Create a new project
```bash
./bin/psp-forge init my_game --template 2d
cd my_game
```

### 2. Cook assets (textures, models, audio)
```bash
psp-forge cook
```

### 3. Build PSP binary (`EBOOT.PBP`)
```bash
psp-forge build
```

### 4. Run in PPSSPP
```bash
psp-forge run
```

---

## 📚 Guides & In-Depth Documentation

* ⚙️ **[Installation & Setup Guide](docs/INSTALLATION.md)**: From-scratch setup of the PSPSDK (`pspdev`) toolchain, building the `libpspforge` runtime, CLI installation, and PPSSPP configuration.
* 🛡️ **[2D Tutorial: "Hero Starter" from Scratch](docs/TUTORIAL_2D.md)**: Step-by-step guide to building a 2D game (RGBA sprites, audio, button edge detection, screen boundaries).
* 🏎️ **[3D Tutorial: "Track Runner" from Scratch](docs/TUTORIAL_3D.md)**: Comprehensive 3D development guide (Wavefront OBJ, texture mapping, perspective camera, jumping gravity, 60 FPS endless track).
* 🏃 **[Tutorial: 2D Animation (Spritesheets & Flipbook)](docs/TUTORIAL_ANIMATION_2D.md)**: 2D sprite animations with `ForgeSpriteAnim` (demo: `demos/demo_anim_2d`).
* 💎 **[Tutorial: 3D Animation (Procedural & Hierarchical)](docs/TUTORIAL_ANIMATION_3D.md)**: Matrix transformations, harmonic floating oscillations, and continuous rotations (demo: `demos/demo_anim_3d`).
* 🎬 **[Tutorial: Scene Management with `ForgeScene`](docs/TUTORIAL_SCENES.md)**: Multi-scene game state architecture with memory management in 24 MB RAM (demo: `demos/demo_scenes`).
* 💥 **[Tutorial: Bounding Boxes & Collisions](docs/TUTORIAL_COLLISIONS.md)**: 2D (`ForgeRect`, `ForgeCircle`) and 3D (`ForgeAABB`, `ForgeSphere`) collision detection (demo: `demos/demo_collisions`).
* 🎨 **[Asset Pipeline & Media Formats (Cooker)](docs/ASSET_PIPELINE.md)**: How to convert common files (**PNG, JPG, Wavefront OBJ, WAV, MP3**) into hardware-optimized binary formats (`.tex`, `.p3d`, `.snd`) with texture swizzling, POT padding, and hardware budget warnings.
* 🕹️ **[C99 Runtime API Reference (`libpspforge`)](docs/API.md)**: Complete API reference for hardware init, VRAM management, 2D/3D rendering loop, multithreaded audio, collisions, animations, and scenes.

---

## 🕹️ Ready-to-Run Demos (`demos/`)

The repository includes 4 complete demos with copyright-free procedural assets:
1. **`demos/demo_anim_2d/`**: Animated walking knight character at 60 FPS with D-Pad/Stick and flipbook spritesheet.
2. **`demos/demo_anim_3d/`**: Floating glowing 3D crystal gem with harmonic bobbing and 360° orbital camera.
3. **`demos/demo_scenes/`**: Complete multi-scene state machine (Title Menu $\rightarrow$ Gameplay $\rightarrow$ Return with Select).
4. **`demos/demo_collisions/`**: Solid rectangular obstacles (AABB) and collectible coins (Circle) with sound triggers.

To build and run any demo:
```bash
cd demos/demo_anim_2d
psp-forge build
psp-forge run
```

---

## 🎮 Real Hardware Deployment

Generated templates include the `BUILD_PRX` directive in `CMakeLists.txt` for instant compatibility with both PPSSPP and real PSP hardware running Custom Firmware (CFW):
1. Copy the project folder containing `EBOOT.PBP` and the `assets/` subfolder to your console's Memory Stick:
   ```text
   ms0:/PSP/GAME/my_game/
   ├── EBOOT.PBP
   └── assets/
       ├── icon0.png
       ├── pic1.png
       └── [.tex, .p3d, .snd files...]
   ```
2. Launch the game directly from the PSP *Game → Memory Stick* menu!

---

## 📄 License
Released under the [MIT](LICENSE) license.
