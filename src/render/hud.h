#ifndef IGN_HUD_H
#define IGN_HUD_H

#include "../game/assets.h"
#include "../game/car.h"
#include "../game/track.h"
#include "font.h"
#include "raster.h"
#include "sprite.h"

#define IGN_HUD_MESSAGES 5 /* [0x1fefb8]: 5 slots of 0x78 bytes */
#define IGN_HUD_MAX_CARS 8

/* [0x1feeac]: HUD layout picked by sub_00028a14 from the video mode. */
enum { IGN_HUD_LOW, IGN_HUD_MEDIUM, IGN_HUD_HIGH }; /* 320x200 S_*, 640x480 N_*, 800x600 H_* */

/* Message slot of sub_0004c7c0 (0x78 bytes). */
typedef struct {
    int active;     /* +0x00 */
    char text[100]; /* +0x04 */
    int duration;   /* +0x68: removed when timer > duration */
    int period;     /* +0x6c: shown while fmod(timer, 2 * period) < period */
    int timer;      /* +0x70 */
    int viewport;   /* +0x74: -1 = centred on the screen */
} IgnHudMessage;

/* HUD-owned copies of car fields the original HUD reads or animates; the game structures stay untouched. */
typedef struct {
    double gear_anim;  /* car+0x280: gear lever frame, moves towards 4 * gear */
    double needle;     /* car+0x290: rev needle angle in degrees, 0..270 */
    int turbo_blink;   /* car+0x540 */
    int sign_blink;    /* car+0x5f8 */
    int wrong_way;     /* car+0x580: sub_0004a150, 0..120 */
    int section;       /* last seen car+0x364 */
    int prev_section;  /* car+0x368: section before the last change (car_update 0x2fbb2) */
    int medal_zoom;    /* car+0x5d4: frames after the finish, up to 25 (sub_000420cc 0x428b0) */
    double blow;       /* car+0x60c: elimination explosion frame, +0.3 per frame up to 10 (cars_body_sway 0x41038) */
    int was_out;
} IgnHudCar;

/* Race data the HUD reads each frame; everything comes from existing structures. cars[0] is the viewed player. */
typedef struct {
    const IgnCar *const *cars;
    int ncars;              /* [0x1fefe4] */
    const IgnLevel *level;
    const IgnTrack *track;
    int mode;               /* [0x1feefc]; 3 = elimination */
    int remaining;          /* [0x1fed78] */
    int difficulty;         /* [0x1fedd8]: picks the sign table */
    double clock;           /* [0x1fb4e8]: 0..200 before the start, -1 racing */
    double total;           /* [0x1fb4d8]: 1/36 s frames since the level started */
    double best_lap;        /* [0x1fb390] */
    double shake;           /* camera +0x38 of the viewed car */
    int viewer;             /* player whose HUD is drawn (car index 0 or 1) */
    int players;            /* [0x1fef14] + 1: one or two viewports (0 counts as one) */
    int view_size;          /* [0x1fefdc]: window size option 0-3 (factors 0xc0b80) */
} IgnHudState;

typedef struct {
    int layout;   /* [0x1feeac] */
    int language; /* [0x1ff6eb]: 0 English, 1 German, 2 Italian, 3 Spanish, 4 Swedish, 5 French */
    uint8_t *pan; /* LEVELS\x\x.PAN: blend table of every HUD sprite ([0x257e48]) */
    IgnPic pangfx_s, pangfx_n, pan1, pan2, signs, sysg2;
    /* sub_00028000 sprites. */
    IgnSprite panel_total, panel_lap, panel_pos, dial; /* 0x1fe9c8..0x1fe9d4: rect table entries 0-3 */
    IgnSprite needle_cap;                              /* 0x1fe9d8: entry 26 */
    IgnSprite gear[9];                                 /* 0x1fe9dc: entries 4-12 */
    IgnSprite turbo_frame, turbo_light[2];             /* 0x1fea00..0x1fea08: entries 27-29 */
    IgnSprite countdown[4];                            /* 0x1fea3c: entry 33 of N_PANGFX, 3-2-1-GO lights */
    IgnSprite sign[16];                                /* 0x1fea4c: entry 34 of *_SIGNS.PIC, 4 x 4 grid */
    IgnSprite marker[12];                              /* 0x1feb4c: N_PANGFX car markers (elimination bar) */
    IgnSprite blast[10];                               /* 0x1feb24: N_PANGFX explosion frames */
    IgnSprite bar[2];                                  /* 0x1febd4 (low res, 7x120), 0x1febd8 (7x300) */
    IgnSprite crown;                                   /* 0x1febdc: leader mark */
    IgnSprite medal[4];                                /* 0x1feb7c: [1..3] = N_SYSG_2 places */
    IgnFont time_font, pos_font, speed_font;           /* YELLOW*, POS*, SPEED* of the layout */
    IgnFont message_font;                              /* MINI (low res) or SMALL ([0x1feeb0]) */
    IgnFnt debug_font;                                 /* IGNITION.FNT */
    IgnHudMessage msg[IGN_HUD_MESSAGES];
    IgnHudCar car[IGN_HUD_MAX_CARS];
    double frame_clock;   /* last whole 1/36 s frame processed */
    double finish_clock;  /* [0x1fb4d0]: frames since a player finished */
    int finish_frames[2]; /* [0x1fed9c]: frames since each player finished, up to 120 */
    double best_lap;      /* last [0x1fb390] seen */
    int started;
    uint32_t rng;         /* private stand-in for sub_00051714 in the shake jitter */
} IgnHud;

/* load_fonts 0x240f4, load_panel_gfx 0x243e0 and sub_00028000 for the layout of a w x h frame. `level` names the
 * LEVELS directory whose PAN table blends the sprites. Language from IGN_LANG (0-5 or en/de/it/es/sv/fr). */
int ign_hud_init(IgnHud *hud, const char *data_dir, const char *level, int w, int h);
void ign_hud_destroy(IgnHud *hud);
/* Advances the per-frame HUD state (counters, animations, messages) one 1/36 s frame at a time up to
 * state->total. Safe to call every physics tick. */
void ign_hud_update(IgnHud *hud, const IgnHudState *state);
/* sub_0004a320 for state->viewer in its viewport, then sub_0004c89c (messages). */
void ign_hud_draw(IgnHud *hud, IgnFrame *frame, const IgnHudState *state);
/* sub_00049724: viewport of player `index` in a w x h frame (x1, y1 exclusive as the clip window of sub_00055958).
 * One player: centred, size factors 0xc0b80[size]; two: the width halves, player 0 on the right half and player 1 on
 * the left, one pixel apart from the middle. */
void ign_hud_viewport(int w, int h, int size, int players, int index, IgnClip *out);
/* sub_0004c7c0: queues a message; returns its slot or -1 when the five are busy. */
int ign_hud_message(IgnHud *hud, const char *text, int duration, int period, int viewport);
/* sub_0004bdf4: "%.2d:%.2d:%.2d" minutes (at most 99), seconds and hundredths, truncated. */
void ign_hud_format_time(char out[16], double seconds);

#endif
