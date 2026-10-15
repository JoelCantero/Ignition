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
/* sub_00064241: starts CD track `track` from data_dir/MUSIC, replacing the current one; `loop` restarts it at once
 * at the end (the race instead replays through ign_music_poll). Returns -1 (and plays nothing) when the file is
 * missing or unreadable. */
int ign_music_play(const char *data_dir, int track, int loop);
/* sub_00064391: stops the music, ends the race state and frees the decoder. Call before ign_sound_stop closes the device. */
void ign_music_stop(void);
/* sub_00063f3e: CD volume 0..255 (the race sets the 0..10 setting [0x1ff253] x 25.5). */
void ign_music_set_volume(int volume);
/* level_prepare 0x2249c: stores the race state (data folder, level, [0x1ff24f] `mode`, [0x1ff253] `volume` 0..10),
 * sets the CD volume and plays the chosen track once. */
int ign_music_start(const char *data_dir, const char *level, int mode, int volume);
/* Once per frame of the race loop (0x212f8): `seconds` of game time; `finish_position` the player's car+0x3a0 once
 * finished (0 before). Replays the track when it stopped and plays track 8 for places 1-3. */
void ign_music_poll(double seconds, int finish_position);
/* 0x2c1cb / 0x2c29a (keys 0xcb/0xcd): previous (-1) or next (+1) value of the music setting and its track. */
void ign_music_step(int dir);
/* Audio thread, with the stream locked: adds `n` mono frames at `rate` Hz to `buf`. */
void ign_music_mix(float *buf, int n, int rate);

#endif
