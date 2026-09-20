# Tutorial: Creare la Demo 3D ("Track Runner") da Zero 🏎️

Questo tutorial spiega come sviluppare da zero una scena tridimensionale interattiva su Sony PSP, gestendo modelli Wavefront OBJ, texture mapping, telecamera prospettica dinamica, salto verticale con gravità e scorrimento continuo della pista a 60 FPS.

---

## 1. Struttura del Progetto

```text
my_3d_runner/
├── CMakeLists.txt     # Build file con BUILD_PRX
├── psp.toml           # Configurazione progetto
├── assets/
│   ├── track.obj      # Modello 3D geometrico (Wavefront OBJ)
│   ├── track.png      # Texture del circuito (64x64 RGBA)
│   ├── jump.wav       # Suono di salto
│   ├── icon0.png      # Icona menu XMB
│   └── pic1.png       # Sfondo menu XMB
└── src/
    └── main.c         # Codice C99 3D
```

---

## 2. Preparazione del Modello 3D (`track.obj`) e Texture

### A. La Geometria del Tracciato (`track.obj`)
Il modello è una sezione rettangolare della pista centrata sull'origine, lunga 6 unità lungo l'asse $Z$ (da $-3.0$ a $+3.0$) e larga 4 unità sull'asse $X$ (da $-2.0$ a $+2.0$).

Esempio sintetico del file `assets/track.obj`:
```obj
# Vertici (X, Y, Z)
v -2.0  0.0 -3.0
v  2.0  0.0 -3.0
v  2.0  0.0  3.0
v -2.0  0.0  3.0

# Coordinate Texture (U, V)
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0

# Normali (NX, NY, NZ)
vn  0.0  1.0  0.0

# Facce triangolari (vertice/uv/normale)
f 1/1/1 2/2/1 3/3/1
f 1/1/1 3/3/1 4/4/1
```

### B. La Texture del Circuito (`track.png`)
Un'immagine PNG da $64 \times 64$ contenente i colori dell'asfalto (grigio antracite), striscia continua bianca centrale e strisce di delimitazione corsie gialle.

### C. Conversione con `psp-forge cook`
Invocando:
```bash
psp-forge cook
```
Verranno creati:
- `track.p3d`: File binario contenente i vertici a 32-byte disposti secondo lo standard hardware:
  $$\text{UV (8 byte)} \longrightarrow \text{Normali (12 byte)} \longrightarrow \text{Posizione (12 byte)}$$
  con allineamento a 16 byte per il Direct Memory Access (DMA) della GPU.
- `track.tex`: Texture swizzlata in blocchi da $16 \times 8$ byte in formato `GU_PSM_8888`.
- `jump.snd`: Suono PCM 16-bit 44.1 kHz a blocchi di 64 campioni.

---

## 3. Configurazione di Compilazione (`CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.10)
project(psp_3d_runner C)

set(CMAKE_C_STANDARD 99)

if(NOT DEFINED ENV{PSPDEV})
    set(ENV{PSPDEV} "/usr/local/pspdev")
endif()
set(PSPDEV $ENV{PSPDEV})

add_executable(psp_3d_runner src/main.c)

target_compile_options(psp_3d_runner PRIVATE
    -O2 -G0 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing
)

target_link_libraries(psp_3d_runner PRIVATE
    pspforge
    pspgum pspgu pspge
    pspaudio pspdisplay pspctrl psprtc pspkernel m
)

create_pbp_file(
    TARGET psp_3d_runner
    TITLE "PSP 3D Runner"
    BUILD_PRX
    ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png"
    BACKGROUND_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/pic1.png"
)
```

---

## 4. Il Codice Sorgente C99 (`src/main.c`)

```c
#include <psp_forge.h>
#include <math.h>

