# PSP-Forge ⚔️
> Modern Dev Suite & Micro-Engine per Sony PlayStation Portable (PSP)

PSP-Forge è un toolkit completo progettato per modernizzare, velocizzare e semplificare lo sviluppo di homebrew e giochi per Sony PSP su sistemi Linux e multipiattaforma.

Il framework unifica in un'unica interfaccia a riga di comando l'orchestrazione del progetto, una pipeline automatica di compilazione delle risorse (Asset Cooker) e un micro-runtime in C99 ottimizzato per l'architettura MIPS Allegrex e l'eDRAM della console.

---

## 🚀 Caratteristiche Principali

* **CLI Orchestrator (`psp-forge`):**
  * `init`: Genera istantaneamente lo scheletro di un progetto (2D o 3D) con configurazione CMake e supporto IDE (include path automatici per VSCode/Clangd).
  * `cook`: Compila e ottimizza automaticamente texture, modelli 3D e tracce audio.
  * `build`: Cross-compilazione veloce nativa MIPS e packaging del file `EBOOT.PBP`.
  * `run`: Avvio diretto su emulatore PPSSPP o notifica path.
  * `clean`: Pulizia delle cache e dei file intermedi di compilazione.

* **Asset Pipeline ("The Cooker"):**
  * **Texture Swizzling:** Riordino automatico dei pixel a blocchi di $16 \times 8$ byte per eliminare i cache miss della GPU PSP.
  * **POT Padding & Formati:** Normalizzazione a potenze di due ($2^n \le 512$), supporto RGBA8888, RGBA5551, RGBA4444 e quantizzazione CLUT a 8-bit (256 colori) e 4-bit (16 colori).
  * **3D Geometry Packer:** Conversione di modelli Wavefront `.obj` nel formato compatto `.p3d` con vertici allineati a 16 byte e calcolo bounding box AABB.
  * **Audio Transcoder:** Conversione automatica in PCM stereo/mono signed 16-bit a 44100 Hz con blocchi allineati a 64 campioni.

* **Micro-Runtime C99 (`libpspforge`):**
  * **Zero Allocation a Runtime:** Allocazione statica deterministica della VRAM da 2 MB (Draw buffer, Display buffer, Z-buffer e Texture scratchpad).
  * **Gestione Hardware Trasparente:** Display List allineate a 16 byte e flush automatico della D-Cache (`sceKernelDcacheWritebackRange`).
  * **Pipeline 2D & 3D:** Wrapper per sprite batching e matrici `pspgum`, con culling dinamico per assegnare ai 4 slot hardware (`GU_LIGHT0..3`) le luci più vicine.
  * **Input con Edge Detection:** Rilevamento di pulsanti premuti (*pressed*), rilasciati (*released*) e tenuti premuti (*held*), con stick analogico normalizzato in $[-1.0, 1.0]$.
  * **Audio Multithread:** Thread audio dedicato ad alta priorità (`0x12`) con doppio buffer PCM da 2048 campioni per non penalizzare il framerate grafico.

---

## 📦 Struttura del Progetto

```text
psp-forge/
├── bin/
│   └── psp-forge              # Launcher eseguibile da terminale
├── cli/
│   ├── psp_forge.py           # Core CLI Orchestrator
│   ├── config.py              # Parser psp.toml
│   ├── cookers/               # Moduli di compilazione asset
│   │   ├── texture.py         # Swizzler, POT padding, CLUT
│   │   ├── mesh.py            # OBJ parser -> .p3d vertex buffer
│   │   └── audio.py           # Audio WAV PCM transcoder
│   └── templates/             # Progetti base per psp-forge init
│       ├── 2d_starter/
│       └── 3d_runner/
├── runtime/                   # libpspforge (Micro-Engine C99)
│   ├── include/
│   │   └── psp_forge.h        # API pubblica
│   ├── src/
│   │   ├── core.c             # GU Init, DisplayList loop, Callback HOME
│   │   ├── vram.c             # Layout VRAM 2MB
│   │   ├── video2d.c          # Sprite e texture swizzlate
│   │   ├── video3d.c          # Mesh 3D, matrici e 4 luci HW
│   │   ├── input.c            # Controller differenziale
│   │   └── audio.c            # Thread PCM prioritario
│   └── CMakeLists.txt
├── docs/                      # Documentazione tecnica e guide API
└── tests/                     # Suite di test unitari
```

---

## 🛠️ Prerequisiti & Setup

### 1. Toolchain PSPSDK
Assicurati che `PSPDEV` sia impostato e presente nel tuo `PATH`:
```bash
export PSPDEV="/usr/local/pspdev"
export PATH="$PATH:$PSPDEV/bin"
```
Verifica con:
```bash
psp-config --pspsdk-path
```

