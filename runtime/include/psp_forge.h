#ifndef PSP_FORGE_H
#define PSP_FORGE_H

/* PSP-Forge version */
#define FORGE_VERSION_MAJOR 1
#define FORGE_VERSION_MINOR 1
#define FORGE_VERSION_PATCH 0

#include <stdint.h>
#include <stdbool.h>
#include <psptypes.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Core & System                                                             */
/* ========================================================================= */

#define FORGE_SCREEN_WIDTH  480
#define FORGE_SCREEN_HEIGHT 272
#define FORGE_BUF_WIDTH     512

/* Flags for forge_init */
#define FORGE_INIT_DEFAULT          0x00
#define FORGE_INIT_ERROR_HANDLER    0x01

void     forge_init(uint32_t flags);
void     forge_shutdown(void);
int      forge_is_running(void);
void     forge_debug_install_error_handler(void);
void     forge_begin_frame(void);
void     forge_end_frame(void);
void     forge_clear(uint32_t color_rgba8888);
float    forge_get_delta_time(void);
float    forge_get_fps(void);
SceUID   forge_io_open(const char* path);
void     forge_set_base_path(const char* path_or_argv0);
const char* forge_get_base_path(void);

/* ========================================================================= */
/* VRAM Static Allocator (2 MB total eDRAM)                                 */
/* ========================================================================= */

void*    forge_vram_get_draw_buffer(void);
void*    forge_vram_get_disp_buffer(void);
void*    forge_vram_get_depth_buffer(void);
void*    forge_vram_get_scratchpad(void);
void*    forge_vram_alloc(uint32_t size);
void     forge_vram_reset(void);
void*    forge_vram_to_uncached_cpu(void* vram_rel);

/* ========================================================================= */
/* Input Subsystem                                                           */
/* ========================================================================= */

typedef struct {
    uint32_t held;
    uint32_t pressed;
    uint32_t released;
    float    analog_x;  /* -1.0 to 1.0 (deadzone filtered) */
    float    analog_y;  /* -1.0 to 1.0 (deadzone filtered) */
} ForgeInput;

void     forge_input_poll(ForgeInput* input);
bool     forge_input_is_pressed(const ForgeInput* in, uint32_t btn);
bool     forge_input_is_held(const ForgeInput* in, uint32_t btn);
bool     forge_input_is_released(const ForgeInput* in, uint32_t btn);

/* ========================================================================= */
/* 2D & Texture Subsystem                                                    */
/* ========================================================================= */

typedef struct {
    void*    data;
    void*    palette;
    uint16_t width;
    uint16_t height;
    uint16_t pwr2_w;
    uint16_t pwr2_h;
    uint8_t  format;         /* GU_PSM_8888, GU_PSM_5551, GU_PSM_T8, etc. */
    uint8_t  is_swizzled;
    uint8_t  has_palette;
    uint16_t palette_count;
    bool     in_vram;
} ForgeTexture;

ForgeTexture* forge_texture_load(const char* path);
ForgeTexture* forge_texture_load_vram(const char* path);
void          forge_texture_free(ForgeTexture* tex);
void          forge_draw_sprite(
    const ForgeTexture* tex,
    float sx, float sy, float sw, float sh,
    float tx, float ty, float tw, float th
);

/* ========================================================================= */
/* 3D Geometry & Camera                                                      */
/* ========================================================================= */

typedef struct __attribute__((aligned(16))) {
    float u, v;        /* Texture UV */
    float nx, ny, nz;  /* Normal */
    float x, y, z;     /* Position */
} ForgeVertex3D;

typedef struct {
    void*    vertices;
    uint32_t count;
    uint32_t vertex_format;
    uint16_t vertex_stride;
    float    aabb_min[3];
    float    aabb_max[3];
    float    center[3];
    float    radius;
} ForgeMesh;

ForgeMesh* forge_mesh_load(const char* path);
void       forge_mesh_free(ForgeMesh* mesh);

void       forge_set_camera(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float fov_degrees
);

void       forge_draw_mesh(
    const ForgeMesh* mesh,
    const ForgeTexture* tex,
    float x, float y, float z,
    float rx_rad, float ry_rad, float rz_rad,
    float sx, float sy, float sz
);

void       forge_draw_mesh_current(const ForgeMesh* mesh, const ForgeTexture* tex);

