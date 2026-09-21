# Complete Environment Setup Guide for PSP-Forge 🛠️

This guide walks you step-by-step through setting up a complete Linux development workstation for the **Sony PlayStation Portable (PSP)** from scratch. By following this guide, you will be able to write code, cook assets, compile native MIPS binaries, and run games both in the **PPSSPP** emulator and on real physical PSP consoles (**PSP-1000/2000/3000/Go/Street** with Custom Firmware).

---

## Table of Contents
1. [Prerequisites & System Dependencies](#1-prerequisites--system-dependencies)
2. [Installing the MIPS Toolchain (`psptoolchain`)](#2-installing-the-mips-toolchain-psptoolchain)
3. [Installing and Integrating PSPSDK](#3-installing-and-integrating-pspsdk)
4. [Configuring Environment Variables & PATH](#4-configuring-environment-variables--path)
5. [Downloading and Setting Up PSP-Forge](#5-downloading-and-setting-up-psp-forge)
6. [Compiling & Installing the PSP-Forge Runtime (`libpspforge`)](#6-compiling--installing-the-psp-forge-runtime-libpspforge)
7. [Installing the PPSSPP Emulator](#7-installing-the-ppsspp-emulator)
8. [End-to-End Verification: Create, Build & Run Your First Game](#8-end-to-end-verification-create-build--run-your-first-game)

---

## 1. Prerequisites & System Dependencies

To compile the cross-toolchain (`psp-gcc`, `binutils`, `newlib`) and packaging utilities from source, install the necessary host development packages for your distribution:

### Ubuntu / Debian / Linux Mint / Pop!_OS
```bash
sudo apt update
sudo apt install -y \
  build-essential \
  autoconf \
  automake \
  bison \
  flex \
  cmake \
  git \
  python3 \
  python3-pip \
  libusb-dev \
  libreadline-dev \
  libmpfr-dev \
  libgmp-dev \
  libmpc-dev \
  libarchive-dev \
  libcurl4-openssl-dev \
  libelf-dev \
  libssl-dev \
  libncurses-dev \
  zlib1g-dev \
  tcl \
  gettext \
  wget \
  curl \
  texinfo \
  subversion \
  ffmpeg
```

### Arch Linux / Manjaro
```bash
sudo pacman -Syu --needed \
  base-devel \
  autoconf \
  automake \
  bison \
  flex \
  cmake \
  git \
  python \
  python-pip \
  libusb \
  readline \
  mpfr \
  gmp \
  libmpc \
  libarchive \
  curl \
  openssl \
  ncurses \
  zlib \
  tcl \
  gettext \
  wget \
  texinfo \
  ffmpeg
```

### Fedora / RHEL
```bash
sudo dnf groupinstall -y "Development Tools"
sudo dnf install -y \
  autoconf automake bison flex cmake git python3 python3-pip \
  libusb-devel readline-devel mpfr-devel gmp-devel libmpc-devel \
  libarchive-devel libcurl-devel elfutils-libelf-devel openssl-devel \
  ncurses-devel zlib-devel tcl gettext wget curl texinfo ffmpeg
```

---

## 2. Installing the MIPS Toolchain (`psptoolchain`)

The open-source [`psptoolchain`](https://github.com/pspdev/psptoolchain) maintained by the PSPDEV organization provides scripts to download, configure, build from source, and install:
- **`binutils`**: MIPS Allegrex assembler (`psp-as`), linker (`psp-ld`), and binary inspection tools (`psp-objcopy`, `psp-readelf`).
- **`gcc`**: The cross-compiler targeting the MIPS Allegrex R4000 CPU (`psp-gcc`, `psp-g++`).
- **`newlib`**: Optimized embedded C standard library.

### A. Prepare the Destination Directory
The standard target path for the PSP toolchain is `/usr/local/pspdev`.  
Create this directory and grant write ownership to your current user:
```bash
sudo mkdir -p /usr/local/pspdev
sudo chown -R $USER:$USER /usr/local/pspdev
```

### B. Clone and Run `toolchain.sh`
Clone the official repository:
```bash
git clone https://github.com/pspdev/psptoolchain.git
cd psptoolchain
```

Execute the automated build script:
```bash
./toolchain.sh
```

> [!NOTE]
> Compiling GCC and support libraries from source typically takes between 10 and 25 minutes depending on CPU core count.

---

## 3. Installing and Integrating PSPSDK

### What is PSPSDK?
**PSPSDK** is the foundational Software Development Kit for PSP homebrew. It provides:
- Hardware headers (`<pspkernel.h>`, `<pspgu.h>`, `<pspaudio.h>`, `<pspctrl.h>`, etc.) mapping Sony hardware coprocessors.
- User-mode system stub libraries (`libpspuser.a`, `libpspgu.a`, `libpspaudio.a`, etc.).
- Packaging and binary transformation utilities:
  - `mksfoex`: Generates `PARAM.SFO` metadata with the game title and firmware minimum version.
  - `pack-pbp`: Combines `PARAM.SFO`, XMB artwork (`ICON0.PNG`, `PIC1.PNG`), and PRX binary into a bootable `EBOOT.PBP`.
  - `prxgen` and `psp-fixup-imports`: Convert the compiled MIPS ELF into a relocatable PRX module and resolve import stub tables.
  - `psp-cmake`: Pre-configured CMake toolchain wrapper for cross-compiling PSP projects.

### How it is Installed
Modern releases of `psptoolchain` automatically execute `006-pspsdk.sh` as part of `./toolchain.sh`.  
Upon completion, PSPSDK is placed directly into:
```text
/usr/local/pspdev/psp/sdk/
├── include/     # PSPSDK headers
├── lib/         # User libraries, prxspecs, and linkfile.prx
└── bin/         # Packaging binaries (pack-pbp, prxgen, mksfoex, psp-cmake)
```

*(If you ever need to manually recompile or update PSPSDK alone, you can clone `https://github.com/pspdev/pspsdk.git`, run `./bootstrap && ./configure --with-pspdev=/usr/local/pspdev && make && sudo make install`).*

---

## 4. Configuring Environment Variables & PATH

To allow your terminal, CMake, and build scripts to locate `psp-gcc`, `psp-cmake`, and headers, export the environment variables in your shell configuration file (`~/.bashrc` for Bash or `~/.zshrc` for Zsh).

Open `~/.bashrc` in your editor:
```bash
nano ~/.bashrc
```

Append the following lines:
```bash
# ==========================================
# Sony PSP Development Environment (PSPDEV)
# ==========================================
export PSPDEV="/usr/local/pspdev"
export PATH="$PATH:$PSPDEV/bin"
export PSPSDK="$PSPDEV/psp/sdk"
export PATH="$PATH:$PSPSDK/bin"
```

Save the file and reload your shell session:
```bash
source ~/.bashrc
```

### Verify Toolchain Setup
Check that all cross-compilation binaries are in your `PATH`:
```bash
psp-gcc --version
psp-config --pspsdk-path
psp-cmake --version
```
Each command should output its version and valid paths (e.g. `/usr/local/pspdev/psp/sdk`).

---

## 5. Downloading and Setting Up PSP-Forge

PSP-Forge combines:
1. The **C99 Micro-Engine** (`libpspforge.a`) pre-tuned for 60 FPS with zero per-frame runtime allocations and deterministic VRAM layout.
2. The **CLI Orchestrator** (`psp-forge`) for project initialization, asset cooking, building, and running.
3. The **Hardware-Aware Asset Cooker** for converting PNGs to $16 \times 8$ swizzled `.tex` textures, Wavefront OBJs to 16-byte aligned DMA `.p3d` meshes, and audio to 44.1 kHz signed 16-bit PCM `.snd` buffers.

### A. Install Python Dependencies
PSP-Forge requires Python 3.10+ and `Pillow`:
```bash
pip3 install --user Pillow
```

### B. Clone PSP-Forge
```bash
git clone https://github.com/hauntlight/psp-forge.git
cd psp-forge
```

---

## 6. Compiling & Installing the PSP-Forge Runtime (`libpspforge`)

To allow any PSP project to link with `-lpspforge` and include `#include <psp_forge.h>`, compile the C99 runtime and install it into the system SDK directory (`$PSPDEV/psp/`).

### Recommended Method: `psp-cmake`
From the root of the `psp-forge` repository:
```bash
cd runtime
psp-cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PSPDEV/psp
cmake --build build -j$(nproc)
sudo cmake --install build
cd ..
```

### Alternative Method: Direct `psp-gcc` Compilation
```bash
cd runtime
psp-gcc -Iinclude -I$PSPDEV/psp/include -I$PSPDEV/psp/sdk/include \
  -O3 -G0 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
  -Wall -Wextra -Wno-unused-parameter \
  -c src/*.c
psp-ar rcs libpspforge.a *.o

sudo cp include/psp_forge.h $PSPDEV/psp/include/psp_forge.h
sudo cp libpspforge.a $PSPDEV/psp/lib/libpspforge.a
rm -f *.o libpspforge.a
cd ..
```

### Setting Up the Global `psp-forge` CLI
Create a symbolic link in `/usr/local/bin` so you can call `psp-forge` anywhere:
```bash
# Make launcher executable
chmod +x bin/psp-forge

# Link to /usr/local/bin
sudo ln -sf "$(pwd)/bin/psp-forge" /usr/local/bin/psp-forge
```

Verify the CLI is accessible:
```bash
psp-forge --help
```

---

## 7. Installing the PPSSPP Emulator

To test your builds instantly on your PC before transferring them to physical hardware, install the **PPSSPP** emulator.

### Option A: Flatpak (Recommended for Linux)
```bash
flatpak install flathub org.ppsspp.PPSSPP
```
`psp-forge run` automatically detects and launches Flatpak-installed PPSSPP.

### Option B: Native Package Manager
- **Ubuntu / Debian**:
  ```bash
  sudo apt install -y ppsspp
  ```
- **Arch Linux**:
  ```bash
  sudo pacman -S ppsspp
  ```
- **Fedora**:
  ```bash
  sudo dnf install -y ppsspp
  ```

### Option C: Standalone AppImage
Download the official AppImage from [ppsspp.org](https://www.ppsspp.org/download/), make it executable, and move it to `/usr/local/bin`:
```bash
chmod +x PPSSPP.AppImage
sudo mv PPSSPP.AppImage /usr/local/bin/ppsspp
```

---

## 8. End-to-End Verification: Create, Build & Run Your First Game

Now test your complete environment end-to-end:

```bash
# 1. Initialize a new 2D project skeleton
psp-forge init my_first_game --template 2d
cd my_first_game

# 2. Cook assets (PNG/WAV -> .tex/.snd with budget validation)
psp-forge cook

# 3. Compile MIPS binary and package EBOOT.PBP
psp-forge build

# 4. Launch in the PPSSPP emulator
psp-forge run
```

### Transferring to Real PSP Hardware (PSP-1000/2000/3000/Go/Street)
Every project built with PSP-Forge compiles in pure User Mode (`BUILD_PRX`) without kernel dependencies, guaranteeing stability on Custom Firmware (e.g., 6.61 PRO-C or ME):

1. Connect your PSP via USB or insert your Memory Stick Duo into your computer.
2. Copy the project folder containing `EBOOT.PBP` and its `assets/` directory to:
   ```text
   ms0:/PSP/GAME/my_first_game/
   ├── EBOOT.PBP
   └── assets/
       ├── icon0.png
       ├── pic1.png
       ├── hero.tex
       └── coin.snd
   ```
3. Safely disconnect USB and launch your game from the PSP XMB menu under **Game → Memory Stick**!
