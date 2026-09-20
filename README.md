<div align="center">

# PSP-FORGE ⚔️

[![Platform](https://img.shields.io/badge/Platform-Sony%20PSP-003791?logo=playstation&logoColor=white)](https://pspdev.github.io/) [![Arch](https://img.shields.io/badge/Arch-MIPS%20Allegrex-FF6600?logo=cpu&logoColor=white)](https://pspdev.github.io/vfpu-docs/) [![C Standard](https://img.shields.io/badge/C%20Standard-C99-00599C?logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C99) [![Toolchain](https://img.shields.io/badge/Toolchain-PSPDEV-008080)](https://github.com/pspdev) [![Vibe Coded](https://img.shields.io/badge/Crafted-100%25%20Vibe--Coded-ff69b4?logo=openai&logoColor=white)](#-100-vibe-coded-origins) [![License: MIT](https://img.shields.io/badge/License-MIT-green)](LICENSE)

**A Unified Developer Suite & High-Performance C99 Micro-Engine for Sony PlayStation Portable® (PSP)**

*Streamlined CLI Orchestration • Automated Hardware Asset Cooker • Bare-Metal 2D/3D Runtime • 60 FPS Guaranteed*

[Get Started](docs/INSTALLATION.md) • [Documentation](docs/) • [Tutorials](#-tutorials--guides) • [Demos](#-showcase-demos) • [Architecture](#-architecture--key-features) • [Thanks](#-thanks--acknowledgments)

---

</div>

## Table of Contents

- [Overview](#overview)
- [✨ 100% Vibe-Coded Origins](#-100-vibe-coded-origins)
- [Up and Running (Quickstart)](#up-and-running-quickstart)
- [Architecture & Key Features](#-architecture--key-features)
  - [1. CLI Orchestrator (`psp-forge`)](#1-cli-orchestrator-psp-forge)
  - [2. Hardware-Aware Asset Cooker](#2-hardware-aware-asset-cooker)
  - [3. C99 Micro-Engine (`libpspforge`)](#3-c99-micro-engine-libpspforge)
- [Repository Structure](#-repository-structure)
- [Requirements](#-requirements)
- [Installation & Setup](#-installation--setup)
- [Showcase Demos](#-showcase-demos)
- [Tutorials & Guides](#-tutorials--guides)
- [Real Hardware Deployment](#-real-hardware-deployment)
- [Open to Contributions](#-open-to-contributions)
- [Thanks & Acknowledgments](#-thanks--acknowledgments)
- [SEO, Discovery & LLM Metadata](#-seo-discovery--llm-metadata)
- [License](#-license)

---

## Overview

**PSP-Forge** bridges the gap between modern game development developer experiences (DX) and classic bare-metal embedded consoles. Built specifically for the **Sony PlayStation Portable (PSP-1000/2000/3000/Go/Street)**, PSP-Forge unifies:
1. An intuitive **CLI tool** (`init`, `cook`, `build`, `run`, `clean`) that abstracts away arcane toolchain incantations.
2. A deterministic **Asset Pipeline ("The Cooker")** that automatically optimizes textures (GPU swizzling, power-of-two padding, CLUT palettization), 3D Wavefront OBJ models, and 44.1 kHz PCM audio, complete with **hardware budget warning checks**.
3. A clean, zero-allocation **C99 Micro-Engine (`libpspforge.a`)** leveraging the PSP's native Graphics Engine (GE), GUM matrix stack, and multithreaded Media Engine PCM audio output.

---

## ✨ 100% Vibe-Coded Origins

> [!NOTE]
> **PSP-Forge is completely and unapologetically vibe-coded.**
> 
> The entire architecture—the Python CLI, the automated asset swizzlers and compilers, the C99 micro-engine, the collision solvers, the multi-scene state machines, the documentation, and the showcase demos—was authored and iterated through **collaborative AI pair-programming** (human intent + advanced LLM agentic engineering).
> 
> This project stands as proof that **vibe coding** is not limited to high-level JavaScript web toys: when guided by solid architectural principles, it can conquer 20-year-old bare-metal embedded MIPS architectures, reverse-engineered hardware registers, display list geometry, cache line flushes, and strict $24\text{ MB}$ RAM physical budgets.

---

## Up and Running (Quickstart)

Get a complete PSP game project initialized, cooked, built, and running in less than 60 seconds:

```bash
# 1. Initialize a new 2D or 3D project skeleton
psp-forge init my_game --template 2d
cd my_game

# 2. Cook assets (PNG, OBJ, WAV -> hardware formats with budget checks)
psp-forge cook

# 3. Build native MIPS EBOOT.PBP via CMake & psp-gcc
psp-forge build

# 4. Run immediately in the PPSSPP emulator
psp-forge run
```

---

## 🚀 Architecture & Key Features

### 1. CLI Orchestrator (`psp-forge`)
* **`psp-forge init <name> [--template 2d|3d]`**: Scaffolds a complete project with CMakeLists.txt, `psp.toml` manifest, assets, and VSCode/Clangd include paths.
* **`psp-forge cook`**: Incremental build system for game assets with timestamp caching.
* **`psp-forge build [--clean] [--release|--debug]`**: Wraps `psp-cmake` and `make` to compile MIPS binaries, call `psp-fixup-imports`, `prxgen`, `mksfoex`, and generate relocatable `EBOOT.PBP` (PRX mode).
* **`psp-forge run`**: Auto-detects local PPSSPP installations (AppImage, native binary, portable configurations) and boots the game in one click.

### 2. Hardware-Aware Asset Cooker
The PSP hardware imposes strict memory and rasterizer constraints. The Asset Cooker automatically converts assets and warns you before hitting hardware bottlenecks:
* **Texture Swizzling**: Interleaves pixel data into $16 \times 8$ byte tiles to eliminate GPU cache misses during texture sampling.
* **Power-of-Two (POT) Padding**: Expands textures to $2^n$ dimensions (up to $512 \times 512$).
* **Format Conversion & CLUT Quantization**: Supports `RGBA8888`, `RGBA5551`, `RGBA4444`, and indexed CLUT8 (256 colors) / CLUT4 (16 colors).
* **Wavefront OBJ to `.p3d`**: Packs vertices into binary stream with 16-byte alignment, normals, texture coordinates, and precalculated AABB bounding boxes.
* **Audio Transcoder**: Resamples WAV/audio to signed 16-bit PCM at 44,100 Hz with 64-sample buffer alignment.
* **Hardware Budget Warnings**:
  * ⚠️ Warns if 3D models exceed 3,000 triangles or 256 KB.
  * 🛑 Hard errors if textures exceed the $512 \times 512$ hardware limit.
  * ⚠️ Warns if texture VRAM footprint exceeds 512 KB (suggesting CLUT or 16-bit formats).
  * ⚠️ Warns if uncompressed audio clips exceed 2 MB RAM.

### 3. C99 Micro-Engine (`libpspforge`)
* **Zero Runtime Dynamic Allocation**: Deterministic 2 MB VRAM allocator partitioning eDRAM into Draw buffer ($512\text{ KB}$), Display buffer ($512\text{ KB}$), Z-Buffer ($512\text{ KB}$), and Texture scratchpad ($512\text{ KB}$).
* **Display List Management**: Safe 16-byte aligned GU Display Lists with automatic D-Cache writeback (`sceKernelDcacheWritebackRange`).
* **2D & 3D Unified Pipeline**: Sprite batching, perspective projection, camera view matrix, and dynamic distance-based culling for the 4 hardware light slots (`GU_LIGHT0..3`).
* **Collision Engine**: Lightweight, allocation-free 2D primitives (`ForgeRect`, `ForgeCircle`) and 3D bounding volumes (`ForgeAABB`, `ForgeSphere`) with fast intersection tests.
* **2D Flipbook Animation**: Grid-based spritesheet player (`ForgeSpriteAnim`) with frame timing, UV coordinate computation, and playback loops.
* **Scene Manager (`ForgeScene`)**: Lifecycle state machine (`on_init`, `on_update`, `on_draw`, `on_destroy`) guaranteeing strict asset unloading when navigating between Title Menus and Gameplay levels in $24\text{ MB}$ RAM.
* **Multithreaded Audio**: High-priority audio thread (`0x12`) operating double 2048-sample stereo PCM ring buffers, preventing audio stutter even during heavy 3D rendering.

---

## 📦 Repository Structure

```text
psp-forge/
├── bin/
│   └── psp-forge              # Portable CLI executable wrapper
├── cli/
│   ├── psp_forge.py           # Core CLI orchestrator
│   ├── config.py              # psp.toml manifest parser
│   ├── cookers/               # Hardware-aware asset compilers
│   │   ├── texture.py         # Swizzling, POT padding, CLUT quantization
│   │   ├── mesh.py            # OBJ to binary .p3d parser + AABB bounds
│   │   └── audio.py           # WAV to PCM 44.1kHz transcoder
│   └── templates/             # Project starter templates (2D & 3D)
├── runtime/                   # libpspforge (C99 Micro-Engine source)
│   ├── include/
│   │   └── psp_forge.h        # Unified public header
│   ├── src/
│   │   ├── core.c             # GU initialization & DisplayList loop
│   │   ├── vram.c             # 2MB VRAM deterministic layout
│   │   ├── video2d.c          # 2D Sprites & swizzled texture rendering
│   │   ├── video3d.c          # 3D Meshes, matrices & 4 HW lights
│   │   ├── input.c            # Differential button & analog polling
│   │   ├── physics.c          # 2D & 3D collision detection
│   │   ├── anim.c             # 2D flipbook sprite animation
│   │   ├── scene.c            # Multi-scene state machine
│   │   └── audio.c            # Dedicated high-priority PCM audio thread
│   └── CMakeLists.txt
├── demos/                     # Complete, ready-to-run showcase games
│   ├── demo_anim_2d/          # 2D animated knight with flipbook spritesheet
│   ├── demo_anim_3d/          # Floating rotating 3D gem with lighting
│   ├── demo_scenes/           # Title Menu <-> Game state transitions
│   └── demo_collisions/       # AABB and Circle collision detection & audio
├── docs/                      # Full documentation, tutorials, and API reference
└── tests/                     # Python unit test suite
```

---

## 📋 Requirements

To build and use PSP-Forge, you need:
- **Linux** (x86_64 or ARM64) or macOS.
- **[PSPDEV Toolchain](https://github.com/pspdev/pspdev)** (`psp-gcc`, `psp-cmake`, `pspsdk`, etc.) installed at `/usr/local/pspdev`.
- **Python 3.11+** with `Pillow` installed (`pip install Pillow`).
- **CMake** (v3.15+) and **Make**.
- **[PPSSPP](https://www.ppsspp.org/)** (optional, recommended for instantaneous testing).

---

## ⚙️ Installation & Setup

### 1. Ensure PSPDEV Environment Variables are Configured
Add these lines to your shell profile (`~/.bashrc` or `~/.zshrc`):
```bash
export PSPDEV=/usr/local/pspdev
export PATH=$PATH:$PSPDEV/bin
```
Verify your toolchain:
```bash
psp-config --pspsdk-path
```

### 2. Install the `libpspforge` Runtime
Compile and install the C99 micro-engine directly into your PSPDEV system includes:
```bash
cd runtime
psp-cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```
This installs `libpspforge.a` in `$PSPDEV/psp/lib/` and `psp_forge.h` in `$PSPDEV/psp/include/`.

### 3. Install the `psp-forge` CLI
Add `bin/` to your PATH, or symlink the launcher:
```bash
sudo ln -sf $(pwd)/bin/psp-forge /usr/local/bin/psp-forge
```

See [docs/INSTALLATION.md](docs/INSTALLATION.md) for complete from-scratch toolchain installation steps.

---

## 🕹️ Showcase Demos

Four complete, standalone showcase demos are provided in `demos/`. All demos feature 100% copyright-free procedural assets:

| Demo | Directory | Key Techniques Demonstrated | Assets Included |
|---|---|---|---|
| **2D Animation** | [`demos/demo_anim_2d/`](demos/demo_anim_2d/) | 60 FPS flipbook sprite animation via `ForgeSpriteAnim`, dynamic state switching (Walk/Idle) | `walker_sheet.png` (4-frame $32\times 32$ sheet), UI icons |
| **3D Animation** | [`demos/demo_anim_3d/`](demos/demo_anim_3d/) | Floating crystal with sinusoidal bobbing ($Y$), tilt ($X$), continuous rotation ($Y$), and orbital camera | `gem.obj`, `gem.png`, `pedestal.obj`, `pedestal.png` |
| **Scene Manager** | [`demos/demo_scenes/`](demos/demo_scenes/) | Clean Title Menu $\leftrightarrow$ Game transitions with automated memory freeing in 24 MB RAM | `menu_banner.png`, `player.png`, synthesized `click.wav` |
| **Collisions** | [`demos/demo_collisions/`](demos/demo_collisions/) | Solid AABB obstacles (`rect_rect`) and collectible coins (`rect_circle`) with audio chime | `box_player.png`, `coin_item.png`, synthesized `collect.wav` |

Run any demo in seconds:
```bash
cd demos/demo_anim_2d
psp-forge build
psp-forge run
```

---

## 📚 Tutorials & Guides

* ⚙️ **[Installation & Environment Setup](docs/INSTALLATION.md)**: Setting up PSPDEV, building the runtime, and configuring PPSSPP.
* 🛡️ **[2D Game Tutorial: "Hero Starter"](docs/TUTORIAL_2D.md)**: Complete guide to 2D sprites, controller edge detection, screen clamping, and audio.
* 🏎️ **[3D Game Tutorial: "Track Runner"](docs/TUTORIAL_3D.md)**: 3D meshes, UV texturing, perspective projection, camera physics, and gravity.
* 🏃 **[Tutorial: 2D Animation & Spritesheets](docs/TUTORIAL_ANIMATION_2D.md)**: Using `ForgeSpriteAnim` for flipbook animations.
* 💎 **[Tutorial: 3D Animation & Procedural Motion](docs/TUTORIAL_ANIMATION_3D.md)**: Mathematical transforms, floating oscillations, and rotations.
* 🎬 **[Tutorial: Scene Management (`ForgeScene`)](docs/TUTORIAL_SCENES.md)**: Designing multi-screen games while preventing RAM leaks.
* 💥 **[Tutorial: Bounding Boxes & Collisions](docs/TUTORIAL_COLLISIONS.md)**: Implementing 2D and 3D collision detection.
* 🎨 **[Asset Pipeline & Format Specification](docs/ASSET_PIPELINE.md)**: Deep dive into swizzling, POT scaling, `.p3d`, `.snd`, and hardware budgets.
* 🕹️ **[C99 API Reference (`psp_forge.h`)](docs/API.md)**: Comprehensive reference for all functions, structures, and callbacks.

---

## 🎮 Real Hardware Deployment

Every PSP-Forge project compiles with the `BUILD_PRX` directive enabled, ensuring seamless operation on both PPSSPP and physical PSP hardware running Custom Firmware (CFW):

1. Connect your PSP via USB or insert your Memory Stick Duo into your computer.
2. Copy the project folder containing `EBOOT.PBP` and its `assets/` directory to:
   ```text
   ms0:/PSP/GAME/my_game/
   ├── EBOOT.PBP
   └── assets/
       ├── icon0.png
       ├── pic1.png
       └── [cooked .tex, .p3d, .snd files...]
   ```
3. Disconnect USB and launch your game from the PSP XMB under **Game → Memory Stick**!

---

## 🤝 Open to Contributions

Contributions from the homebrew community are welcome and encouraged! Whether you are an experienced embedded C hacker, a tool developer, an artist, or a game designer, there are many ways to get involved:

- **New Asset Cookers**: Add support for font atlas generation (`.fnt`/TrueType), compressed tracker music (MOD, XM, IT via `libmodplug`), or compressed textures.
- **Engine Capabilities**: Implement particle systems, tilemap rendering engines, or VFPU-accelerated matrix transforms.
- **Showcase Games & Templates**: Submit new gameplay templates (e.g., Shmup, Platformer, RPG) or complete open-source demo games.
- **Hardware Compatibility Testing**: Test and report performance across different PSP models (PSP-1000, 2000, 3000, PSP Go, and PSP Street / E1000).
- **Bug Reports & Improvements**: Open an issue or submit a pull request on GitHub!

---

## 🙏 Thanks & Acknowledgments

PSP-Forge stands on the shoulders of giants. We express our deepest appreciation to:

* **[The PSPDEV Organization](https://github.com/pspdev)** & **[pspdev.github.io](https://pspdev.github.io/)**: For maintaining and preserving the modern open-source PSP development ecosystem, including [`psptoolchain`](https://github.com/pspdev/psptoolchain), [`psptoolchain-allegrex`](https://github.com/pspdev/psptoolchain-allegrex), [`pspsdk`](https://github.com/pspdev/pspsdk), and [`psp-packages`](https://github.com/pspdev/psp-packages).
* **nem**: For pioneering PSP homebrew with the original Hello World and reverse-engineering the foundational system call imports.
* **The PSP Homebrew Community**: Everyone on the [PSP Homebrew Discord](https://discord.gg/bePrj9W) (`#psp-toolchain`) whose passion keeps the PSP scene thriving decades after the console's release.
* **Henrik Rydgård & the [PPSSPP Team](https://www.ppsspp.org/)**: For creating the gold standard in PSP emulation, enabling rapid iteration and debugging.
* **The Authors of Unofficial Hardware Documentation**: Including the authors of *Yet Another PSP Documentation (YAPSPD)*, the *uofw* team, and the *Allegrex VFPU* documentation maintainers.

---

## 🏷️ SEO, Discovery & LLM Metadata

### GitHub Topic Tags
> `psp` `pspdev` `pspsdk` `sony-psp` `playstation-portable` `homebrew` `game-engine` `vibe-coding` `vibe-coded` `ai-coding` `mips` `allegrex` `c99` `retro-gaming` `gamedev` `asset-pipeline` `texture-swizzling` `edram` `ppsspp` `embedded-systems` `cmake`

### LLM Indexing Summary Card
```yaml
project: psp-forge
type: homebrew-game-development-suite-and-c99-micro-engine
target_hardware: Sony PlayStation Portable (MIPS Allegrex R4000 @ 333MHz, 24MB RAM, 2MB eDRAM)
programming_languages: [C99, Python 3.11, CMake]
architecture_features:
  - Zero-allocation static 2MB VRAM layout (Draw, Display, Depth, Texture scratchpad)
  - 16x8 block texture swizzling to prevent GE cache line stalls
  - Power-of-two texture padding up to 512x512 with CLUT4/CLUT8 quantization
  - Compact .p3d vertex streaming with precomputed AABB bounds
  - High-priority 44.1kHz PCM audio thread with double 2048-sample buffers
  - Lightweight AABB, Sphere, Rect, and Circle collision detection
  - Multi-scene lifecycle architecture (ForgeScene on_init, on_update, on_draw, on_destroy)
  - 100% vibe-coded via human-directed AI pair programming
toolchain_dependencies: [pspdev, psp-gcc, psp-cmake, pspsdk, libgu, libgum]
license: MIT
```

---

## 📄 License

PSP-Forge is distributed under the [MIT License](LICENSE).
Sony, PlayStation Portable, and PSP are registered trademarks of Sony Interactive Entertainment Inc. This project is an independent open-source tool and is not affiliated with or endorsed by Sony.
