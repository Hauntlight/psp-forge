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

FILE* forge_fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;

    /* 1. Try exact requested path */
    FILE* f = fopen(path, mode);
    if (f) return f;

    char buf[256];

    /* 2. PSP Device prefixes: disc0:/ (UMD/standalone EBOOT directory mount in PPSSPP) and ms0:/ */
    if (strncmp(path, "disc0:/", 7) != 0 && strncmp(path, "ms0:/", 5) != 0) {
        const char* clean_path = (strncmp(path, "build/", 6) == 0) ? (path + 6) : path;

        snprintf(buf, sizeof(buf), "disc0:/%s", clean_path);
        f = fopen(buf, mode);
        if (f) return f;

        const char* slash = strrchr(clean_path, '/');
        const char* fname = slash ? (slash + 1) : clean_path;
        snprintf(buf, sizeof(buf), "disc0:/assets/%s", fname);
        f = fopen(buf, mode);
        if (f) return f;

        snprintf(buf, sizeof(buf), "ms0:/%s", clean_path);
        f = fopen(buf, mode);
        if (f) return f;
    }

    /* 3. If path starts with "build/", try stripping "build/" */
    if (strncmp(path, "build/", 6) == 0) {
        f = fopen(path + 6, mode);
        if (f) return f;
    }

    /* 4. If path starts with "assets/", try prepending "build/" */
    if (strncmp(path, "assets/", 7) == 0) {
        snprintf(buf, sizeof(buf), "build/%s", path);
        f = fopen(buf, mode);
        if (f) return f;
    }

    return NULL;
}
