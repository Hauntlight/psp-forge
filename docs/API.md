# PSP-Forge C99 Runtime API Reference (`libpspforge`)

L'header principale da includere in ogni progetto è `#include <psp_forge.h>`.

---

## 1. Core & Ciclo di Frame

### `void forge_init(uint32_t flags)`
Inizializza la Graphics Utility (GU), il sottosistema audio, i callback di sistema per la pressione del tasto HOME, il controller e il timer hardware ad alta precisione.
* **Parametri:** `flags` riservato (passare `0`).

### `void forge_shutdown(void)`
Spegne il controller video e rilascia le risorse allocate.

### `int forge_is_running(void)`
Restituisce `1` se l'applicazione è attiva, `0` se l'utente ha richiesto l'uscita tramite menu HOME della PSP.

### `void forge_begin_frame(void)`
Apre una nuova Display List per iniziare a registrare comandi di disegno per il frame corrente.

### `void forge_clear(uint32_t color_rgba8888)`
Pulisce i buffer colore e profondità (Z-buffer).
* **Esempio:** `forge_clear(0xFF2E1C12);`

### `void forge_end_frame(void)`
Chiude la Display List, attende il sincronismo verticale (`VBlank`), effettua lo swap dei buffer video e calcola il `delta_time` e il framerate.

### `float forge_get_delta_time(void)`
Restituisce il tempo trascorso (in secondi) dall'ultimo frame (utile per movimenti indipendenti dal framerate).

### `float forge_get_fps(void)`
Restituisce i frame al secondo medi correnti.

---

## 2. VRAM Static Allocator (2 MB eDRAM)

La VRAM è partizionata in modo deterministico:
* `0x00000000` - `0x00080000` (512 KB): Draw Buffer
* `0x00080000` - `0x00100000` (512 KB): Display Buffer
* `0x00100000` - `0x00140000` (256 KB): Depth Buffer (16-bit Z)
* `0x00140000` - `0x00200000` (768 KB): Texture Scratchpad ad accesso ultrarapido

### `void* forge_vram_alloc(uint32_t size)`
Alloca memoria lineare allineata a 16 byte nello scratchpad VRAM. Restituisce un offset relativo per la GPU, oppure `NULL` se lo scratchpad è esaurito.

### `void forge_vram_reset(void)`
Azzera il puntatore di allocazione dello scratchpad (permette di riciclare la memoria VRAM tra scene o livelli).

### `void* forge_vram_to_uncached_cpu(void* vram_rel)`
Traduce un offset VRAM in un puntatore assoluto MIPS CPU uncached (`0x44000000 + offset`) per consentire alla CPU di scrivere direttamente senza passare per la cache L1.

---

## 3. Gestione Input

### `void forge_input_poll(ForgeInput* input)`
Effettua il polling del controller PSP e calcola lo stato istantaneo:
```c
typedef struct {
    uint32_t held;      // Pulsanti attualmente tenuti premuti
    uint32_t pressed;   // Pulsanti appena premuti in questo frame
    uint32_t released;  // Pulsanti appena rilasciati in questo frame
    float    analog_x;  // Asse orizzontale stick [-1.0 .. 1.0]
    float    analog_y;  // Asse verticale stick   [-1.0 .. 1.0]
} ForgeInput;
```

### Funzioni di controllo rapido:
* `bool forge_input_is_pressed(const ForgeInput* in, uint32_t btn);`
* `bool forge_input_is_held(const ForgeInput* in, uint32_t btn);`
* `bool forge_input_is_released(const ForgeInput* in, uint32_t btn);`

---

## 4. Pipeline 2D e Texture

### `ForgeTexture* forge_texture_load(const char* path)`
Carica un file `.tex` (generato da `psp-forge cook`) nella RAM principale.

### `ForgeTexture* forge_texture_load_vram(const char* path)`
Carica un file `.tex` direttamente nello scratchpad VRAM (massime prestazioni di fill-rate). Se lo scratchpad è pieno, ripiega automaticamente sulla RAM.

### `void forge_texture_free(ForgeTexture* tex)`
Rilascia le risorse allocate dalla texture.

### `void forge_draw_sprite(const ForgeTexture* tex, float sx, float sy, float sw, float sh, float tx, float ty, float tw, float th)`
Disegna uno sprite 2D sullo schermo:
* `sx, sy`: coordinate a schermo (in pixel, da 0 a 480x272).
* `sw, sh`: larghezza e altezza a schermo.
* `tx, ty`: coordinate UV di origine sulla texture.
* `tw, th`: larghezza e altezza del ritaglio sulla texture.

---

## 5. Pipeline 3D, Mesh e Illuminazione

### `ForgeMesh* forge_mesh_load(const char* path)`
Carica una mesh 3D precompilata in formato binario `.p3d`. Include vertici allineati a 16 byte, normali, coordinate texture e calcolo bounding box AABB.

### `void forge_mesh_free(ForgeMesh* mesh)`
Rilascia la memoria della mesh.

### `void forge_set_camera(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z, float fov_degrees)`
Configura le matrici di vista e proiezione con rapporto d'aspetto $16:9$.

### `void forge_set_light(uint8_t id, float x, float y, float z, uint32_t color_rgba, float intensity)`
Registra una luce virtuale (fino a 16 luci simultanee).

### `void forge_draw_mesh(const ForgeMesh* mesh, const ForgeTexture* tex, float x, float y, float z, float rx, float ry, float rz, float sx, float sy, float sz)`
Applica la trasformazione locale (traslazione, rotazione e scala), seleziona automaticamente le 4 luci più vicine per i registri hardware `GU_LIGHT0..3`, attiva backface culling e renderizza i triangoli della mesh.

---

## 6. Sottosistema Audio Multithread

### `ForgeSound* forge_sound_load(const char* path)`
Carica un file audio `.snd` trascodificato a 44100 Hz PCM a 16 bit.

### `void forge_sound_free(ForgeSound* snd)`
Rilascia il buffer sonoro.

### `void forge_sound_play(const ForgeSound* snd, uint8_t loop)`
Invia il suono al thread audio dedicato (`0x12` priority). Se `loop` è `1`, il suono viene riprodotto in ciclo continuo (ad es. per musica di sottofondo BGM).

### `void forge_sound_stop(void)`
Interrompe la riproduzione corrente.
