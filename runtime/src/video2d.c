#include "psp_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    float   u, v;
    int16_t x, y, z;
} ForgeVertex2D;

static ForgeTexture* load_texture_internal(const char* path, bool to_vram) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    PtexHeader hdr;
    if (fread(&hdr, sizeof(PtexHeader), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (memcmp(hdr.magic, "PTEX", 4) != 0) {
        fclose(f);
        return NULL;
    }

    ForgeTexture* tex = (ForgeTexture*)calloc(1, sizeof(ForgeTexture));
    if (!tex) {
        fclose(f);
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

    /* Read palette if present */
    if (tex->has_palette && tex->palette_count > 0) {
        uint32_t pal_size = tex->palette_count * 4; /* RGBA8888 */
        tex->palette = malloc(pal_size);
        if (tex->palette) {
            fread(tex->palette, pal_size, 1, f);
            sceKernelDcacheWritebackRange(tex->palette, pal_size);
        }
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

    if (to_vram) {
        void* vram_rel = forge_vram_alloc(pixel_bytes);
        if (vram_rel) {
            void* uncached_cpu = forge_vram_to_uncached_cpu(vram_rel);
            fread(uncached_cpu, pixel_bytes, 1, f);
            tex->data = vram_rel;
        } else {
            /* Fallback to RAM if VRAM scratchpad is exhausted */
            tex->in_vram = false;
            tex->data = memalign(16, pixel_bytes);
            if (tex->data) {
                fread(tex->data, pixel_bytes, 1, f);
                sceKernelDcacheWritebackRange(tex->data, pixel_bytes);
            }
        }
    } else {
        tex->data = memalign(16, pixel_bytes);
        if (tex->data) {
            fread(tex->data, pixel_bytes, 1, f);
            sceKernelDcacheWritebackRange(tex->data, pixel_bytes);
        }
    }

    fclose(f);

    if (!tex->data) {
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
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);

    if (tex->has_palette && tex->palette) {
        sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
        sceGuClutLoad(tex->palette_count / 8, tex->palette);
    }

    ForgeVertex2D* vtx = (ForgeVertex2D*)sceGuGetMemory(2 * sizeof(ForgeVertex2D));
    if (!vtx) return;

    vtx[0].u = tx;
    vtx[0].v = ty;
    vtx[0].x = (int16_t)sx;
    vtx[0].y = (int16_t)sy;
    vtx[0].z = 0;

    vtx[1].u = tx + tw;
    vtx[1].v = ty + th;
    vtx[1].x = (int16_t)(sx + sw);
    vtx[1].y = (int16_t)(sy + sh);
    vtx[1].z = 0;

    sceGuDrawArray(
        GU_SPRITES,
        GU_TEXTURE_32BITF | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        2,
        NULL,
        vtx
    );
}
