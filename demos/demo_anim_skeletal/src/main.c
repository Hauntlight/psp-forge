#include <psp_forge.h>
#include <stdio.h>
#include <math.h>

PSP_MODULE_INFO("DEMO_ANIM_SKELETAL", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

typedef enum {
    STATE_IDLE = 0,
    STATE_WALK,
    STATE_RUN,
    STATE_ACTION
} CharacterState;

int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    /* Load 3D Skeletal Model and Texture */
    ForgeModel3D* model = forge_model3d_load("assets/character.p3d");
    ForgeTexture* tex   = forge_texture_load("assets/character_characterw_color.tex");

    /* Load Precompiled Animation Clips */
    ForgeAnimClip* clip_idle = forge_anim3d_load("assets/character_anim_iddle.panm");
    ForgeAnimClip* clip_walk = forge_anim3d_load("assets/character_anim_walk.panm");
    ForgeAnimClip* clip_run  = forge_anim3d_load("assets/character_anim_run.panm");
    ForgeAnimClip* clip_jump = forge_anim3d_load("assets/character_anim_jump.panm");
    ForgeAnimClip* clip_flip = forge_anim3d_load("assets/character_anim_flip.panm");

    /* Setup Animator */
    ForgeAnimator animator;
    forge_anim3d_init(&animator);
    if (clip_idle) {
        forge_anim3d_play(&animator, clip_idle, true);
    }

    /* Lighting setup */
    forge_set_light(0,  0.0f,  5.0f, -3.0f, 0xFFFFFFFF, 2.5f);
    forge_set_light(1,  3.0f,  2.0f,  4.0f, 0xFF80B0FF, 1.8f);
    sceGuAmbient(0xFF383838);

    /* Character and Camera state */
    CharacterState state = STATE_IDLE;
    float char_x = 0.0f;
    float char_y = 0.0f;
    float char_z = 0.0f;
    float char_yaw = 0.0f;

    float cam_dist  = 3.2f;
    float cam_yaw   = 0.0f;
    float cam_pitch = 0.35f;

    ForgeInput in;

    while (forge_is_running()) {
        forge_input_poll(&in);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();

        /* Camera Controls via L / R Shoulder Triggers */
        if (forge_input_is_held(&in, PSP_CTRL_LTRIGGER)) cam_yaw -= 2.0f * dt;
        if (forge_input_is_held(&in, PSP_CTRL_RTRIGGER)) cam_yaw += 2.0f * dt;

        /* Movement vector from Analog Stick / D-Pad */
        float move_x = in.analog_x;
        float move_y = in.analog_y;

        if (forge_input_is_held(&in, PSP_CTRL_LEFT))  move_x = -1.0f;
        if (forge_input_is_held(&in, PSP_CTRL_RIGHT)) move_x =  1.0f;
        if (forge_input_is_held(&in, PSP_CTRL_UP))    move_y = -1.0f;
        if (forge_input_is_held(&in, PSP_CTRL_DOWN))  move_y =  1.0f;

        float move_len = sqrtf(move_x * move_x + move_y * move_y);
        if (move_len > 1.0f) {
            move_x /= move_len;
            move_y /= move_len;
            move_len = 1.0f;
        }

        /* Action triggers (Cross = Jump, Square = Flip) */
        if (forge_input_is_pressed(&in, PSP_CTRL_CROSS) && state != STATE_ACTION) {
            if (clip_jump) {
                state = STATE_ACTION;
                forge_anim3d_play(&animator, clip_jump, false);
            }
        } else if (forge_input_is_pressed(&in, PSP_CTRL_SQUARE) && state != STATE_ACTION) {
            if (clip_flip) {
                state = STATE_ACTION;
                forge_anim3d_play(&animator, clip_flip, false);
            }
        }

        /* State machine */
        if (state == STATE_ACTION) {
            if (animator.finished) {
                state = STATE_IDLE;
                if (clip_idle) forge_anim3d_play(&animator, clip_idle, true);
            }
        } else {
            if (move_len > 0.6f) {
                if (state != STATE_RUN && clip_run) {
                    state = STATE_RUN;
                    forge_anim3d_play(&animator, clip_run, true);
                }
                float speed = 2.4f;
                char_x += (move_x * cosf(cam_yaw) - move_y * sinf(cam_yaw)) * speed * dt;
                char_z += (move_x * sinf(cam_yaw) + move_y * cosf(cam_yaw)) * speed * dt;
                char_yaw = atan2f(move_x, -move_y) + cam_yaw;
            } else if (move_len > 0.15f) {
                if (state != STATE_WALK && clip_walk) {
                    state = STATE_WALK;
                    forge_anim3d_play(&animator, clip_walk, true);
                }
                float speed = 1.2f;
                char_x += (move_x * cosf(cam_yaw) - move_y * sinf(cam_yaw)) * speed * dt;
                char_z += (move_x * sinf(cam_yaw) + move_y * cosf(cam_yaw)) * speed * dt;
                char_yaw = atan2f(move_x, -move_y) + cam_yaw;
            } else {
                if (state != STATE_IDLE && clip_idle) {
                    state = STATE_IDLE;
                    forge_anim3d_play(&animator, clip_idle, true);
                }
            }
        }

        /* Update Skeletal Animation and Forward Kinematics */
        forge_anim3d_update(&animator, model, dt);

        /* Compute orbiting camera position */
        float cam_eye_x = char_x + sinf(cam_yaw) * cosf(cam_pitch) * cam_dist;
        float cam_eye_y = char_y + sinf(cam_pitch) * cam_dist + 0.8f;
        float cam_eye_z = char_z - cosf(cam_yaw) * cosf(cam_pitch) * cam_dist;

        /* Begin Frame Rendering */
        forge_begin_frame();
        forge_clear(0xFF1C1412); /* Midnight slate background */

        /* Setup Camera inside display list */
        forge_set_camera(
            cam_eye_x, cam_eye_y, cam_eye_z,
            char_x, char_y + 0.8f, char_z,
            60.0f
        );

        /* Render 3D Model with active animation pose */
        if (model) {
            sceGumMatrixMode(GU_MODEL);
            sceGumLoadIdentity();

            ScePspFVector3 root_pos = { char_x, char_y, char_z };
            sceGumTranslate(&root_pos);
            sceGumRotateY(char_yaw);

            forge_model3d_draw(model, &animator, tex);
        }

        /* On-Screen FPS Indicator */
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
                bar[0].x = 396.0f;          bar[0].y = 2.0f;  bar[0].z = 0.0f;
                bar[1].x = 396.0f + bar_w;  bar[1].y = 10.0f; bar[1].z = 0.0f;
                sceGuColor(bar_color);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bar);
            }
            sceGuEnable(GU_DEPTH_TEST);
        }

        forge_end_frame();
    }

    /* Cleanup Resources */
    if (clip_idle) forge_anim3d_free(clip_idle);
    if (clip_walk) forge_anim3d_free(clip_walk);
    if (clip_run)  forge_anim3d_free(clip_run);
    if (clip_jump) forge_anim3d_free(clip_jump);
    if (clip_flip) forge_anim3d_free(clip_flip);

    if (model) forge_model3d_free(model);
    if (tex)   forge_texture_free(tex);

    forge_shutdown();
    sceKernelExitGame();
    return 0;
}
