#include "psp_forge.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psprtc.h>
#include <string.h>
#include <stdio.h>

/* Internal VRAM accessors from vram.c */
void* forge_vram_get_draw_buffer(void);
void* forge_vram_get_disp_buffer(void);
void* forge_vram_get_depth_buffer(void);

/* Hardware Display List: must be aligned to 16 bytes */
static unsigned int __attribute__((aligned(16))) s_display_list[262144];

static int   s_running = 1;
static void* s_current_fbp = NULL;

/* Base path for asset loading (set by forge_set_base_path or auto-detected) */
static char s_base_path[256] = "";

/* Timing & FPS stats */
static u64   s_last_tick = 0;
static u32   s_tick_freq = 0;
static float s_delta_time = 0.016666f;
static float s_fps = 60.0f;
static u32   s_frame_count = 0;
static u64   s_fps_last_tick = 0;

/* ========================================================================= */
/* System Callbacks (HOME Button handling)                                   */
/* ========================================================================= */

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    s_running = 0;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, 0);
    }
}

/* ========================================================================= */
/* Core Engine Implementation                                                */
/* ========================================================================= */

void forge_init(uint32_t flags) {
    (void)flags;

    /* Setup system callbacks so HOME button exits cleanly */
    setup_callbacks();

    /* Setup timer */
    sceRtcGetCurrentTick(&s_last_tick);
    s_fps_last_tick = s_last_tick;
    s_tick_freq = sceRtcGetTickResolution();

    /* Initialize controller */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    /* Initialize GU */
    sceGuInit();

    sceGuStart(GU_DIRECT, s_display_list);

    void* fbp0 = forge_vram_get_draw_buffer();
    void* fbp1 = forge_vram_get_disp_buffer();
    void* zbp  = forge_vram_get_depth_buffer();
    s_current_fbp = fbp0;

    sceGuDrawBuffer(GU_PSM_8888, fbp0, FORGE_BUF_WIDTH);
    sceGuDispBuffer(FORGE_SCREEN_WIDTH, FORGE_SCREEN_HEIGHT, fbp1, FORGE_BUF_WIDTH);
    sceGuDepthBuffer(zbp, FORGE_BUF_WIDTH);

    /* Setup Viewport (centered on 2048, 2048 coordinate space) */
    sceGuOffset(2048 - (FORGE_SCREEN_WIDTH / 2), 2048 - (FORGE_SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, FORGE_SCREEN_WIDTH, FORGE_SCREEN_HEIGHT);
    sceGuDepthRange(65535, 0);

    /* Setup Scissor */
    sceGuScissor(0, 0, FORGE_SCREEN_WIDTH, FORGE_SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);

    /* Depth test */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);

    /* Alpha testing and standard alpha blending */
    sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
    sceGuEnable(GU_ALPHA_TEST);

    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_BLEND);

    sceGuFinish();
    sceGuSync(0, 0);

    /* Wait for VBlank and turn display ON */
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    /* Initialize GUM matrix stack */
    gumInit();
}

void forge_shutdown(void) {
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
}

int forge_is_running(void) {
    return s_running;
}

void forge_begin_frame(void) {
    sceGuStart(GU_DIRECT, s_display_list);
}

void forge_clear(uint32_t color_rgba8888) {
    sceGuClearColor(color_rgba8888);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void forge_end_frame(void) {
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    s_current_fbp = sceGuSwapBuffers();

    /* Compute Delta Time */
    u64 now;
    sceRtcGetCurrentTick(&now);
    if (s_tick_freq > 0) {
        s_delta_time = (float)(now - s_last_tick) / (float)s_tick_freq;
        if (s_delta_time <= 0.0f || s_delta_time > 1.0f) {
            s_delta_time = 0.016666f;
        }

        s_frame_count++;
        if (now - s_fps_last_tick >= s_tick_freq) {
            s_fps = (float)s_frame_count * (float)s_tick_freq / (float)(now - s_fps_last_tick);
            s_frame_count = 0;
            s_fps_last_tick = now;
        }
    }
    s_last_tick = now;
}

float forge_get_delta_time(void) {
    return s_delta_time;
}

float forge_get_fps(void) {
    return s_fps;
}



static const char* find_last_char(const char* s, char c) {
    if (!s) return NULL;
    const char* last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    return last;
}

static bool starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

static void string_copy(char* dest, size_t max_len, const char* src) {
    if (!dest || max_len == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < max_len) {
            dest[i] = src[i];
            i++;
        }
    }
    dest[i] = '\0';
}

void forge_set_base_path(const char* path_or_argv0) {
    if (!path_or_argv0) return;
    /* On PPSSPP CLI launch, argv[0] is often "umd0:/EBOOT.PBP", which is not a valid filesystem path */
    if (starts_with(path_or_argv0, "umd0:")) return;
    string_copy(s_base_path, sizeof(s_base_path), path_or_argv0);
    char* slash = (char*)find_last_char(s_base_path, '/');
    if (slash) {
        *slash = '\0';
    }
    if (s_base_path[0] != '\0') {
        sceIoChdir(s_base_path);
    }
}

