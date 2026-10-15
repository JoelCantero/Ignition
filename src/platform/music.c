#include "music.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "../../third_party/stb_vorbis/stb_vorbis.c"

/* Full CD volume against the 0.4 x 0x10000 of one sample in audio.c: the rips are mastered near full scale. */
#define MUSIC_GAIN 0.5f
#define CHUNK 1024

typedef struct {
    stb_vorbis *v;
    unsigned char *data; /* the whole OGG file, read by stb_vorbis_open_memory */
    int rate, chans, loop, done;
    float pcm[CHUNK]; /* decoded mono frames */
    int len, at;
    float s0, s1; /* linear resampling between two source frames */
    double frac;
} Song;

static Song *song;
static float gain = MUSIC_GAIN; /* volume 255 */

int ign_music_track(const char *level, int mode) {
    /* 0xc3fd8, indexed by the track id [0x1fef10]. */
    static const char *const LEVEL[7] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};
    static const int TRACK[7] = {2, 3, 4, 5, 6, 6, 7};
    /* 0x22b0c: random 1 + rand x [0x25a934] (the disc's last track). Deviation: the data track 1 is skipped. */
    if (mode == IGN_MUSIC_RANDOM) return 2 + SDL_rand(IGN_MUSIC_LAST - 1);
    if (mode > 0) return mode > IGN_MUSIC_LAST ? IGN_MUSIC_LAST : mode;
    for (int i = 0; i < 7; i++)
        if (level && !strcmp(level, LEVEL[i])) return TRACK[i];
    return IGN_MUSIC_MENU;
}

static void song_free(Song *s) {
    if (!s) return;
    if (s->v) stb_vorbis_close(s->v);
    SDL_free(s->data);
    SDL_free(s);
}

/* Swaps the playing song under the stream lock and frees the old one outside it; -1 if no device is open. */
static int song_swap(Song *s) {
    Song *old = s;
    int ok = ign_audio_lock();
    if (ok) {
        old = song;
        song = s;
        ign_audio_unlock();
    }
    song_free(old);
    return ok ? 0 : -1;
}

int ign_music_play(const char *data_dir, int track, int loop) {
    char path[1024];
    snprintf(path, sizeof path, "%s/MUSIC/Track%02d.ogg", data_dir, track);
    Song *s = SDL_calloc(1, sizeof *s);
    size_t size = 0;
    if (s) s->data = SDL_LoadFile(path, &size);
    int err = 0;
    if (s && s->data && size <= SDL_MAX_SINT32) s->v = stb_vorbis_open_memory(s->data, (int)size, &err, NULL);
    if (!s || !s->v) {
        fprintf(stderr, "music: cannot play %s\n", path);
        song_free(s);
        ign_music_stop();
        return -1;
    }
    stb_vorbis_info info = stb_vorbis_get_info(s->v);
    s->rate = (int)info.sample_rate;
    s->chans = info.channels;
    s->loop = loop;
    s->frac = 1.0; /* reads two frames before the first output */
    return song_swap(s);
}

void ign_music_stop(void) { song_swap(NULL); }

void ign_music_set_volume(int volume) {
    volume = volume < 0 ? 0 : volume > 255 ? 255 : volume;
    int locked = ign_audio_lock();
    gain = volume / 255.0f * MUSIC_GAIN;
    if (locked) ign_audio_unlock();
}

/* Next mono source frame; 0 = the song ended without looping. */
static int next_frame(Song *s, float *out) {
    while (s->at >= s->len) {
        float tmp[CHUNK * 2];
        int per = s->chans < 2 ? 1 : 2;
        int n = stb_vorbis_get_samples_float_interleaved(s->v, per, tmp, CHUNK * per);
        if (n <= 0) {
            if (!s->loop || !stb_vorbis_seek_start(s->v)) return 0;
            n = stb_vorbis_get_samples_float_interleaved(s->v, per, tmp, CHUNK * per);
            if (n <= 0) return 0;
        }
        for (int i = 0; i < n; i++) s->pcm[i] = per == 2 ? 0.5f * (tmp[2 * i] + tmp[2 * i + 1]) : tmp[i];
        s->len = n;
        s->at = 0;
    }
    *out = s->pcm[s->at++];
    return 1;
}

void ign_music_mix(float *buf, int n, int rate) {
    Song *s = song;
    if (!s || s->done || gain <= 0.0f) return;
    double step = s->rate / (double)rate;
    for (int i = 0; i < n; i++) {
        while (s->frac >= 1.0) {
            s->s0 = s->s1;
            if (!next_frame(s, &s->s1)) {
                /* Played out: the next play or stop frees it outside the audio thread. */
                s->done = 1;
                return;
            }
            s->frac -= 1.0;
        }
        buf[i] += (s->s0 + (s->s1 - s->s0) * (float)s->frac) * gain;
        s->frac += step;
    }
}
