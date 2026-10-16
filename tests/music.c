#include <math.h>
#include <stdio.h>
#include <string.h>

#include "platform/audio.h"
#include "platform/music.h"

/* Exercise the decoder synchronously, without an audio callback or a real playback device. */
static int device_open = 1, locked;
int ign_audio_lock(void) {
    if (!device_open) return 0;
    locked++;
    return 1;
}
void ign_audio_unlock(void) { locked--; }

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

static int play_to_end(const char *data, int volume, int *chunks) {
    *chunks = 0;
    ign_music_set_volume(volume);
    if (ign_music_play(data, 2, 0)) return check(0, "open track 2");
    int failed = check(ign_music_playing(), "busy after playback starts");
    float buffer[4096];
    int heard = 0;
    for (int chunk = 0; chunk < 3600 * 22050 / 4096 && ign_music_playing(); chunk++) {
        memset(buffer, 0, sizeof buffer);
        ign_music_mix(buffer, 4096, 22050);
        (*chunks)++;
        for (int i = 0; i < 4096; i++) {
            if (!isfinite(buffer[i])) return check(0, "finite decoded samples");
            heard |= buffer[i] != 0;
        }
    }
    failed |= check(!ign_music_playing(), "finish the track even with volume zero");
    failed |= check(heard == (volume != 0), "mute changes the gain, not the playback clock");
    ign_music_stop();
    return failed;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "music: expected game data directory\n");
        return 1;
    }
    int failed = check(!ign_music_playing(), "idle before playback");
    const char *levels[] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};
    const int tracks[] = {2, 3, 4, 5, 6, 6, 7};
    for (int i = 0; i < 7; i++)
        failed |= check(ign_music_track(levels[i], IGN_MUSIC_BY_LEVEL) == tracks[i], "original track mapping");
    for (int i = 0; i < 128; i++) {
        int track = ign_music_track("USA", IGN_MUSIC_RANDOM);
        failed |= check(track >= 2 && track <= IGN_MUSIC_LAST, "random mode skips the data track");
    }
    int audible_chunks, muted_chunks;
    failed |= play_to_end(argv[1], 255, &audible_chunks);
    failed |= play_to_end(argv[1], 0, &muted_chunks);
    failed |= check(audible_chunks == muted_chunks, "muted and audible playback have identical duration");
    failed |= check(!ign_music_play(argv[1], 2, 1), "start looping track");
    ign_music_set_volume(255);
    for (int chunk = 0; chunk < audible_chunks + 3; chunk++) {
        float buffer[4096] = {0};
        ign_music_mix(buffer, 4096, 22050);
    }
    failed |= check(ign_music_playing(), "loop past EOF without reporting an idle CD");
    ign_music_stop();
    for (int track = 2; track <= IGN_MUSIC_LAST; track++) {
        failed |= check(!ign_music_play(argv[1], track, 1), "open each native CD track");
        ign_music_set_volume(255);
        float buffer[4096] = {0};
        ign_music_mix(buffer, 4096, 48000);
        failed |= check(ign_music_playing(), "looping playback remains busy");
        for (int i = 0; i < 4096; i++) failed |= check(isfinite(buffer[i]), "finite resampled output");
        ign_music_stop();
        failed |= check(!ign_music_playing(), "stop clears busy state");
        ign_music_stop();
    }
    failed |= check(ign_music_play(argv[1], IGN_MUSIC_LAST + 1, 0) == -1, "missing track reports failure");
    failed |= check(!ign_music_playing(), "failed play does not report success or keep an old song");
    device_open = 0;
    failed |= check(ign_music_play(argv[1], 2, 0) == -1, "closed device rejects playback");
    failed |= check(!ign_music_playing() && locked == 0, "balanced ownership and locks");
    ign_music_stop();
    return failed ? 1 : 0;
}
