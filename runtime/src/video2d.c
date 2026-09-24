#include "psp_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

typedef struct __attribute__((packed)) {
    char     magic[4];      /* "PTEX" */
    uint16_t version;       /* 1 */
    uint16_t format;        /* PSM_* */
    uint16_t orig_w;
    uint16_t orig_h;
    uint16_t pwr2_w;
    uint16_t pwr2_h;
    uint8_t  is_swizzled;
    uint8_t  has_palette;
    uint16_t palette_count;
    uint8_t  reserved[12];
} PtexHeader;

typedef struct {
    float u, v;
    float x, y, z;
} ForgeVertex2D;

typedef struct {
    uint32_t color;
    float x, y, z;
} ForgeColorVertex2D;

static ForgeTexture* load_texture_internal(const char* path, bool to_vram) {
    SceUID fd = forge_io_open(path);
    if (fd < 0) return NULL;

    PtexHeader hdr;
    if (sceIoRead(fd, &hdr, sizeof(PtexHeader)) != (int)sizeof(PtexHeader)) {
        sceIoClose(fd);
        return NULL;
    }

    if (memcmp(hdr.magic, "PTEX", 4) != 0 || hdr.version != 1) {
        sceIoClose(fd);
        return NULL;
    }

    if (hdr.pwr2_w == 0 || hdr.pwr2_h == 0 || hdr.pwr2_w > 512 || hdr.pwr2_h > 512) {
        sceIoClose(fd);
        return NULL;
    }

    ForgeTexture* tex = (ForgeTexture*)calloc(1, sizeof(ForgeTexture));
    if (!tex) {
        sceIoClose(fd);
        return NULL;
    }

    tex->width         = hdr.orig_w;
    tex->height        = hdr.orig_h;
    tex->pwr2_w        = hdr.pwr2_w;
    tex->pwr2_h        = hdr.pwr2_h;
    tex->format        = (uint8_t)hdr.format;
    tex->is_swizzled   = hdr.is_swizzled;
    tex->has_palette   = hdr.has_palette;
    tex->palette_count = hdr.palette_count;
    tex->in_vram       = to_vram;

    /* Read palette if present (must be 16-byte aligned for GPU CLUT DMA) */
    if (tex->has_palette) {
        if (tex->palette_count == 0 || tex->palette_count > 256) {
            free(tex);
            sceIoClose(fd);
            return NULL;
        }
        uint32_t pal_size = tex->palette_count * 4; /* RGBA8888 */
        tex->palette = memalign(16, pal_size);
        if (!tex->palette) {
            free(tex);
            sceIoClose(fd);
            return NULL;
        }
        if (sceIoRead(fd, tex->palette, pal_size) != (int)pal_size) {
            free(tex->palette);
            free(tex);
            sceIoClose(fd);
            return NULL;
        }
        sceKernelDcacheWritebackRange(tex->palette, pal_size);
    }

    /* Compute pixel buffer size */
    uint32_t bpp_shift = 2; /* Default 4 bytes per pixel */
    if (tex->format == GU_PSM_5551 || tex->format == GU_PSM_4444 || tex->format == GU_PSM_5650) {
        bpp_shift = 1; /* 2 bytes */
    } else if (tex->format == GU_PSM_T8) {
        bpp_shift = 0; /* 1 byte */
    }

    uint32_t pixel_bytes = 0;
    if (tex->format == GU_PSM_T4) {
        pixel_bytes = (tex->pwr2_w * tex->pwr2_h) / 2;
    } else {
        pixel_bytes = (tex->pwr2_w * tex->pwr2_h) << bpp_shift;
    }

    if (pixel_bytes == 0 || pixel_bytes > 2 * 1024 * 1024) {
        if (tex->palette) free(tex->palette);
        free(tex);
        sceIoClose(fd);
        return NULL;
    }

    bool read_ok = false;
    if (to_vram) {
        void* vram_rel = forge_vram_alloc(pixel_bytes);
        if (vram_rel) {
            void* uncached_cpu = forge_vram_to_uncached_cpu(vram_rel);
            if (sceIoRead(fd, uncached_cpu, pixel_bytes) == (int)pixel_bytes) {
                tex->data = vram_rel;
                read_ok = true;
            }
        }
        if (!read_ok) {
            tex->in_vram = false;
            tex->data = memalign(16, pixel_bytes);
            if (tex->data) {
                if (sceIoRead(fd, tex->data, pixel_bytes) == (int)pixel_bytes) {
                    sceKernelDcacheWritebackRange(tex->data, pixel_bytes);
                    read_ok = true;
                }
            }
        }
    } else {
        tex->data = memalign(16, pixel_bytes);
        if (tex->data) {
            if (sceIoRead(fd, tex->data, pixel_bytes) == (int)pixel_bytes) {
                sceKernelDcacheWritebackRange(tex->data, pixel_bytes);
                read_ok = true;
            }
        }
    }

    sceIoClose(fd);

    if (!read_ok || !tex->data) {
        if (tex->data && !tex->in_vram) free(tex->data);
        if (tex->palette) free(tex->palette);
        free(tex);
        return NULL;
    }

    return tex;
}

