# Complete Installation & Setup Guide for PSP-Forge 🛠️

This guide describes the complete setup process for PlayStation Portable (PSP) development on Linux (Ubuntu, Debian, Fedora, Arch, and derivatives), from installing the official **PSPSDK** toolchain to configuring the **PSP-Forge** suite and the **PPSSPP** emulator.

---

## 1. System Prerequisites

### Base Packages
Ensure you have the essential build tools, Git, CMake, and Python:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  git \
  python3 \
  python3-pip \
  libusb-dev \
  libreadline-dev \
  ffmpeg
```

### Required Python Modules
PSP-Forge requires Python 3.10+ and the `Pillow` library for image and texture processing:
```bash
pip install Pillow
```

---

## 2. Installing the PSPSDK Toolchain (`pspdev`)

Compiling C code for the PSP's MIPS Allegrex architecture requires the cross-compiler `psp-gcc`.

### Recommended Standard Path: `/usr/local/pspdev`

If you downloaded or built a precompiled toolchain, extract it to `/usr/local/pspdev`:
```bash
sudo mkdir -p /usr/local/pspdev
sudo chown -R $USER:$USER /usr/local/pspdev
# (Extract toolchain binaries into /usr/local/pspdev)
```

### Environment Variables (`~/.bashrc` or `~/.zshrc`)
Add the required environment variables to your shell configuration file:

```bash
# Sony PSP Toolchain Environment Variables
export PSPDEV="/usr/local/pspdev"
export PATH="$PSPDEV/bin:$PATH"
```

Reload your terminal session with:
```bash
source ~/.bashrc
```

Verify that the toolchain is working properly:
```bash
psp-gcc --version
psp-config --pspsdk-path
```

---

## 3. Building and Installing the PSP-Forge Runtime (`libpspforge`)

PSP-Forge includes the high-performance C99 micro-runtime `libpspforge.a`, managing deterministic VRAM, GPU display lists, multithreaded audio, input, collisions, animations, and scene states.

Navigate to the runtime directory and compile the static library:

```bash
cd support_toolchain/psp-forge/runtime

# Compile all C source modules
psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/core.c -o src/core.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/vram.c -o src/vram.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/video2d.c -o src/video2d.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/video3d.c -o src/video3d.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/input.c -o src/input.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/audio.c -o src/audio.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/physics.c -o src/physics.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/anim.c -o src/anim.o

psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/scene.c -o src/scene.o

# Create static library archive
psp-ar rcs libpspforge.a src/*.o

# Install headers and library into toolchain system directories
sudo cp include/psp_forge.h $PSPDEV/psp/include/psp_forge.h
sudo cp libpspforge.a $PSPDEV/psp/lib/libpspforge.a
```

---

## 4. Installing the `psp-forge` CLI

To invoke `psp-forge` globally from any working directory:

```bash
cd support_toolchain/psp-forge

# Make launcher executable
chmod +x bin/psp-forge

# Create symlink in /usr/local/bin
sudo ln -sf "$(pwd)/bin/psp-forge" /usr/local/bin/psp-forge
```

Test the installation by running:
```bash
psp-forge --help
```

---

## 5. Configuring the PPSSPP Emulator

### Installing PPSSPP
PPSSPP can be installed via your system package manager, Flatpak, or by downloading the official standalone AppImage:

```bash
# Option A: Via Flatpak (recommended for modern Linux distributions)
flatpak install flathub org.ppsspp.PPSSPP

# Option B: Standalone AppImage
# Download the official AppImage from ppsspp.org and place it in your PATH:
chmod +x PPSSPP.AppImage
sudo mv PPSSPP.AppImage /usr/local/bin/ppsspp
```

`psp-forge run` will automatically search for `ppsspp`, `PPSSPPQt`, or the Flatpak installation in your system. Alternatively, you can explicitly pass the emulator path with:
```bash
psp-forge run --emulator /path/to/your/ppsspp
```

---

## 6. End-to-End Workflow Verification

You can now create, build, and test a game in 3 simple commands:

```bash
# 1. Create a project from template
psp-forge init test_game --template 2d
cd test_game

# 2. Cook assets and compile EBOOT.PBP executable
psp-forge build

# 3. Launch in emulator
psp-forge run
```
