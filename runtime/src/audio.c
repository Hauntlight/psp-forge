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

struct ForgeMusic {
    SceUID   fd;
    uint8_t  channels;
    uint32_t sample_rate;
    uint32_t data_size;
    uint32_t data_start_offset;
    uint32_t bytes_read;
    bool     loop;
    bool     is_playing;
};

static int16_t s_audio_buffer[AUDIO_BUFFER_SAMPLES * 2] __attribute__((aligned(64)));
static int16_t s_music_buffer[AUDIO_BUFFER_SAMPLES * 2] __attribute__((aligned(64)));

static volatile int         s_audio_thread_running = 1;
static volatile int         s_audio_processing     = 0;
static volatile bool        s_is_playing           = false;
static volatile uint8_t     s_loop                 = 0;
static volatile uint32_t    s_playhead             = 0;
static const ForgeSound*    s_current_sound        = NULL;
static ForgeMusic*          s_current_music        = NULL;
static volatile int         s_audio_volume         = PSP_AUDIO_VOLUME_MAX;
static SceUID               s_audio_thid           = -1;
static int                  s_audio_channel        = -1;

void forge_audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > PSP_AUDIO_VOLUME_MAX) volume = PSP_AUDIO_VOLUME_MAX;
    s_audio_volume = volume;
}

int forge_audio_get_volume(void) {
    return s_audio_volume;
}

