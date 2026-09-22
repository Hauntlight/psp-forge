# Tutorial: Scene Management with `ForgeScene` 🎬

This tutorial explains how to structure a complete multi-screen game (Main Menu, Game Levels, Pause Screen, Game Over) using PSP-Forge's unified **`ForgeScene`** object, preventing memory leaks within the PSP's limited $24\text{ MB}$ RAM.

The working demo associated with this guide is located in:
`demos/demo_scenes/`

<div align="center">
  <img src="media/demo_scenes.png" width="600" alt="Scene Manager Demo Running on PPSSPP" />
  <p><em>Title Menu to Gameplay scene transition with deterministic memory cleanup in 24 MB RAM.</em></p>
</div>

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
    const char*        name;        // Scene identifier (e.g. "TitleScene")
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
void        forge_scene_reset(void);
```

---

## 3. Implementing Two Scenes: Menu & Gameplay

Here is the complete implementation based on `demos/demo_scenes/src/main.c`.

### A. Scene Declarations & Shared State

```c
#include <psp_forge.h>
#include <stdio.h>

PSP_MODULE_INFO("DEMO_SCENES", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* Forward declarations of scenes */
static ForgeScene g_title_scene;
static ForgeScene g_game_scene;

/* Global sound effect */
static ForgeSound* g_click_snd = NULL;
```

### B. Defining Scene 1: Title / Menu

```c
typedef struct {
    ForgeTexture* banner_tex;
    float         blink_timer;
} TitleSceneData;

static TitleSceneData s_title_data;

static void title_init(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    s_title_data.banner_tex  = forge_texture_load("assets/menu_banner.tex");
    s_title_data.blink_timer = 0.0f;
}

static void title_update(ForgeScene* scene, float dt) {
    (void)scene;
    s_title_data.blink_timer += dt;

    ForgeInput in;
    forge_input_poll(&in);

    // On START or CROSS press, transition to the gameplay scene!
    if (forge_input_is_pressed(&in, PSP_CTRL_START) || forge_input_is_pressed(&in, PSP_CTRL_CROSS)) {
        if (g_click_snd) forge_sound_play(g_click_snd, 0);
        forge_scene_set(&g_game_scene);
    }
}

static void title_draw(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    forge_clear(0xFF1B1015); // Deep royal purple

    if (s_title_data.banner_tex) {
        forge_draw_sprite(
            s_title_data.banner_tex,
            (FORGE_SCREEN_WIDTH - 256.0f) / 2.0f, 50.0f,
            256.0f, 64.0f,
            0.0f, 0.0f, 256.0f, 64.0f
        );
    }
}

static void title_destroy(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    // Strict graphics memory release
    if (s_title_data.banner_tex) {
        forge_texture_free(s_title_data.banner_tex);
        s_title_data.banner_tex = NULL;
    }
}
```

### C. Defining Scene 2: Gameplay

```c
typedef struct {
    ForgeTexture* player_tex;
    float         player_x;
    float         player_y;
    float         play_time;
} GameSceneData;

static GameSceneData s_game_data;

static void game_init(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    s_game_data.player_tex = forge_texture_load("assets/player.tex");
    s_game_data.player_x   = (FORGE_SCREEN_WIDTH - 32.0f) / 2.0f;
    s_game_data.player_y   = (FORGE_SCREEN_HEIGHT - 32.0f) / 2.0f;
    s_game_data.play_time  = 0.0f;
}

static void game_update(ForgeScene* scene, float dt) {
    (void)scene;
    s_game_data.play_time += dt;

    ForgeInput in;
    forge_input_poll(&in);

    float speed = 150.0f;
    if (forge_input_is_held(&in, PSP_CTRL_LEFT)  || in.analog_x < -0.2f) s_game_data.player_x -= speed * dt;
    if (forge_input_is_held(&in, PSP_CTRL_RIGHT) || in.analog_x >  0.2f) s_game_data.player_x += speed * dt;
    if (forge_input_is_held(&in, PSP_CTRL_UP)    || in.analog_y < -0.2f) s_game_data.player_y -= speed * dt;
    if (forge_input_is_held(&in, PSP_CTRL_DOWN)  || in.analog_y >  0.2f) s_game_data.player_y += speed * dt;

    // Screen boundary clamping
    if (s_game_data.player_x < 0.0f) s_game_data.player_x = 0.0f;
    if (s_game_data.player_x > FORGE_SCREEN_WIDTH - 32.0f) s_game_data.player_x = FORGE_SCREEN_WIDTH - 32.0f;
    if (s_game_data.player_y < 0.0f) s_game_data.player_y = 0.0f;
    if (s_game_data.player_y > FORGE_SCREEN_HEIGHT - 32.0f) s_game_data.player_y = FORGE_SCREEN_HEIGHT - 32.0f;

    // Press SELECT or TRIANGLE to return to title menu
    if (forge_input_is_pressed(&in, PSP_CTRL_SELECT) || forge_input_is_pressed(&in, PSP_CTRL_TRIANGLE)) {
        if (g_click_snd) forge_sound_play(g_click_snd, 0);
        forge_scene_set(&g_title_scene);
    }
}

static void game_draw(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    forge_clear(0xFF102518); // Dark teal gameplay arena

    if (s_game_data.player_tex) {
        forge_draw_sprite(
            s_game_data.player_tex,
            s_game_data.player_x, s_game_data.player_y,
            32.0f, 32.0f,
            0.0f, 0.0f, 32.0f, 32.0f
        );
    }
}

static void game_destroy(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    if (s_game_data.player_tex) {
        forge_texture_free(s_game_data.player_tex);
        s_game_data.player_tex = NULL;
    }
}
```

### D. Unified Game Loop in `main()`

```c
int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    g_click_snd = forge_sound_load("assets/click.snd");

    // Configure scene definitions
    g_title_scene.name       = "TitleScene";
    g_title_scene.on_init    = title_init;
    g_title_scene.on_update  = title_update;
    g_title_scene.on_draw    = title_draw;
    g_title_scene.on_destroy = title_destroy;

    g_game_scene.name       = "GameScene";
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

    // Destroy active scene on shutdown
    ForgeScene* cur = forge_scene_get_current();
    if (cur && cur->on_destroy) {
        cur->on_destroy(cur, 0.0f);
    }

    if (g_click_snd) forge_sound_free(g_click_snd);
    forge_shutdown();
    sceKernelExitGame();
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
