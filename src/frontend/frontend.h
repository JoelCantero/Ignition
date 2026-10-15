#ifndef IGN_FRONTEND_FRONTEND_H
#define IGN_FRONTEND_FRONTEND_H

/* Frontend of MAINDOS.EXE (0x10010..0x1aea0): menu_load_gfx 0x11504, menu_frame 0x12bb8, menu_shutdown 0x13a5c,
 * the page key callback 0x160c4, the item callbacks 0x16f18..0x17054, the page draw callback 0x170ac (with the text
 * layer 0x155d0), the iris 0x15344, best times 0x1911c, the 3D menu cars 0x19508/0x1aaf8 and the settings 0x198bc /
 * 0x1a864. The host (SDL in the port; VESA, Sound Blaster, MSCDEX and the joystick port in the original) is reached
 * through IgnFrontendHost. */

#include <stdint.h>

#define IGN_FRONTEND_W 320
#define IGN_FRONTEND_H 200

enum { IGN_FRONTEND_STAY = 1, IGN_FRONTEND_RACE = 2, IGN_FRONTEND_QUIT = 3 };

typedef struct {
    int (*timer_ms)(void);                       /* g_timer_ms 0xab5dc */
    void (*set_palette)(const uint8_t rgb[768]); /* 0x55910, 8-bit RGB */
    void (*present)(const uint8_t *buf);         /* 0x558a0 + 0x55930: 320x200 */
    /* Sound bank 0 = BALTAZAR/DATA/SOUND (0_SEL, 1_FLIP, 2_NO, 3_INTRO). NULL members are no-ops. */
    int (*sound_init)(void);                      /* 0x623c0: 0 no sound, 1 ok */
    int (*sound_play)(int sample, int vol, int rate); /* 0x625f4: handle or -1 */
    void (*sound_stop_voice)(int handle);         /* voice +0x10 = 0 through 0x626fc */
    void (*sound_silence)(void);                  /* 0x62544 */
    void (*sound_free)(int sample);               /* 0x63298 */
    void (*sound_volume)(int vol);                /* 0x62740: 0..0x10000 */
    void (*sound_update)(void);                   /* 0x624cc */
    void (*sound_shutdown)(void);                 /* 0x6250c */
    /* CD audio (MSCDEX in the original, MUSIC/TrackNN.ogg in the GOG release). */
    int (*cd_tracks)(void);                       /* 0x63d56 + 0x640b3: 0 = no disc */
    void (*cd_play)(int track);                   /* 0x64241 */
    void (*cd_stop)(void);                        /* 0x64391 */
    void (*cd_volume)(int vol);                   /* 0x63f3e: 0..255 */
    int (*cd_busy)(void);                         /* 0x63cf3 */
    /* User folder (trailing separator optional) for IGN_DOS.BTZ and GHOSTS, shared with the race (save.c). The
     * original wrote both into the game folder; the port only reads the game folder's IGN_DOS.BTZ as a fallback. */
    const char *save_dir;
} IgnFrontendHost;

/* Keyboard as seen by the menu (0x55e04 / 0x55ae4): DOS set-1 scancodes, extended keys + 0x80. */
extern uint8_t g_key_latch[256]; /* 0x20aab0 */
void ign_frontend_key_down(int scancode);

/* game_init 0x215d0: identity LUT, fonts, N_SYSGFX sprites, MenuBkg.dat, iris table. */
int ign_frontend_init(const char *data_dir, const IgnFrontendHost *host);
void ign_frontend_destroy(void);
int ign_frontend_load(void);     /* menu_load_gfx 0x11504 */
int ign_frontend_frame(void);    /* menu_frame 0x12bb8: IGN_FRONTEND_STAY / RACE / QUIT */
int ign_frontend_shutdown(void); /* menu_shutdown 0x13a5c */
/* Cheat flags 0x1fb2f0: BANARNE, SURMULE, SLASKTRATT, FILMJOLK, SKUNK, SVINPOLE, STRINGS. */
int ign_frontend_cheat(int i);
/* Level directory of menu track t (0 MOOSEJAW FALLS .. 6 TOKYO BULLET). */
const char *ign_frontend_level(int track);

#endif