static inline int16_t clamp_s16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int AudioThread(SceSize args, void *argp) {
    (void)args; (void)argp;

    s_audio_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BUFFER_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (s_audio_channel < 0) {
        return -1;
    }

    while (s_audio_thread_running) {
        s_audio_processing = 1;
        __sync_synchronize();

        const ForgeSound* snd = s_current_sound;
        ForgeMusic* mus = s_current_music;

        bool has_snd = (s_is_playing && snd && snd->pcm_data && snd->sample_count > 0);
        bool has_mus = (mus && mus->is_playing && mus->fd >= 0);

        if (!has_snd && !has_mus) {
            memset(s_audio_buffer, 0, sizeof(s_audio_buffer));
        } else {
            /* 1. Handle Music Stream if active */
            if (has_mus) {
                uint32_t bytes_per_sample = mus->channels * sizeof(int16_t);
                uint32_t requested_bytes = AUDIO_BUFFER_SAMPLES * bytes_per_sample;
                uint32_t remaining = (mus->data_size > mus->bytes_read) ? (mus->data_size - mus->bytes_read) : 0;
                uint32_t to_read = (remaining < requested_bytes) ? remaining : requested_bytes;

                int bytes_read_now = 0;
                if (to_read > 0) {
                    bytes_read_now = sceIoRead(mus->fd, s_music_buffer, to_read);
                }

                if (bytes_read_now < (int)requested_bytes) {
                    if (mus->loop && mus->data_size > 0) {
                        /* Seek back to PCM data start */
                        sceIoLseek(mus->fd, mus->data_start_offset, PSP_SEEK_SET);
                        mus->bytes_read = 0;

                        uint32_t wrap_needed = requested_bytes - (bytes_read_now > 0 ? bytes_read_now : 0);
                        uint32_t wrap_to_read = (mus->data_size < wrap_needed) ? mus->data_size : wrap_needed;
                        int wrap_read = sceIoRead(mus->fd, ((char*)s_music_buffer) + (bytes_read_now > 0 ? bytes_read_now : 0), wrap_to_read);
                        if (wrap_read > 0) {
                            mus->bytes_read += wrap_read;
                            bytes_read_now += wrap_read;
                        }
                    } else {
                        mus->is_playing = false;
                    }
                    if (bytes_read_now < (int)requested_bytes) {
                        memset(((char*)s_music_buffer) + (bytes_read_now > 0 ? bytes_read_now : 0), 0, requested_bytes - (bytes_read_now > 0 ? bytes_read_now : 0));
                    }
                } else {
                    mus->bytes_read += bytes_read_now;
                }

                /* Copy music to audio buffer (handling mono expansion if needed) */
                if (mus->channels == 2) {
                    memcpy(s_audio_buffer, s_music_buffer, AUDIO_BUFFER_SAMPLES * 2 * sizeof(int16_t));
                } else {
                    for (int i = 0; i < AUDIO_BUFFER_SAMPLES; ++i) {
                        int16_t m = s_music_buffer[i];
                        s_audio_buffer[i * 2]     = m;
                        s_audio_buffer[i * 2 + 1] = m;
                    }
                }
            } else {
                memset(s_audio_buffer, 0, sizeof(s_audio_buffer));
            }

            /* 2. Mix Sound Effect if active */
            if (has_snd) {
                uint32_t total_samples = snd->sample_count;
                const int16_t* pcm = snd->pcm_data;
                uint8_t channels = snd->channels;

                uint32_t remaining = (s_playhead < total_samples) ? (total_samples - s_playhead) : 0;
                uint32_t to_copy = (remaining < AUDIO_BUFFER_SAMPLES) ? remaining : AUDIO_BUFFER_SAMPLES;

                for (uint32_t i = 0; i < to_copy; ++i) {
                    int16_t sl = (channels == 2) ? pcm[(s_playhead + i) * 2]     : pcm[s_playhead + i];
                    int16_t sr = (channels == 2) ? pcm[(s_playhead + i) * 2 + 1] : pcm[s_playhead + i];
                    s_audio_buffer[i * 2]     = clamp_s16((int32_t)s_audio_buffer[i * 2]     + (int32_t)sl);
                    s_audio_buffer[i * 2 + 1] = clamp_s16((int32_t)s_audio_buffer[i * 2 + 1] + (int32_t)sr);
                }

                s_playhead += to_copy;

                if (to_copy < AUDIO_BUFFER_SAMPLES) {
                    if (s_loop && total_samples > 0) {
                        s_playhead = 0;
                        uint32_t needed = AUDIO_BUFFER_SAMPLES - to_copy;
                        uint32_t wrap_copy = (total_samples < needed) ? total_samples : needed;
                        for (uint32_t i = 0; i < wrap_copy; ++i) {
                            int16_t sl = (channels == 2) ? pcm[i * 2]     : pcm[i];
                            int16_t sr = (channels == 2) ? pcm[i * 2 + 1] : pcm[i];
                            s_audio_buffer[(to_copy + i) * 2]     = clamp_s16((int32_t)s_audio_buffer[(to_copy + i) * 2]     + (int32_t)sl);
                            s_audio_buffer[(to_copy + i) * 2 + 1] = clamp_s16((int32_t)s_audio_buffer[(to_copy + i) * 2 + 1] + (int32_t)sr);
                        }
                        s_playhead += wrap_copy;
                    } else {
                        s_is_playing = false;
                    }
                }
            }
        }

        s_audio_processing = 0;
        __sync_synchronize();

        sceKernelDcacheWritebackRange(s_audio_buffer, sizeof(s_audio_buffer));
        sceAudioOutputBlocking(s_audio_channel, s_audio_volume, s_audio_buffer);
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
        s_audio_thid = sceKernelCreateThread(
            "forge_audio_thread",
            AudioThread,
            0x12,
            0x10000,
            PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU,
            NULL
        );
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
    while (s_audio_processing) {
        sceKernelDelayThread(200);
        __sync_synchronize();
    }
}

bool forge_sound_is_playing(void) {
    return s_is_playing;
}

/* ========================================================================= */
/* Streaming Music Implementation                                            */
/* ========================================================================= */

ForgeMusic* forge_music_open(const char* path) {
    if (!path) return NULL;
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

    ForgeMusic* mus = (ForgeMusic*)calloc(1, sizeof(ForgeMusic));
    if (!mus) {
        sceIoClose(fd);
        return NULL;
    }

    mus->fd                 = fd;
    mus->channels           = hdr.channels;
    mus->sample_rate        = hdr.sample_rate;
    mus->data_size          = hdr.data_size;
    mus->data_start_offset  = sizeof(PsndHeader);
    mus->bytes_read         = 0;
    mus->is_playing         = false;
    mus->loop               = false;

    return mus;
}

void forge_music_play(ForgeMusic* music, bool loop) {
    if (!music || music->fd < 0) return;
    ensure_audio_thread_started();
    music->loop               = loop;
    music->bytes_read         = 0;
    music->is_playing         = true;
    sceIoLseek(music->fd, music->data_start_offset, PSP_SEEK_SET);
    s_current_music = music;
    __sync_synchronize();
}

void forge_music_stop(void) {
    if (s_current_music) {
        s_current_music->is_playing = false;
        s_current_music = NULL;
        __sync_synchronize();
    }
    while (s_audio_processing) {
        sceKernelDelayThread(200);
        __sync_synchronize();
    }
}

void forge_music_close(ForgeMusic* music) {
    if (!music) return;
    if (s_current_music == music) {
        forge_music_stop();
    }
    if (music->fd >= 0) {
        sceIoClose(music->fd);
        music->fd = -1;
    }
    free(music);
}

void forge_audio_shutdown(void) {
    if (s_audio_thid >= 0) {
        forge_sound_stop();
        forge_music_stop();
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
