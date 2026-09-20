#include <psp_forge.h>
#include <stdio.h>

PSP_MODULE_INFO("DEMO_SCENES", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(16384);

/* Forward declarations */
static ForgeScene g_title_scene;
static ForgeScene g_game_scene;

static ForgeSound* g_click_snd = NULL;

/* ========================================================================= */
/* Scene 1: Title Screen (Menu)                                              */
/* ========================================================================= */

typedef struct {
    ForgeTexture* banner_tex;
    float blink_timer;
} TitleSceneData;

static TitleSceneData s_title_data;

static void title_init(ForgeScene* scene, float dt) {
    (void)dt;
    s_title_data.banner_tex  = forge_texture_load("assets/menu_banner.tex");
    s_title_data.blink_timer = 0.0f;
}

static void title_update(ForgeScene* scene, float dt) {
    s_title_data.blink_timer += dt;

    ForgeInput in;
    forge_input_poll(&in);

    if (forge_input_is_pressed(&in, PSP_CTRL_START) || forge_input_is_pressed(&in, PSP_CTRL_CROSS)) {
        if (g_click_snd) forge_sound_play(g_click_snd, 0);
        forge_scene_set(&g_game_scene);
    }
}

static void title_draw(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    forge_clear(0xFF1B1015); /* Deep royal purple */

    /* Draw banner at center-top */
    if (s_title_data.banner_tex) {
        forge_draw_sprite(
            s_title_data.banner_tex,
            (FORGE_SCREEN_WIDTH - 256.0f) / 2.0f, 50.0f,
            256.0f, 64.0f,
            0.0f, 0.0f, 256.0f, 64.0f
        );
    }

    /* Blinking prompt: PRESS START (simulate blinking box indicator) */
    int blink = ((int)(s_title_data.blink_timer * 3.0f)) % 2;
    if (blink == 0) {
        typedef struct { float x, y, z; } BoxVtx;
        BoxVtx* box = (BoxVtx*)sceGuGetMemory(2 * sizeof(BoxVtx));
        if (box) {
            box[0].x = (FORGE_SCREEN_WIDTH - 180.0f) / 2.0f;
            box[0].y = 170.0f;
            box[0].z = 0.0f;
            box[1].x = box[0].x + 180.0f;
            box[1].y = 195.0f;
            box[1].z = 0.0f;
            sceGuDisable(GU_TEXTURE_2D);
            sceGuColor(0xFF20C060); /* Radiant amber/green */
            sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, box);
        }
    }
}

static void title_destroy(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    if (s_title_data.banner_tex) {
        forge_texture_free(s_title_data.banner_tex);
        s_title_data.banner_tex = NULL;
    }
}

/* ========================================================================= */
/* Scene 2: Gameplay Screen                                                  */
/* ========================================================================= */

typedef struct {
    ForgeTexture* player_tex;
    float player_x;
    float player_y;
    float play_time;
} GameSceneData;

static GameSceneData s_game_data;

static void game_init(ForgeScene* scene, float dt) {
    (void)dt;
    s_game_data.player_tex = forge_texture_load("assets/player.tex");
    s_game_data.player_x   = (FORGE_SCREEN_WIDTH - 32.0f) / 2.0f;
    s_game_data.player_y   = (FORGE_SCREEN_HEIGHT - 32.0f) / 2.0f;
    s_game_data.play_time  = 0.0f;
}

static void game_update(ForgeScene* scene, float dt) {
    s_game_data.play_time += dt;

    ForgeInput in;
    forge_input_poll(&in);

    float speed = 150.0f;
    if (forge_input_is_held(&in, PSP_CTRL_LEFT)  || in.analog_x < -0.2f) s_game_data.player_x -= speed * dt;
    if (forge_input_is_held(&in, PSP_CTRL_RIGHT) || in.analog_x >  0.2f) s_game_data.player_x += speed * dt;
    if (forge_input_is_held(&in, PSP_CTRL_UP)    || in.analog_y < -0.2f) s_game_data.player_y -= speed * dt;
    if (forge_input_is_held(&in, PSP_CTRL_DOWN)  || in.analog_y >  0.2f) s_game_data.player_y += speed * dt;

    /* Boundary check */
    if (s_game_data.player_x < 0.0f) s_game_data.player_x = 0.0f;
    if (s_game_data.player_x > FORGE_SCREEN_WIDTH - 32.0f) s_game_data.player_x = FORGE_SCREEN_WIDTH - 32.0f;
    if (s_game_data.player_y < 0.0f) s_game_data.player_y = 0.0f;
    if (s_game_data.player_y > FORGE_SCREEN_HEIGHT - 32.0f) s_game_data.player_y = FORGE_SCREEN_HEIGHT - 32.0f;

    /* Return to title screen on SELECT button */
    if (forge_input_is_pressed(&in, PSP_CTRL_SELECT) || forge_input_is_pressed(&in, PSP_CTRL_TRIANGLE)) {
        if (g_click_snd) forge_sound_play(g_click_snd, 0);
        forge_scene_set(&g_title_scene);
    }
}

static void game_draw(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    forge_clear(0xFF102518); /* Dark teal gameplay arena */

    if (s_game_data.player_tex) {
        forge_draw_sprite(
            s_game_data.player_tex,
            s_game_data.player_x, s_game_data.player_y,
            32.0f, 32.0f,
            0.0f, 0.0f, 32.0f, 32.0f
        );
    }

    /* Small HUD banner indicating gameplay status */
    typedef struct { float x, y, z; } BarVtx;
    BarVtx* bar = (BarVtx*)sceGuGetMemory(2 * sizeof(BarVtx));
    if (bar) {
        bar[0].x = 10.0f; bar[0].y = 10.0f; bar[0].z = 0.0f;
        bar[1].x = 80.0f; bar[1].y = 16.0f; bar[1].z = 0.0f;
        sceGuDisable(GU_TEXTURE_2D);
        sceGuColor(0xFF00AAFF); /* Blue strip */
        sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bar);
    }
}

static void game_destroy(ForgeScene* scene, float dt) {
    (void)scene; (void)dt;
    if (s_game_data.player_tex) {
        forge_texture_free(s_game_data.player_tex);
        s_game_data.player_tex = NULL;
    }
}

/* ========================================================================= */
/* Main Entry Point                                                          */
/* ========================================================================= */

int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    g_click_snd = forge_sound_load("assets/click.snd");

    /* Configure Scene definitions */
    g_title_scene.name       = "TitleScene";
    g_title_scene.on_init    = title_init;
    g_title_scene.on_update  = title_update;
    g_title_scene.on_draw    = title_draw;
    g_title_scene.on_destroy = title_destroy;

    g_game_scene.name       = "GameScene";
    g_game_scene.on_init    = game_init;
    g_game_scene.on_update  = game_update;
    g_game_scene.on_draw    = game_draw;
    g_game_scene.on_destroy = game_destroy;

    /* Start at Title Scene */
    forge_scene_set(&g_title_scene);

    while (forge_is_running()) {
        float dt = forge_get_delta_time();

        forge_begin_frame();

        /* Update & render active scene through ForgeScene manager */
        forge_scene_update_and_draw(dt);

        forge_end_frame();
    }

    /* Destroy active scene on shutdown */
    ForgeScene* cur = forge_scene_get_current();
    if (cur && cur->on_destroy) {
        cur->on_destroy(cur, 0.0f);
    }

    if (g_click_snd) forge_sound_free(g_click_snd);
    forge_shutdown();
    return 0;
}
