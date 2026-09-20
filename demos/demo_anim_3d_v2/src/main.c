#include <psp_forge.h>
#include <math.h>
#include <stdio.h>

PSP_MODULE_INFO("DEMO_ANIM_3D_V2", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(16384);

typedef enum {
    STATE_IDLE = 0,
    STATE_RUN,
    STATE_JUMP,
    STATE_ATTACK
} AnimState;

int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    /* Load Humanoid 3D Meshes & Textures */
    ForgeMesh*    arena_mesh  = forge_mesh_load("assets/arena.p3d");
    ForgeMesh*    shadow_mesh = forge_mesh_load("assets/shadow_disc.p3d");
    ForgeMesh*    torso_mesh  = forge_mesh_load("assets/torso.p3d");
    ForgeMesh*    head_mesh   = forge_mesh_load("assets/head.p3d");
    ForgeMesh*    limb_mesh   = forge_mesh_load("assets/limb.p3d");
    ForgeMesh*    sword_mesh  = forge_mesh_load("assets/sword.p3d");
    ForgeTexture* arena_tex   = forge_texture_load("assets/arena.tex");
    ForgeTexture* bot_tex     = forge_texture_load("assets/knight_bot.tex");
    ForgeTexture* shadow_tex  = forge_texture_load("assets/shadow.tex");

    /* Character position and physics */
    float char_x       = 0.0f;
    float char_y       = 0.0f;
    float char_z       = 0.0f;
    float vel_y        = 0.0f;
    float facing_angle = 0.0f;
    float target_angle = 0.0f;

    /* Camera state: 3/4 isometric perspective */
    float cam_angle    = 0.52f;
    float cam_dist     = 5.8f;
    float cam_height   = 2.6f;

    /* Animation variables */
    AnimState state    = STATE_IDLE;
    float anim_phase   = 0.0f;
    float attack_time  = 0.0f;

    ForgeInput in;

    while (forge_is_running()) {
        forge_input_poll(&in);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();
        if (dt > 0.05f) dt = 0.05f;

        /* 1. Camera Orbit Controls with L / R Triggers */
        if (forge_input_is_held(&in, PSP_CTRL_LTRIGGER)) cam_angle -= 2.0f * dt;
        if (forge_input_is_held(&in, PSP_CTRL_RTRIGGER)) cam_angle += 2.0f * dt;

        /* 2. Character Locomotion Inputs (Analog Stick + D-Pad) */
        float input_x = in.analog_x;
        float input_y = in.analog_y;

        if (forge_input_is_held(&in, PSP_CTRL_LEFT))  input_x = -1.0f;
        if (forge_input_is_held(&in, PSP_CTRL_RIGHT)) input_x =  1.0f;
        if (forge_input_is_held(&in, PSP_CTRL_UP))    input_y = -1.0f;
        if (forge_input_is_held(&in, PSP_CTRL_DOWN))  input_y =  1.0f;

        /* Deadzone check */
        float input_mag = sqrtf(input_x * input_x + input_y * input_y);
        if (input_mag < 0.25f) {
            input_x = 0.0f;
            input_y = 0.0f;
            input_mag = 0.0f;
        } else if (input_mag > 1.0f) {
            input_x /= input_mag;
            input_y /= input_mag;
            input_mag = 1.0f;
        }

        /* 3. Attack Trigger */
        if (forge_input_is_pressed(&in, PSP_CTRL_SQUARE) && attack_time <= 0.0f) {
            attack_time = 0.38f;
            state = STATE_ATTACK;
        }

        /* 4. Jump Trigger */
        if (forge_input_is_pressed(&in, PSP_CTRL_CROSS) && char_y <= 0.01f) {
            vel_y = 5.8f;
            state = STATE_JUMP;
        }

        /* 5. Reset on START */
        if (forge_input_is_pressed(&in, PSP_CTRL_START)) {
            char_x = 0.0f;
            char_y = 0.0f;
            char_z = 0.0f;
            vel_y  = 0.0f;
            facing_angle = 0.0f;
            target_angle = 0.0f;
            cam_angle = 0.52f;
        }

        /* 6. Physics Update (Jump & Gravity) */
        if (char_y > 0.0f || vel_y > 0.0f) {
            char_y += vel_y * dt;
            vel_y  -= 16.0f * dt;
            if (char_y <= 0.0f) {
                char_y = 0.0f;
                vel_y  = 0.0f;
                if (attack_time <= 0.0f) {
                    state = (input_mag > 0.0f) ? STATE_RUN : STATE_IDLE;
                }
            } else {
                if (attack_time <= 0.0f) state = STATE_JUMP;
            }
        }

        /* 7. Locomotion Movement relative to Camera Angle */
        if (input_mag > 0.0f) {
            float sin_cam = sinf(cam_angle);
            float cos_cam = cosf(cam_angle);

            /* Camera-relative movement vectors:
             * On PSP: input_y is -1.0 for UP (forward), +1.0 for DOWN (backward).
             *         input_x is +1.0 for RIGHT, -1.0 for LEFT.
             * Camera forward vector: (-sin_cam,  cos_cam)
             * Camera right vector:   ( cos_cam,  sin_cam)
             *
             * stick_forward = -input_y; stick_right = input_x;
             * move = stick_right * cam_right + stick_forward * cam_forward
             */
            float move_x = (input_x * cos_cam + input_y * sin_cam);
            float move_z = (input_x * sin_cam - input_y * cos_cam);

            float move_speed = 3.8f;
            char_x += move_x * move_speed * dt;
            char_z += move_z * move_speed * dt;

            /* Clamping inside Arena radius */
            float dist_from_center = sqrtf(char_x * char_x + char_z * char_z);
            if (dist_from_center > 4.1f) {
                char_x = (char_x / dist_from_center) * 4.1f;
                char_z = (char_z / dist_from_center) * 4.1f;
            }

            /* Smooth facing angle interpolation */
            target_angle = atan2f(move_x, move_z);
            float angle_diff = target_angle - facing_angle;
            while (angle_diff >  3.14159f) angle_diff -= 6.28318f;
            while (angle_diff < -3.14159f) angle_diff += 6.28318f;
            facing_angle += angle_diff * 12.0f * dt;

            if (char_y <= 0.0f && attack_time <= 0.0f) {
                state = STATE_RUN;
            }
        } else {
            if (char_y <= 0.0f && attack_time <= 0.0f) {
                state = STATE_IDLE;
            }
        }

        /* 7. Animation State Calculations */
        float torso_bob_y  = 0.0f;
        float torso_pitch  = 0.0f;
        float torso_yaw    = 0.0f;
        float head_pitch   = 0.0f;
        float l_arm_swing  = 0.0f;
        float r_arm_swing  = 0.0f;
        float l_forearm_rx = 0.35f;
        float r_forearm_rx = 0.55f;
        float l_leg_swing  = 0.0f;
        float r_leg_swing  = 0.0f;
        float l_knee_bend  = 0.0f;
        float r_knee_bend  = 0.0f;
        float sword_rot_x  = -0.95f;

        if (attack_time > 0.0f) {
            attack_time -= dt;
            float progress = 1.0f - (attack_time / 0.38f);
            /* Fast overhead sword slash arc */
            torso_pitch  = sinf(progress * 3.14159f) * 0.30f;
            torso_yaw    = sinf(progress * 3.14159f) * 0.50f;
            r_arm_swing  = -2.2f + progress * 3.8f;
            r_forearm_rx = 0.3f  + progress * 0.6f;
            sword_rot_x  = -0.6f - progress * 1.5f;
            l_arm_swing  =  0.7f;
        } else if (state == STATE_JUMP) {
            /* Airborne pose */
            torso_pitch  = -0.15f;
            l_arm_swing  = -1.2f;
            r_arm_swing  = -1.1f;
            l_leg_swing  =  0.5f;
            r_leg_swing  = -0.4f;
            l_knee_bend  =  0.6f;
            r_knee_bend  =  0.8f;
            sword_rot_x  = -1.2f;
        } else if (state == STATE_RUN) {
            anim_phase += dt * 11.0f;
            /* Harmonically alternating running gait */
            torso_bob_y  = fabsf(sinf(anim_phase)) * 0.10f;
            torso_pitch  = 0.18f;
            head_pitch   = -0.10f;
            l_leg_swing  =  sinf(anim_phase) * 0.75f;
            r_leg_swing  = -sinf(anim_phase) * 0.75f;
            l_arm_swing  = -sinf(anim_phase) * 0.65f;
            r_arm_swing  =  sinf(anim_phase) * 0.65f;
            l_knee_bend  = (l_leg_swing < 0.0f) ? (-l_leg_swing * 0.9f) : 0.0f;
            r_knee_bend  = (r_leg_swing < 0.0f) ? (-r_leg_swing * 0.9f) : 0.0f;
            r_forearm_rx = 0.50f + fabsf(sinf(anim_phase)) * 0.30f;
        } else {
            /* Idle breathing state */
            anim_phase += dt * 2.5f;
            torso_bob_y = sinf(anim_phase) * 0.035f;
            head_pitch  = sinf(anim_phase * 0.8f) * 0.05f;
            l_arm_swing = 0.15f + sinf(anim_phase) * 0.06f;
            r_arm_swing = -0.25f - sinf(anim_phase) * 0.06f;
            sword_rot_x = -0.90f + sinf(anim_phase) * 0.05f;
        }

        /* 8. Render Frame */
        forge_begin_frame();
        forge_clear(0xFF140F0A);

        /* Smooth camera positioning orbiting the character */
        float eye_x = char_x + sinf(cam_angle) * cam_dist;
        float eye_y = char_y + cam_height;
        float eye_z = char_z - cosf(cam_angle) * cam_dist;
        forge_set_camera(
            eye_x, eye_y, eye_z,
            char_x, char_y + 1.0f, char_z,
            60.0f
        );

        /* 9. Draw Arena Platform with dedicated arena texture */
        if (arena_mesh && arena_tex) {
            forge_draw_mesh(
                arena_mesh, arena_tex,
                0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f,
                1.0f, 1.0f, 1.0f
            );
        }

        /* 10. Draw Contact Shadow on Ground */
        if (shadow_mesh && shadow_tex) {
            float shadow_scale = 1.0f / (1.0f + char_y * 1.2f);
            if (shadow_scale < 0.25f) shadow_scale = 0.25f;
            
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            
            forge_draw_mesh(
                shadow_mesh, shadow_tex,
                char_x, 0.015f, char_z,
                0.0f, 0.0f, 0.0f,
                shadow_scale, 1.0f, shadow_scale
            );
            
            sceGuDisable(GU_BLEND);
        }

        /* 11. Draw Hierarchical Humanoid Character (Matrix Stack) */
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();

        /* Character Root position and facing angle */
        ScePspFVector3 root_pos = { char_x, char_y + torso_bob_y + 0.60f, char_z };
        sceGumTranslate(&root_pos);
        sceGumRotateY(facing_angle);

        /* Torso */
        sceGumPushMatrix();
        sceGumRotateX(torso_pitch);
        sceGumRotateY(torso_yaw);
        if (torso_mesh) forge_draw_mesh_current(torso_mesh, bot_tex);

        /* Head (child of Torso) */
        sceGumPushMatrix();
        ScePspFVector3 head_off = { 0.0f, 0.85f, 0.0f };
        sceGumTranslate(&head_off);
        sceGumRotateX(head_pitch);
        if (head_mesh) forge_draw_mesh_current(head_mesh, bot_tex);
        sceGumPopMatrix(); /* Head */

        /* Left Arm (child of Torso) */
        ScePspFVector3 arm_sc = { 0.85f, 0.85f, 0.85f };
        ScePspFVector3 elbow_off = { 0.0f, -0.52f, 0.0f };

        sceGumPushMatrix();
        ScePspFVector3 l_shoulder = { -0.42f, 0.75f, 0.0f };
        sceGumTranslate(&l_shoulder);
        sceGumRotateX(l_arm_swing);
        sceGumScale(&arm_sc);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        /* Left Forearm */
        sceGumTranslate(&elbow_off);
        sceGumRotateX(l_forearm_rx);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        sceGumPopMatrix(); /* Left Arm */

        /* Right Arm (child of Torso) */
        sceGumPushMatrix();
        ScePspFVector3 r_shoulder = { 0.42f, 0.75f, 0.0f };
        sceGumTranslate(&r_shoulder);
        sceGumRotateX(r_arm_swing);
        sceGumScale(&arm_sc);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        /* Right Forearm */
        sceGumTranslate(&elbow_off);
        sceGumRotateX(r_forearm_rx);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        /* Equipped Sword in Right Hand! */
        if (sword_mesh) {
            ScePspFVector3 hand_off = { 0.0f, -0.42f, 0.12f };
            sceGumTranslate(&hand_off);
            sceGumRotateX(sword_rot_x);
            forge_draw_mesh_current(sword_mesh, bot_tex);
        }
        sceGumPopMatrix(); /* Right Arm */

        /* Left Leg (child of Torso waist) */
        ScePspFVector3 leg_sc = { 1.05f, 1.0f, 1.05f };
        ScePspFVector3 knee_off = { 0.0f, -0.55f, 0.0f };

        sceGumPushMatrix();
        ScePspFVector3 l_hip = { -0.22f, 0.0f, 0.0f };
        sceGumTranslate(&l_hip);
        sceGumRotateX(l_leg_swing);
        sceGumScale(&leg_sc);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        /* Left Calf / Shin */
        sceGumTranslate(&knee_off);
        sceGumRotateX(l_knee_bend);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        sceGumPopMatrix(); /* Left Leg */

        /* Right Leg (child of Torso waist) */
        sceGumPushMatrix();
        ScePspFVector3 r_hip = { 0.22f, 0.0f, 0.0f };
        sceGumTranslate(&r_hip);
        sceGumRotateX(r_leg_swing);
        sceGumScale(&leg_sc);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        /* Right Calf / Shin */
        sceGumTranslate(&knee_off);
        sceGumRotateX(r_knee_bend);
        if (limb_mesh) forge_draw_mesh_current(limb_mesh, bot_tex);
        sceGumPopMatrix(); /* Right Leg */

        sceGumPopMatrix(); /* Torso & Character Root */

        /* 12. 2D HUD / FPS Bar */
        {
            float bar_w = (fps / 60.0f) * 80.0f;
            if (bar_w > 80.0f) bar_w = 80.0f;
            if (bar_w < 2.0f)  bar_w = 2.0f;

            typedef struct { float x, y, z; } HudVtx;
            sceGuDisable(GU_DEPTH_TEST);
            sceGuDisable(GU_TEXTURE_2D);

            /* FPS Indicator Background and Bar */
            HudVtx* bg = (HudVtx*)sceGuGetMemory(2 * sizeof(HudVtx));
            if (bg) {
                bg[0].x = 396.0f; bg[0].y = 4.0f;  bg[0].z = 0.0f;
                bg[1].x = 478.0f; bg[1].y = 12.0f; bg[1].z = 0.0f;
                sceGuColor(0xFF333333);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bg);
            }
            HudVtx* bar = (HudVtx*)sceGuGetMemory(2 * sizeof(HudVtx));
            if (bar) {
                uint32_t bar_color = (fps >= 55.0f) ? 0xFF00DD00 :
                                     (fps >= 28.0f) ? 0xFF00CCDD : 0xFF0000FF;
                bar[0].x = 396.0f;          bar[0].y = 4.0f;  bar[0].z = 0.0f;
                bar[1].x = 396.0f + bar_w;  bar[1].y = 12.0f; bar[1].z = 0.0f;
                sceGuColor(bar_color);
                sceGuDrawArray(GU_SPRITES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, bar);
            }

            sceGuEnable(GU_DEPTH_TEST);
        }

        forge_end_frame();
    }

    if (arena_mesh)  forge_mesh_free(arena_mesh);
    if (shadow_mesh) forge_mesh_free(shadow_mesh);
    if (torso_mesh)  forge_mesh_free(torso_mesh);
    if (head_mesh)   forge_mesh_free(head_mesh);
    if (limb_mesh)   forge_mesh_free(limb_mesh);
    if (sword_mesh)  forge_mesh_free(sword_mesh);
    if (arena_tex)   forge_texture_free(arena_tex);
    if (bot_tex)     forge_texture_free(bot_tex);
    if (shadow_tex)  forge_texture_free(shadow_tex);

    forge_shutdown();
    return 0;
}
