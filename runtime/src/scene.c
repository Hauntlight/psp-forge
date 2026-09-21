#include "psp_forge.h"
#include <stddef.h>

static ForgeScene* s_current_scene = NULL;
static ForgeScene* s_next_scene    = NULL;

void forge_scene_set(ForgeScene* scene) {
    s_next_scene = scene;
}

ForgeScene* forge_scene_get_current(void) {
    return s_current_scene;
}

void forge_scene_update_and_draw(float dt) {
    /* Handle pending scene switch */
    if (s_next_scene != s_current_scene) {
        if (s_current_scene && s_current_scene->on_destroy) {
            s_current_scene->on_destroy(s_current_scene, 0.0f);
        }

        s_current_scene = s_next_scene;

        if (s_current_scene && s_current_scene->on_init) {
            s_current_scene->on_init(s_current_scene, 0.0f);
        }
    }

    if (!s_current_scene) return;

    /* Update current scene */
    if (s_current_scene->on_update) {
        s_current_scene->on_update(s_current_scene, dt);
    }

    /* Draw current scene */
    if (s_current_scene->on_draw) {
        s_current_scene->on_draw(s_current_scene, dt);
    }
}

void forge_scene_reset(void) {
    if (s_current_scene && s_current_scene->on_destroy) {
        s_current_scene->on_destroy(s_current_scene, 0.0f);
    }
    s_current_scene = NULL;
    s_next_scene    = NULL;
}
