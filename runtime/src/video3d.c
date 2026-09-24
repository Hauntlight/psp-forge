#include "psp_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>

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
static int        s_lighting_enabled   = 0;
static bool       s_alpha_test_enabled = false;
static uint8_t    s_alpha_test_ref     = 0;

ForgeMesh* forge_mesh_load(const char* path) {
    SceUID fd = forge_io_open(path);
    if (fd < 0) return NULL;

    Pm3dHeader hdr;
    if (sceIoRead(fd, &hdr, sizeof(Pm3dHeader)) != (int)sizeof(Pm3dHeader)) {
        sceIoClose(fd);
        return NULL;
    }

    if (memcmp(hdr.magic, "PM3D", 4) != 0 || hdr.version != 1) {
        sceIoClose(fd);
        return NULL;
    }

    if (hdr.vertex_count == 0 || hdr.vertex_stride == 0 || hdr.vertex_count > 500000) {
        sceIoClose(fd);
        return NULL;
    }

    uint64_t total_size = (uint64_t)hdr.vertex_count * hdr.vertex_stride;
    if (total_size > 16 * 1024 * 1024) {
        sceIoClose(fd);
        return NULL;
    }
    uint32_t data_size = (uint32_t)total_size;

    ForgeMesh* mesh = (ForgeMesh*)calloc(1, sizeof(ForgeMesh));
    if (!mesh) {
        sceIoClose(fd);
        return NULL;
    }

    mesh->count         = hdr.vertex_count;
    mesh->vertex_format = hdr.vertex_format;
    mesh->vertex_stride = hdr.vertex_stride;
    memcpy(mesh->aabb_min, hdr.aabb_min, sizeof(float) * 3);
    memcpy(mesh->aabb_max, hdr.aabb_max, sizeof(float) * 3);
    memcpy(mesh->center,   hdr.center,   sizeof(float) * 3);
    mesh->radius        = hdr.radius;

    mesh->vertices = memalign(16, data_size);
    if (!mesh->vertices) {
        free(mesh);
        sceIoClose(fd);
        return NULL;
    }

    if (sceIoRead(fd, mesh->vertices, data_size) != (int)data_size) {
        free(mesh->vertices);
        free(mesh);
        sceIoClose(fd);
        return NULL;
    }
    sceIoClose(fd);

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
    for (int i = 0; i < 4; ++i) {
        sceGuDisable(GU_LIGHT0 + i);
    }
    sceGuDisable(GU_LIGHTING);
    s_lighting_enabled = 0;
}

void forge_cull_and_apply_lights(float obj_x, float obj_y, float obj_z) {
    /* Build candidate list: score each active light by squared distance */
    typedef struct { int id; float dist_sq; } LightCandidate;

    LightCandidate cand[FORGE_MAX_VIRTUAL_LIGHTS];
    int count = 0;

    for (int i = 0; i < FORGE_MAX_VIRTUAL_LIGHTS; ++i) {
        if (!s_virtual_lights[i].active || s_virtual_lights[i].intensity <= 0.001f) continue;
        float dx = s_virtual_lights[i].pos[0] - obj_x;
        float dy = s_virtual_lights[i].pos[1] - obj_y;
        float dz = s_virtual_lights[i].pos[2] - obj_z;
        cand[count].id = i;
        cand[count].dist_sq = dx * dx + dy * dy + dz * dz;
        count++;
    }

    int slots = (count < 4) ? count : 4;

    /* Partial selection sort for closest 4 lights */
    for (int s = 0; s < slots; ++s) {
        int min_idx = s;
        for (int j = s + 1; j < count; ++j) {
            if (cand[j].dist_sq < cand[min_idx].dist_sq) {
                min_idx = j;
            }
        }
        if (min_idx != s) {
            LightCandidate tmp = cand[s];
            cand[s] = cand[min_idx];
            cand[min_idx] = tmp;
        }
    }

    /* Ambient material so mesh is never fully black when lit */
    sceGuModelColor(0xFF404040, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000);

    /* Assign up to 4 lights to hardware slots GU_LIGHT0..3 */
    for (int slot = 0; slot < 4; ++slot) {
        if (slot < slots) {
            int lid = cand[slot].id;
            ScePspFVector3 lpos = {
                s_virtual_lights[lid].pos[0],
                s_virtual_lights[lid].pos[1],
                s_virtual_lights[lid].pos[2]
            };
            sceGuEnable(GU_LIGHT0 + slot);
            sceGuLight(slot, GU_POINTLIGHT, GU_DIFFUSE_AND_SPECULAR, &lpos);
            sceGuLightColor(slot, GU_DIFFUSE,  s_virtual_lights[lid].color);
            sceGuLightColor(slot, GU_SPECULAR, 0xFFFFFFFF);
            float att = (s_virtual_lights[lid].intensity > 0.001f)
                ? (1.0f / s_virtual_lights[lid].intensity)
                : 1.0f;
            sceGuLightAtt(slot, 1.0f, 0.05f * att, 0.005f * att);
        } else {
            sceGuDisable(GU_LIGHT0 + slot);
        }
    }

    if (slots > 0) {
        sceGuEnable(GU_LIGHTING);
        s_lighting_enabled = 1;
    } else {
        sceGuDisable(GU_LIGHTING);
        s_lighting_enabled = 0;
    }
}

