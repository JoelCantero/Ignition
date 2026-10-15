#ifndef IGN_AUDIO_H
#define IGN_AUDIO_H

/* Sound banks of sub_0002a4ec (sub_00063310): group 0 = GENERAL/SOUND/{ROLL, SKID, COLL, BOOST, DIV}, group 1 = the
 * level's SOUND, group 2 = CARS/<model>/SOUND per car. Samples are indexed by their "NN_" file prefix. */
enum { IGN_SND_GENERAL = 0, IGN_SND_LEVEL = 1, IGN_SND_CAR = 2 };
enum { IGN_SND_ROLL = 0, IGN_SND_SKID = 1, IGN_SND_COLL = 2, IGN_SND_BOOST = 3, IGN_SND_DIV = 4 };

/* Opens the SDL audio device and loads the banks; `models` are the CARS folder names of the race cars. */
int ign_audio_open(const char *data_dir, const char *level, const char *const *models, int ncars);
void ign_audio_close(void);
/* sub_000625f4: starts sample `idx` of a bank. `loops` -1 repeats forever, n plays n extra times; `vol` 0..0x10000;
 * `freq` playback rate in Hz, 0 = the sample's own. Returns a handle (0 if nothing plays). */
int ign_audio_play(int group, int sub, int idx, int loops, int vol, int freq);
/* sub_000626fc + the +0xc (volume) / +0x10 (rate, 0 holds the sample) fields; negative values are left as they are. */
void ign_audio_set(int handle, int vol, int freq);
/* Holds the mixer (for the music channel of music.c); 0 when no device is open, and then nothing to unlock. */
int ign_audio_lock(void);
void ign_audio_unlock(void);

#endif
