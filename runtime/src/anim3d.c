#include "psp_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>

/* ========================================================================= */
/* Quaternion Mathematics                                                    */
/* ========================================================================= */

void forge_quat_identity(ForgeQuat* q) {
    if (!q) return;
    q->x = 0.0f;
    q->y = 0.0f;
    q->z = 0.0f;
    q->w = 1.0f;
}

void forge_quat_normalize(ForgeQuat* q) {
    if (!q) return;
    float len_sq = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
    if (len_sq > 1e-8f) {
        float inv = 1.0f / sqrtf(len_sq);
        q->x *= inv;
        q->y *= inv;
        q->z *= inv;
        q->w *= inv;
    } else {
        q->x = 0.0f;
        q->y = 0.0f;
        q->z = 0.0f;
        q->w = 1.0f;
    }
}

void forge_quat_slerp(ForgeQuat* out, const ForgeQuat* a, const ForgeQuat* b, float t) {
    if (!out || !a || !b) return;

    float cos_half_theta = a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;

    ForgeQuat target = *b;
    if (cos_half_theta < 0.0f) {
        /* Shortest arc path on 4D hypersphere */
        target.x = -target.x;
        target.y = -target.y;
        target.z = -target.z;
        target.w = -target.w;
        cos_half_theta = -cos_half_theta;
    }

    /* If quaternions are very close, fallback to NLERP to avoid division by zero */
    if (cos_half_theta > 0.9995f) {
        out->x = a->x + t * (target.x - a->x);
        out->y = a->y + t * (target.y - a->y);
        out->z = a->z + t * (target.z - a->z);
        out->w = a->w + t * (target.w - a->w);
        forge_quat_normalize(out);
        return;
    }

    float half_theta = acosf(cos_half_theta);
    float sin_half_theta = sqrtf(1.0f - cos_half_theta * cos_half_theta);

    if (fabsf(sin_half_theta) < 1e-4f) {
        out->x = (a->x + target.x) * 0.5f;
        out->y = (a->y + target.y) * 0.5f;
        out->z = (a->z + target.z) * 0.5f;
        out->w = (a->w + target.w) * 0.5f;
        forge_quat_normalize(out);
        return;
    }

    float ratio_a = sinf((1.0f - t) * half_theta) / sin_half_theta;
    float ratio_b = sinf(t * half_theta) / sin_half_theta;

    out->x = a->x * ratio_a + target.x * ratio_b;
    out->y = a->y * ratio_a + target.y * ratio_b;
    out->z = a->z * ratio_a + target.z * ratio_b;
    out->w = a->w * ratio_a + target.w * ratio_b;
    forge_quat_normalize(out);
}

void forge_quat_to_matrix(ScePspFMatrix4* m, const ForgeQuat* q, const float pos[3]) {
    if (!m || !q) return;

    float x = q->x;
    float y = q->y;
    float z = q->z;
    float w = q->w;

    float x2 = x + x;
    float y2 = y + y;
    float z2 = z + z;

    float xx = x * x2;
    float xy = x * y2;
    float xz = x * z2;
    float yy = y * y2;
    float yz = y * z2;
    float zz = z * z2;
    float wx = w * x2;
    float wy = w * y2;
    float wz = w * z2;

    /* Column 0 */
    m->x.x = 1.0f - (yy + zz);
    m->x.y = xy + wz;
    m->x.z = xz - wy;
    m->x.w = 0.0f;

    /* Column 1 */
    m->y.x = xy - wz;
    m->y.y = 1.0f - (xx + zz);
    m->y.z = yz + wx;
    m->y.w = 0.0f;

    /* Column 2 */
    m->z.x = xz + wy;
    m->z.y = yz - wx;
    m->z.z = 1.0f - (xx + yy);
    m->z.w = 0.0f;

    /* Column 3 (Translation) */
    m->w.x = pos ? pos[0] : 0.0f;
    m->w.y = pos ? pos[1] : 0.0f;
    m->w.z = pos ? pos[2] : 0.0f;
    m->w.w = 1.0f;
}

/* ========================================================================= */
/* Animation Clip Loader & Lifecycle                                         */
/* ========================================================================= */