void       forge_set_alpha_test(bool enable, uint8_t ref_value);

void       forge_draw_mesh_node(
    const ForgeMesh* mesh,
    const ForgeTexture* tex,
    float x, float y, float z,
    float rx_rad, float ry_rad, float rz_rad,
    float sx, float sy, float sz
);

/* ========================================================================= */
/* 3D Skeletal Animation & Quaternions                                       */
/* ========================================================================= */

#define FORGE_MAX_BONES    96
#define FORGE_MAX_HW_BONES 8

typedef struct {
    float x, y, z, w;
} ForgeQuat;

typedef struct {
    char     name[24];             /* Identifier (e.g. "DEF-spine", "head") */
    uint8_t  parent_index;         /* Parent node index (0xFF = root) */
    uint8_t  reserved[3];
    float    local_pos[3];         /* Neutral rest position */
    float    local_rot[4];         /* Neutral rest quaternion (x,y,z,w) */
    float    inv_bind_matrix[16];  /* 4x4 inverse bind matrix for skinning */
} ForgeBoneDef;

typedef struct __attribute__((packed)) {
    char     magic[4];             /* "PANM" */
    uint16_t version;              /* 1 */
    uint16_t bone_count;
    uint32_t frame_count;
    float    framerate;
    float    duration;
    float    pos_scale;            /* Scale factor for compressed positions (e.g. 0.001f) */
    uint8_t  reserved[8];
} PanmHeader;

typedef struct __attribute__((packed)) {
    int16_t rot_quat[4];           /* Quantized x,y,z,w (* 32767) */
    int16_t pos[3];                /* Quantized x,y,z: pos = raw * pos_scale */
    int16_t reserved;
} ForgeBoneSample;

typedef struct {
    PanmHeader       header;
    ForgeBoneSample* samples;      /* frame_count * bone_count samples */
} ForgeAnimClip;

typedef struct {
    ForgeMesh* mesh;               /* Geometry chunk */
    int16_t    node_index;         /* Mode A: attached bone index (-1 if skinned) */
    uint8_t    num_local_bones;    /* Mode B: number of active local bones (<= 8) */
    uint8_t    bone_palette[8];    /* Mode B: maps local bone index (0..7) to global bone */
} ForgeModelChunk;

typedef struct {
    uint16_t         bone_count;
    ForgeBoneDef*    bones;
    uint16_t         chunk_count;
    ForgeModelChunk* chunks;
} ForgeModel3D;

/* ========================================================================= */
/* Agnostic Toolchain Format (.p3dx) Definitions                             */
/* ========================================================================= */

typedef struct __attribute__((packed)) {
    char     magic[4];       /* "P3DX" */
    uint16_t version;        /* 1 */
    uint16_t bone_count;     /* Any number (no bone reduction) */
    uint16_t chunk_count;
    uint16_t material_count; /* Number of associated materials/textures */
    uint8_t  reserved[6];
} P3dxHeader;

typedef struct __attribute__((packed)) {
    int16_t  node_index;      /* -1 = skinned, >= 0 = rigid bone */
    uint8_t  num_local_bones; /* 0..8 */
    uint8_t  bone_palette[8];
    uint16_t material_id;     /* Index of material in Material Name Table */
    uint32_t vertex_format;
    uint16_t vertex_stride;
    uint32_t vertex_count;
    float    aabb_min[3];
    float    aabb_max[3];
    float    center[3];
    float    radius;
    uint8_t  reserved[2];
} P3dxChunkHeader;

typedef struct {
    const ForgeAnimClip* clip;
    float                time;
    float                speed;
    bool                 loop;
    bool                 is_playing;
    bool                 finished;
    ScePspFMatrix4       world_matrices[FORGE_MAX_BONES]; /* Computed world transforms */
    ScePspFMatrix4       skin_matrices[FORGE_MAX_BONES];  /* world * inv_bind_matrix */
} ForgeAnimator;

/* Quaternion Math Functions */
void forge_quat_identity(ForgeQuat* q);
void forge_quat_normalize(ForgeQuat* q);
void forge_quat_slerp(ForgeQuat* out, const ForgeQuat* a, const ForgeQuat* b, float t);
void forge_quat_to_matrix(ScePspFMatrix4* m, const ForgeQuat* q, const float pos[3]);