const char* forge_get_base_path(void) {
    return s_base_path;
}

static void path_join3(char* dest, size_t max_len, const char* s1, const char* s2, const char* s3) {
    if (!dest || max_len == 0) return;
    dest[0] = '\0';
    size_t cur = 0;
    const char* parts[] = { s1, s2, s3, NULL };
    for (int i = 0; parts[i] != NULL; ++i) {
        const char* p = parts[i];
        if (!p) continue;
        while (*p && cur + 1 < max_len) {
            dest[cur++] = *p++;
        }
    }
    dest[cur] = '\0';
}

static void path_join4(char* dest, size_t max_len, const char* s1, const char* s2, const char* s3, const char* s4) {
    if (!dest || max_len == 0) return;
    dest[0] = '\0';
    size_t cur = 0;
    const char* parts[] = { s1, s2, s3, s4, NULL };
    for (int i = 0; parts[i] != NULL; ++i) {
        const char* p = parts[i];
        if (!p) continue;
        while (*p && cur + 1 < max_len) {
            dest[cur++] = *p++;
        }
    }
    dest[cur] = '\0';
}

SceUID forge_io_open(const char* path) {
    if (!path) return -1;

    /* 1. Try path as-is (handles absolute ms0:/ or disc0:/ paths) */
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd >= 0) return fd;

    /* Strip "build/" development prefix */
    const char* clean = starts_with(path, "build/") ? (path + 6) : path;

    /* Extract bare filename (last path component) */
    const char* slash = find_last_char(clean, '/');
    const char* fname = slash ? (slash + 1) : clean;

    char buf[512];

    /* 2. Try relative to s_base_path (main path on real PSP and when cached).
     *    Real PSP: ms0:/PSP/GAME/<gameid>
     *    We try:
     *      base_path/clean           → base/assets/hero.tex
     *      base_path/assets/fname    → base/assets/hero.tex (shorthand)
     */
    if (s_base_path[0] != '\0') {
        path_join3(buf, sizeof(buf), s_base_path, "/", clean);
        fd = sceIoOpen(buf, PSP_O_RDONLY, 0777);
        if (fd >= 0) return fd;

        if (!starts_with(clean, "assets/")) {
            path_join4(buf, sizeof(buf), s_base_path, "/assets/", fname, "");
            fd = sceIoOpen(buf, PSP_O_RDONLY, 0777);
            if (fd >= 0) return fd;
        }
    }

    /* 3. Scan ms0:/PSP/GAME/ entries (covers PPSSPP emulator and Memory Stick).
     *    Iterates entries and tests both original case and lowercase. */
    {
        SceUID dir = sceIoDopen("ms0:/PSP/GAME");
        if (dir >= 0) {
            SceIoDirent ent;
            while (sceIoDread(dir, &ent) > 0) {
                if (ent.d_name[0] == '.') continue;

                /* Try exact name from directory */
                path_join4(buf, sizeof(buf), "ms0:/PSP/GAME/", ent.d_name, "/assets/", fname);
                fd = sceIoOpen(buf, PSP_O_RDONLY, 0777);
                if (fd >= 0) {
                    sceIoDclose(dir);
                    char game_dir[256];
                    path_join3(game_dir, sizeof(game_dir), "ms0:/PSP/GAME/", ent.d_name, "");
                    forge_set_base_path(game_dir);
                    return fd;
                }

                /* Try lowercase name (e.g. Linux ext4 host filesystem in PPSSPP) */
                char lower_name[64];
                string_copy(lower_name, sizeof(lower_name), ent.d_name);
                for (int i = 0; lower_name[i]; ++i) {
                    if (lower_name[i] >= 'A' && lower_name[i] <= 'Z') {
                        lower_name[i] = (char)(lower_name[i] + ('a' - 'A'));
                    }
                }
                path_join4(buf, sizeof(buf), "ms0:/PSP/GAME/", lower_name, "/assets/", fname);
                fd = sceIoOpen(buf, PSP_O_RDONLY, 0777);
                if (fd >= 0) {
                    sceIoDclose(dir);
                    char game_dir[256];
                    path_join3(game_dir, sizeof(game_dir), "ms0:/PSP/GAME/", lower_name, "");
                    forge_set_base_path(game_dir);
                    return fd;
                }
            }
            sceIoDclose(dir);
        }
    }

    /* 4. UMD / disc0 support (for physical disc releases) */
    path_join3(buf, sizeof(buf), "disc0:/PSP_GAME/USRDIR/", clean, "");
    fd = sceIoOpen(buf, PSP_O_RDONLY, 0777);
    if (fd >= 0) return fd;

    path_join4(buf, sizeof(buf), "disc0:/PSP_GAME/USRDIR/assets/", fname, "", "");
    fd = sceIoOpen(buf, PSP_O_RDONLY, 0777);
    if (fd >= 0) return fd;

    /* 5. Bare filename relative (last resort) */
    fd = sceIoOpen(fname, PSP_O_RDONLY, 0777);
    if (fd >= 0) return fd;

    return -1;
}

FILE* forge_fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    return fopen(path, mode);
}
