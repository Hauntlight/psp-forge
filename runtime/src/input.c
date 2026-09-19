#include "psp_forge.h"
#include <math.h>

#define FORGE_ANALOG_DEADZONE 0.15f

static uint32_t s_prev_buttons = 0;

void forge_input_poll(ForgeInput* input) {
    if (!input) return;

    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);

    input->held     = pad.Buttons;
    input->pressed  = pad.Buttons & ~s_prev_buttons;
    input->released = ~pad.Buttons & s_prev_buttons;
    s_prev_buttons  = pad.Buttons;

    /* Normalize analog stick from [0..255] (center 128) to [-1.0..1.0] */
    float raw_x = ((float)pad.Lx - 128.0f) / 128.0f;
    float raw_y = ((float)pad.Ly - 128.0f) / 128.0f;

    /* Deadzone filter */
    if (fabsf(raw_x) < FORGE_ANALOG_DEADZONE) {
        raw_x = 0.0f;
    }
    if (fabsf(raw_y) < FORGE_ANALOG_DEADZONE) {
        raw_y = 0.0f;
    }

    input->analog_x = raw_x;
    input->analog_y = raw_y;
}

bool forge_input_is_pressed(const ForgeInput* in, uint32_t btn) {
    return in ? ((in->pressed & btn) != 0) : false;
}

bool forge_input_is_held(const ForgeInput* in, uint32_t btn) {
    return in ? ((in->held & btn) != 0) : false;
}

bool forge_input_is_released(const ForgeInput* in, uint32_t btn) {
    return in ? ((in->released & btn) != 0) : false;
}