ForgeAnimClip* forge_anim3d_load(const char* path) {
    SceUID fd = forge_io_open(path);
    if (fd < 0) return NULL;

    PanmHeader hdr;
    if (sceIoRead(fd, &hdr, sizeof(PanmHeader)) != (int)sizeof(PanmHeader)) {
        sceIoClose(fd);
        return NULL;
    }

    if (memcmp(hdr.magic, "PANM", 4) != 0 || hdr.version != 1) {
        sceIoClose(fd);
        return NULL;
    }

    if (hdr.bone_count == 0 || hdr.frame_count == 0 || hdr.bone_count > FORGE_MAX_BONES) {
        sceIoClose(fd);
        return NULL;
    }

    uint32_t sample_total = hdr.frame_count * (uint32_t)hdr.bone_count;
    uint32_t samples_bytes = sample_total * sizeof(ForgeBoneSample);

    ForgeAnimClip* clip = (ForgeAnimClip*)calloc(1, sizeof(ForgeAnimClip));
    if (!clip) {
        sceIoClose(fd);
        return NULL;
    }

    clip->header = hdr;
    clip->samples = (ForgeBoneSample*)memalign(16, samples_bytes);
    if (!clip->samples) {
        free(clip);
        sceIoClose(fd);
        return NULL;
    }

    if (sceIoRead(fd, clip->samples, samples_bytes) != (int)samples_bytes) {
        free(clip->samples);
        free(clip);
        sceIoClose(fd);
        return NULL;
    }

    sceIoClose(fd);
    sceKernelDcacheWritebackRange(clip->samples, samples_bytes);
    return clip;
}

void forge_anim3d_free(ForgeAnimClip* clip) {
    if (!clip) return;
    if (clip->samples) {
        free(clip->samples);
        clip->samples = NULL;
    }
    free(clip);
}

/* ========================================================================= */
/* Animator State Machine                                                    */
/* ========================================================================= */

void forge_anim3d_init(ForgeAnimator* animator) {
    if (!animator) return;
    memset(animator, 0, sizeof(ForgeAnimator));
    animator->speed = 1.0f;

    for (int i = 0; i < FORGE_MAX_BONES; ++i) {
        gumLoadIdentity(&animator->world_matrices[i]);
        gumLoadIdentity(&animator->skin_matrices[i]);
    }
}

void forge_anim3d_play(ForgeAnimator* animator, const ForgeAnimClip* clip, bool loop) {
    if (!animator) return;
    animator->clip       = clip;
    animator->time       = 0.0f;
    animator->loop       = loop;
    animator->is_playing = (clip != NULL);
    animator->finished   = false;
}

void forge_anim3d_stop(ForgeAnimator* animator) {
    if (!animator) return;
    animator->is_playing = false;
}

void forge_anim3d_set_speed(ForgeAnimator* animator, float speed) {
    if (!animator) return;
    animator->speed = speed;
}

