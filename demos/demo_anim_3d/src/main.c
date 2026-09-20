#include <psp_forge.h>
#include <math.h>

PSP_MODULE_INFO("DEMO_ANIM_3D", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(16384);

int main(int argc, char* argv[]) {
    if (argc > 0 && argv && argv[0]) {
        forge_set_base_path(argv[0]);
    }

    forge_init(0);

    /* Load 3D Meshes & Textures */
    ForgeMesh*    gem_mesh  = forge_mesh_load("assets/gem.p3d");
    ForgeTexture* gem_tex   = forge_texture_load("assets/gem.tex");
    ForgeMesh*    ped_mesh  = forge_mesh_load("assets/pedestal.p3d");
    ForgeTexture* ped_tex   = forge_texture_load("assets/pedestal.tex");

    float time_acc   = 0.0f;
    float cam_angle  = 0.0f;
    float cam_dist   = 7.0f;
    float cam_height = 2.5f;

    ForgeInput in;

    while (forge_is_running()) {
        forge_input_poll(&in);
        float dt  = forge_get_delta_time();
        float fps = forge_get_fps();
        time_acc += dt;

        /* Orbit camera controls with D-Pad or Analog Stick */
        if (forge_input_is_held(&in, PSP_CTRL_LEFT)  || in.analog_x < -0.2f) cam_angle -= 1.5f * dt;
        if (forge_input_is_held(&in, PSP_CTRL_RIGHT) || in.analog_x >  0.2f) cam_angle += 1.5f * dt;
        if (forge_input_is_held(&in, PSP_CTRL_UP)    || in.analog_y < -0.2f) cam_height += 2.0f * dt;
        if (forge_input_is_held(&in, PSP_CTRL_DOWN)  || in.analog_y >  0.2f) cam_height -= 2.0f * dt;

        if (cam_height < 0.5f) cam_height = 0.5f;
        if (cam_height > 6.0f) cam_height = 6.0f;

        /* Procedural 3D Animation calculations */
        float gem_bob_y = sinf(time_acc * 2.5f) * 0.35f;
        float gem_rot_y = time_acc * 2.0f;
        float gem_tilt  = cosf(time_acc * 1.5f) * 0.15f;

        forge_begin_frame();
        forge_clear(0xFF140F0A); /* Deep obsidian dark background */

        /* Orbit camera setup */
        float cam_x = sinf(cam_angle) * cam_dist;
        float cam_z = -cosf(cam_angle) * cam_dist;
        forge_set_camera(
            cam_x, cam_height, cam_z,
            0.0f, 0.0f, 0.0f,
            60.0f
        );

        /* Draw static pedestal */
        if (ped_mesh) {
            forge_draw_mesh(
                ped_mesh, ped_tex,
                0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f,
                1.0f, 1.0f, 1.0f
            );
        }

        /* Draw procedurally animated floating crystal gem */
        if (gem_mesh) {
            forge_draw_mesh(
                gem_mesh, gem_tex,
                0.0f, 0.5f + gem_bob_y, 0.0f, /* Position: floating Y */
                gem_tilt, gem_rot_y, 0.0f,     /* Rotation: continuous Y spin + X tilt */
                1.0f, 1.0f, 1.0f              /* Scale */
            );
        }

        /* FPS Indicator bar */
        {
            float bar_w = (fps / 60.0f) * 80.0f;
            if (bar_w > 80.0f) bar_w = 80.0f;
            if (bar_w < 2.0f)  bar_w = 2.0f;

            typedef struct { float x, y, z; } HudVtx;
            sceGuDisable(GU_DEPTH_TEST);

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
            sceGuEnable(GU_DEPTH_TEST);
        }

        forge_end_frame();
    }

    if (gem_mesh) forge_mesh_free(gem_mesh);
    if (gem_tex)  forge_texture_free(gem_tex);
    if (ped_mesh) forge_mesh_free(ped_mesh);
    if (ped_tex)  forge_texture_free(ped_tex);

    forge_shutdown();
    return 0;
}