### 2. Dipendenze Python
* Python 3.11+ (modulo standard `tomllib`)
* `Pillow` per l'elaborazione immagini (`pip install Pillow`)

---

## ⚡ Guida Rapida

### 1. Crea un nuovo progetto
```bash
./bin/psp-forge init my_game --template 2d
cd my_game
```

### 2. Compila le risorse grafiche e audio
```bash
psp-forge cook
```

### 3. Compila il binario per PSP (`EBOOT.PBP`)
```bash
psp-forge build
```

### 4. Esegui su PPSSPP
```bash
psp-forge run
```

---

## 📚 Guide & Documentazione Approfondita

* ⚙️ **[Guida all'Installazione & Configurazione](docs/INSTALLATION.md)**: Setup da zero della toolchain PSPSDK (`pspdev`), compilazione del runtime `libpspforge`, installazione della CLI e configurazione di PPSSPP.
* 🛡️ **[Tutorial 2D: "Hero Starter" da Zero](docs/TUTORIAL_2D.md)**: Guida passo per passo alla creazione del gioco 2D (sprite RGBA, audio, edge detection dei tasti e limiti schermo).
* 🏎️ **[Tutorial 3D: "Track Runner" da Zero](docs/TUTORIAL_3D.md)**: Guida completa al 3D (modello geometrico OBJ, texture mapping, telecamera in prospettiva, salto e corsa infinita a 60 FPS).
* 🏃 **[Tutorial: Animazione 2D (Spritesheet & Flipbook)](docs/TUTORIAL_ANIMATION_2D.md)**: Gestione delle animazioni 2D con `ForgeSpriteAnim` (demo: `demos/demo_anim_2d`).
* 💎 **[Tutorial: Animazione 3D (Procedurale & Gerarchica)](docs/TUTORIAL_ANIMATION_3D.md)**: Animazioni matriciali, oscillazioni armoniche e rotazioni continue (demo: `demos/demo_anim_3d`).
* 🎬 **[Tutorial: Gestione Scene con `ForgeScene`](docs/TUTORIAL_SCENES.md)**: Architettura multi-scena con caricamento e rilascio controllato della memoria da 24 MB (demo: `demos/demo_scenes`).
* 💥 **[Tutorial: Bounding Boxes & Collisioni](docs/TUTORIAL_COLLISIONS.md)**: Rilevamento collisioni 2D (`ForgeRect`, `ForgeCircle`) e 3D (`ForgeAABB`, `ForgeSphere`) (demo: `demos/demo_collisions`).
* 🎨 **[Asset Pipeline & Formati Multimediali (Cooker)](docs/ASSET_PIPELINE.md)**: Come convertire file comuni (**PNG, JPG, Wavefront OBJ, WAV, MP3**) nei formati binari nativi ad alte prestazioni della console (`.tex`, `.p3d`, `.snd`) con texture swizzling, POT padding e warning sui limiti hardware.
* 🕹️ **[API C99 Runtime (`libpspforge`)](docs/API.md)**: Riferimento completo su inizializzazione hardware, VRAM, ciclo di rendering 2D/3D, audio multithread, collisioni, animazioni e scene.

---

## 🕹️ Demo Pronte all'Uso (`demos/`)

Il repository include 4 demo complete con asset generati esenti da copyright:
1. **`demos/demo_anim_2d/`**: Personaggio animato che cammina a 60 FPS con D-Pad/Stick e spritesheet flipbook.
2. **`demos/demo_anim_3d/`**: Cristallo fluttuante con oscillazione armonica e telecamera orbitale a 360°.
3. **`demos/demo_scenes/`**: Sistema multi-scena completo (Menu Principale $\rightarrow$ Livello di Gioco $\rightarrow$ Ritorno al menu con Select).
4. **`demos/demo_collisions/`**: Rilevamento collisioni con ostacoli solidi e raccolta monete con trigger sonoro.

Per compilare ed eseguire qualsiasi demo:
```bash
cd demos/demo_anim_2d
psp-forge build
psp-forge run
```

---

## 🎮 Distribuzione su PSP Reale

I template generati includono la direttiva `BUILD_PRX` in `CMakeLists.txt` per garantire compatibilità immediata sia con PPSSPP che con console reale dotata di Custom Firmware (CFW):
1. Copia la cartella del progetto contenente `EBOOT.PBP` e la sottocartella `assets/` sulla Memory Stick della console:
   ```text
   ms0:/PSP/GAME/mio_gioco/
   ├── EBOOT.PBP
   └── assets/
       ├── icon0.png
       ├── pic1.png
       └── [file .tex, .p3d, .snd...]
   ```
2. Avvia il gioco direttamente dal menu *Gioco → Memory Stick* della PSP!

---

## 📄 Licenza
Rilasciato sotto licenza [MIT](LICENSE).
