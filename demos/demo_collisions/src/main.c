#include <psp_forge.h>
#include <stdio.h>
#include <stdlib.h>

PSP_MODULE_INFO("DEMO_COLLISIONS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    ForgeTexture* player_tex = forge_texture_load("assets/box_player.tex");
    ForgeTexture* coin_tex   = forge_texture_load("assets/coin_item.tex");
    ForgeSound*   chime_snd  = forge_sound_load("assets/collect.snd");

    /* Player state & rect collider */
    ForgeRect player_box = { 60.0f, 120.0f, 32.0f, 32.0f };
    float speed = 160.0f;

    /* Static solid obstacle */
    ForgeRect obstacle_box = { 220.0f, 100.0f, 48.0f, 48.0f };

    /* Collectible circular coin */
    ForgeCircle coin_col = { 360.0f, 124.0f, 14.0f };

    int score = 0;
    bool is_hitting_obstacle = false;

    ForgeInput in;

    while (forge_is_running()) {
        forge_input_poll(&in);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();

        float old_x = player_box.x;
        float old_y = player_box.y;

        if (forge_input_is_held(&in, PSP_CTRL_LEFT)  || in.analog_x < -0.2f) player_box.x -= speed * dt;
        if (forge_input_is_held(&in, PSP_CTRL_RIGHT) || in.analog_x >  0.2f) player_box.x += speed * dt;
        if (forge_input_is_held(&in, PSP_CTRL_UP)    || in.analog_y < -0.2f) player_box.y -= speed * dt;
        if (forge_input_is_held(&in, PSP_CTRL_DOWN)  || in.analog_y >  0.2f) player_box.y += speed * dt;

        /* Screen bounds */
        if (player_box.x < 0.0f) player_box.x = 0.0f;
        if (player_box.x > FORGE_SCREEN_WIDTH - player_box.w) player_box.x = FORGE_SCREEN_WIDTH - player_box.w;
        if (player_box.y < 0.0f) player_box.y = 0.0f;
        if (player_box.y > FORGE_SCREEN_HEIGHT - player_box.h) player_box.y = FORGE_SCREEN_HEIGHT - player_box.h;

        /* 1. Rectangle vs Rectangle Collision (Obstacle) */
        is_hitting_obstacle = forge_collide_rect_rect(player_box, obstacle_box);
        if (is_hitting_obstacle) {
            /* Block player movement: revert to previous position */
            player_box.x = old_x;
            player_box.y = old_y;
        }

        /* 2. Rectangle vs Circle Collision (Coin collection) */
        if (forge_collide_rect_circle(player_box, coin_col)) {
            score++;
            if (chime_snd) forge_sound_play(chime_snd, 0);

            /* Relocate coin to a new position away from obstacle */
            coin_col.x = 60.0f + (float)(rand() % 360);
            coin_col.y = 40.0f + (float)(rand() % 190);
        }

        forge_begin_frame();
        forge_clear(0xFF181512); /* Dark arena background */

        /* Draw static obstacle (Red normally, Yellow if colliding) */
        {
            typedef struct { float x, y, z; } Vtx;
            Vtx* obs = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
            if (obs) {
                obs[0].x = obstacle_box.x;
                obs[0].y = obstacle_box.y;
                obs[0].z = 0.0f;
                obs[1].x = obstacle_box.x + obstacle_box.w;
                obs[1].y = obstacle_box.y + obstacle_box.h;
                obs[1].z = 0.0f;
                sceGuDisable(GU_TEXTURE_2D);
                sceGuColor(is_hitting_obstacle ? 0xFF30D0F0 : 0xFF2020D0); /* Amber or Crimson Red */
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, obs);
            }
        }

        /* Draw collectible coin */
        if (coin_tex) {
            forge_draw_sprite(
                coin_tex,
                coin_col.x - 16.0f, coin_col.y - 16.0f,
                32.0f, 32.0f,
                0.0f, 0.0f, 32.0f, 32.0f
            );
        }

        /* Draw player */
        if (player_tex) {
            forge_draw_sprite(
                player_tex,
                player_box.x, player_box.y,
                player_box.w, player_box.h,
                0.0f, 0.0f, 32.0f, 32.0f
            );
        }

        /* Score & FPS indicator */
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

    if (player_tex) forge_texture_free(player_tex);
    if (coin_tex)   forge_texture_free(coin_tex);
    if (chime_snd)  forge_sound_free(chime_snd);

    forge_shutdown();
    sceKernelExitGame();
    return 0;
}
