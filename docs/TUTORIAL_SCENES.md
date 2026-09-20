# Tutorial: Scene Management with `ForgeScene` 🎬

This tutorial explains how to structure a complete multi-screen game (Main Menu, Game Levels, Pause Screen, Game Over) using PSP-Forge's unified **`ForgeScene`** object, preventing memory leaks within the PSP's limited $24\text{ MB}$ RAM.

The working demo associated with this guide is located in:
`demos/demo_scenes/`

---

## 1. Why a Scene Manager on PSP?

On the original PlayStation Portable (PSP-1000), approximately **$24\text{ MB}$ of usable RAM** is available in User mode.  
Loading all assets for all levels and menus at startup quickly causes physical memory exhaustion.

With the `ForgeScene` object, each screen encapsulates its own lifecycle:
- **`on_init`**: allocates and loads only textures, audio, and meshes strictly needed for that scene.
- **`on_update`**: processes game logic and inputs for the active scene.
- **`on_draw`**: registers graphics commands into the frame's Display List.
- **`on_destroy`**: deallocates and frees all memory before a new scene takes control.

---

## 2. Structure of the `ForgeScene` Object

In `psp_forge.h`:

```c
typedef struct ForgeScene ForgeScene;
typedef void (*ForgeSceneCallback)(ForgeScene* scene, float dt);

struct ForgeScene {
    const char*        name;        // Scene identifier
    void*              user_data;   // Pointer to optional custom data
    ForgeSceneCallback on_init;     // Called on scene transition (resource loading)
    ForgeSceneCallback on_update;   // Called every frame for logic and physics
    ForgeSceneCallback on_draw;     // Called every frame for video rendering
    ForgeSceneCallback on_destroy;  // Called before transitioning to another scene (cleanup)
};

// Management functions
void        forge_scene_set(ForgeScene* scene);
ForgeScene* forge_scene_get_current(void);
void        forge_scene_update_and_draw(float dt);
```

---

## 3. Implementing Two Scenes: Menu & Gameplay

### A. Defining Scene 1: Title / Menu
```c
static void title_init(ForgeScene* scene, float dt) {
    s_banner = forge_texture_load("assets/menu_banner.tex");
}

static void title_update(ForgeScene* scene, float dt) {
    ForgeInput in;
    forge_input_poll(&in);

    // On START button press, switch to the gameplay scene!
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
    // Strict graphics memory release
    if (s_banner) {
        forge_texture_free(s_banner);
        s_banner = NULL;
    }
}
```

### B. Defining Scene 2: Gameplay
```c
static void game_init(ForgeScene* scene, float dt) {
    s_player_tex = forge_texture_load("assets/player.tex");
}

static void game_update(ForgeScene* scene, float dt) {
    ForgeInput in;
    forge_input_poll(&in);

    // Press SELECT to return to the main menu
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

### C. Unified Game Loop in `main()`
The main loop does not need to worry about which scene is active:
```c
int main(int argc, char* argv[]) {
    forge_init(0);

    // Configure scene definitions
    g_title_scene.on_init    = title_init;
    g_title_scene.on_update  = title_update;
    g_title_scene.on_draw    = title_draw;
    g_title_scene.on_destroy = title_destroy;

    g_game_scene.on_init     = game_init;
    g_game_scene.on_update   = game_update;
    g_game_scene.on_draw     = game_draw;
    g_game_scene.on_destroy  = game_destroy;

    // Set initial scene
    forge_scene_set(&g_title_scene);

    while (forge_is_running()) {
        float dt = forge_get_delta_time();

        forge_begin_frame();
        // Automatically updates and renders the active scene
        forge_scene_update_and_draw(dt);
        forge_end_frame();
    }

    forge_shutdown();
    return 0;
}
```

---

## 4. How to Run the Demo

```bash
cd demos/demo_scenes
psp-forge build
psp-forge run
```
- On the title screen, press **START** or **Cross ($\times$)** to enter the game.
- In gameplay, move the character using the D-pad or Analog Stick and press **SELECT** or **Triangle ($\Delta$)** to return to the title menu.
