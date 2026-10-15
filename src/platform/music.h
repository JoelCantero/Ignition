#ifndef IGN_MUSIC_H
#define IGN_MUSIC_H

/* CD audio of the original (MSCDEX through DPMI 0x300 / int 0x2f, sub_00064559 and sub_000645e1), played from the
 * GOG rips MUSIC/Track02-08.ogg. CD track n is TrackNN.ogg; track 1 is the data track. */
enum { IGN_MUSIC_MENU = 7, IGN_MUSIC_RESULTS = 8, IGN_MUSIC_LAST = 8 };
/* [0x1ff24f]: 0 picks the level's track, -1 a random one, n > 0 always track n. */
enum { IGN_MUSIC_BY_LEVEL = 0, IGN_MUSIC_RANDOM = -1 };

/* level_prepare 0x22ae9 / 0xc3fd8: CD track of a level by name (CANADA 2, USA 3, CARIB 4, BRAZIL 5, AUSTRIA 6,
 * ICELAND 6, JAPAN 7) or, for `mode` != 0, the random or fixed track of the music setting. */
int ign_music_track(const char *level, int mode);
/* sub_00064241: starts CD track `track` from data_dir/MUSIC, replacing the current one; `loop` restarts it at the
 * end as the race loop 0x21311 does. Returns -1 (and plays nothing) when the file is missing or unreadable. */
int ign_music_play(const char *data_dir, int track, int loop);
/* sub_00064391: stops the music and frees the decoder. Call before ign_sound_stop closes the device. */
void ign_music_stop(void);
/* sub_00063f3e: CD volume 0..255 (the race sets the 0..10 setting [0x1ff253] x 25.5). */
void ign_music_set_volume(int volume);
/* Audio thread, with the stream locked: adds `n` mono frames at `rate` Hz to `buf`. */
void ign_music_mix(float *buf, int n, int rate);

#endif
