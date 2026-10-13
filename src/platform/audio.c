#include "audio.h"

#include <SDL3/SDL.h>
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_RATE 44100
#define MAX_CHANNELS 96
#define MAX_SUBS 16
#define MAX_SAMPLES 32

typedef struct {
    int16_t *pcm;
    int len, rate;
    int loop_start, loop_end; /* loop_end > loop_start: sustain loop of a PAT/XI */
} Sample;

typedef struct {
    const Sample *s;
    double pos;
    int vol, freq, loops, gen;
} Channel;

static Sample banks[3][MAX_SUBS][MAX_SAMPLES];
static Channel channels[MAX_CHANNELS];
static SDL_AudioStream *stream;
static int next_gen = 1;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int16_t *to_pcm(const uint8_t *d, int n, int bits16, int is_unsigned) {
    int16_t *out = malloc(sizeof *out * (n > 0 ? n : 1));
    if (!out) return NULL;
    for (int i = 0; i < n; i++) {
        int v = bits16 ? (int16_t)rd16(d + 2 * i) : (int)(int8_t)d[i] * 256;
        if (is_unsigned) v = bits16 ? (int)rd16(d + 2 * i) - 32768 : ((int)d[i] - 128) * 256;
        out[i] = (int16_t)v;
    }
    return out;
}

static int load_wav(const uint8_t *d, size_t size, Sample *s) {
    if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return -1;
    int bits = 8, rate = 11025;
    for (size_t p = 12; p + 8 <= size;) {
        uint32_t len = rd32(d + p + 4);
        if (p + 8 + len > size) len = (uint32_t)(size - p - 8);
        if (!memcmp(d + p, "fmt ", 4) && len >= 16) {
            rate = (int)rd32(d + p + 12);
            bits = rd16(d + p + 22);
        } else if (!memcmp(d + p, "data", 4)) {
            s->len = bits == 16 ? (int)len / 2 : (int)len;
            s->rate = rate;
            s->pcm = to_pcm(d + p + 8, s->len, bits == 16, bits != 16);
            return s->pcm ? 0 : -1;
        }
        p += 8 + len + (len & 1);
    }
    return -1;
}

/* GF1 patch: 129-byte header, 63-byte instrument, 47-byte layer, 96-byte wave header, data. */
static int load_pat(const uint8_t *d, size_t size, Sample *s) {
    const size_t w = 129 + 63 + 47;
    if (size < w + 96 || memcmp(d, "GF1PATCH", 8)) return -1;
    uint32_t bytes = rd32(d + w + 8), ls = rd32(d + w + 12), le = rd32(d + w + 16);
    int modes = d[w + 55], bits16 = modes & 1;
    if (w + 96 + bytes > size) bytes = (uint32_t)(size - w - 96);
    s->rate = rd16(d + w + 20);
    s->len = bits16 ? (int)bytes / 2 : (int)bytes;
    if (modes & 4) {
        s->loop_start = bits16 ? (int)ls / 2 : (int)ls;
        s->loop_end = bits16 ? (int)le / 2 : (int)le;
    }
    s->pcm = to_pcm(d + w + 96, s->len, bits16, modes & 2);
    return s->pcm ? 0 : -1;
}

/* FT2 instrument: sample headers from 0x12a (40 bytes each), then delta-coded data; only the first sample. */
static int load_xi(const uint8_t *d, size_t size, Sample *s) {
    if (size < 0x12a + 40 || memcmp(d, "Extended Instrument", 19)) return -1;
    int nsmp = rd16(d + 0x128);
    if (nsmp < 1) return -1;
    const uint8_t *h = d + 0x12a;
    uint32_t bytes = rd32(h), ls = rd32(h + 4), ll = rd32(h + 8);
    int type = h[14], bits16 = (type >> 4) & 1, rel = (int8_t)h[16], fine = (int8_t)h[13];
    size_t data = 0x12a + 40u * (size_t)nsmp;
    if (data + bytes > size) bytes = (uint32_t)(size - data);
    s->len = bits16 ? (int)bytes / 2 : (int)bytes;
    s->rate = (int)(8363.0 * pow(2.0, (rel + fine / 128.0) / 12.0));
    if ((type & 3) && ll > 0) {
        s->loop_start = bits16 ? (int)ls / 2 : (int)ls;
        s->loop_end = s->loop_start + (bits16 ? (int)ll / 2 : (int)ll);
    }
    s->pcm = malloc(sizeof *s->pcm * (s->len > 0 ? s->len : 1));
    if (!s->pcm) return -1;
    int acc = 0;
    for (int i = 0; i < s->len; i++) {
        if (bits16) {
            acc = (int16_t)(acc + (int16_t)rd16(d + data + 2 * i));
            s->pcm[i] = (int16_t)acc;
        } else {
            acc = (int8_t)(acc + (int8_t)d[data + i]);
            s->pcm[i] = (int16_t)(acc * 256);
        }
    }
    return 0;
}

