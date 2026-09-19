#include <psp_forge.h>
#include <math.h>

PSP_MODULE_INFO("PSP_3D_RUNNER", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define NUM_TRACK_SEGMENTS 6
#define SEGMENT_LENGTH     6.0f

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    forge_init(0);

    /* Load 3D assets */
    ForgeMesh*    track_mesh = forge_mesh_load("build/assets/track.p3d");
    ForgeTexture* track_tex  = forge_texture_load("build/assets/track.tex");
    ForgeSound*   jump_snd   = forge_sound_load("build/assets/jump.snd");

    /* Setup dynamic lighting */
    forge_set_light(0, 0.0f, 4.0f, -2.0f, 0xFFFFFFFF, 2.5f); /* White key light */
    forge_set_light(1, 3.0f, 1.5f,  4.0f, 0xFF20A0FF, 1.8f); /* Warm amber fill */

    /* Player state */
    int   target_lane = 0; /* -1: Left, 0: Center, 1: Right */
    float player_x    = 0.0f;
    float player_y    = 0.0f;
    float jump_vel    = 0.0f;
    float gravity     = -20.0f;
    bool  is_grounded = true;

    /* Endless track segments z-positions */
    float segment_z[NUM_TRACK_SEGMENTS];
    for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
        segment_z[i] = (float)i * SEGMENT_LENGTH;
    }
    float scroll_speed = 10.0f; /* units/second */

    ForgeInput in;

    while (forge_is_running()) {
        forge_input_poll(&in);
        float dt = forge_get_delta_time();

        /* Lane switching */
        if (forge_input_is_pressed(&in, PSP_CTRL_LEFT)) {
            if (target_lane > -1) target_lane--;
        }
        if (forge_input_is_pressed(&in, PSP_CTRL_RIGHT)) {
            if (target_lane < 1) target_lane++;
        }

        /* Smooth lane interpolation */
        float dest_x = (float)target_lane * 1.5f;
        player_x += (dest_x - player_x) * 12.0f * dt;

        /* Jumping */
        if (forge_input_is_pressed(&in, PSP_CTRL_CROSS) && is_grounded) {
            jump_vel = 7.0f;
            is_grounded = false;
            if (jump_snd) {
                forge_sound_play(jump_snd, 0);
            }
        }

        if (!is_grounded) {
            jump_vel += gravity * dt;
            player_y += jump_vel * dt;
            if (player_y <= 0.0f) {
                player_y = 0.0f;
                jump_vel = 0.0f;
                is_grounded = true;
            }
        }

        /* Scroll track segments */
        float total_track_span = NUM_TRACK_SEGMENTS * SEGMENT_LENGTH;
        for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
            segment_z[i] -= scroll_speed * dt;
            if (segment_z[i] < -SEGMENT_LENGTH) {
                segment_z[i] += total_track_span;
            }
        }

        /* Camera setup: following player */
        forge_set_camera(
            player_x * 0.4f, 2.8f + player_y * 0.3f, -5.5f,
            player_x * 0.7f, 0.6f + player_y * 0.5f, 6.0f,
            65.0f
        );

        /* Render */
        forge_begin_frame();
        forge_clear(0xFF1B140E); /* Dark midnight sky */

        /* Draw track chunks */
        if (track_mesh) {
            for (int i = 0; i < NUM_TRACK_SEGMENTS; ++i) {
                forge_draw_mesh(
                    track_mesh,
                    track_tex,
                    0.0f, 0.0f, segment_z[i],
                    0.0f, 0.0f, 0.0f,
                    1.0f, 1.0f, 1.0f
                );
            }
        }

        forge_end_frame();
    }

    /* Cleanup */
    if (track_mesh) forge_mesh_free(track_mesh);
    if (track_tex)  forge_texture_free(track_tex);
    if (jump_snd)   forge_sound_free(jump_snd);

    forge_shutdown();
    return 0;
}
