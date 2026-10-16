#ifndef IGN_SAVE_H
#define IGN_SAVE_H

#include <stddef.h>
#include <stdint.h>

#include "car.h"
#include "ghost.h"

/* IGN_DOS.BTZ: raw image of the 0x597 bytes at 0x1ff158 (packed, unaligned i32 and binary64).
 * Read whole by sub_0002293c (read_file_into), written whole by sub_0002cf60 and on exit by sub_0002b430.
 * The port keeps the image as bytes so unknown fields survive a load/save round trip unchanged. */
#define IGN_BTZ_SIZE 0x597
#define IGN_BTZ_NAME "IGN_DOS.BTZ"

/* Field offsets inside the image (global = 0x1ff158 + offset). */
#define IGN_BTZ_VERSION 0x000  /* i32 30: the menu (menu_load_gfx 0x1161b, 0x1271e) only loads a file with 30 */
#define IGN_BTZ_PLAYERS 0x004  /* i32 -> [0x1fef14]: human players - 1 */
#define IGN_BTZ_MODE 0x008     /* i32 0..3 -> [0x1feefc]; 2 = time trial, 3 = elimination */
#define IGN_BTZ_CLASS 0x00c    /* i32 -> [0x1fedd8]: championship class 0..3 (5, 6, 7, 7 tracks), <= +0x587 */
#define IGN_BTZ_GHOST 0x010    /* i32 -> [0x1fee4c]: ghost on; forced 0 outside time trial */
#define IGN_BTZ_CAR 0x01c      /* i32[9] -> participant+0x00 (car model) */
#define IGN_BTZ_NAMES 0x06c    /* char[9][15] -> participant+0x3c */
#define IGN_BTZ_NAME_LEN 15
#define IGN_BTZ_TRACK 0x0f3    /* i32: track id, or championship round when mode == 0 */
#define IGN_BTZ_GRID 0x133     /* i32[8] -> 0xc33d0: grid order; 6 - place after a championship race */
#define IGN_BTZ_NCARS 0x153    /* i32 -> [0x1fefe4] */
#define IGN_BTZ_HUMAN 0x15f    /* i32[8] -> participant+0x04 (1 = computer ¿?) */
#define IGN_BTZ_RECORDS 0x1bf  /* IgnTrackRecords[7], 0x78 bytes each */
#define IGN_BTZ_OPTIONS 0x507  /* i32[25]: menu option widget values (+0x14 of the widgets at 0xea47c) */
#define IGN_BTZ_UNLOCKED 0x587 /* i32 -> [0x1ff6df]: highest championship class unlocked (render_main 0x45008) */
#define IGN_BTZ_BONUS 0x58b    /* i32 -> [0x1ff6e3]: 1 after winning class 3 (0x44ffe) */
#define IGN_BTZ_LANGUAGE 0x593 /* i32 -> [0x1ff6eb]: 0 EN, 1 DE, 2 IT, 3 ES, 4 SV, 5 FR */

#define IGN_BTZ_VERSION_VALUE 30
#define IGN_BTZ_MAX_CARS 9
#define IGN_BTZ_TRACKS 7
#define IGN_RECORD_SLOTS 5
#define IGN_RECORD_STRIDE 0x78

typedef struct {
    uint8_t raw[IGN_BTZ_SIZE];
} IgnBtz;

/* Track block at 0x1ff317 + track * 0x78: race top 5 at +0x00/+0x14, lap top 5 at +0x3c/+0x50.
 * Names are 3 characters + NUL; times in seconds, best first. */
typedef struct {
    char race_name[IGN_RECORD_SLOTS][4];
    double race[IGN_RECORD_SLOTS];
    char lap_name[IGN_RECORD_SLOTS][4];
    double lap[IGN_RECORD_SLOTS];
} IgnTrackRecords;

/* Return 0 on success; load requires the exact size. */
int ign_btz_load(const char *path, IgnBtz *b);
int ign_btz_save(const char *path, const IgnBtz *b);
/* The menu's fresh image (menu_load_gfx 0x126be..0x12738): BSS zero, defaults of sub_0001a864, option widgets
 * and the factory record table 0xbf628. Used when the file is missing or its version is not 30. */
void ign_btz_default(IgnBtz *b);

int32_t ign_btz_i32(const IgnBtz *b, size_t off);
void ign_btz_set_i32(IgnBtz *b, size_t off, int32_t v);
/* Participant name (15-byte slot, NUL-terminated by the reader). */
void ign_btz_name(const IgnBtz *b, int car, char out[IGN_BTZ_NAME_LEN + 1]);

/* Return -1 for a track id outside 0..6. */
int ign_btz_records(const IgnBtz *b, int track, IgnTrackRecords *r);
int ign_btz_set_records(IgnBtz *b, int track, const IgnTrackRecords *r);

/* render_main 0x459c6..0x45b14: a time strictly below slot 4 replaces it and bubbles up one pass.
 * Returns the slot reached (0 = new record) or -1 when the table is unchanged. */
int ign_records_insert_race(IgnTrackRecords *r, const char *name, double seconds);
/* 0x45b1a..0x45dc9: the same for the lap table; called once per recorded lap (car+0x3bc/+0x3c4/+0x3cc). */
int ign_records_insert_lap(IgnTrackRecords *r, const char *name, double seconds);