static void load_dir(const char *dir, Sample *bank) {
    DIR *dp = opendir(dir);
    if (!dp) return;
    for (struct dirent *e; (e = readdir(dp));) {
        const char *n = e->d_name;
        if (n[0] < '0' || n[0] > '9' || n[1] < '0' || n[1] > '9' || n[2] != '_') continue;
        int idx = (n[0] - '0') * 10 + n[1] - '0';
        const char *ext = strrchr(n, '.');
        if (idx >= MAX_SAMPLES || !ext || bank[idx].pcm) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, n);
        size_t size;
        uint8_t *d = SDL_LoadFile(path, &size);
        if (!d) continue;
        Sample s = {0};
        int rc = !SDL_strcasecmp(ext, ".WAV")  ? load_wav(d, size, &s)
                 : !SDL_strcasecmp(ext, ".PAT") ? load_pat(d, size, &s)
                 : !SDL_strcasecmp(ext, ".XI")  ? load_xi(d, size, &s)
                                                : -1;
        SDL_free(d);
        if (rc == 0 && s.len > 0) bank[idx] = s;
        else free(s.pcm);
    }
    closedir(dp);
}

static void SDLCALL mix(void *user, SDL_AudioStream *st, int additional, int total) {
    (void)user;
    (void)total;
    float buf[1024];
    for (int left = additional / (int)sizeof(float); left > 0;) {
        int n = left < 1024 ? left : 1024;
        memset(buf, 0, sizeof(float) * (size_t)n);
        for (int c = 0; c < MAX_CHANNELS; c++) {
            Channel *ch = &channels[c];
            if (!ch->s) continue;
            const Sample *s = ch->s;
            double step = ch->freq / (double)OUT_RATE;
            float g = (ch->vol > 0x10000 ? 0x10000 : ch->vol) / 65536.0f / 32768.0f * 0.4f;
            for (int i = 0; i < n && ch->s; i++) {
                int p = (int)ch->pos;
                if (p >= s->len || (ch->loops < 0 && s->loop_end > s->loop_start && p >= s->loop_end)) {
                    if (ch->loops < 0 && s->loop_end > s->loop_start) ch->pos -= s->loop_end - s->loop_start;
                    else if (ch->loops != 0) ch->pos -= s->len, ch->loops -= ch->loops > 0;
                    else {
                        ch->s = NULL;
                        break;
                    }
                    p = (int)ch->pos;
                    if (p < 0 || p >= s->len) p = 0, ch->pos = 0;
                }
                buf[i] += s->pcm[p] * g;
                ch->pos += step;
            }
        }
        for (int i = 0; i < n; i++) buf[i] = buf[i] > 1.0f ? 1.0f : buf[i] < -1.0f ? -1.0f : buf[i];
        SDL_PutAudioStreamData(st, buf, n * (int)sizeof(float));
        left -= n;
    }
}

int ign_audio_open(const char *data_dir, const char *level, const char *const *models, int ncars) {
    static const char *const GENERAL[5] = {"ROLL", "SKID", "COLL", "BOOST", "DIV"};
    char dir[1024];
    for (int i = 0; i < 5; i++) {
        snprintf(dir, sizeof dir, "%s/GENERAL/SOUND/%s", data_dir, GENERAL[i]);
        load_dir(dir, banks[IGN_SND_GENERAL][i]);
    }
    snprintf(dir, sizeof dir, "%s/LEVELS/%s/SOUND", data_dir, level);
    load_dir(dir, banks[IGN_SND_LEVEL][0]);
    for (int i = 0; i < ncars && i < MAX_SUBS; i++) {
        snprintf(dir, sizeof dir, "%s/CARS/%s/SOUND", data_dir, models[i]);
        load_dir(dir, banks[IGN_SND_CAR][i]);
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, OUT_RATE};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix, NULL);
    if (!stream) return -1;
    SDL_ResumeAudioStreamDevice(stream);
    return 0;
}

void ign_audio_close(void) {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = NULL;
    for (int g = 0; g < 3; g++)
        for (int b = 0; b < MAX_SUBS; b++)
            for (int i = 0; i < MAX_SAMPLES; i++) free(banks[g][b][i].pcm), banks[g][b][i].pcm = NULL;
}

int ign_audio_play(int group, int sub, int idx, int loops, int vol, int freq) {
    if (!stream || group < 0 || group > 2 || sub < 0 || sub >= MAX_SUBS || idx < 0 || idx >= MAX_SAMPLES) return 0;
    const Sample *s = &banks[group][sub][idx];
    if (!s->pcm) return 0;
    SDL_LockAudioStream(stream);
    int c = 0;
    while (c < MAX_CHANNELS && channels[c].s) c++;
    int handle = 0;
    if (c < MAX_CHANNELS) {
        int gen = next_gen++ & 0xffff;
        channels[c] = (Channel){s, 0, vol, freq > 0 ? freq : s->rate, loops, gen};
        handle = gen << 7 | (c + 1);
    }
    SDL_UnlockAudioStream(stream);
    return handle;
}

void ign_audio_set(int handle, int vol, int freq) {
    int c = (handle & 0x7f) - 1;
    if (!stream || c < 0 || c >= MAX_CHANNELS) return;
    SDL_LockAudioStream(stream);
    Channel *ch = &channels[c];
    if (ch->s && ch->gen == handle >> 7) {
        if (vol >= 0) ch->vol = vol;
        if (freq >= 0) ch->freq = freq;
    }
    SDL_UnlockAudioStream(stream);
}