ForgeTexture* forge_texture_load(const char* path) {
    return load_texture_internal(path, false);
}

ForgeTexture* forge_texture_load_vram(const char* path) {
    return load_texture_internal(path, true);
}

void forge_texture_free(ForgeTexture* tex) {
    if (!tex) return;
    if (!tex->in_vram && tex->data) {
        free(tex->data);
    }
    if (tex->palette) {
        free(tex->palette);
    }
    free(tex);
}

void forge_draw_sprite(
    const ForgeTexture* tex,
    float sx, float sy, float sw, float sh,
    float tx, float ty, float tw, float th
) {
    if (!tex || !tex->data) return;

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(tex->format, 0, 0, tex->is_swizzled ? 1 : 0);
    sceGuTexImage(0, tex->pwr2_w, tex->pwr2_h, tex->pwr2_w, tex->data);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
    sceGuDisable(GU_DEPTH_TEST);

    if (tex->has_palette && tex->palette) {
        sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
        sceGuClutLoad(tex->palette_count / 8, tex->palette);
    }

    ForgeVertex2D* vtx = (ForgeVertex2D*)sceGuGetMemory(2 * sizeof(ForgeVertex2D));
    if (!vtx) return;

    vtx[0].u = tx;
    vtx[0].v = ty;
    vtx[0].x = sx;
    vtx[0].y = sy;
    vtx[0].z = 0.0f;

    vtx[1].u = tx + tw;
    vtx[1].v = ty + th;
    vtx[1].x = sx + sw;
    vtx[1].y = sy + sh;
    vtx[1].z = 0.0f;

    sceGuDrawArray(
        GU_SPRITES,
        GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        2,
        NULL,
        vtx
    );

    /* Restore state: re-enable depth test so any subsequent 3D draw calls
     * are not broken by the 2D state we set above. */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
}

void forge_parallax_draw_bg(const ForgeTexture* tex, float cam_x, float factor) {
    if (!tex || !tex->data) return;

    float offset_x = (480.0f - (float)tex->width) * 0.5f - (cam_x * factor);
    float offset_y = 0.0f;
    if (tex->height >= 272) {
        offset_y = 272.0f - (float)tex->height;
    } else {
        offset_y = (272.0f - (float)tex->height) * 0.5f;
    }

    sceGuDepthMask(GU_TRUE);
    forge_draw_sprite(
        tex,
        offset_x, offset_y, (float)tex->width, (float)tex->height,
        0.0f, 0.0f, (float)tex->width, (float)tex->height
    );
    sceGuDepthMask(GU_FALSE);
}

void forge_parallax_draw_fg(const ForgeTexture* tex, float cam_x, float factor) {
    if (!tex || !tex->data) return;

    float offset_x = (480.0f - (float)tex->width) * 0.5f - (cam_x * factor);
    float offset_y = 0.0f;
    if (tex->height >= 272) {
        offset_y = 272.0f - (float)tex->height;
    } else {
        offset_y = (272.0f - (float)tex->height) * 0.5f;
    }

    sceGuDepthMask(GU_TRUE);
    forge_draw_sprite(
        tex,
        offset_x, offset_y, (float)tex->width, (float)tex->height,
        0.0f, 0.0f, (float)tex->width, (float)tex->height
    );
    sceGuDepthMask(GU_FALSE);
}

void forge_draw_pause_overlay(const ForgeFont* font, const char* message, uint32_t overlay_color) {
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);

    ForgeColorVertex2D* vtx = (ForgeColorVertex2D*)sceGuGetMemory(2 * sizeof(ForgeColorVertex2D));
    if (vtx) {
        vtx[0].color = overlay_color;
        vtx[0].x = 0.0f;
        vtx[0].y = 0.0f;
        vtx[0].z = 0.0f;

        vtx[1].color = overlay_color;
        vtx[1].x = 480.0f;
        vtx[1].y = 272.0f;
        vtx[1].z = 0.0f;

        sceGuDrawArray(
            GU_SPRITES,
            GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
            2,
            NULL,
            vtx
        );
    }

    if (font && message) {
        float tw = forge_font_get_text_width(font, message, 1.0f);
        float tx = (480.0f - tw) * 0.5f;
        float ty = (272.0f - (float)font->header.line_height) * 0.5f;
        forge_font_draw_text(font, message, tx, ty, 0xFFFFFFFF);
    }

    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}
