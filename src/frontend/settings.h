#ifndef IGN_SETTINGS_H
#define IGN_SETTINGS_H

#include <stdint.h>

/* The settings block g_cfg at 0xe9bfc, saved raw as IGN_DOS.BTZ (0x597 bytes, packed, little-endian). Offsets below
 * are from 0xe9bfc. */
#define IGN_CFG_SIZE 0x597
#define IGN_CFG_VERSION 30

enum {
    CFG_VERSION = 0x000,      /* 30 */
    CFG_TWO_PLAYERS = 0x004,  /* split screen */
    CFG_GAME_MODE = 0x008,    /* 0 championship, 1 single race, 2 time trial, 3 pursue */
    CFG_DIFFICULTY = 0x00c,   /* 0..3 NOVICE, AMATEUR, PRO, MIRROR (page 15) */
    CFG_GHOST = 0x010,        /* GHOST CAR */
    CFG_OBSTACLES = 0x014,    /* OBSTACLES */
    CFG_FLAG_018 = 0x018,
    CFG_CAR_SLOT = 0x01c,     /* int[8] car id per racer slot */
    CFG_UNK_03C = 0x03c,
    CFG_P1_CAR = 0x040,       /* index into the selectable car list (page 4) */
    CFG_P2_CAR = 0x044,       /* page 5 */
    CFG_GEARBOX = 0x048,      /* int[9] per slot; [0] P1 GEARBOX, [8] P2 GEARBOX (pages 11/12) */
    CFG_NAMES = 0x06c,        /* char[9][15]; [0] P1 name, [8] P2 name */
    CFG_TRACK = 0x0f3,        /* selected track (game mode != 0) */
    CFG_CD_TRACK = 0x0f7,     /* -1 random, 0 = CD track 7, n */
    CFG_MUSIC_VOL = 0x0fb,    /* CD VOLUME 0..10 */
    CFG_SFX_VOL = 0x0ff,      /* SFX VOLUME 0..10 */
    CFG_SCREEN_SIZE = 0x103,  /* 0..3, = 3 - CFG_SCREEN_INV */
    CFG_SCREEN_INV = 0x107,
    CFG_RESOLUTION = 0x10b,   /* 0..2 */
    CFG_PERSP = 0x10f,        /* PERSP. POLY */
    CFG_SKIDS = 0x113,        /* SKID MARKS */
    CFG_SMOKE = 0x117,        /* SMOKE */
    CFG_UNK_11B = 0x11b,
    CFG_UNK_11F = 0x11f,
    CFG_P1_CONTROLS = 0x123,  /* 0 keyboard, 1.. joystick/wheel */
    CFG_P2_CONTROLS = 0x127,
    CFG_P1_AUTOACC = 0x12b,
    CFG_P2_AUTOACC = 0x12f,
    CFG_GRID = 0x133,         /* int[8] grid order */
    CFG_RACERS = 0x153,       /* number of racers */
    CFG_UNK_157 = 0x157,
    CFG_MIPMAP = 0x15b,       /* MIP MAPPING */
    CFG_CONTROL = 0x15f,      /* int[8]: 0 human, 1 computer */
    CFG_P1_KEYS = 0x17f,      /* u8[8] scancodes: left, right, accelerate, brake, gear up, gear down, turbo, rear */
    CFG_P2_KEYS = 0x187,
    CFG_UNK_18F = 0x18f,
    CFG_UNK_193 = 0x193,
    CFG_STR_19B = 0x19b,      /* "G1" */
    CFG_STR_1AA = 0x1aa,      /* "12" */
    CFG_RECORDS = 0x1bf,      /* 7 tracks x 0x78: {char name[5][4]; double total[5]; char lap_name[5][4]; double lap[5]} */
    CFG_PAGE_CURSOR = 0x507,  /* int[25] page cursors */
    CFG_HS_TRACK = 0x57f,     /* BEST TIMES track 0..6 */
    CFG_HS_LAP = 0x583,       /* BEST TIMES 0 total, 1 lap */
    CFG_UNLOCK = 0x587,       /* highest unlocked difficulty */
    CFG_BONUS = 0x58b,
    CFG_UNK_58F = 0x58f,
    CFG_LANGUAGE = 0x593,     /* language page value */
};

extern uint8_t g_cfg[IGN_CFG_SIZE];

int32_t ign_cfg_get(int off);
void ign_cfg_set(int off, int32_t v);
/* Factory best times of 0xbf628 into g_cfg (from ign_btz_default, game/save.c). */
void ign_cfg_default_records(void);
/* sub_0001a864 alone: unlike ign_btz_default it keeps the records and the page cursors (RESTORE SETTINGS). */
void ign_cfg_defaults(void);
/* Reads `path` (ign_btz_load) over g_cfg when its first int is 30. Returns 0 if it was loaded. */
int ign_cfg_load(const char *path);
int ign_cfg_save(const char *path); /* ign_btz_save */

#endif
