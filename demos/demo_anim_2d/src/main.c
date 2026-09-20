#include <psp_forge.h>
#include <stdio.h>

PSP_MODULE_INFO("DEMO_ANIM_2D", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(16384);

int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    /* Load 128x32 spritesheet (4 frames of 32x32) */
    ForgeTexture* sheet_tex = forge_texture_load("assets/walker_sheet.tex");

    ForgeSpriteAnim walk_anim;
    forge_anim2d_init(&walk_anim, sheet_tex, 32, 32, 4, 8.0f, true);

    float pos_x = (FORGE_SCREEN_WIDTH  / 2.0f) - 24.0f;
    float pos_y = (FORGE_SCREEN_HEIGHT / 2.0f) - 24.0f;
    float speed = 140.0f;

    ForgeInput input;

    while (forge_is_running()) {
        forge_input_poll(&input);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();

        bool is_moving = false;

        if (forge_input_is_held(&input, PSP_CTRL_LEFT)  || input.analog_x < -0.2f) { pos_x -= speed * dt; is_moving = true; }
        if (forge_input_is_held(&input, PSP_CTRL_RIGHT) || input.analog_x >  0.2f) { pos_x += speed * dt; is_moving = true; }
        if (forge_input_is_held(&input, PSP_CTRL_UP)    || input.analog_y < -0.2f) { pos_y -= speed * dt; is_moving = true; }
        if (forge_input_is_held(&input, PSP_CTRL_DOWN)  || input.analog_y >  0.2f) { pos_y += speed * dt; is_moving = true; }

        if (pos_x < 0.0f) pos_x = 0.0f;
        if (pos_x > (FORGE_SCREEN_WIDTH  - 48.0f)) pos_x = FORGE_SCREEN_WIDTH  - 48.0f;
        if (pos_y < 0.0f) pos_y = 0.0f;
        if (pos_y > (FORGE_SCREEN_HEIGHT - 48.0f)) pos_y = FORGE_SCREEN_HEIGHT - 48.0f;

        /* Update animation only if moving or cycle slowly */
        if (is_moving) {
            walk_anim.fps = 10.0f;
            forge_anim2d_update(&walk_anim, dt);
        } else {
            /* Idle: stay on frame 0 */
            forge_anim2d_set_frame(&walk_anim, 0);
        }

        forge_begin_frame();
        forge_clear(0xFF1E2818); /* Forest night background */

        /* Draw animated sprite (48x48 on screen scaled from 32x32) */
        forge_anim2d_draw(&walk_anim, pos_x, pos_y, 48.0f, 48.0f);

        /* FPS Indicator bar */
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

    if (sheet_tex) forge_texture_free(sheet_tex);
    forge_shutdown();
    return 0;
}
