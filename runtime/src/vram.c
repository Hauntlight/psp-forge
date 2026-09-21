#include "psp_forge.h"
#include <string.h>

/* ========================================================================= */
/* VRAM Layout (2048 KB eDRAM)                                               */
/* ========================================================================= */
/* Exact pitch/height calculation with FORGE_BUF_WIDTH = 512, HEIGHT = 272:  */
/* 0x00000000 - 0x00088000 (544 KiB): Draw Buffer   (RGBA8888, 512x272)      */
/* 0x00088000 - 0x00110000 (544 KiB): Disp Buffer   (RGBA8888, 512x272)      */
/* 0x00110000 - 0x00154000 (272 KiB): Depth Buffer  (16-bit Z,  512x272)      */
/* 0x00154000 - 0x00200000 (688 KiB): Texture Scratchpad                     */
/* ========================================================================= */

#define VRAM_DRAW_SIZE     (FORGE_BUF_WIDTH * FORGE_SCREEN_HEIGHT * 4) /* 557056 B = 544 KiB */
#define VRAM_DISP_SIZE     (FORGE_BUF_WIDTH * FORGE_SCREEN_HEIGHT * 4) /* 557056 B = 544 KiB */
#define VRAM_DEPTH_SIZE    (FORGE_BUF_WIDTH * FORGE_SCREEN_HEIGHT * 2) /* 278528 B = 272 KiB */

#define VRAM_DRAW_OFFSET   ((void*)0x00000000)
#define VRAM_DISP_OFFSET   ((void*)((uintptr_t)VRAM_DRAW_OFFSET + VRAM_DRAW_SIZE))   /* 0x00088000 */
#define VRAM_DEPTH_OFFSET  ((void*)((uintptr_t)VRAM_DISP_OFFSET + VRAM_DISP_SIZE))   /* 0x00110000 */
#define VRAM_SCRATCH_START ((void*)((uintptr_t)VRAM_DEPTH_OFFSET + VRAM_DEPTH_SIZE)) /* 0x00154000 */

#define VRAM_TOTAL_SIZE    (2 * 1024 * 1024)
#define VRAM_SCRATCH_SIZE  (VRAM_TOTAL_SIZE - (uint32_t)(uintptr_t)VRAM_SCRATCH_START) /* 704512 B = 688 KiB */

#define VRAM_CPU_UNCACHED_BASE ((uintptr_t)0x44000000)

static uint32_t s_scratch_allocated = 0;

void* forge_vram_get_draw_buffer(void) {
    return VRAM_DRAW_OFFSET;
}

void* forge_vram_get_disp_buffer(void) {
    return VRAM_DISP_OFFSET;
}

void* forge_vram_get_depth_buffer(void) {
    return VRAM_DEPTH_OFFSET;
}

void* forge_vram_get_scratchpad(void) {
    return (void*)((uintptr_t)VRAM_SCRATCH_START + s_scratch_allocated);
}

void* forge_vram_alloc(uint32_t size) {
    /* Align to 64 bytes — PSP bus burst boundary for DMA transfers */
    uint32_t aligned_size = (size + 63) & ~63;

    if (s_scratch_allocated + aligned_size > VRAM_SCRATCH_SIZE) {
        /* Out of VRAM scratchpad */
        return NULL;
    }

    void* ptr = (void*)((uintptr_t)VRAM_SCRATCH_START + s_scratch_allocated);
    s_scratch_allocated += aligned_size;
    return ptr;
}

void forge_vram_reset(void) {
    s_scratch_allocated = 0;
}

void* forge_vram_to_uncached_cpu(void* vram_rel) {
    return (void*)(VRAM_CPU_UNCACHED_BASE + (uintptr_t)vram_rel);
}
