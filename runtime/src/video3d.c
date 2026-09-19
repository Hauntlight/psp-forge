#include "psp_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct __attribute__((packed)) {
    char     magic[4];       /* "PM3D" */
    uint16_t version;        /* 1 */
    uint32_t vertex_format;  /* GU_* flags */
    uint16_t vertex_stride;  /* Stride in bytes */
    uint32_t vertex_count;
    float    aabb_min[3];
    float    aabb_max[3];
    float    center[3];
    float    radius;
    uint8_t  reserved[4];
} Pm3dHeader;

static ForgeLight s_virtual_lights[FORGE_MAX_VIRTUAL_LIGHTS];

ForgeMesh* forge_mesh_load(const char* path) {
    FILE* f = forge_fopen(path, "rb");
    if (!f) return NULL;

    Pm3dHeader hdr;
    if (fread(&hdr, sizeof(Pm3dHeader), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (memcmp(hdr.magic, "PM3D", 4) != 0) {
        fclose(f);
        return NULL;
    }

    ForgeMesh* mesh = (ForgeMesh*)calloc(1, sizeof(ForgeMesh));
    if (!mesh) {
        fclose(f);
        return NULL;
    }

    mesh->count         = hdr.vertex_count;
    mesh->vertex_format = hdr.vertex_format;
    mesh->vertex_stride = hdr.vertex_stride;
    memcpy(mesh->aabb_min, hdr.aabb_min, sizeof(float) * 3);
    memcpy(mesh->aabb_max, hdr.aabb_max, sizeof(float) * 3);
    memcpy(mesh->center,   hdr.center,   sizeof(float) * 3);
    mesh->radius        = hdr.radius;

    uint32_t data_size = mesh->count * mesh->vertex_stride;
    mesh->vertices = memalign(16, data_size);
    if (!mesh->vertices) {
        free(mesh);
        fclose(f);
        return NULL;
    }

    fread(mesh->vertices, data_size, 1, f);
    fclose(f);

    /* Flush D-Cache to guarantee DMA read consistency */
    sceKernelDcacheWritebackRange(mesh->vertices, data_size);

    return mesh;
}

void forge_mesh_free(ForgeMesh* mesh) {
    if (!mesh) return;
    if (mesh->vertices) {
        free(mesh->vertices);
    }
    free(mesh);
}

void forge_set_camera(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float fov_degrees
) {
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fov_degrees, 16.0f / 9.0f, 0.5f, 1000.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye    = { eye_x, eye_y, eye_z };
    ScePspFVector3 center = { target_x, target_y, target_z };
    ScePspFVector3 up     = { 0.0f, 1.0f, 0.0f };
    sceGumLookAt(&eye, &center, &up);
}

void forge_set_light(uint8_t id, float x, float y, float z, uint32_t color_rgba, float intensity) {
    if (id >= FORGE_MAX_VIRTUAL_LIGHTS) return;
    s_virtual_lights[id].pos[0]    = x;
    s_virtual_lights[id].pos[1]    = y;
    s_virtual_lights[id].pos[2]    = z;
    s_virtual_lights[id].color     = color_rgba;
    s_virtual_lights[id].intensity = intensity;
    s_virtual_lights[id].active    = true;
}

void forge_clear_lights(void) {
    memset(s_virtual_lights, 0, sizeof(s_virtual_lights));
}

void forge_cull_and_apply_lights(float obj_x, float obj_y, float obj_z) {
    /* Score each active light by distance to the object */
    typedef struct {
        int   id;
        float dist_sq;
    } LightCandidate;

    LightCandidate cand[FORGE_MAX_VIRTUAL_LIGHTS];
    int count = 0;

    for (int i = 0; i < FORGE_MAX_VIRTUAL_LIGHTS; ++i) {
        if (!s_virtual_lights[i].active) continue;
        float dx = s_virtual_lights[i].pos[0] - obj_x;
        float dy = s_virtual_lights[i].pos[1] - obj_y;
        float dz = s_virtual_lights[i].pos[2] - obj_z;
        cand[count].id = i;
        cand[count].dist_sq = dx * dx + dy * dy + dz * dz;
        count++;
    }

    /* Sort ascending: closest 4 lights */
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (cand[j].dist_sq < cand[i].dist_sq) {
                LightCandidate tmp = cand[i];
                cand[i] = cand[j];
                cand[j] = tmp;
            }
        }
    }

    /* Assign up to 4 lights to hardware slots GU_LIGHT0..3 */
    int assigned = 0;
    for (int slot = 0; slot < 4; ++slot) {
        if (slot < count) {
            int lid = cand[slot].id;
            ScePspFVector3 lpos = {
                s_virtual_lights[lid].pos[0],
                s_virtual_lights[lid].pos[1],
                s_virtual_lights[lid].pos[2]
            };
            sceGuEnable(GU_LIGHT0 + slot);
            sceGuLight(slot, GU_POINTLIGHT, GU_DIFFUSE_AND_SPECULAR, &lpos);
            sceGuLightColor(slot, GU_DIFFUSE, s_virtual_lights[lid].color);
            sceGuLightColor(slot, GU_SPECULAR, 0xFFFFFFFF);
            float att = (s_virtual_lights[lid].intensity > 0.001f)
                ? (1.0f / s_virtual_lights[lid].intensity)
                : 1.0f;
            sceGuLightAtt(slot, 1.0f, 0.05f * att, 0.005f * att);
            assigned++;
        } else {
            sceGuDisable(GU_LIGHT0 + slot);
        }
    }

    if (assigned > 0) {
        sceGuEnable(GU_LIGHTING);
    } else {
        sceGuDisable(GU_LIGHTING);
    }
}

void forge_draw_mesh(
    const ForgeMesh* mesh,
    const ForgeTexture* tex,
    float x, float y, float z,
    float rx_rad, float ry_rad, float rz_rad,
    float sx, float sy, float sz
) {
    if (!mesh || !mesh->vertices) return;

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    ScePspFVector3 pos = { x, y, z };
    sceGumTranslate(&pos);

    if (rx_rad != 0.0f) sceGumRotateX(rx_rad);
    if (ry_rad != 0.0f) sceGumRotateY(ry_rad);
    if (rz_rad != 0.0f) sceGumRotateZ(rz_rad);

    ScePspFVector3 sc = { sx, sy, sz };
    sceGumScale(&sc);

    /* Setup Culling and Depth */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CCW);

    /* Apply closest lights */
    forge_cull_and_apply_lights(x, y, z);

    /* Texture configuration */
    if (tex && tex->data) {
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(tex->format, 0, 0, tex->is_swizzled ? 1 : 0);
        sceGuTexImage(0, tex->pwr2_w, tex->pwr2_h, tex->pwr2_w, tex->data);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    } else {
        sceGuDisable(GU_TEXTURE_2D);
    }

    /* Draw */
    sceGumDrawArray(GU_TRIANGLES, mesh->vertex_format, mesh->count, 0, mesh->vertices);
}
