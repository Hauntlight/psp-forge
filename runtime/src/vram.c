#include "psp_forge.h"
#include <string.h>

/* ========================================================================= */
/* VRAM Layout (2048 KB eDRAM)                                               */
/* ========================================================================= */
/* 0x00000000 - 0x00080000 (512 KB): Draw Buffer   (RGBA8888, 512x272)      */
/* 0x00080000 - 0x00100000 (512 KB): Disp Buffer   (RGBA8888, 512x272)      */
/* 0x00100000 - 0x00140000 (256 KB): Depth Buffer  (16-bit Z, 512x272)      */
/* 0x00140000 - 0x00200000 (768 KB): Texture Scratchpad                     */
/* ========================================================================= */

#define VRAM_DRAW_OFFSET   ((void*)0x00000000)
#define VRAM_DISP_OFFSET   ((void*)0x00080000)
#define VRAM_DEPTH_OFFSET  ((void*)0x00100000)
#define VRAM_SCRATCH_START ((void*)0x00140000)
#define VRAM_TOTAL_SIZE    (2 * 1024 * 1024)
#define VRAM_SCRATCH_SIZE  (VRAM_TOTAL_SIZE - 0x00140000) /* 768 KB */

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
    /* Align allocation to 16 bytes */
    uint32_t aligned_size = (size + 15) & ~15;

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