void forge_anim3d_update(ForgeAnimator* animator, const ForgeModel3D* model, float dt) {
    if (!animator) return;

    if (!animator->clip || !animator->is_playing) {
        /* If no active clip, keep or compute rest pose */
        if (model && model->bones) {
            for (uint16_t i = 0; i < model->bone_count && i < FORGE_MAX_BONES; ++i) {
                ForgeQuat rest_q;
                rest_q.x = model->bones[i].local_rot[0];
                rest_q.y = model->bones[i].local_rot[1];
                rest_q.z = model->bones[i].local_rot[2];
                rest_q.w = model->bones[i].local_rot[3];
                forge_quat_normalize(&rest_q);

                ScePspFMatrix4 m_local;
                forge_quat_to_matrix(&m_local, &rest_q, model->bones[i].local_pos);

                uint8_t parent = model->bones[i].parent_index;
                if (parent == 0xFF || parent >= FORGE_MAX_BONES) {
                    animator->world_matrices[i] = m_local;
                } else {
                    gumMultMatrix(&animator->world_matrices[i], &animator->world_matrices[parent], &m_local);
                }

                ScePspFMatrix4 inv_bind;
                memcpy(&inv_bind, model->bones[i].inv_bind_matrix, sizeof(ScePspFMatrix4));
                gumMultMatrix(&animator->skin_matrices[i], &animator->world_matrices[i], &inv_bind);
            }
        }
        return;
    }

    const ForgeAnimClip* clip = animator->clip;
    animator->time += dt * animator->speed;

    if (animator->time >= clip->header.duration) {
        if (animator->loop) {
            if (clip->header.duration > 1e-4f) {
                animator->time = fmodf(animator->time, clip->header.duration);
            } else {
                animator->time = 0.0f;
            }
        } else {
            animator->time = clip->header.duration;
            animator->finished = true;
            animator->is_playing = false;
        }
    }

    float frame_float = animator->time * clip->header.framerate;
    int f0 = (int)frame_float;
    if (f0 >= (int)clip->header.frame_count - 1) {
        f0 = (int)clip->header.frame_count - 1;
    }
    int f1 = f0 + 1;
    if (f1 >= (int)clip->header.frame_count) {
        f1 = animator->loop ? 0 : (int)clip->header.frame_count - 1;
    }

    float alpha = frame_float - (float)f0;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    uint16_t bone_count = clip->header.bone_count;
    if (bone_count > FORGE_MAX_BONES) bone_count = FORGE_MAX_BONES;
    if (model && model->bone_count < bone_count) bone_count = model->bone_count;

    float pos_scale = clip->header.pos_scale;
    if (pos_scale <= 0.0f) pos_scale = 0.001f;

    for (uint16_t i = 0; i < bone_count; ++i) {
        const ForgeBoneSample* s0 = &clip->samples[f0 * clip->header.bone_count + i];
        const ForgeBoneSample* s1 = &clip->samples[f1 * clip->header.bone_count + i];

        ForgeQuat q0 = {
            (float)s0->rot_quat[0] / 32767.0f,
            (float)s0->rot_quat[1] / 32767.0f,
            (float)s0->rot_quat[2] / 32767.0f,
            (float)s0->rot_quat[3] / 32767.0f
        };
        ForgeQuat q1 = {
            (float)s1->rot_quat[0] / 32767.0f,
            (float)s1->rot_quat[1] / 32767.0f,
            (float)s1->rot_quat[2] / 32767.0f,
            (float)s1->rot_quat[3] / 32767.0f
        };

        ForgeQuat q_interp;
        forge_quat_slerp(&q_interp, &q0, &q1, alpha);

        float p0[3] = {
            (float)s0->pos[0] * pos_scale,
            (float)s0->pos[1] * pos_scale,
            (float)s0->pos[2] * pos_scale
        };
        float p1[3] = {
            (float)s1->pos[0] * pos_scale,
            (float)s1->pos[1] * pos_scale,
            (float)s1->pos[2] * pos_scale
        };

        float p_interp[3] = {
            p0[0] + alpha * (p1[0] - p0[0]),
            p0[1] + alpha * (p1[1] - p0[1]),
            p0[2] + alpha * (p1[2] - p0[2])
        };

        ScePspFMatrix4 m_local;
        forge_quat_to_matrix(&m_local, &q_interp, p_interp);

        uint8_t parent = model ? model->bones[i].parent_index : 0xFF;
        if (parent == 0xFF || parent >= FORGE_MAX_BONES) {
            animator->world_matrices[i] = m_local;
        } else {
            gumMultMatrix(&animator->world_matrices[i], &animator->world_matrices[parent], &m_local);
        }

        if (model) {
            ScePspFMatrix4 inv_bind;
            memcpy(&inv_bind, model->bones[i].inv_bind_matrix, sizeof(ScePspFMatrix4));
            gumMultMatrix(&animator->skin_matrices[i], &animator->world_matrices[i], &inv_bind);
        }
    }
}

/* ========================================================================= */
/* Model3D (Multi-Chunk / Skeletal Model) Implementation                     */
/* ========================================================================= */

typedef struct __attribute__((packed)) {
    char     magic[4];       /* "P3D2" */
    uint16_t version;        /* 2 */
    uint16_t bone_count;
    uint16_t chunk_count;
    uint8_t  reserved[8];
} P3d2Header;

typedef struct __attribute__((packed)) {
    int16_t  node_index;      /* -1 = skinned, >= 0 = rigid bone index */
    uint8_t  num_local_bones; /* 0..8 */
    uint8_t  bone_palette[8];
    uint32_t vertex_format;
    uint16_t vertex_stride;
    uint32_t vertex_count;
    float    aabb_min[3];
    float    aabb_max[3];
    float    center[3];
    float    radius;
    uint8_t  reserved[4];
} P3d2ChunkHeader;

