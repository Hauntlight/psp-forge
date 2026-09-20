# Tutorial: Creare la Demo 2D ("Hero Starter") da Zero 🛡️

Questo tutorial guida passo per passo alla creazione completa della demo 2D interattiva, spiegando la gestione degli asset (sprite, audio e icone), la compilazione dell'eseguibile `EBOOT.PBP` e la scrittura del loop di gioco in C99.

---

## 1. Struttura del Progetto

Un progetto 2D ha la seguente anatomia essenziale:

```text
my_2d_game/
├── CMakeLists.txt     # Script di compilazione con create_pbp_file e BUILD_PRX
├── psp.toml           # Metadati del gioco per la CLI
├── assets/            # File multimediali sorgente (PNG, WAV)
│   ├── hero.png       # Sprite del personaggio principale
│   ├── coin.wav       # Effetto sonoro al salto/interazione
│   ├── icon0.png      # Icona per la XMB (144x80 PNG)
│   └── pic1.png       # Sfondo per la XMB (480x272 PNG)
└── src/
    └── main.c         # Codice sorgente C99
```

---

## 2. Preparazione degli Asset Multimediali

### A. Lo Sprite del Personaggio (`hero.png`)
1. Disegna o esporta un'immagine PNG (anche con canale alfa/trasparenza RGBA).
2. Per massimizzare le prestazioni della GPU della PSP, le dimensioni ideali sono potenze di due ($16 \times 16$, $32 \times 32$, $64 \times 64$, ecc.). Nel nostro esempio usiamo uno sprite $32 \times 32$.
3. Salvalo in `assets/hero.png`.

### B. L'Effetto Sonoro (`coin.wav`)
1. Salva un file audio in formato WAV PCM 16-bit (Mono o Stereo a 44100 Hz).
2. Salvalo in `assets/coin.wav`.

### C. Conversione con `psp-forge cook`
Quando esegui:
```bash
psp-forge cook
```
Il compilatore automatico genererà in `build/assets/`:
- `hero.tex`: Texture swizzlata a blocchi hardware da $16 \times 8$ byte in formato `GU_PSM_8888`.
- `coin.snd`: Traccia audio raw PCM 16-bit allineata esattamente a multipli di 64 campioni.

---

## 3. Configurazione di Compilazione (`CMakeLists.txt`)

Crea il file `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.10)
project(psp_2d_game C)

set(CMAKE_C_STANDARD 99)

if(NOT DEFINED ENV{PSPDEV})
    set(ENV{PSPDEV} "/usr/local/pspdev")
endif()
set(PSPDEV $ENV{PSPDEV})

add_executable(psp_2d_game src/main.c)

target_compile_options(psp_2d_game PRIVATE
    -O2 -G0 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing
)

target_link_libraries(psp_2d_game PRIVATE
    pspforge
    pspgum pspgu pspge
    pspaudio pspdisplay pspctrl psprtc pspkernel m
)

# Cruciale: BUILD_PRX assicura la compatibilità sia su PPSSPP sia su hardware reale
create_pbp_file(
    TARGET psp_2d_game
    TITLE "PSP 2D Starter"
    BUILD_PRX
    ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png"
    BACKGROUND_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/pic1.png"
)
```

---

## 4. Il Codice Sorgente C99 (`src/main.c`)

Ecco l'implementazione completa con caricamento texture, input differenziale e barra FPS:

```c
#include <psp_forge.h>
#include <stdio.h>

PSP_MODULE_INFO("PSP_2D_GAME", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(16384);

int main(int argc, char* argv[]) {
    // 1. Risoluzione trasparente del percorso cartella su Memory Stick o PC
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    // 2. Inizializzazione della libreria grafica e audio
    forge_init(0);

    // 3. Caricamento degli asset binari compilati
    ForgeTexture* hero_tex = forge_texture_load("assets/hero.tex");
    ForgeSound*   coin_snd = forge_sound_load("assets/coin.snd");

    // Coordinate e velocità del personaggio
    float hero_x = (FORGE_SCREEN_WIDTH  / 2.0f) - 16.0f;
    float hero_y = (FORGE_SCREEN_HEIGHT / 2.0f) - 16.0f;
    float speed  = 160.0f; /* pixel al secondo */

    ForgeInput input;

    // 4. Game loop principale
    while (forge_is_running()) {
        forge_input_poll(&input);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();

        // Movimento con D-Pad o Stick Analogico
        if (forge_input_is_held(&input, PSP_CTRL_LEFT)  || input.analog_x < -0.2f) hero_x -= speed * dt;
        if (forge_input_is_held(&input, PSP_CTRL_RIGHT) || input.analog_x >  0.2f) hero_x += speed * dt;
        if (forge_input_is_held(&input, PSP_CTRL_UP)    || input.analog_y < -0.2f) hero_y -= speed * dt;
        if (forge_input_is_held(&input, PSP_CTRL_DOWN)  || input.analog_y >  0.2f) hero_y += speed * dt;

        // Limiti dello schermo (480x272)
        if (hero_x < 0.0f) hero_x = 0.0f;
        if (hero_x > (FORGE_SCREEN_WIDTH - 32.0f)) hero_x = FORGE_SCREEN_WIDTH - 32.0f;
        if (hero_y < 0.0f) hero_y = 0.0f;
        if (hero_y > (FORGE_SCREEN_HEIGHT - 32.0f)) hero_y = FORGE_SCREEN_HEIGHT - 32.0f;

        // Pressione pulsante Croce per suonare l'effetto
        if (forge_input_is_pressed(&input, PSP_CTRL_CROSS)) {
            if (coin_snd) forge_sound_play(coin_snd, 0);
        }

        // 5. Inizio del frame di rendering
        forge_begin_frame();
        forge_clear(0xFF2E1C12); /* Sfondo blu scuro */

        // Disegno dello sprite 2D
        if (hero_tex) {
            forge_draw_sprite(
                hero_tex,
                hero_x, hero_y, 32.0f, 32.0f, // Coordinate e dimensioni a schermo
                0.0f, 0.0f, 32.0f, 32.0f      // Coordinate UV sorgente
            );
        }

        // Barra indicatrice degli FPS (verde = 60fps)
        {
            float bar_w = (fps / 60.0f) * 80.0f;
            if (bar_w > 80.0f) bar_w = 80.0f;
            if (bar_w < 2.0f)  bar_w = 2.0f;

            typedef struct { float x, y, z; } HudVtx;
            HudVtx* bg = (HudVtx*)sceGuGetMemory(2 * sizeof(HudVtx));
            if (bg) {
                bg[0].x = 396.0f; bg[0].y = 2.0f;  bg[0].z = 0.0f;
                bg[1].x = 478.0f; bg[1].y = 10.0f; bg[1].z = 0.0f;
                sceGuDisable(GU_TEXTURE_2D);
                sceGuColor(0xFF333333);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bg);
            }
            HudVtx* bar = (HudVtx*)sceGuGetMemory(2 * sizeof(HudVtx));
            if (bar) {
                uint32_t bar_color = (fps >= 55.0f) ? 0xFF00DD00 :
                                     (fps >= 28.0f) ? 0xFF00CCDD : 0xFF0000FF;
                bar[0].x = 396.0f;          bar[0].y = 2.0f;  bar[0].z = 0.0f;
                bar[1].x = 396.0f + bar_w;  bar[1].y = 10.0f; bar[1].z = 0.0f;
                sceGuColor(bar_color);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bar);
            }
        }

        forge_end_frame();
    }

    // 6. Rilascio risorse
    if (hero_tex) forge_texture_free(hero_tex);
    if (coin_snd) forge_sound_free(coin_snd);

    forge_shutdown();
    return 0;
}
```

---

## 5. Compilazione ed Esecuzione

Per compilare tutto ed avviare:
```bash
psp-forge build
psp-forge run
```