PSP_MODULE_INFO("PSP_3D_RUNNER", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(16384);

#define NUM_TRACK_SEGMENTS 6
#define SEGMENT_LENGTH     6.0f

int main(int argc, char* argv[]) {
    // 1. Risoluzione trasparente del percorso asset
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    // 2. Caricamento degli asset 3D
    ForgeMesh*    track_mesh = forge_mesh_load("assets/track.p3d");
    ForgeTexture* track_tex  = forge_texture_load("assets/track.tex");
    ForgeSound*   jump_snd   = forge_sound_load("assets/jump.snd");

    // 3. Configurazione illuminazione hardware (luce chiave bianca e luce di riempimento blu)
    forge_set_light(0,  0.0f, 4.0f, -2.0f, 0xFFFFFFFF, 2.5f);
    forge_set_light(1,  3.0f, 1.5f,  4.0f, 0xFF80C0FF, 1.8f);

    // Stato del giocatore
    int   target_lane = 0; // -1: Sinistra, 0: Centro, 1: Destra
    float player_x    = 0.0f;
    float player_y    = 0.0f;
    float jump_vel    = 0.0f;
    float gravity     = -20.0f;
    bool  is_grounded = true;

    // Posizioni Z dei segmenti della pista infinita
    float segment_z[NUM_TRACK_SEGMENTS];
    for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
        segment_z[i] = (float)i * SEGMENT_LENGTH;
    }
    float scroll_speed = 10.0f; // unità/secondo

    ForgeInput in;

    while (forge_is_running()) {
        forge_input_poll(&in);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();

        // Cambio corsia (Frecce Sinistra / Destra)
        if (forge_input_is_pressed(&in, PSP_CTRL_LEFT)  && target_lane > -1) target_lane--;
        if (forge_input_is_pressed(&in, PSP_CTRL_RIGHT) && target_lane <  1) target_lane++;

        // Interpolazione morbida verso la corsia bersaglio
        float dest_x = (float)target_lane * 1.5f;
        player_x += (dest_x - player_x) * 12.0f * dt;

        // Salto con pulsante Croce (X)
        if (forge_input_is_pressed(&in, PSP_CTRL_CROSS) && is_grounded) {
            jump_vel    = 7.0f;
            is_grounded = false;
            if (jump_snd) forge_sound_play(jump_snd, 0);
        }

        // Fisica della caduta e gravità
        if (!is_grounded) {
            jump_vel += gravity * dt;
            player_y += jump_vel * dt;
            if (player_y <= 0.0f) {
                player_y    = 0.0f;
                jump_vel    = 0.0f;
                is_grounded = true;
            }
        }

        // Scorrimento e riciclo infinito dei segmenti della pista
        float total_track_span = NUM_TRACK_SEGMENTS * SEGMENT_LENGTH;
        for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
            segment_z[i] -= scroll_speed * dt;
            if (segment_z[i] < -SEGMENT_LENGTH) {
                segment_z[i] += total_track_span;
            }
        }

        // Inizio registrazione comandi GPU
        forge_begin_frame();
        forge_clear(0xFF1B140E); // Cielo notturno scuro

        // 4. Configurazione telecamera prospettica che insegue il player
        // (x_eye, y_eye, z_eye, x_target, y_target, z_target, fov_gradi)
        forge_set_camera(
            player_x * 0.4f, 2.8f + player_y * 0.3f, -5.5f,
            player_x * 0.7f, 0.6f + player_y * 0.5f,  6.0f,
            65.0f
        );

        // 5. Disegno dei segmenti 3D
        if (track_mesh) {
            for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
                forge_draw_mesh(
                    track_mesh,
                    track_tex,
                    0.0f, 0.0f, segment_z[i], // Posizione
                    0.0f, 0.0f, 0.0f,         // Rotazione
                    1.0f, 1.0f, 1.0f          // Scala
                );
            }
        }

        // Barra degli FPS in sovrimpressione 2D
        {
            float bar_w = (fps / 60.0f) * 80.0f;
            if (bar_w > 80.0f) bar_w = 80.0f;
            if (bar_w < 2.0f)  bar_w = 2.0f;

            typedef struct { float x, y, z; } HudVtx;
            sceGuDisable(GU_DEPTH_TEST);

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

            sceGuEnable(GU_DEPTH_TEST);
        }

        forge_end_frame();
    }

    // Pulizia
    if (track_mesh) forge_mesh_free(track_mesh);
    if (track_tex)  forge_texture_free(track_tex);
    if (jump_snd)   forge_sound_free(jump_snd);

    forge_shutdown();
    return 0;
}
```

---

## 5. Compilazione e Avvio Rapido

```bash
psp-forge build
psp-forge run
```
