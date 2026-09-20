# Tutorial: Gestione delle Scene con `ForgeScene` 🎬

Questo tutorial spiega come strutturare un gioco completo a più schermate (Menu Principale, Livelli di Gioco, Schermata Pause, Game Over) utilizzando l'oggetto unificato **`ForgeScene`** di PSP-Forge, prevenendo leak di memoria nella limitata RAM di $24\text{ MB}$ della PSP.

La demo funzionante associata a questa guida si trova in:
`demos/demo_scenes/`

---

## 1. Perché un Scene Manager su PSP?

Sulla PlayStation Portable classica (PSP-1000) sono disponibili circa **$24\text{ MB}$ di RAM usabile** in modalità User.  
Caricare all'avvio tutti gli asset di tutti i livelli e menu provoca rapidamente l'esaurimento della memoria fisica.

Con l'oggetto `ForgeScene`, ogni schermata incapsula il proprio ciclo di vita:
- **`on_init`**: alloca e carica solo le texture, suoni e mesh strettamente necessari a quella scena.
- **`on_update`**: elabora logica e input della scena attiva.
- **`on_draw`**: registra i comandi grafici nella Display List del frame.
- **`on_destroy`**: dealloca e rilascia tutta la memoria prima che la nuova scena prenda il controllo.

---

## 2. Struttura dell'Oggetto `ForgeScene`

In `psp_forge.h`:

```c
typedef struct ForgeScene ForgeScene;
typedef void (*ForgeSceneCallback)(ForgeScene* scene, float dt);

struct ForgeScene {
    const char*        name;        // Identificativo della scena
    void*              user_data;   // Puntatore a dati opzionali personalizzati
    ForgeSceneCallback on_init;     // Invocato al cambio scena (caricamento risorse)
    ForgeSceneCallback on_update;   // Invocato ogni frame per logica e fisica
    ForgeSceneCallback on_draw;     // Invocato ogni frame per il rendering video
    ForgeSceneCallback on_destroy;  // Invocato prima di cambiare scena (pulizia memoria)
};

// Funzioni di gestione
void        forge_scene_set(ForgeScene* scene);
ForgeScene* forge_scene_get_current(void);
void        forge_scene_update_and_draw(float dt);
```

---

## 3. Implementazione di Due Scene: Menu & Gioco

### A. Definizione della Scena 1: Titolo / Menu
```c
static void title_init(ForgeScene* scene, float dt) {
    s_banner = forge_texture_load("assets/menu_banner.tex");
}

static void title_update(ForgeScene* scene, float dt) {
    ForgeInput in;
    forge_input_poll(&in);

    // Alla pressione del tasto START, passa alla scena di gioco!
    if (forge_input_is_pressed(&in, PSP_CTRL_START)) {
        forge_sound_play(g_click_snd, 0);
        forge_scene_set(&g_game_scene);
    }
}

static void title_draw(ForgeScene* scene, float dt) {
    forge_clear(0xFF1B1015);
    if (s_banner) {
        forge_draw_sprite(s_banner, 112.0f, 50.0f, 256.0f, 64.0f, 0, 0, 256, 64);
    }
}

static void title_destroy(ForgeScene* scene, float dt) {
    // Rilascio rigoroso della memoria grafica
    if (s_banner) {
        forge_texture_free(s_banner);
        s_banner = NULL;
    }
}
```

### B. Definizione della Scena 2: Gioco
```c
static void game_init(ForgeScene* scene, float dt) {
    s_player_tex = forge_texture_load("assets/player.tex");
}

static void game_update(ForgeScene* scene, float dt) {
    ForgeInput in;
    forge_input_poll(&in);

    // Con SELECT si può tornare al menu principale
    if (forge_input_is_pressed(&in, PSP_CTRL_SELECT)) {
        forge_scene_set(&g_title_scene);
    }
}

static void game_destroy(ForgeScene* scene, float dt) {
    if (s_player_tex) {
        forge_texture_free(s_player_tex);
        s_player_tex = NULL;
    }
}
```

### C. Game Loop Unificato nel `main()`
Il ciclo principale non deve preoccuparsi di quale scena sia attiva:
```c
int main(int argc, char* argv[]) {
    forge_init(0);

    // Configurazione delle definizioni di scena
    g_title_scene.on_init    = title_init;
    g_title_scene.on_update  = title_update;
    g_title_scene.on_draw    = title_draw;
    g_title_scene.on_destroy = title_destroy;

    g_game_scene.on_init     = game_init;
    g_game_scene.on_update   = game_update;
    g_game_scene.on_draw     = game_draw;
    g_game_scene.on_destroy  = game_destroy;

    // Imposta la scena iniziale
    forge_scene_set(&g_title_scene);

    while (forge_is_running()) {
        float dt = forge_get_delta_time();

        forge_begin_frame();
        // Esegue automaticamente l'aggiornamento e il disegno della scena corrente
        forge_scene_update_and_draw(dt);
        forge_end_frame();
    }

    forge_shutdown();
    return 0;
}
```

---

## 4. Come Provare la Demo

```bash
cd demos/demo_scenes
psp-forge build
psp-forge run
```
- Nella schermata del titolo, premi **START** o **Croce ($\times$)** per entrare in partita.
- Nella schermata di gioco, muovi il personaggio con il D-pad o lo Stick e premi **SELECT** o **Triangolo ($\Delta$)** per ritornare al menu.
