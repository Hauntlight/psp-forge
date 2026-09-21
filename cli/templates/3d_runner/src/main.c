#include <psp_forge.h>
#include <math.h>

PSP_MODULE_INFO("PSP_3D_RUNNER", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define NUM_TRACK_SEGMENTS 6
#define SEGMENT_LENGTH     6.0f

int main(int argc, char* argv[]) {
    /* Set base path from argv[0] — works on PPSSPP and real PSP hardware.
     * On real PSP: argv[0] = "ms0:/PSP/GAME/psp_3d_runner/EBOOT.PBP"
     * forge_set_base_path strips filename → "ms0:/PSP/GAME/psp_3d_runner"
     * Asset loads resolve to: "ms0:/PSP/GAME/psp_3d_runner/assets/track.p3d" */
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    /* Load 3D assets — path relative to game folder, no "build/" prefix */
    ForgeMesh*    track_mesh = forge_mesh_load("assets/track.p3d");
    ForgeTexture* track_tex  = forge_texture_load("assets/track.tex");
    ForgeSound*   jump_snd   = forge_sound_load("assets/jump.snd");

    /* Setup lighting:
     * Light 0: white key light from above-front
     * Light 1: cool blue fill light from the side
     * Ambient is set inside forge_cull_and_apply_lights via sceGuModelColor */
    forge_set_light(0,  0.0f, 4.0f, -2.0f, 0xFFFFFFFF, 2.5f);
    forge_set_light(1,  3.0f, 1.5f,  4.0f, 0xFF80C0FF, 1.8f);

    /* Global scene ambient — ensures mesh is never pitch black */
    sceGuAmbient(0xFF303030);

    /* Player state */
    int   target_lane = 0;
    float player_x    = 0.0f;
    float player_y    = 0.0f;
    float jump_vel    = 0.0f;
    float gravity     = -20.0f;
    bool  is_grounded = true;

    /* Endless track segments */
    float segment_z[NUM_TRACK_SEGMENTS];
    for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i)
        segment_z[i] = (float)i * SEGMENT_LENGTH;
    float scroll_speed = 10.0f;

    ForgeInput in;

    while (forge_is_running()) {
        forge_input_poll(&in);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();

        /* Lane switching */
        if (forge_input_is_pressed(&in, PSP_CTRL_LEFT)  && target_lane > -1) target_lane--;
        if (forge_input_is_pressed(&in, PSP_CTRL_RIGHT) && target_lane <  1) target_lane++;

        /* Smooth lane interpolation */
        float dest_x = (float)target_lane * 1.5f;
        player_x += (dest_x - player_x) * 12.0f * dt;

        /* Jumping */
        if (forge_input_is_pressed(&in, PSP_CTRL_CROSS) && is_grounded) {
            jump_vel    = 7.0f;
            is_grounded = false;
            if (jump_snd) forge_sound_play(jump_snd, 0);
        }

        if (!is_grounded) {
            jump_vel += gravity * dt;
            player_y += jump_vel * dt;
            if (player_y <= 0.0f) {
                player_y    = 0.0f;
                jump_vel    = 0.0f;
                is_grounded = true;
            }
        }

        /* Scroll track segments */
        float total_span = NUM_TRACK_SEGMENTS * SEGMENT_LENGTH;
        for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
            segment_z[i] -= scroll_speed * dt;
            if (segment_z[i] < -SEGMENT_LENGTH)
                segment_z[i] += total_span;
        }

        forge_begin_frame();
        forge_clear(0xFF1B140E); /* Dark midnight sky */
        if (!track_mesh) {
            forge_clear(0xFF0000FF); /* RED: mesh failed to load */
        } else if (!track_tex) {
            forge_clear(0xFF00FFFF); /* YELLOW: texture failed to load */
        }

        /* Camera: follows player (must be inside display list) */
        forge_set_camera(
            player_x * 0.4f, 2.8f + player_y * 0.3f, -5.5f,
            player_x * 0.7f, 0.6f + player_y * 0.5f,  6.0f,
            65.0f
        );

        if (track_mesh) {
            for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
                forge_draw_mesh(
                    track_mesh, track_tex,
                    0.0f, 0.0f, segment_z[i],
                    0.0f, 0.0f, 0.0f,
                    1.0f, 1.0f, 1.0f
                );
            }
        }

        /* Visual FPS indicator: colored bar in top-right corner */
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
                sceGuDisable(GU_LIGHTING);
                sceGuDisable(GU_DEPTH_TEST);
                sceGuColor(0xFF333333);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bg);
            }
            HudVtx* bar = (HudVtx*)sceGuGetMemory(2 * sizeof(HudVtx));
            if (bar) {
                uint32_t bar_color = (fps >= 55.0f) ? 0xFF00DD00 :
                                     (fps >= 28.0f) ? 0xFF00CCDD : 0xFF0000FF;
                bar[0].x = 396.0f;         bar[0].y = 2.0f;  bar[0].z = 0.0f;
                bar[1].x = 396.0f + bar_w; bar[1].y = 10.0f; bar[1].z = 0.0f;
                sceGuColor(bar_color);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bar);
            }
            sceGuEnable(GU_DEPTH_TEST);
        }

        forge_end_frame();
    }

    if (track_mesh) forge_mesh_free(track_mesh);
    if (track_tex)  forge_texture_free(track_tex);
    if (jump_snd)   forge_sound_free(jump_snd);

    forge_shutdown();
    sceKernelExitGame();
    return 0;
}
