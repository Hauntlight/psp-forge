# Guida Ufficiale Asset Pipeline (`The Cooker`) ⚔️

La GPU e la CPU della Sony PSP (MIPS R4000 Allegrex) hanno caratteristiche architetturali molto particolari:
- **eDRAM interna di soli 2 MB** ad altissima banda.
- **Cache miss gravosi**: il campionamento lineare delle texture in memoria convenzionale rallenta il fillrate.
- **Allineamento a 16 byte obbligatorio** per i vertici inviati al GE (Graphics Engine) tramite DMA.
- **Supporto audio nativo in blocchi da 64 campioni** a 44100 Hz PCM 16-bit signed little-endian.

La suite **PSP-Forge** include il compilatore di asset (`psp-forge cook`) per convertire automaticamente i file standard di modellazione e grafica (PNG, JPG, OBJ, WAV, MP3) in formati binari pronti per l'hardware:

| Formato Sorgente Standard | Formato Cooked PSP | Modulo Cooker | Ottimizzazioni Hardware Eseguite |
|---|---|---|---|
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga` | **`.tex`** (PSP Texture) | `cli/cookers/texture.py` | Swizzling $16 \times 8$ byte, POT Padding ($2^n \le 512$), allineamento PSM hardware |
| Wavefront `.obj` | **`.p3d`** (PSP 3D Mesh) | `cli/cookers/mesh.py` | Normali, UV capovolte, triangolazione, AABB Bounding Box, allineamento 16-byte |
| `.wav`, `.mp3`, `.ogg`, `.flac` | **`.snd`** (PSP Sound) | `cli/cookers/audio.py` | Resampling lineare 44.1kHz, PCM S16-LE, allineamento a blocchi di 64 campioni |

---

## 1. Texture: Da PNG / JPG a `.tex`

### Cos'è il Texture Swizzling?
In un'immagine normale (lineare), i pixel sono salvati riga per riga da sinistra a destra. Quando la GPU della PSP campiona texture su superfici inclinate o in 3D, deve saltare continuamente da una riga all'altra della RAM, generando continui cache miss.  
Lo **swizzling** riorganizza la sequenza di byte in blocchetti rettangolari da **$16 \times 8$ byte**: i pixel vicini nello spazio 2D si trovano così contigui anche nella memoria fisica.

### Come funziona la conversione:
1. **Dimensioni Potenza di Due (POT)**: La PSP richiede texture con dimensioni potenze di due ($16, 32, 64, 128, 256, 512$). Il cooker calcola la potenza di due minima superiore e applica il padding trasparente automatico.
2. **Formati Pixel Supportati (Pixel Storage Mode - PSM)**:
   - `8888` / `rgba8888` (`GU_PSM_8888 = 3`): 32-bit RGBA (massima qualità).
   - `5551` / `rgba5551` (`GU_PSM_5551 = 1`): 16-bit RGBA (1 bit per la trasparenza on/off).
   - `4444` / `rgba4444` (`GU_PSM_4444 = 2`): 16-bit RGBA (trasparenza a 16 livelli).
   - `5650` / `rgb5650`  (`GU_PSM_5650 = 0`): 16-bit RGB senza trasparenza (ideale per sfondi e cielo).

### Utilizzo rapido da riga di comando:
```bash
# Tramite l'orchestratore del progetto:
psp-forge cook

# Oppure cuocendo una singola immagine:
python3 -c "
from cli.cookers.texture import cook_texture
cook_texture('assets/hero.png', 'build/assets/hero.tex', format_type='8888', swizzle=True)
"
```

### Caricamento e rendering nel codice C:
```c
ForgeTexture* tex = forge_texture_load("assets/hero.tex");

// Disegno 2D immediato (pixel coord):
forge_draw_sprite(tex, screen_x, screen_y, width, height, tex_u, tex_v, tex_w, tex_h);