/* sub_0002cf60 0x2cf67..0x2cfd6: outside championship (mode != 0) with more than one car, swap grid slots
 * ncars times using two random indices floor(random01() * ncars). mode and ncars are the live [0x1feefc] and
 * [0x1fefe4], not the image fields. */
void ign_btz_shuffle_grid(IgnBtz *b, int mode, int ncars, double (*random01)(void));

/* Championship (mode 0). render_main 0x44a2a: qualified when car 0 (or car 1 with two players) finished in the
 * first 3; the loop does not advance its car pointer, so only these two cars are looked at. This API takes
 * zero-based places; car+0x3a0 in the executable is one-based. */
int ign_champ_qualified(int place0, int place1, int players_minus_one);
/* 0x44a66: round 4 of class 0, 5 of class 1 or 6 of classes 2/3 completes the class. */
int ign_champ_class_complete(int round, int klass);
/* sub_0002ccc8 0x2ce8d..0x2cf40 for mode 0: a qualified car 0 that did not complete the class advances the round
 * ([0x1fee54] = track + 1 > track; capped at 6); grid slot i becomes 5 - zero_based_place[i]. */
void ign_champ_end_race(IgnBtz *b, int qualified_advance, const int *place, int ncars);
/* render_main 0x44fd2..0x45095, only when car 0 leads the standings with one player: the last round of a class
 * raises +0x587 and moves +0x00c to the next class; the last round of class 3 sets +0x58b. */
void ign_champ_unlock(IgnBtz *b);
/* sub_0002cf60 before writing: shuffle the grid, then write back +0x107 = [0x1fef08] and +0x10b = [0x1fefcc]. */
void ign_btz_before_save(IgnBtz *b, int mode, int ncars, int value_107, int value_10b, double (*random01)(void));

/* Localised "LAP RECORD" (0x2f43a) and "TRACK RECORD" (0x2f548) for the BTZ language. */
const char *ign_lap_record_text(int language);
const char *ign_track_record_text(int language);

/* "<dir><name>" into out; dir is a user folder ending in a separator (SDL_GetPrefPath). -1 if too long. */
int ign_save_path(char *out, size_t size, const char *dir, const char *name);
/* Creates <dir>GHOSTS/ if needed; 0 when it exists afterwards. */
int ign_save_make_ghost_dir(const char *dir);

/* Bits returned by ign_records_commit and ign_session_finish. */
#define IGN_SAVED_TRACK_RECORD 1 /* race time reached slot 0 */
#define IGN_SAVED_LAP_RECORD 2   /* a lap reached slot 0 */
#define IGN_SAVED_RACE_TABLE 4   /* race time entered the top 5 */
#define IGN_SAVED_LAP_TABLE 8    /* a lap entered the top 5 */
#define IGN_SAVED_GHOST 16       /* GHOSTS\<name>.GST written */
#define IGN_SAVED_BTZ 32         /* IGN_DOS.BTZ written */

/* render_main 0x45938..0x45f20 for one finished human car: race time (car+0x3a4) and the three lap slots.
 * Port guard: a wrecked car or an empty lap slot (0) is skipped instead of entering a 0.00 record. */
int ign_records_commit(IgnBtz *b, int track, const char *name, const IgnCar *car);

/* Session persistence in a user folder; the game folder is only read. */
typedef struct {
    char dir[1024];     /* user folder with a trailing separator */
    IgnBtz btz;
    int btz_source;     /* 0 user file, 1 copy of the game folder file, 2 port default */
    int track;          /* 0..6 or -1 */
    int time_trial;     /* [0x1feefc] == 2 with the ghost option on */
    int value_107, value_10b; /* [0x1fef08], [0x1fefcc] as loaded (the port does not change them) */
    IgnTrialGhost ghost_state;
    int record_cs;      /* [0x1fee04] */
    IgnGhost rec;       /* recorded run (0x51204) */
    char ghost_path[1100];
    int finished, ended;
} IgnSession;

/* Loads <dir>IGN_DOS.BTZ, else <data_dir>/IGN_DOS.BTZ, else the default image. With time_trial, prepares the
 * recording and loads <dir>GHOSTS/<name>.GST (else the game folder one) into *play. Returns 0 on success. */
int ign_session_begin(IgnSession *s, const char *dir, const char *data_dir, const char *level, int car_model,
                      int time_trial, IgnGhost *play);
/* sub_0002a410 0x2a442: [0x1fb390] starts from the saved best lap of the track (DBL_MAX without one). */
double ign_session_best_lap(const IgnSession *s);
/* sub_00051204 cadence: one sample per physics tick after the start, until the car is out. */
void ign_session_tick(IgnSession *s, const IgnCar *player, int sections);
/* sub_0002ccc8 at the end of the race (every car out): championship round/grid in mode 0, grid shuffle otherwise,
 * then IGN_DOS.BTZ. place[i] = arrival order of car i (0-based). Returns IGN_SAVED_* bits or -1. */
int ign_session_race_end(IgnSession *s, int mode, const int *place, int ncars);
/* Once, when the human car finishes: records, the ghost if beaten (sub_0002ccc8) and IGN_DOS.BTZ (sub_0002cf60).
 * Returns IGN_SAVED_* bits; -1 if a file could not be written. */
int ign_session_finish(IgnSession *s, const IgnCar *player);

#endif