ForgeModel3D* forge_model3d_load(const char* path) {
    SceUID fd = forge_io_open(path);
    if (fd < 0) return NULL;

    P3d2Header hdr;
    if (sceIoRead(fd, &hdr, sizeof(P3d2Header)) != (int)sizeof(P3d2Header)) {
        sceIoClose(fd);
        return NULL;
    }

    if (memcmp(hdr.magic, "P3D2", 4) != 0 || hdr.version != 2) {
        sceIoClose(fd);
        return NULL;
    }

    ForgeModel3D* model = (ForgeModel3D*)calloc(1, sizeof(ForgeModel3D));
    if (!model) {
        sceIoClose(fd);
        return NULL;
    }

    model->bone_count  = hdr.bone_count;
    model->chunk_count = hdr.chunk_count;

    if (model->bone_count > 0) {
        uint32_t bones_bytes = model->bone_count * sizeof(ForgeBoneDef);
        model->bones = (ForgeBoneDef*)malloc(bones_bytes);
        if (!model->bones || sceIoRead(fd, model->bones, bones_bytes) != (int)bones_bytes) {
            forge_model3d_free(model);
            sceIoClose(fd);
            return NULL;
        }
    }

    if (model->chunk_count > 0) {
        model->chunks = (ForgeModelChunk*)calloc(model->chunk_count, sizeof(ForgeModelChunk));
        if (!model->chunks) {
            forge_model3d_free(model);
            sceIoClose(fd);
            return NULL;
        }

        for (uint16_t c = 0; c < model->chunk_count; ++c) {
            P3d2ChunkHeader chdr;
            if (sceIoRead(fd, &chdr, sizeof(P3d2ChunkHeader)) != (int)sizeof(P3d2ChunkHeader)) {
                forge_model3d_free(model);
                sceIoClose(fd);
                return NULL;
            }

            model->chunks[c].node_index      = chdr.node_index;
            model->chunks[c].num_local_bones = chdr.num_local_bones;
            memcpy(model->chunks[c].bone_palette, chdr.bone_palette, 8);

            uint32_t vtx_bytes = chdr.vertex_count * chdr.vertex_stride;
            ForgeMesh* mesh = (ForgeMesh*)calloc(1, sizeof(ForgeMesh));
            if (!mesh) {
                forge_model3d_free(model);
                sceIoClose(fd);
                return NULL;
            }

            mesh->count         = chdr.vertex_count;
            mesh->vertex_format = chdr.vertex_format;
            mesh->vertex_stride = chdr.vertex_stride;
            memcpy(mesh->aabb_min, chdr.aabb_min, sizeof(float) * 3);
            memcpy(mesh->aabb_max, chdr.aabb_max, sizeof(float) * 3);
            memcpy(mesh->center,   chdr.center,   sizeof(float) * 3);
            mesh->radius        = chdr.radius;

            mesh->vertices = memalign(16, vtx_bytes);
            if (!mesh->vertices || sceIoRead(fd, mesh->vertices, vtx_bytes) != (int)vtx_bytes) {
                if (mesh->vertices) free(mesh->vertices);
                free(mesh);
                forge_model3d_free(model);
                sceIoClose(fd);
                return NULL;
            }

            sceKernelDcacheWritebackRange(mesh->vertices, vtx_bytes);
            model->chunks[c].mesh = mesh;
        }
    }

    sceIoClose(fd);
    return model;
}

void forge_model3d_free(ForgeModel3D* model) {
    if (!model) return;
    if (model->bones) {
        free(model->bones);
        model->bones = NULL;
    }
    if (model->chunks) {
        for (uint16_t c = 0; c < model->chunk_count; ++c) {
            if (model->chunks[c].mesh) {
                forge_mesh_free(model->chunks[c].mesh);
                model->chunks[c].mesh = NULL;
            }
        }
        free(model->chunks);
        model->chunks = NULL;
    }
    free(model);
}

void forge_model3d_draw(const ForgeModel3D* model, const ForgeAnimator* animator, const ForgeTexture* tex) {
    if (!model || !model->chunks) return;

    for (uint16_t c = 0; c < model->chunk_count; ++c) {
        const ForgeModelChunk* chunk = &model->chunks[c];
        if (!chunk->mesh) continue;

        if (chunk->node_index >= 0) {
            /* Mode A: Rigid hierarchical part attached to a bone */
            sceGumPushMatrix();
            if (animator && chunk->node_index < FORGE_MAX_BONES) {
                sceGumMultMatrix(&animator->world_matrices[chunk->node_index]);
            }
            forge_draw_mesh_current(chunk->mesh, tex);
            sceGumPopMatrix();
        } else {
            /* Mode B: Hardware Vertex Blending with <= 8 bones palette */
            sceGumPushMatrix();
            if (animator && chunk->num_local_bones > 0) {
                for (uint8_t b = 0; b < chunk->num_local_bones && b < FORGE_MAX_HW_BONES; ++b) {
                    uint8_t global_id = chunk->bone_palette[b];
                    if (global_id < FORGE_MAX_BONES) {
                        sceGuBoneMatrix(b, &animator->skin_matrices[global_id]);
                    }
                }
            }
            forge_draw_mesh_current(chunk->mesh, tex);
            sceGumPopMatrix();
        }
    }
}