void forge_draw_mesh_current(const ForgeMesh* mesh, const ForgeTexture* tex) {
    if (!mesh || !mesh->vertices) return;

    /* Setup Culling and Depth */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDisable(GU_CULL_FACE);

    /* Ensure fragment color is fully opaque white so modulation preserves texture */
    sceGuColor(0xFFFFFFFF);

    /* Texture configuration */
    if (tex && tex->data) {
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(tex->format, 0, 0, tex->is_swizzled ? 1 : 0);
        sceGuTexImage(0, tex->pwr2_w, tex->pwr2_h, tex->pwr2_w, tex->data);
        sceGuTexFunc(s_lighting_enabled ? GU_TFX_MODULATE : GU_TFX_REPLACE, GU_TCC_RGBA);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        if (tex->has_palette && tex->palette) {
            sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
            sceGuClutLoad(tex->palette_count / 8, tex->palette);
        }
    } else {
        sceGuDisable(GU_TEXTURE_2D);
    }

    if (s_lighting_enabled) {
        sceGuEnable(GU_LIGHTING);
    } else {
        sceGuDisable(GU_LIGHTING);
    }

    if (s_alpha_test_enabled) {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, s_alpha_test_ref, 0xFF);
    } else {
        sceGuDisable(GU_ALPHA_TEST);
    }

    /* Synchronize matrix stack to hardware and draw */
    sceGumUpdateMatrix();
    sceGumDrawArray(GU_TRIANGLES, mesh->vertex_format, mesh->count, 0, mesh->vertices);
}

void forge_set_alpha_test(bool enable, uint8_t ref_value) {
    s_alpha_test_enabled = enable;
    s_alpha_test_ref     = ref_value;
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

    forge_draw_mesh_current(mesh, tex);
}

void forge_draw_mesh_node(
    const ForgeMesh* mesh,
    const ForgeTexture* tex,
    float x, float y, float z,
    float rx_rad, float ry_rad, float rz_rad,
    float sx, float sy, float sz
) {
    if (!mesh || !mesh->vertices) return;

    sceGumPushMatrix();

    ScePspFVector3 pos = { x, y, z };
    sceGumTranslate(&pos);

    if (rx_rad != 0.0f) sceGumRotateX(rx_rad);
    if (ry_rad != 0.0f) sceGumRotateY(ry_rad);
    if (rz_rad != 0.0f) sceGumRotateZ(rz_rad);

    ScePspFVector3 sc = { sx, sy, sz };
    sceGumScale(&sc);

    forge_draw_mesh_current(mesh, tex);

    sceGumPopMatrix();
}

/* ========================================================================= */
/* 3D Camera & Cinematic Transitions                                         */
/* ========================================================================= */

void forge_camera3d_init(ForgeCamera3D* cam, float fov_deg) {
    if (!cam) return;
    memset(cam, 0, sizeof(ForgeCamera3D));
    cam->eye.x    = 0.0f;
    cam->eye.y    = 1.0f;
    cam->eye.z    = 3.5f;
    cam->target.x = 0.0f;
    cam->target.y = 0.8f;
    cam->target.z = 0.0f;
    cam->up.x     = 0.0f;
    cam->up.y     = 1.0f;
    cam->up.z     = 0.0f;
    cam->fov      = fov_deg > 0.0f ? fov_deg : 45.0f;
    cam->is_lerping = false;
    cam->lerp_speed = 5.0f;
}

void forge_camera3d_set(ForgeCamera3D* cam, float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z) {
    if (!cam) return;
    cam->eye.x    = eye_x;
    cam->eye.y    = eye_y;
    cam->eye.z    = eye_z;
    cam->target.x = target_x;
    cam->target.y = target_y;
    cam->target.z = target_z;
    cam->is_lerping = false;
}

void forge_camera3d_lerp_to(ForgeCamera3D* cam, float target_eye_x, float target_eye_y, float target_eye_z, float target_look_x, float target_look_y, float target_look_z, float speed) {
    if (!cam) return;
    cam->target_eye.x    = target_eye_x;
    cam->target_eye.y    = target_eye_y;
    cam->target_eye.z    = target_eye_z;
    cam->target_target.x = target_look_x;
    cam->target_target.y = target_look_y;
    cam->target_target.z = target_look_z;
    cam->lerp_speed      = speed > 0.0f ? speed : 5.0f;
    cam->is_lerping      = true;
}

void forge_camera3d_update(ForgeCamera3D* cam, float dt) {
    if (!cam || !cam->is_lerping) return;
    float t = dt * cam->lerp_speed;
    if (t > 1.0f) t = 1.0f;

    cam->eye.x    += (cam->target_eye.x    - cam->eye.x)    * t;
    cam->eye.y    += (cam->target_eye.y    - cam->eye.y)    * t;
    cam->eye.z    += (cam->target_eye.z    - cam->eye.z)    * t;
    cam->target.x += (cam->target_target.x - cam->target.x) * t;
    cam->target.y += (cam->target_target.y - cam->target.y) * t;
    cam->target.z += (cam->target_target.z - cam->target.z) * t;

    float dx = cam->target_eye.x - cam->eye.x;
    float dy = cam->target_eye.y - cam->eye.y;
    float dz = cam->target_eye.z - cam->eye.z;
    if (dx * dx + dy * dy + dz * dz < 0.0001f) {
        cam->eye = cam->target_eye;
        cam->target = cam->target_target;
        cam->is_lerping = false;
    }
}

void forge_camera3d_apply(const ForgeCamera3D* cam) {
    if (!cam) return;
    forge_set_camera(
        cam->eye.x, cam->eye.y, cam->eye.z,
        cam->target.x, cam->target.y, cam->target.z,
        cam->fov
    );
}