/* Model3D & Skeletal Mesh Functions */
ForgeModel3D* forge_model3d_load(const char* path);
void          forge_model3d_free(ForgeModel3D* model);
void          forge_model3d_draw(const ForgeModel3D* model, const ForgeAnimator* animator, const ForgeTexture* tex);

/* 3D Skeletal Animation Player Functions */
ForgeAnimClip* forge_anim3d_load(const char* path);
void           forge_anim3d_free(ForgeAnimClip* clip);
void           forge_anim3d_init(ForgeAnimator* animator);
void           forge_anim3d_play(ForgeAnimator* animator, const ForgeAnimClip* clip, bool loop);
void           forge_anim3d_stop(ForgeAnimator* animator);
void           forge_anim3d_set_speed(ForgeAnimator* animator, float speed);
void           forge_anim3d_update(ForgeAnimator* animator, const ForgeModel3D* model, float dt);

/* ========================================================================= */
/* Hardware Lighting & Culling                                               */
/* ========================================================================= */

#define FORGE_MAX_VIRTUAL_LIGHTS 16

typedef struct {
    float    pos[3];
    uint32_t color;
    float    intensity;
    bool     active;
} ForgeLight;

void       forge_set_light(uint8_t id, float x, float y, float z, uint32_t color_rgba, float intensity);
void       forge_clear_lights(void);
void       forge_cull_and_apply_lights(float obj_x, float obj_y, float obj_z);

/* ========================================================================= */
/* Audio Subsystem (Dedicated PCM Thread)                                    */
/* ========================================================================= */

typedef struct {
    int16_t* pcm_data;
    uint32_t sample_count; /* Per channel */
    uint32_t sample_rate;
    uint8_t  channels;
} ForgeSound;

ForgeSound* forge_sound_load(const char* path);
void        forge_sound_free(ForgeSound* snd);
void        forge_sound_play(const ForgeSound* snd, uint8_t loop);
void        forge_sound_stop(void);
bool        forge_sound_is_playing(void);
void        forge_audio_shutdown(void);

/* ========================================================================= */
/* 2D & 3D Collision Detection                                               */
/* ========================================================================= */

typedef struct {
    float x, y, w, h;
} ForgeRect;

typedef struct {
    float x, y, radius;
} ForgeCircle;

typedef struct {
    ScePspFVector3 min;
    ScePspFVector3 max;
} ForgeAABB;

typedef struct {
    ScePspFVector3 center;
    float radius;
} ForgeSphere;

bool forge_collide_rect_rect(ForgeRect a, ForgeRect b);
bool forge_collide_rect_circle(ForgeRect r, ForgeCircle c);
bool forge_collide_point_rect(float px, float py, ForgeRect r);

bool forge_collide_aabb_aabb(ForgeAABB a, ForgeAABB b);
bool forge_collide_sphere_sphere(ForgeSphere a, ForgeSphere b);
bool forge_collide_aabb_sphere(ForgeAABB b, ForgeSphere s);
ForgeAABB forge_mesh_get_transformed_aabb(
    const ForgeMesh* mesh,
    float x, float y, float z,
    float sx, float sy, float sz
);

/* ========================================================================= */
/* 2D Sprite Animation                                                       */
/* ========================================================================= */

typedef struct {
    const ForgeTexture* texture;
    int   frame_w;
    int   frame_h;
    int   num_frames;
    int   columns;
    float fps;
    float timer;
    int   current_frame;
    bool  loop;
    bool  is_playing;
} ForgeSpriteAnim;

void forge_anim2d_init(
    ForgeSpriteAnim* anim,
    const ForgeTexture* tex,
    int frame_w, int frame_h,
    int num_frames,
    float fps,
    bool loop
);
void forge_anim2d_update(ForgeSpriteAnim* anim, float dt);
void forge_anim2d_draw(
    const ForgeSpriteAnim* anim,
    float x, float y,
    float w, float h
);
void forge_anim2d_set_frame(ForgeSpriteAnim* anim, int frame);

/* ========================================================================= */
/* Scene Management                                                          */
/* ========================================================================= */

typedef struct ForgeScene ForgeScene;
typedef void (*ForgeSceneCallback)(ForgeScene* scene, float dt);