// Rilascio alla chiusura:
forge_texture_free(tex);
```

---

## 2. Modelli 3D: Da Wavefront `.obj` a `.p3d`

### Struttura Vertici Richiesta dalla PSP:
Il Graphics Engine della PSP si aspetta che i componenti di ogni vertice siano disposti nell'ordine rigoroso:
Texture (U, V) -> Colore -> Normali (NX, NY, NZ) -> Posizione (X, Y, Z)

Il cooker `mesh.py`:
1. Legge le coordinate di vertici `v`, texture `vt` e normali `vn`.
2. Se il modello non ha normali, le calcola automaticamente tramite prodotto vettoriale.
3. Converte l'asse V delle UV (invertendo 1.0 - V) per uniformarlo allo standard del GE.
4. Triangola le facce (anche quadrangolari o N-goni) con ventaglio di triangoli (*triangle fan*).
5. Calcola la **Bounding Box AABB** (`aabb_min`, `aabb_max`, centro e raggio di culling).
6. Scrive i vertici con stride di 32 byte, perfettamente allineati a 16 byte per il DMA hardware.

### Esportazione corretta da Blender / Maya:
Quando esporti un `.obj` da Blender:
- Seleziona **Triangulate Faces** (o lascia che lo faccia il cooker).
- Spunta **Write Normals** e **Include UVs**.
- Orientamento: Forward `-Z`, Up `+Y`.

### Utilizzo:
```bash
python3 -c "
from cli.cookers.mesh import cook_mesh
cook_mesh('assets/track.obj', 'build/assets/track.p3d')
"
```

### Caricamento e rendering nel codice C:
```c
ForgeMesh* mesh = forge_mesh_load("assets/track.p3d");
ForgeTexture* tex = forge_texture_load("assets/track.tex");

// Disegno nello spazio 3D (posizione x,y,z, rotazioni in radianti, scala):
forge_draw_mesh(mesh, tex, 0.0f, 0.0f, pos_z, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

// Rilascio:
forge_mesh_free(mesh);
```

---

## 3. Audio: Da WAV / MP3 / OGG a `.snd`

### Vincoli del Chip Audio PSP:
Il chip audio della PSP richiede campioni a **44.1 kHz signed 16-bit** con buffer multipli di **64 campioni**.

Il cooker `audio.py`:
- Supporta file non compressi `.wav`.
- Se è installato `ffmpeg`, converte automaticamente anche `.mp3`, `.ogg`, `.flac`, `.m4a`.
- Applica ricampionamento bilineare se la frequenza sorgente differisce da 44100 Hz.
- Normalizza e allinea la lunghezza finale al multiplo di 64 campioni più vicino con padding a zero (silenzio).

### Caricamento e riproduzione nel codice C:
```c
ForgeSound* sound = forge_sound_load("assets/jump.snd");

// Riproduzione immediata nel thread audio dedicato:
forge_sound_play(sound, 0); // secondo parametro: 0 = una tantum, 1 = loop continuo

// Rilascio:
forge_sound_free(sound);
```

---

## 4. Packaging Eseguibile e Compatibilità Hardware (`EBOOT.PBP`)

Per garantire che il file compilato funzioni **sia sull'emulatore PPSSPP sia sulla PSP reale**, è fondamentale che nel file `CMakeLists.txt` sia presente la direttiva `BUILD_PRX` all'interno della funzione `create_pbp_file()`:

```cmake
create_pbp_file(
    TARGET mio_gioco
    TITLE "Mio Gioco PSP"
    BUILD_PRX
    ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png"
    BACKGROUND_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/pic1.png"
)
```

Inoltre, all'inizio del `main()` in C:
```c
int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }
    forge_init(0);
    ...
```
Questo consente all'engine di determinare esattamente se l'applicazione sta girando da `ms0:/PSP/GAME/<nome_cartella>` o dall'ambiente di sviluppo, risolvendo gli asset in modo trasparente e infallibile.
