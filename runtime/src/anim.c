#include "psp_forge.h"

void forge_anim2d_init(
    ForgeSpriteAnim* anim,
    const ForgeTexture* tex,
    int frame_w, int frame_h,
    int num_frames,
    float fps,
    bool loop
) {
    if (!anim) return;
    anim->texture       = tex;
    anim->frame_w       = frame_w;
    anim->frame_h       = frame_h;
    anim->num_frames    = num_frames;
    anim->fps           = (fps > 0.0f) ? fps : 10.0f;
    anim->timer         = 0.0f;
    anim->current_frame = 0;
    anim->loop          = loop;
    anim->is_playing    = true;

    if (tex && frame_w > 0) {
        anim->columns = tex->width / frame_w;
        if (anim->columns < 1) anim->columns = 1;
    } else {
        anim->columns = 1;
    }
}

void forge_anim2d_update(ForgeSpriteAnim* anim, float dt) {
    if (!anim || !anim->is_playing || anim->num_frames <= 1) return;

    anim->timer += dt;
    float frame_duration = 1.0f / anim->fps;

    while (anim->timer >= frame_duration) {
        anim->timer -= frame_duration;
        anim->current_frame++;
        if (anim->current_frame >= anim->num_frames) {
            if (anim->loop) {
                anim->current_frame = 0;
            } else {
                anim->current_frame = anim->num_frames - 1;
                anim->is_playing = false;
                break;
            }
        }
    }
}

void forge_anim2d_draw(
    const ForgeSpriteAnim* anim,
    float x, float y,
    float w, float h
) {
    if (!anim || !anim->texture) return;

    int col = anim->current_frame % anim->columns;
    int row = anim->current_frame / anim->columns;

    float tx = (float)(col * anim->frame_w);
    float ty = (float)(row * anim->frame_h);

    forge_draw_sprite(
        anim->texture,
        x, y, w, h,
        tx, ty, (float)anim->frame_w, (float)anim->frame_h
    );
}

void forge_anim2d_set_frame(ForgeSpriteAnim* anim, int frame) {
    if (!anim) return;
    if (frame < 0) frame = 0;
    if (frame >= anim->num_frames) frame = anim->num_frames - 1;
    anim->current_frame = frame;
    anim->timer = 0.0f;
}
