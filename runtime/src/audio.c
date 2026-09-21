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
static volatile int         s_audio_processing     = 0;
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
        /* Mark that we are processing/reading from current sound */
        s_audio_processing = 1;
        __sync_synchronize();

        const ForgeSound* snd = s_current_sound;

        if (s_is_playing && snd && snd->pcm_data && snd->sample_count > 0) {
            uint32_t total_samples = snd->sample_count;
            const int16_t* pcm = snd->pcm_data;
            uint8_t channels = snd->channels;

            uint32_t remaining = (s_playhead < total_samples) ? (total_samples - s_playhead) : 0;
            uint32_t to_copy = (remaining < AUDIO_BUFFER_SAMPLES) ? remaining : AUDIO_BUFFER_SAMPLES;

            /* Interleaved stereo source */
            if (channels == 2) {
                memcpy(s_audio_buffer, &pcm[s_playhead * 2], to_copy * 2 * sizeof(int16_t));
            } else {
                /* Mono to stereo expansion */
                for (uint32_t i = 0; i < to_copy; ++i) {
                    int16_t s = pcm[s_playhead + i];
                    s_audio_buffer[i * 2]     = s;
                    s_audio_buffer[i * 2 + 1] = s;
                }
            }

            s_playhead += to_copy;

            /* If end of track reached */
            if (to_copy < AUDIO_BUFFER_SAMPLES) {
                if (s_loop && total_samples > 0) {
                    s_playhead = 0;
                    uint32_t needed = AUDIO_BUFFER_SAMPLES - to_copy;
                    uint32_t wrap_copy = (total_samples < needed) ? total_samples : needed;
                    if (channels == 2) {
                        memcpy(&s_audio_buffer[to_copy * 2], pcm, wrap_copy * 2 * sizeof(int16_t));
                    } else {
                        for (uint32_t i = 0; i < wrap_copy; ++i) {
                            int16_t s = pcm[i];
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

        /* Done reading from sound structure */
        s_audio_processing = 0;
        __sync_synchronize();

        /* Flush D-Cache before DMA transfer */
        sceKernelDcacheWritebackRange(s_audio_buffer, sizeof(s_audio_buffer));

        /* Blocking DMA write */
        sceAudioOutputBlocking(s_audio_channel, PSP_AUDIO_VOLUME_MAX, s_audio_buffer);
    }

    if (s_audio_channel >= 0) {
        sceAudioChRelease(s_audio_channel);
        s_audio_channel = -1;
    }
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

    if (memcmp(hdr.magic, "PSND", 4) != 0 || hdr.version != 1) {
        sceIoClose(fd);
        return NULL;
    }

    if ((hdr.channels != 1 && hdr.channels != 2) || hdr.sample_rate != 44100 || hdr.data_size == 0 || hdr.data_size > 20 * 1024 * 1024) {
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

    if (sceIoRead(fd, snd->pcm_data, hdr.data_size) != (int)hdr.data_size) {
        free(snd->pcm_data);
        free(snd);
        sceIoClose(fd);
        return NULL;
    }
    sceIoClose(fd);

    sceKernelDcacheWritebackRange(snd->pcm_data, hdr.data_size);
    return snd;
}

void forge_sound_free(ForgeSound* snd) {
    if (!snd) return;
    if (s_current_sound == snd) {
        forge_sound_stop();
    }
    /* Wait if audio thread is actively reading pcm_data */
    while (s_audio_processing) {
        sceKernelDelayThread(200);
        __sync_synchronize();
    }
    if (snd->pcm_data) {
        free(snd->pcm_data);
        snd->pcm_data = NULL;
    }
    free(snd);
}

void forge_sound_play(const ForgeSound* snd, uint8_t loop) {
    if (!snd) return;
    ensure_audio_thread_started();
    s_current_sound = snd;
    s_loop          = loop;
    s_playhead      = 0;
    __sync_synchronize();
    s_is_playing    = true;
}

void forge_sound_stop(void) {
    s_is_playing    = false;
    s_current_sound = NULL;
    s_playhead      = 0;
    __sync_synchronize();
    /* Wait if audio thread is actively reading pcm_data */
    while (s_audio_processing) {
        sceKernelDelayThread(200);
        __sync_synchronize();
    }
}

bool forge_sound_is_playing(void) {
    return s_is_playing;
}

void forge_audio_shutdown(void) {
    if (s_audio_thid >= 0) {
        forge_sound_stop();
        s_audio_thread_running = 0;
        __sync_synchronize();
        sceKernelWaitThreadEnd(s_audio_thid, NULL);
        sceKernelDeleteThread(s_audio_thid);
        s_audio_thid = -1;
    }
    if (s_audio_channel >= 0) {
        sceAudioChRelease(s_audio_channel);
        s_audio_channel = -1;
    }
}
