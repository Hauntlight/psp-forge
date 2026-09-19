#include <psp_forge.h>
#include <stdio.h>

PSP_MODULE_INFO("PSP_2D_GAME", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    /* Initialize PSP-Forge micro-engine */
    forge_init(0);

    /* Load cooked assets */
    ForgeTexture* hero_tex = forge_texture_load("build/assets/hero.tex");
    ForgeSound*   coin_snd = forge_sound_load("build/assets/coin.snd");

    float hero_x = 240.0f - 16.0f;
    float hero_y = 136.0f - 16.0f;
    float speed  = 160.0f; /* pixels per second */

    ForgeInput input;

    /* Main game loop */
    while (forge_is_running()) {
        forge_input_poll(&input);
        float dt = forge_get_delta_time();

        /* Directional movement (D-pad + Analog Stick) */
        if (forge_input_is_held(&input, PSP_CTRL_LEFT) || input.analog_x < -0.2f) {
            hero_x -= speed * dt;
        }
        if (forge_input_is_held(&input, PSP_CTRL_RIGHT) || input.analog_x > 0.2f) {
            hero_x += speed * dt;
        }
        if (forge_input_is_held(&input, PSP_CTRL_UP) || input.analog_y < -0.2f) {
            hero_y -= speed * dt;
        }
        if (forge_input_is_held(&input, PSP_CTRL_DOWN) || input.analog_y > 0.2f) {
            hero_y += speed * dt;
        }

        /* Screen boundary constraints */
        if (hero_x < 0.0f) hero_x = 0.0f;
        if (hero_x > (FORGE_SCREEN_WIDTH - 32.0f)) hero_x = FORGE_SCREEN_WIDTH - 32.0f;
        if (hero_y < 0.0f) hero_y = 0.0f;
        if (hero_y > (FORGE_SCREEN_HEIGHT - 32.0f)) hero_y = FORGE_SCREEN_HEIGHT - 32.0f;

        /* Play audio on Cross button press */
        if (forge_input_is_pressed(&input, PSP_CTRL_CROSS)) {
            if (coin_snd) {
                forge_sound_play(coin_snd, 0);
            }
        }

        /* Render frame */
        forge_begin_frame();
        forge_clear(0xFF2E1C12); /* Dark slate blue */

        if (hero_tex) {
            forge_draw_sprite(
                hero_tex,
                hero_x, hero_y, 32.0f, 32.0f,
                0.0f, 0.0f, 32.0f, 32.0f
            );
        }

        forge_end_frame();
    }

    /* Cleanup */
    if (hero_tex) forge_texture_free(hero_tex);
    if (coin_snd) forge_sound_free(coin_snd);

    forge_shutdown();
    return 0;
}
