# Tutorial: Animazione 2D con Spritesheet & Flipbook (`ForgeSpriteAnim`) 🏃

Questo tutorial spiega come realizzare animazioni 2D fluide a 60 FPS su PlayStation Portable partendo da un unico spritesheet di fotogrammi fino al rendering hardware ottimizzato con la componente `ForgeSpriteAnim` di PSP-Forge.

La demo completa e funzionante associata a questa guida si trova in:
`demos/demo_anim_2d/`

---

## 1. Come Funzionano le Animazioni 2D sulla PSP

Invece di caricare singoli file di immagini per ogni frame di un'animazione (che causerebbero continue allocazioni in RAM e cambi di stato texture `sceGuTexImage` rallentando il rendering), su PSP si utilizza uno **Spritesheet** (un'unica immagine contenente tutti i frame allineati in griglia orizzontale o verticale).

### Requisiti Architetturali:
1. **Dimensioni POT (Power of Two)**: La texture complessiva deve avere larghezza e altezza potenze di due ($16, 32, 64, 128, 256, 512$). Ad esempio, per 4 frame da $32 \times 32$, la larghezza totale è $128 \times 32$ (entrambe potenze di due!).
2. **Swizzling Automatico**: Invocando `psp-forge cook`, l'immagine viene automaticamente swizzlata in blocchi da $16 \times 8$ byte, massimizzando il fillrate della GPU.
3. **Calcolo Coordinate UV**: L'engine ricava al volo le coordinate di texture per ogni fotogramma:
   $$tx = (\text{frame} \pmod{\text{colonne}}) \times \text{frame\_w}$$
   $$ty = (\lfloor\text{frame} / \text{colonne}\rfloor) \times \text{frame\_h}$$

---

## 2. L'API `ForgeSpriteAnim`

PSP-Forge include nativamente il supporto per animazioni flipbook in `psp_forge.h`:

```c
typedef struct {
    const ForgeTexture* texture;       // Texture dello spritesheet caricata
    int   frame_w;                     // Larghezza del singolo fotogramma (es. 32)
    int   frame_h;                     // Altezza del singolo fotogramma (es. 32)
    int   num_frames;                  // Numero totale di frame (es. 4)
    int   columns;                     // Colonne nella texture
    float fps;                         // Velocità dell'animazione (es. 8.0f FPS)
    float timer;                       // Accumulatore di tempo interno
    int   current_frame;               // Indice del fotogramma corrente
    bool  loop;                        // Ripetizione continua o arresto all'ultimo frame
    bool  is_playing;                  // Stato di riproduzione
} ForgeSpriteAnim;
```

### Funzioni Disponibili:
- `forge_anim2d_init(...)`: Inizializza l'animazione con dimensioni frame, velocità e opzione di loop.
- `forge_anim2d_update(&anim, dt)`: Avanza il timer dell'animazione in base al delta time del frame.
- `forge_anim2d_draw(&anim, x, y, w, h)`: Renderizza il fotogramma corrente alle coordinate a schermo specificate con scaling arbitrario.
- `forge_anim2d_set_frame(&anim, frame_index)`: Forza un fotogramma specifico (es. frame 0 per lo stato di Idle quando il personaggio si ferma).

---

## 3. Implementazione Passo per Passo

### A. Caricamento dello Spritesheet
```c
ForgeTexture* sheet_tex = forge_texture_load("assets/walker_sheet.tex");

ForgeSpriteAnim walk_anim;
// 4 fotogrammi da 32x32 pixel a 8 frame al secondo con loop attivo
forge_anim2d_init(&walk_anim, sheet_tex, 32, 32, 4, 8.0f, true);
```

### B. Nel Game Loop: Aggiornamento Dinamico
```c
bool is_moving = false;

if (forge_input_is_held(&input, PSP_CTRL_LEFT))  { pos_x -= speed * dt; is_moving = true; }
if (forge_input_is_held(&input, PSP_CTRL_RIGHT)) { pos_x += speed * dt; is_moving = true; }

if (is_moving) {
    // Il personaggio cammina: aggiorna l'animazione
    walk_anim.fps = 10.0f;
    forge_anim2d_update(&walk_anim, dt);
} else {
    // Il personaggio è fermo: resetta al frame 0 (Idle)
    forge_anim2d_set_frame(&walk_anim, 0);
}
```

### C. Nel Blocco di Rendering
```c
forge_begin_frame();
forge_clear(0xFF1E2818);

// Disegna l'animazione a schermo ingrandita a 48x48 pixel
forge_anim2d_draw(&walk_anim, pos_x, pos_y, 48.0f, 48.0f);

forge_end_frame();
```

---

## 4. Come Provare la Demo Inclusa

Puoi compilare e avviare immediatamente la demo presente nella repository:

```bash
cd demos/demo_anim_2d
psp-forge build
psp-forge run
```
