#include "psp_forge.h"
#include <pspaudio.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_BUFFER_SAMPLES 512

typedef struct __attribute__((packed)) {
    char     magic[4];       /* "PSND" */
    uint16_t version;        /* 1 */
    uint8_t  channels;       /* 1 or 2 */
    uint8_t  bits_per_sample;/* 16 */
    uint32_t sample_rate;    /* 44100 */
    uint32_t frame_count;    /* Samples per channel */
    uint32_t data_size;      /* Bytes */
    uint8_t  reserved[12];
} PsndHeader;

static int16_t s_audio_buffer[AUDIO_BUFFER_SAMPLES * 2] __attribute__((aligned(64)));

static volatile int         s_audio_thread_running = 1;
static volatile bool        s_is_playing           = false;
static volatile uint8_t     s_loop                 = 0;
static volatile uint32_t    s_playhead             = 0;
static const ForgeSound*    s_current_sound        = NULL;
static SceUID               s_audio_thid           = -1;
static int                  s_audio_channel        = -1;

static int AudioThread(SceSize args, void *argp) {
    (void)args; (void)argp;

    s_audio_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BUFFER_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (s_audio_channel < 0) {
        return -1;
    }

    while (s_audio_thread_running) {
        /* Memory barrier: ensure we see the latest values of volatile state
         * written by the main thread (s_is_playing, s_current_sound, s_loop). */
        __sync_synchronize();

        if (s_is_playing && s_current_sound && s_current_sound->pcm_data) {
            uint32_t remaining = s_current_sound->sample_count - s_playhead;
            uint32_t to_copy = (remaining < AUDIO_BUFFER_SAMPLES) ? remaining : AUDIO_BUFFER_SAMPLES;

            /* Interleaved stereo source */
            if (s_current_sound->channels == 2) {
                memcpy(s_audio_buffer, &s_current_sound->pcm_data[s_playhead * 2], to_copy * 2 * sizeof(int16_t));
            } else {
                /* Mono to stereo expansion */
                for (uint32_t i = 0; i < to_copy; ++i) {
                    int16_t s = s_current_sound->pcm_data[s_playhead + i];
                    s_audio_buffer[i * 2]     = s;
                    s_audio_buffer[i * 2 + 1] = s;
                }
            }

            s_playhead += to_copy;

            /* If end of track reached */
            if (to_copy < AUDIO_BUFFER_SAMPLES) {
                if (s_loop) {
                    s_playhead = 0;
                    uint32_t needed = AUDIO_BUFFER_SAMPLES - to_copy;
                    uint32_t wrap_copy = (s_current_sound->sample_count < needed) ? s_current_sound->sample_count : needed;
                    if (s_current_sound->channels == 2) {
                        memcpy(&s_audio_buffer[to_copy * 2], s_current_sound->pcm_data, wrap_copy * 2 * sizeof(int16_t));
                    } else {
                        for (uint32_t i = 0; i < wrap_copy; ++i) {
                            int16_t s = s_current_sound->pcm_data[i];
                            s_audio_buffer[(to_copy + i) * 2]     = s;
                            s_audio_buffer[(to_copy + i) * 2 + 1] = s;
                        }
                    }
                    s_playhead += wrap_copy;
                    if (to_copy + wrap_copy < AUDIO_BUFFER_SAMPLES) {
                        memset(&s_audio_buffer[(to_copy + wrap_copy) * 2], 0, (AUDIO_BUFFER_SAMPLES - to_copy - wrap_copy) * 2 * sizeof(int16_t));
                    }
                } else {
                    /* Zero pad remainder and stop */
                    memset(&s_audio_buffer[to_copy * 2], 0, (AUDIO_BUFFER_SAMPLES - to_copy) * 2 * sizeof(int16_t));
                    s_is_playing = false;
                }
            }
        } else {
            /* Output silence */
            memset(s_audio_buffer, 0, sizeof(s_audio_buffer));
        }

        /* Flush D-Cache before DMA transfer */
        sceKernelDcacheWritebackRange(s_audio_buffer, sizeof(s_audio_buffer));

        /* Blocking DMA write */
        sceAudioOutputBlocking(s_audio_channel, PSP_AUDIO_VOLUME_MAX, s_audio_buffer);
    }

    sceAudioChRelease(s_audio_channel);
    s_audio_channel = -1;
    return 0;
}

static void ensure_audio_thread_started(void) {
    if (s_audio_thid < 0) {
        s_audio_thread_running = 1;
        s_audio_thid = sceKernelCreateThread("forge_audio_thread", AudioThread, 0x12, 0x10000, 0, NULL);
        if (s_audio_thid >= 0) {
            sceKernelStartThread(s_audio_thid, 0, NULL);
        }
    }
}

ForgeSound* forge_sound_load(const char* path) {
    SceUID fd = forge_io_open(path);
    if (fd < 0) return NULL;

    PsndHeader hdr;
    if (sceIoRead(fd, &hdr, sizeof(PsndHeader)) != (int)sizeof(PsndHeader)) {
        sceIoClose(fd);
        return NULL;
    }

    if (memcmp(hdr.magic, "PSND", 4) != 0) {
        sceIoClose(fd);
        return NULL;
    }

    ForgeSound* snd = (ForgeSound*)calloc(1, sizeof(ForgeSound));
    if (!snd) {
        sceIoClose(fd);
        return NULL;
    }

    snd->channels     = hdr.channels;
    snd->sample_rate  = hdr.sample_rate;
    snd->sample_count = hdr.frame_count;

    snd->pcm_data = (int16_t*)malloc(hdr.data_size);
    if (!snd->pcm_data) {
        free(snd);
        sceIoClose(fd);
        return NULL;
    }

    sceIoRead(fd, snd->pcm_data, hdr.data_size);
    sceIoClose(fd);

    sceKernelDcacheWritebackRange(snd->pcm_data, hdr.data_size);
    return snd;
}

void forge_sound_free(ForgeSound* snd) {
    if (!snd) return;
    if (s_current_sound == snd) {
        forge_sound_stop();
    }
    if (snd->pcm_data) {
        free(snd->pcm_data);
    }
    free(snd);
}

void forge_sound_play(const ForgeSound* snd, uint8_t loop) {
    if (!snd) return;
    ensure_audio_thread_started();
    s_current_sound = snd;
    s_loop          = loop;
    s_playhead      = 0;
    s_is_playing    = true;
}

void forge_sound_stop(void) {
    s_is_playing    = false;
    s_current_sound = NULL;
    s_playhead      = 0;
}

bool forge_sound_is_playing(void) {
    return s_is_playing;
}
