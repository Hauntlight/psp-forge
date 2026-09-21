#include <psp_forge.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("PSP_2D_GAME", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    /* Load cooked assets — path is relative to the game folder (no "build/") */
    ForgeTexture* hero_tex = forge_texture_load("assets/hero.tex");
    ForgeSound*   coin_snd = forge_sound_load("assets/coin.snd");

    float hero_x = (FORGE_SCREEN_WIDTH  / 2.0f) - 16.0f;
    float hero_y = (FORGE_SCREEN_HEIGHT / 2.0f) - 16.0f;
    float speed  = 160.0f;
    float time_acc = 0.0f;

    ForgeInput input;

    while (forge_is_running()) {
        forge_input_poll(&input);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();
        time_acc += dt;

        if (forge_input_is_held(&input, PSP_CTRL_LEFT)  || input.analog_x < -0.2f) hero_x -= speed * dt;
        if (forge_input_is_held(&input, PSP_CTRL_RIGHT) || input.analog_x >  0.2f) hero_x += speed * dt;
        if (forge_input_is_held(&input, PSP_CTRL_UP)    || input.analog_y < -0.2f) hero_y -= speed * dt;
        if (forge_input_is_held(&input, PSP_CTRL_DOWN)  || input.analog_y >  0.2f) hero_y += speed * dt;

        if (hero_x < 0.0f)                          hero_x = 0.0f;
        if (hero_x > (FORGE_SCREEN_WIDTH  - 32.0f)) hero_x = FORGE_SCREEN_WIDTH  - 32.0f;
        if (hero_y < 0.0f)                          hero_y = 0.0f;
        if (hero_y > (FORGE_SCREEN_HEIGHT - 32.0f)) hero_y = FORGE_SCREEN_HEIGHT - 32.0f;

        if (forge_input_is_pressed(&input, PSP_CTRL_CROSS)) {
            if (coin_snd) forge_sound_play(coin_snd, 0);
        }

        /* Animated background: slow color drift */
        float t   = time_acc * 0.4f;
        float frac = t - (int)t;
        int   r_bg = (int)(18.0f + 12.0f * frac);
        int   g_bg = (int)(28.0f + 10.0f * frac);
        uint32_t bg = 0xFF000000 | (r_bg & 0xFF) | ((g_bg & 0xFF) << 8) | (0x38 << 16);

        forge_begin_frame();
        forge_clear(bg);

        if (hero_tex) {
            forge_draw_sprite(
                hero_tex,
                hero_x, hero_y, 32.0f, 32.0f,
                0.0f, 0.0f, (float)hero_tex->width, (float)hero_tex->height
            );
        }

        /* Visual indicator: green bar in top-right corner proves rendering is working.
         * Width proportional to FPS (60 fps = full bar of 80px). */
        {
            float bar_w = (fps / 60.0f) * 80.0f;
            if (bar_w > 80.0f) bar_w = 80.0f;
            if (bar_w < 2.0f)  bar_w = 2.0f;

            /* Background strip (dark) */
            typedef struct { float x, y, z; } HudVtx;
            HudVtx* bg = (HudVtx*)sceGuGetMemory(2 * sizeof(HudVtx));
            if (bg) {
                bg[0].x = 396.0f; bg[0].y = 2.0f;  bg[0].z = 0.0f;
                bg[1].x = 478.0f; bg[1].y = 10.0f; bg[1].z = 0.0f;
                sceGuDisable(GU_TEXTURE_2D);
                sceGuColor(0xFF333333);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bg);
            }
            /* Foreground bar (green = 60fps, cyan = 30fps, red < 30fps) */
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

    if (hero_tex) forge_texture_free(hero_tex);
    if (coin_snd) forge_sound_free(coin_snd);
    forge_shutdown();
    sceKernelExitGame();
    return 0;
}
