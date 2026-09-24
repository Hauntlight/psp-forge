#include "psp_forge.h"
#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float    u, v;
    uint32_t color;
    float    x, y, z;
} FontVertex;

ForgeFont* forge_font_load(const char* fnt_path, const char* tex_path) {
    if (!fnt_path || !tex_path) return NULL;

    SceUID fd = forge_io_open(fnt_path);
    if (fd < 0) return NULL;

    ForgeFontHeader hdr;
    if (sceIoRead(fd, &hdr, sizeof(ForgeFontHeader)) != (int)sizeof(ForgeFontHeader)) {
        sceIoClose(fd);
        return NULL;
    }

    if (memcmp(hdr.magic, "FFNT", 4) != 0 || hdr.version != 1) {
        sceIoClose(fd);
        return NULL;
    }

    ForgeFont* font = (ForgeFont*)calloc(1, sizeof(ForgeFont));
    if (!font) {
        sceIoClose(fd);
        return NULL;
    }

    font->header = hdr;

    for (uint16_t i = 0; i < hdr.glyph_count; ++i) {
        ForgeGlyphDef g;
        if (sceIoRead(fd, &g, sizeof(ForgeGlyphDef)) != (int)sizeof(ForgeGlyphDef)) {
            free(font);
            sceIoClose(fd);
            return NULL;
        }
        if (g.char_code < 128) {
            font->glyphs[g.char_code] = g;
        }
    }
    sceIoClose(fd);

    font->texture = forge_texture_load(tex_path);
    if (!font->texture) {
        free(font);
        return NULL;
    }

    return font;
}

void forge_font_free(ForgeFont* font) {
    if (!font) return;
    if (font->texture) {
        forge_texture_free(font->texture);
        font->texture = NULL;
    }
    free(font);
}

float forge_font_get_text_width(const ForgeFont* font, const char* text, float scale) {
    if (!font || !text) return 0.0f;
    float w = 0.0f;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        unsigned char c = *p++;
        if (c < 128) {
            w += (float)font->glyphs[c].xadvance * scale;
        }
    }
    return w;
}

void forge_font_draw_text(const ForgeFont* font, const char* text, float x, float y, uint32_t color) {
    forge_font_draw_text_scaled(font, text, x, y, 1.0f, color);
}

void forge_font_draw_text_scaled(const ForgeFont* font, const char* text, float x, float y, float scale, uint32_t color) {
    if (!font || !font->texture || !text || !*text) return;

    size_t len = strlen(text);
    if (len == 0) return;

    /* Setup hardware texture & blending state */
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    const ForgeTexture* tex = font->texture;
    sceGuTexMode(tex->format, 0, 0, tex->is_swizzled);
    sceGuTexImage(0, tex->pwr2_w, tex->pwr2_h, tex->pwr2_w, tex->data);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);

    /* Allocate 2 vertices per quad on the active display list */
    FontVertex* verts = (FontVertex*)sceGuGetMemory(len * 2 * sizeof(FontVertex));
    if (!verts) return;

    float cur_x = x;
    int quad_count = 0;
    const unsigned char* p = (const unsigned char*)text;

    while (*p) {
        unsigned char c = *p++;
        if (c >= 128) continue;

        const ForgeGlyphDef* g = &font->glyphs[c];
        if (g->w > 0 && g->h > 0) {
            float gx0 = cur_x + (float)g->xoffset * scale;
            float gy0 = y     + (float)g->yoffset * scale;
            float gx1 = gx0   + (float)g->w * scale;
            float gy1 = gy0   + (float)g->h * scale;

            /* Sprite Quad: Vertex 0 (top-left) */
            verts[quad_count * 2].u     = g->u0 * tex->pwr2_w;
            verts[quad_count * 2].v     = g->v0 * tex->pwr2_h;
            verts[quad_count * 2].color = color;
            verts[quad_count * 2].x     = gx0;
            verts[quad_count * 2].y     = gy0;
            verts[quad_count * 2].z     = 0.0f;

            /* Sprite Quad: Vertex 1 (bottom-right) */
            verts[quad_count * 2 + 1].u     = g->u1 * tex->pwr2_w;
            verts[quad_count * 2 + 1].v     = g->v1 * tex->pwr2_h;
            verts[quad_count * 2 + 1].color = color;
            verts[quad_count * 2 + 1].x     = gx1;
            verts[quad_count * 2 + 1].y     = gy1;
            verts[quad_count * 2 + 1].z     = 0.0f;

            quad_count++;
        }

        cur_x += (float)g->xadvance * scale;
    }

    if (quad_count > 0) {
        sceGuDrawArray(
            GU_SPRITES,
            GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
            quad_count * 2,
            NULL,
            verts
        );
    }
}