struct ForgeScene {
    const char*        name;
    void*              user_data;
    ForgeSceneCallback on_init;
    ForgeSceneCallback on_update;
    ForgeSceneCallback on_draw;
    ForgeSceneCallback on_destroy;
};

void        forge_scene_set(ForgeScene* scene);
ForgeScene* forge_scene_get_current(void);
void        forge_scene_update_and_draw(float dt);
void        forge_scene_reset(void);

/* ========================================================================= */
/* Font & 2D Text Rendering                                                  */
/* ========================================================================= */

typedef struct __attribute__((packed)) {
    char     magic[4];       /* "FFNT" */
    uint16_t version;        /* 1 */
    uint16_t font_size;      /* Point size */
    uint16_t line_height;    /* Line advance */
    uint16_t base_line;      /* Base offset */
    uint16_t tex_w;          /* Texture atlas width */
    uint16_t tex_h;          /* Texture atlas height */
    uint16_t glyph_count;    /* Number of glyph entries */
    uint8_t  reserved[14];   /* 32 bytes header total */
} ForgeFontHeader;

typedef struct __attribute__((packed)) {
    uint8_t  char_code;      /* ASCII character code (32..126) */
    uint8_t  reserved;
    uint16_t x;              /* Top-left X in texture atlas */
    uint16_t y;              /* Top-left Y in texture atlas */
    uint16_t w;              /* Glyph width in pixels */
    uint16_t h;              /* Glyph height in pixels */
    int16_t  xoffset;        /* Left bearing */
    int16_t  yoffset;        /* Top bearing */
    int16_t  xadvance;       /* Horizontal cursor advance */
    float    u0, v0, u1, v1; /* Precomputed normalized UVs */
} ForgeGlyphDef;

typedef struct {
    ForgeFontHeader header;
    ForgeGlyphDef   glyphs[128]; /* Fast ASCII lookup table [0..127] */
    ForgeTexture*   texture;
} ForgeFont;

ForgeFont* forge_font_load(const char* fnt_path, const char* tex_path);
void       forge_font_free(ForgeFont* font);
void       forge_font_draw_text(const ForgeFont* font, const char* text, float x, float y, uint32_t color);
void       forge_font_draw_text_scaled(const ForgeFont* font, const char* text, float x, float y, float scale, uint32_t color);
float      forge_font_get_text_width(const ForgeFont* font, const char* text, float scale);

/* ========================================================================= */
/* Streaming Music & Volume Control                                          */
/* ========================================================================= */

typedef struct ForgeMusic ForgeMusic;

ForgeMusic* forge_music_open(const char* path);
void        forge_music_play(ForgeMusic* music, bool loop);
void        forge_music_stop(void);
void        forge_music_close(ForgeMusic* music);
void        forge_audio_set_volume(int volume);
int         forge_audio_get_volume(void);

/* ========================================================================= */
/* 3D Camera & Cinematic Transitions                                         */
/* ========================================================================= */

typedef struct {
    ScePspFVector3 eye;
    ScePspFVector3 target;
    ScePspFVector3 up;
    float          fov;

    /* Smooth LERP Target */
    ScePspFVector3 target_eye;
    ScePspFVector3 target_target;
    float          lerp_speed;
    bool           is_lerping;
} ForgeCamera3D;

void forge_camera3d_init(ForgeCamera3D* cam, float fov_deg);
void forge_camera3d_set(ForgeCamera3D* cam, float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z);
void forge_camera3d_lerp_to(ForgeCamera3D* cam, float target_eye_x, float target_eye_y, float target_eye_z, float target_look_x, float target_look_y, float target_look_z, float speed);
void forge_camera3d_update(ForgeCamera3D* cam, float dt);
void forge_camera3d_apply(const ForgeCamera3D* cam);

/* ========================================================================= */
/* Generic Parallax Scrolling & Pause Overlay                                */
/* ========================================================================= */

void forge_parallax_draw_bg(const ForgeTexture* tex, float cam_x, float factor);
void forge_parallax_draw_fg(const ForgeTexture* tex, float cam_x, float factor);
void forge_draw_pause_overlay(const ForgeFont* font, const char* message, uint32_t overlay_color);

#ifdef __cplusplus
}
#endif

#endif /* PSP_FORGE_H */
