#include "frontend.h"
#include "../platform/runtime.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game/assets.h"
#include "../game/palette.h"
#include "../game/save.h"
#include "../render/lisa.h"
#include "../render/raster.h"
#include "cdp.h"
#include "gfx2d.h"
#include "gui.h"
#include "menu_data.h"
#include "pfm.h"
#include "settings.h"

#define W IGN_FRONTEND_W
#define H IGN_FRONTEND_H
#define SCREEN_BYTES 64000 /* every clear and copy of the original is 320 x 200 */
#define MENU_BUF 0xfb41    /* allocation size of the menu buffers */

#define SC_ESC 0x01
#define SC_BACKSPACE 0x0e
#define SC_ENTER 0x1c
#define SC_UP 0xc8
#define SC_LEFT 0xcb
#define SC_RIGHT 0xcd
#define SC_DOWN 0xd0

enum { SND_SEL = 0, SND_FLIP = 1, SND_NO = 2, SND_INTRO = 3 };
enum { F_RED_DARK, F_RED_LITE, F_BLUEDARK, F_BLUELITE, F_RED_GREY, F_BLUEGREY, F_MINI, F_SMALL };

uint8_t g_key_latch[256];

static char g_data_dir[1024];
static IgnFrontendHost g_host;

/* ---- state of game_init 0x215d0 (lives across menu entries) -------------------------------------------------- */
static IgnFont g_font_rec[8];          /* 0x24c214 records, slots 0x1ac4a8 */
static IgnFont *g_font_slot[8];
static uint8_t *g_sysgfx;              /* N_SYSGFX.PIC pixels, 256 x 412 */
static IgnPic g_sysg2;                 /* N_SYSG_2.PIC */
static IgnPalette g_system_palette;    /* SYS.COL */
static IgnMenuSprite g_corner[4];      /* 0x1ac478: TL, TR, BL, BR, 20 x 20 */
static IgnMenuSprite g_loading;        /* 0x1fed98: 61 x 58 at N_SYSGFX + 0xc1e7 */
/* Draw targets: the menu back buffer [0xea400] and the text layer [0xea408] (320 x 200, pitch 320). */
static IgnFrame g_back_frame, g_text_frame;
static uint8_t *g_iris_tab;            /* [0x1fef4c]: 405 x 800 */
static uint32_t g_cheat[7];            /* 0x1fb2f0 */
static int g_first_menu = 1;           /* 0xbed18 */
static int g_intro_snd_once = 1;       /* 0xbecfc */
static int g_nosound_msg_shown;        /* 0xea1b0 */

/* ---- menu state (menu_load_gfx .. menu_shutdown) ---------------------------------------------------------------- */
static struct {
    int abort, exit, result;                   /* 0xea42c, 0xea428, 0xea778 */
    int lang;                                  /* 0xea764 */
    uint8_t *backbuf, *bgbuf, *textbuf;        /* 0xea400, 0xea404, 0xea408 */
    IgnPalette menu_pal;                       /* 0xea200 / 0xea1fc */
    IgnPalette pal_black, pal_white, pal_work; /* 0xea204, 0xea208, 0xea20c */
    uint8_t *menu_tab;                         /* 0xea218: MENU.TAB */
    uint8_t *pic_bilar, *pic_trk_spr, *pic_ign_logo, *pic_car_sel, *pic_flaggor; /* 0xea750.. */
    IgnMenuSprite spr_logo, spr_sys_a, spr_sys_b, spr_cells[12], spr_car_sel; /* 0xea2b8.., 0xea2dc */
    IgnMenuSprite spr_carname, spr_trk, spr_flag[5]; /* 0xea2a8, 0xea2b0, 0xea2c8 */
    int pages[25];                             /* 0xea47c: menu index per page slot, current language */
    int car_list[11], car_count;               /* 0xea444, 0xea470 */
    double clock, dt;                          /* 0xea21c, 0xea224 */
    int tick_i;                                /* 0xea22c */
    double step;                               /* 0xea230 */
    double intro_t, intro_vid_acc;             /* 0xea198, 0xea1a0 */
    int intro_clip;                            /* 0xea1d0 */
    uint8_t *intro_buf[6];                     /* 0xea1b8 */
    size_t intro_size[6];
    int intro_bufs_live;                       /* 0xea1d4 */
    IgnCdp cdp;                                /* 0xea1d8 */
    int intro_snd_a, intro_snd_b;              /* 0xea274, 0xea278 */
    double idle_t;                             /* 0xea1a8 */
    int redraw;                                /* 0xea474 */
    double fadein_t, quit_t, race_t;           /* 0xea410, 0xea418, 0xea420 */
    int carpic_ms, carpic_dir, carpic_dirty;   /* 0xea768, 0xea76c, 0xea2ac */
    int anim_a, anim_b;                        /* 0xea770, 0xea774 */
    int trkpic_ms, trkpic_dirty;               /* 0xea310, 0xea2b4 */
    int flags_dirty;                           /* 0xea30c */
    int key_msg_ms;                            /* 0xea478 */
    int cd_poll_ms, cd_track_applied, cd_vol_applied; /* 0xea27c, 0xea280, 0xea284 */
    int sfx_vol_applied;                       /* 0xea26c */
    int snd_present, snd_on;                   /* 0xea264, 0xea268 */
    int cd_tracks;                             /* 0x25a934 */
    int page7_autosel;                         /* 0xea434 */
    int p8_frames;                             /* 0xea430 */
    int frames;                                /* 0xea40c */
    IgnFrontendMessages messages;             /* 0xea77c durations, 0xea7cc text */
    /* background animation (menu_load_car 0x1aaf8) */
    uint8_t *psq;                              /* 0xea314 */
    size_t psq_size;                           /* 0xea318 */
    int psq_idx, psq_frame;                    /* 0xea31c, 0xea320 */
    uint8_t *pfm_data;                         /* 0xea324 */
    size_t pfm_size;
    IgnPfm *pfm;
    uint8_t *tex_files[8], *pages_pfm[32];     /* 0xea3b0 (one buffer per file here), 0xea338 */
    uint8_t *shd_raw, *shades[3];              /* 0xea3b4, 0xea32c */
    double bg_acc;                             /* 0xea3b8 */
    IgnModel menucar;                          /* MENUCAR.PLC/MSH/TEX (0xea3d4, 0xea3d8, 0xea3dc) */
    int menucar_ok;
    int car_ay[11];                            /* 0xea3c4[i] +0x14: yaw in tenths of a degree */
    double cam_x, cam_y, cam_z;                /* [0xea3c0] +0, +8, +0x10 */
} M;

#define PAGE(k) M.pages[k]
#define CUR_PAGE (g_gui_pub.cur)
#define TEXT(i) ign_menu_text[M.lang][i]

/* ---- small helpers ---------------------------------------------------------------------------------------------- */

/* "baltazar\data\menu.col" -> "<data>/BALTAZAR/DATA/MENU.COL" */
static const char *data_path(const char *dos) {
    static char buf[4][1200];
    static int n;
    char *out = buf[n++ & 3];
    size_t len = (size_t)snprintf(out, sizeof buf[0], "%s/", g_data_dir);
    for (const char *c = dos; *c && len + 1 < sizeof buf[0]; c++)
        out[len++] = *c == '\\' ? '/' : (char)toupper((unsigned char)*c);
    out[len] = 0;
    return out;
}

/* sub_00060f40: returns 1, 0x7d0 when the file cannot be opened, 0x7da on a short read. */
static int read_file_into(const char *path, void *buf, size_t size, long offset) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0x7d0;
    int rc = 1;
    if (fseek(f, offset, SEEK_SET) || fread(buf, 1, size, f) != size) rc = 0x7da;
    fclose(f);
    return rc;
}

/* <save_dir><name>; the save folder replaces the game folder of the original for everything written. */
static const char *save_path(const char *name) {
    static char buf[2][1200];
    static int n;
    char *out = buf[n++ & 1];
    const char *dir = g_host.save_dir ? g_host.save_dir : "";
    size_t len = strlen(dir);
    snprintf(out, sizeof buf[0], "%s%s%s", dir, len && dir[len - 1] != '/' && dir[len - 1] != '\\' ? "/" : "", name);
    return out;
}

/* IGN_DOS.BTZ of the user folder, else the game folder's (read only), as ign_session_begin. */
static const char *btz_read_path(void) {
    FILE *f = fopen(save_path(IGN_BTZ_NAME), "rb");
    if (f) {
        fclose(f);
        return save_path(IGN_BTZ_NAME);
    }
    return data_path("ign_dos.btz");
}

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    long n = fseek(f, 0, SEEK_END) ? 0 : ftell(f);
    fclose(f);
    return n;
}

static void *xcalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "frontend: out of memory\n");
        exit(1);
    }
    return p;
}

static int timer_ms(void) { return g_host.timer_ms ? g_host.timer_ms() : 0; }
static void set_palette(const uint8_t *pal) {
    if (g_host.set_palette) g_host.set_palette(pal);
}
static void sound_update(void) {
    if (g_host.sound_update) g_host.sound_update();
}
static void present(void) {
    if (M.snd_on) sound_update();
    if (g_host.present) g_host.present(M.backbuf);
}
static int play(int sample, int vol, int rate) {
    if (!M.snd_on || !g_host.sound_play) return -1;
    return g_host.sound_play(sample, vol, rate);
}
static IgnMenuItem *page_item(int k, int i) { return ign_gui_item(PAGE(k), i); }
static IgnMenuPage *page(int k) { return ign_gui_page(PAGE(k)); }
static int sel_of_page(int k) { return page(k)->sel; }
static IgnFont *font(int slot) { return g_font_slot[slot]; }

int ign_frontend_cheat(int i) { return i >= 0 && i < 7 ? (int)(g_cheat[i] & 0xff) : 0; }

int ign_frontend_messages_post(IgnFrontendMessages *messages, const char *text, double duration) {
    int slot = 0;
    while (slot < IGN_FRONTEND_MESSAGES - 1 && messages->time[slot] != 0.0) slot++;
    messages->time[slot] = duration;
    snprintf(messages->text[slot], sizeof messages->text[slot], "%s", text);
    return slot;
}

int ign_frontend_message(const char *text, double duration) {
    return ign_frontend_messages_post(&M.messages, text, duration);
}

const char *ign_frontend_level(int track) {
    static const char *const levels[7] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};
    return track >= 0 && track < 7 ? levels[track] : levels[0];
}

/* ---- keyboard (0x55e04 / 0x55ae4 / 0x55cd0) --------------------------------------------------------------------- */

static char g_typed[33]; /* 0x20af3c: last typed characters */

void ign_frontend_key_down(int sc) {
    if (sc <= 0 || sc > 255) return;
    g_key_latch[sc] = 1;
    if (sc < 0x54 && ign_menu_ascii[sc] != 0xff) {
        memmove(g_typed, g_typed + 1, sizeof g_typed - 2);
        g_typed[sizeof g_typed - 2] = (char)ign_menu_ascii[sc];
    }
}

/* sub_00055cd0: the word was typed; clears the buffer. */
static int kbd_typed(const char *word) {
    if (!strstr(g_typed, word)) return 0;
    memset(g_typed, 0, sizeof g_typed);
    return 1;
}

/* ---- game_init 0x215d0 part ------------------------------------------------------------------------------------- */

/* sub_0002ac28 */
static void iris_table_build(void) {
    g_iris_tab = xcalloc(405 * 800);
    memset(g_iris_tab, 20, 405 * 800);
    for (int y = 0; y < 405; y++) {
        uint8_t *row = g_iris_tab + y * 800;
        for (int k = 0; k < 6; k++)
            for (int x = 400 - y + k; x < 400 + y - k; x++)
                if (x >= 0 && x < 800) row[x] = (uint8_t)(5 - k);
    }
}

int ign_frontend_init(const char *data_dir, const IgnFrontendHost *host) {
    static const char *const fonts[8] = {"baltazar\\data\\red_dark.lft", "baltazar\\data\\red_lite.lft",
                                         "baltazar\\data\\bluedark.lft", "baltazar\\data\\bluelite.lft",
                                         "baltazar\\data\\red_grey.lft", "baltazar\\data\\bluegrey.lft",
                                         "fonts\\mini.lft",              "fonts\\small.lft"};
    ign_frontend_destroy();
    snprintf(g_data_dir, sizeof g_data_dir, "%s", data_dir);
    g_host = *host;
    ign_gfx_set_lut(ign_gfx_identity_lut());
    for (int i = 0; i < 8; i++) {
        if (ign_font_load_lft(&g_font_rec[i], data_path(fonts[i]))) {
            fprintf(stderr, "Could not get a font handle (%s).\n", data_path(fonts[i]));
            goto fail;
        }
        g_font_rec[i].proportional = 1; /* load_system_gfx 0x21860 after each load */
        g_font_rec[i].spacing = 1;
        g_font_slot[i] = &g_font_rec[i];
    }
    /* load_system_gfx 0x21860: N_SYSGFX.PIC, 0x34e-byte header then 256 x 412 pixels. */
    long size = file_size(data_path("n_sysgfx.pic"));
    if (size < 0x34e + 256 * 412) goto fail;
    g_sysgfx = xcalloc((size_t)size);
    if (read_file_into(data_path("n_sysgfx.pic"), g_sysgfx, (size_t)size, 0) != 1 ||
        ign_load_pic(data_path("n_sysg_2.pic"), &g_sysg2))
        goto fail;
    uint8_t col[0x308];
    if (read_file_into(data_path("sys.col"), col, sizeof col, 0) != 1 ||
        ign_parse_col(col, sizeof col, &g_system_palette))
        goto fail;
    for (int i = 0; i < 4; i++) /* rect 0xc3cd8 = {236, 0, 20, 20}, tiles stacked down */
        if (ign_menu_sprite_set(&g_corner[i], g_sysgfx + 0x34e + (20 * i) * 256 + 236, 20, 20, 256, 0, 0))
            goto fail;
    if (ign_menu_sprite_set(&g_loading, g_sysgfx + 0xc1e7, 61, 58, 256, 0, 0) ||
        ign_menubox_init(data_path("menubkg.dat")))
        goto fail;
    iris_table_build();
    memset(g_cheat, 0, sizeof g_cheat);
    return 0;
fail:
    ign_frontend_destroy();
    return -1;
}

void ign_frontend_destroy(void) {
    ign_font_free_all(g_font_rec, 8);
    memset(g_font_slot, 0, sizeof g_font_slot);
    for (int i = 0; i < 4; i++) ign_menu_sprite_free(&g_corner[i]);
    ign_menu_sprite_free(&g_loading);
    ign_menubox_free();
    ign_free_pic(&g_sysg2);
    memset(&g_system_palette, 0, sizeof g_system_palette);
    free(g_sysgfx);
    free(g_iris_tab);
    g_sysgfx = g_iris_tab = NULL;
}

const IgnMenuSprite *ign_frontend_loading_sprite(void) { return g_loading.px ? &g_loading : NULL; }
const IgnPic *ign_frontend_system_picture(void) { return g_sysg2.pixels ? &g_sysg2 : NULL; }
const IgnPalette *ign_frontend_system_palette(void) { return g_sysgfx ? &g_system_palette : NULL; }
const IgnFont *ign_frontend_font(int slot) { return slot >= 0 && slot < 8 ? g_font_slot[slot] : NULL; }

/* ---- settings (0x198bc) ----------------------------------------------------------------------------------------- */

/* sub_000198bc: menu selections -> race configuration. */
static void menu_commit_settings(void) {
    int sel[25];
    for (int k = 0; k < 25; k++) sel[k] = sel_of_page(k);
    for (int k = 0; k < 25; k++) ign_cfg_set(CFG_PAGE_CURSOR + 4 * k, sel[k]);
    if (sel[0] == 1) ign_cfg_set(CFG_TWO_PLAYERS, (sel[2] & ~0xff) | ((sel[2] ^ 1) & 0xff));
    else ign_cfg_set(CFG_TWO_PLAYERS, 0);
    if (sel[3] >= 0 && sel[3] <= 3) ign_cfg_set(CFG_GAME_MODE, sel[3]);
    int two = ign_cfg_get(CFG_TWO_PLAYERS);
    ign_cfg_set(CFG_CAR_SLOT, M.car_list[ign_cfg_get(CFG_P1_CAR)]);
    ign_cfg_set(CFG_CAR_SLOT + 4, two == 1 ? M.car_list[ign_cfg_get(CFG_P2_CAR)] : 1);
    for (int s = 2; s < 8; s++) ign_cfg_set(CFG_CAR_SLOT + 4 * s, s);
    for (int s = 1; s < 8; s++)
        strcpy((char *)g_cfg + CFG_NAMES + 15 * s, ign_menu_driver_tags[ign_cfg_get(CFG_CAR_SLOT + 4 * s)]);
    if (two == 1) strcpy((char *)g_cfg + CFG_NAMES + 15, (char *)g_cfg + CFG_NAMES + 15 * 8);
    ign_cfg_set(CFG_GEARBOX + 4, two == 1 ? ign_cfg_get(CFG_GEARBOX + 32) : 1);
    for (int s = 2; s < 8; s++) ign_cfg_set(CFG_GEARBOX + 4 * s, 1);
    ign_cfg_set(CFG_CONTROL, 0);
    ign_cfg_set(CFG_CONTROL + 4, two != 1);
    for (int s = 2; s < 8; s++) ign_cfg_set(CFG_CONTROL + 4 * s, 1);
    ign_cfg_set(CFG_TRACK, ign_cfg_get(CFG_GAME_MODE) == 0 ? 0 : sel[6]);
    ign_cfg_set(CFG_SCREEN_INV, 3 - ign_cfg_get(CFG_SCREEN_SIZE));
    if (ign_cfg_get(CFG_GAME_MODE) != 2) ign_cfg_set(CFG_RACERS, 6);
    else ign_cfg_set(CFG_RACERS, two ? 2 : 1);
    for (int s = 0; s < 8; s++) ign_cfg_set(CFG_GRID + 4 * s, s);
    ign_cfg_set(CFG_UNK_58F, 0);
}

/* ---- item callbacks 0xbf01c ------------------------------------------------------------------------------------- */

/* sub_00016f18: QUIT? YES. */
static void act_quit(int arg, int ev) {
    (void)arg;
    if (ev != 6) return;
    ign_gui_set_input_lock(800);
    M.quit_t = 0.01;
}

/* sub_00016f44: start the race after the 0.8 s iris. */
static void act_start_race(int arg, int ev) {
    (void)arg;
    if (ev != 6) return;
    ign_gui_set_input_lock(800);
    M.race_t = 0.01;
}

/* sub_00016f70: re-sets the menu video mode (320 x 200 here) and the MENU.COL palette; not referenced by the data. */
static void act_video_mode(int arg, int ev) {
    (void)arg;
    if (ev != 6) return;
    g_key_latch[SC_ESC] = 1;
    set_palette(M.menu_pal.rgb[0]);
}

/* sub_00017014: RESTORE SETTINGS? YES keeps progress, bonus and language. */
static void act_restore(int arg, int ev) {
    (void)arg;
    if (ev != 6) return;
    int32_t u = ign_cfg_get(CFG_UNLOCK), b = ign_cfg_get(CFG_BONUS), l = ign_cfg_get(CFG_LANGUAGE);
    ign_cfg_defaults();
    ign_cfg_set(CFG_BONUS, b);
    ign_cfg_set(CFG_UNLOCK, u);
    ign_cfg_set(CFG_LANGUAGE, l);
    g_key_latch[SC_ESC] = 1;
}

/* sub_00017054: RESET GHOST CAR? YES deletes the seven best-lap ghosts (of the user folder, where the race writes
 * them; the game folder is never written). */
static void act_reset_ghosts(int arg, int ev) {
    static const char *const ghosts[7] = {"GHOSTS\\CAPETHOR.GST", "GHOSTS\\MOOSEJAW.GST", "GHOSTS\\GOLDRUSH.GST",
                                          "GHOSTS\\SNAKEISL.GST", "GHOSTS\\YODELPEA.GST", "GHOSTS\\TOKYOBUL.GST",
                                          "GHOSTS\\LOSTRUIN.GST"};
    (void)arg;
    if ((int8_t)ev != 6) return;
    for (int i = 0; i < 7; i++) {
        char name[32];
        snprintf(name, sizeof name, "GHOSTS/%s", ghosts[i] + 7);
        remove(save_path(name));
    }
    g_key_latch[SC_ESC] = 1;
}

static void (*const g_callbacks[5])(int, int) = {act_quit, act_start_race, act_video_mode, act_restore,
                                                 act_reset_ghosts};

/* ---- 0x15344 iris ----------------------------------------------------------------------------------------------- */

static void menu_iris(double r) {
    if (M.snd_on) sound_update();
    double scale = (double)(W / 320);
    r = r * 4.0 * scale;
    int y = (int)(scale * -100.0);
    double r2 = r * 2.0, yend = scale * 300.0;
    for (; (double)y < yend; y++) {
        double fy = y, cy = (double)(H / 2), top = cy - r;
        int hw;
        if (fy > top && cy + r > fy) {
            double u = ((fy - top) / r2 + -0.5) * 2.0;
            hw = (int)(cos(asin(u)) * r * 3.0 * 0.25 * W / H);
        } else
            hw = 0;
        if (hw < 0) hw = 0;
        if (W / 2 + 4 < hw) continue;
        if (y < 0 || y >= H) continue;
        uint8_t *row = M.backbuf + y * W;
        const uint8_t *sh = g_iris_tab + hw * 800 + 400 - W / 2;
        for (int x = 0; x < W; x++) row[x] = M.menu_tab[(sh[x] << 8) + row[x]];
    }
}

/* ---- 0x1911c best times (with menu_format_time 0x15548) ------------------------------------------------------- */

static const char *menu_format_time(double t) {
    static char buf[32]; /* 0xe9bdc */
    int min = (int)(t * (1.0 / 60.0));
    if (min > 99) min = 99;
    int s = (int)t;
    int cs = (int)((t - (double)s) * 100.0);
    snprintf(buf, sizeof buf, "%.2d:%.2d:%.2d", min, s - 60 * min, cs);
    return buf;
}

static void menu_draw_hiscores(void) {
    static const char *const rank[5] = {"1", "2", "3", "4", "5"};
    ign_gfx_set_target(&g_text_frame);
    ign_gfx_set_lut(ign_gfx_identity_lut());
    ign_menubox_clear();
    for (int y = 77; y != 172; y += 19) {
        ign_menubox_add(39, y / 19, 15, y % 19 - 12, 1);
        ign_menubox_add(81, y / 19, 45, y % 19 - 12, 1);
        ign_menubox_add(153, y / 19, 102, y % 19 - 12, 1);
    }
    ign_menubox_draw(M.textbuf, W, W, H);
    int lap = ign_cfg_get(CFG_HS_LAP) != 0;
    const uint8_t *t = g_cfg + CFG_RECORDS + ign_cfg_get(CFG_HS_TRACK) * 0x78 + (lap ? 0x3c : 0);
    IgnFont *f = font(F_BLUEDARK);
    for (int i = 0, y = 77; i < 5; i++, y += 19) {
        char name[5];
        memcpy(name, t + 4 * i, 4);
        name[4] = 0;
        double sec;
        memcpy(&sec, t + 0x14 + 8 * i, 8);
        f->align = 1;
        ign_gfx_text(f, rank[i], 59, y);
        ign_gfx_text(f, name, 116, y);
        ign_gfx_text(f, menu_format_time(sec), 217, y);
        f->align = 0;
    }
    ign_gfx_set_lut(M.menu_tab);
    ign_gfx_set_target(&g_back_frame);
}

/* ---- 0x1aaf8 / 0x19508 menu cars and background animation ------------------------------------------------------ */

static void menu_load_car(void) {
    static const char *const tex_files[8] = {
        "baltazar\\data\\TEXTURES\\CANADA.TEX", "baltazar\\data\\TEXTURES\\CARS.TEX",
        "baltazar\\data\\TEXTURES\\DIVSPR.TEX", "baltazar\\data\\TEXTURES\\LIGHT.TEX",
        "baltazar\\data\\TEXTURES\\SMOKE.TEX",  "baltazar\\data\\TEXTURES\\DARKSMOK.TEX",
        "baltazar\\data\\TEXTURES\\BOOM.TEX",   "baltazar\\data\\TEXTURES\\EXSMOKE.TEX"};
    M.psq_size = (size_t)file_size(data_path("baltazar\\data\\default2.psq"));
    M.psq = xcalloc(M.psq_size);
    read_file_into(data_path("baltazar\\data\\default2.psq"), M.psq, M.psq_size, 0);
    M.psq_idx = M.psq_frame = 0;
    M.pfm_size = (size_t)file_size(data_path("baltazar\\data\\test2.pfm"));
    M.pfm_data = xcalloc(M.pfm_size + 4);
    read_file_into(data_path("baltazar\\data\\test2.pfm"), M.pfm_data, M.pfm_size, 0);

    /* 0x19790 inlined: up to 1 MB of each file, cut into 64 KB pages. */
    int n = 0;
    for (int f = 0; f < 8; f++) {
        long sz = file_size(data_path(tex_files[f]));
        if (sz <= 0) break; /* the original then leaves the table unterminated */
        if (sz > 0x100000) sz = 0x100000;
        uint8_t *buf = M.tex_files[f] = xcalloc(((size_t)sz + 0xffff) & ~(size_t)0xffff);
        read_file_into(data_path(tex_files[f]), buf, (size_t)sz, 0);
        for (long o = 0; o < sz && n < 31; o += 0x10000) M.pages_pfm[n++] = buf + o;
    }
    M.pages_pfm[n] = NULL;
    /* 0x19840 inlined: CANADA.SHD, then an identity row 0 and solid rows r. */
    M.shd_raw = xcalloc(0x20000);
    M.shades[0] = M.shd_raw;
    read_file_into(data_path("levels\\canada\\canada.SHD"), M.shades[0], 0x10000, 0);
    M.shades[1] = M.shd_raw + 0x10000;
    for (int c = 0; c < 256; c++) M.shades[1][c] = (uint8_t)c;
    for (int r = 1; r < 256; r++)
        for (int c = 0; c < 256; c++) M.shades[1][r * 256 + c] = (uint8_t)r;
    M.shades[2] = NULL;
    M.pfm = ign_pfm_load(M.pfm_data, M.pfm_size, M.pages_pfm, (const uint8_t *const *)M.shades);
    M.bg_acc = 0.0;

    M.menucar_ok = !ign_load_model(data_path("baltazar\\data\\menucar.plc"), data_path("baltazar\\data\\menucar.msh"),
                                   data_path("baltazar\\data\\menucar.tex"), &M.menucar);
    M.cam_x = 1000.0;
    M.cam_y = 1100.0;
    M.cam_z = 800.0;
    for (int i = 0; i < 11; i++) M.car_ay[i] = 0;
}

static void menu_free_car(void) {
    ign_pfm_free(M.pfm);
    M.pfm = NULL;
    for (int i = 0; i < 8; i++) {
        free(M.tex_files[i]);
        M.tex_files[i] = NULL;
    }
    memset(M.pages_pfm, 0, sizeof M.pages_pfm);
    free(M.shd_raw);
    free(M.psq);
    free(M.pfm_data);
    M.shd_raw = M.psq = M.pfm_data = NULL;
    if (M.menucar_ok) ign_free_model(&M.menucar);
    M.menucar_ok = 0;
}

/* sub_00019508: one MENUCAR model (PLC type = car id, else model 0) turned by rot_step tenths of a degree and
 * projected around (cx, cy) through the Lisa 2 renderer. */
static void menu_car_render(int car_id, int rot_step, double zoom, int cx, int cy) {
    static LisaFrame frame;
    if (!M.menucar_ok) return;
    int i;
    for (i = 0; i < 11 && i < M.menucar.nobj; i++)
        if ((int)M.menucar.objs[i].type == car_id) break;
    if (i == 11 || i >= M.menucar.nobj) i = 0;
    double r = 1.0 / zoom;
    M.cam_y = 1000.0 + 240.0 * r;
    M.cam_z = 1000.0 - 460.0 * r;
    M.car_ay[i] += rot_step;
    if (M.car_ay[i] >= 3600) M.car_ay[i] -= 3600;

    LisaScene scene;
    if (lisa_scene_init(&scene, 0xc800, 0xc800, 1)) {
        fprintf(stderr, "frontend: out of memory for menu car\n");
        lisa_scene_free(&scene);
        exit(1);
    }
    scene.tab = M.menu_tab;
    scene.shade = M.shades[0];
    int id = lisa_create(&scene, M.menucar.objs[i].verts - 2, M.menucar.tex, M.menucar.tex_h / 256, 0, 0);
    const int16_t ang[3] = {0, (int16_t)M.car_ay[i], 0};
    lisa_place(&scene, id, 1000, 1000, 1000, ang);
    LisaView view = {.x = M.cam_x, .y = M.cam_y, .z = M.cam_z,
                     .pitch = atan2(M.cam_y - 1000.0, 1000.0 - M.cam_z) * (1800.0 / 3.14159265358979),
                     .zoom = 1, .fx = 200, .fy = 164, .near = 25, .cx = cx, .cy = cy,
                     .layers = LISA_LAYER_ALL};
    lisa_view_clip(&view, 0, 0, W - 1, H - 1);
    lisa_build(&scene, &view, &frame);
    IgnRaster raster = {.fb = M.backbuf, .stride = W};
    ign_raster_clip(&raster, 0, 0, W - 1, H - 1);
    lisa_draw(&frame, &raster);
    lisa_scene_free(&scene);
}

/* sub_0001969c, inlined in 0x170ac: one step of the DEFAULT2.PSQ sequence into the background buffer. */
static void menu_bg_anim_step(void) {
    if (M.snd_on) sound_update();
    if (!M.psq || M.psq_size < 12) return;
    const uint8_t *e = M.psq + 12 * M.psq_idx;
    int first = (int)(e[0] | e[1] << 8 | e[2] << 16 | (uint32_t)e[3] << 24);
    int count = (int)(e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24);
    int frame = first + M.psq_frame;
    if (++M.psq_frame == count) {
        M.psq_frame = 0;
        if (++M.psq_idx == (int)(M.psq_size / 12)) M.psq_idx = 0;
    }
    if (M.pfm) ign_pfm_draw(M.pfm, frame, M.bgbuf, W, H, M.menu_tab);
}

/* ---- 0x170ac page draw callback (with the text layer of 0x155d0) ----------------------------------------------- */

#define BAR(x, y, w, style) ign_menubox_add((x) - 13, (y) / 19, (w), (y) % 19 - 12, (style))

static int slide_index(double t) {
    int i = t >= 1080.0 ? (int)(t + -1080.0) / 168 : 0;
    return i > 15 ? 15 : i; /* the idle clock wraps at 3780, one tick short of slide 16 */
}

static void blend(int x0, int y0, int x1, int y1) {
    ign_blend_rect(M.textbuf, x0, y0, x1, y1, M.backbuf, x0, y0, M.menu_tab, W, W, W, H);
}

static int value_text_width(const IgnMenuElem *e) { return e->text ? ign_gfx_text_width(font(e->font), e->text) : 0; }

/* sub_000155d0 (inlined at 0x1789a..0x17ffe): highlight boxes and texts of the current page, or the credits. */
static void menu_draw_text_layer(void) {
    if (M.snd_on) sound_update();
    memset(M.textbuf, 0, SCREEN_BYTES);
    if (M.intro_t != 0.0) return;
    ign_gfx_set_target(&g_text_frame);
    ign_gfx_set_lut(ign_gfx_identity_lut());
    double t = M.idle_t;
    const int16_t *slide = ign_menu_slides[slide_index(t)];
    const char *title = NULL, *name[4] = {0};
    if (t < 1080.0) {
        ign_menubox_clear();
        const IgnMenuPage *pg = ign_gui_page(CUR_PAGE);
        for (int i = 0; i < pg->nitems; i++) {
            const IgnMenuItem *it = ign_gui_item(CUR_PAGE, i);
            if (it->elems < 0) continue;
            const IgnMenuElem *el = &ign_menu_elems[it->elems];
            if (el[0].text) BAR(el[0].tx + W / 2, el[0].ty + H / 2, ign_gfx_text_width(font(el[0].font), el[0].text), 0);
            if (it->value >= 0) {
                const IgnMenuElem *v = &el[2 + 2 * (ign_cfg_get(it->value) - it->min)];
                BAR(v->tx + W / 2, v->ty + H / 2, value_text_width(v), 1);
            } else if (it->edit >= 0) {
                const IgnMenuEdit *ed = &ign_menu_edits[it->edit];
                const char *s = ed->type == 3 ? ign_menu_key_names[g_cfg[ed->dest]] : (const char *)g_cfg + ed->dest;
                IgnFont *f = font(ed->font_sel);
                int w = g_gui_pub.editmode == 1 ? ign_gfx_text_width(f, "WWW|") : ign_gfx_text_width(f, s);
                BAR(ed->x + W / 2, ed->y + H / 2, w, 1);
            }
        }
    } else {
        ign_menubox_clear();
        if (slide[0] >= 0) {
            title = TEXT(slide[0]);
            ign_menubox_add(11, 3, ign_gfx_text_width(font(F_RED_LITE), title), 5, 0);
        }
        for (int k = 3, y = 123; k >= 1; k--, y += 19)
            if (slide[k] >= 0) {
                name[k] = ign_menu_credit_names[slide[k]];
                int w = ign_gfx_text_width(font(F_BLUELITE), name[k]);
                BAR(296 - w, y, w, 1);
            }
        ign_menubox_draw(M.textbuf, W, W, H);
    }
    if (M.key_msg_ms == 0 && t < 1080.0) {
        ign_menubox_draw(M.textbuf, W, W, H);
        ign_gui_draw_items();
    } else if (t >= 1080.0) {
        if (slide[0] >= 0) ign_gfx_text(font(F_RED_LITE), title, 24, 74);
        for (int k = 3, y = 123; k >= 1; k--, y += 19)
            if (slide[k] >= 0)
                ign_gfx_text(font(F_BLUELITE), name[k], 296 - ign_gfx_text_width(font(F_BLUELITE), name[k]), y);
        const char *p = TEXT(1); /* PRESS ANY KEY */
        ign_gfx_text(font(F_MINI), p, 160 - ign_gfx_text_width(font(F_MINI), p) / 2, 191);
    }
    if (t < 1080.0) {
        int pg = CUR_PAGE;
        if (M.key_msg_ms) { /* KEY ALREADY ASSIGNED */
            const char *s = TEXT(0);
            int w = ign_gfx_text_width(font(F_RED_LITE), s), x = 160 - w / 2;
            ign_menubox_clear();
            ign_menubox_add(x - 13, 4, w, 2, 0);
            ign_menubox_draw(M.textbuf, W, W, H);
            ign_gfx_text(font(F_RED_LITE), s, x, 90);
        }
        int q = pg == PAGE(22) ? 47 : pg == PAGE(23) ? 48 : pg == PAGE(24) ? 49 : -1;
        if (q >= 0) { /* QUIT? / RESTORE SETTINGS? / RESET GHOST CAR? */
            const char *s = TEXT(q);
            int w = ign_gfx_text_width(font(F_BLUELITE), s), x = 160 - w / 2;
            ign_menubox_clear();
            ign_menubox_add(x - 13, 3, w, 1, 1);
            ign_menubox_draw(M.textbuf, W, W, H);
            ign_gfx_text(font(F_BLUELITE), s, x, 70);
        }
        int lines = pg == PAGE(17) || pg == PAGE(18) ? 4 : pg == PAGE(19) || pg == PAGE(20) ? 5 : 0;
        if (lines) { /* joystick (25..28) or wheel and pedals (35..39) calibration */
            int base = lines == 4 ? 25 : 35;
            static const char *const extra = "E PREMI IL PULSANTE A.";
            int italian = lines == 5 && ign_cfg_get(CFG_LANGUAGE) == 2;
            IgnFont *f = font(F_BLUELITE);
            ign_menubox_clear();
            for (int i = 0, y = 70; i < lines; i++, y += 19) {
                int w = ign_gfx_text_width(f, TEXT(base + i));
                BAR(160 - w / 2, y, w, 1);
            }
            if (italian) {
                int w = ign_gfx_text_width(f, extra);
                BAR(160 - w / 2, 165, w, 1);
            }
            ign_menubox_draw(M.textbuf, W, W, H);
            for (int i = 0, y = 70; i < lines; i++, y += 19)
                ign_gfx_text(f, TEXT(base + i), 160 - ign_gfx_text_width(f, TEXT(base + i)) / 2, y);
            if (italian) ign_gfx_text(f, extra, 160 - ign_gfx_text_width(f, extra) / 2, 165);
        }
    }
    ign_gfx_set_lut(M.menu_tab);
    ign_gfx_set_target(&g_back_frame);
}

static int wrap_car(int i) {
    if (i < 0) i += M.car_count;
    else if (i >= M.car_count) i -= M.car_count;
    return M.car_list[i];
}

static void put_sprite(const IgnMenuSprite *s, int x, int y) { ign_menu_sprite_put(s, x << 8, y << 8, NULL); }

static void menu_page_draw(void) {
    int pg = CUR_PAGE, entered = g_gui_pub.page_entered;
    double t = M.idle_t, intro = M.intro_t;

    /* 1. keymap tweaks */
    ign_menu_keymap[SC_BACKSPACE] = g_gui_pub.editmode ? 0 : -6;
    char *name1 = (char *)g_cfg + CFG_NAMES, *name2 = (char *)g_cfg + CFG_NAMES + 15;
    if (!name1[0]) name1[0] = ' ', name1[1] = 0;
    if (!name2[0]) name2[0] = ' ', name2[1] = 0;
    const uint8_t *p2 = g_cfg + CFG_P2_KEYS;
    if (pg == PAGE(5)) {
        static const int8_t ev[4] = {-3, 3, -1, 1};
        for (int i = 0; i < 4; i++)
            if (!ign_menu_keymap[p2[i]]) ign_menu_keymap[p2[i]] = ev[i];
    } else
        for (int i = 0; i < 4; i++) ign_menu_keymap[p2[i]] = ign_menu_keymap_pristine[p2[i]];
    if (pg != PAGE(22) && pg != PAGE(23) && pg != PAGE(24)) page(22)->sel = page(23)->sel = page(24)->sel = 1;

    /* 2. CD TRACK range (joysticks are not supported by the port: no calibration starts) */
    if (M.cd_tracks > 0) {
        IgnMenuItem *it = page_item(10, 2);
        it->max = M.cd_tracks > 25 ? 25 : M.cd_tracks;
        if (ign_cfg_get(CFG_CD_TRACK) > it->max) g_key_latch[SC_RIGHT] = 1;
    }

    /* 3. sprite grabs */
    if ((entered || M.carpic_dirty) && (pg == PAGE(4) || pg == PAGE(5))) {
        const IgnMenuItem *it = ign_gui_item(pg, ign_gui_page(pg)->sel);
        int car = M.car_list[ign_cfg_get(it->value)];
        M.carpic_dirty = 0;
        ign_menu_sprite_set(&M.spr_carname, M.pic_bilar + car * 0x2c52, 186, 61, 186, 0x5d00, 0x1e80);
    }
    if ((entered && pg == PAGE(6)) || M.trkpic_dirty) {
        page_item(6, 5)->enabled = 1;
        page_item(6, 6)->enabled = 1;
        int ndis = 2 - page_item(15, 0)->max;
        for (int i = 0; i < ndis; i++) page_item(6, 6 - i)->enabled = 0;
        M.trkpic_dirty = 0;
        ign_menu_sprite_set(&M.spr_trk, M.pic_trk_spr + ign_menu_track_thumbs[page(6)->sel], 115, 99, 115, 0x3980, 0x3980);
    }
    if ((entered && pg == PAGE(21)) || M.flags_dirty) {
        M.flags_dirty = 0;
        int cursor = ign_cfg_get(CFG_LANGUAGE);
        for (int i = 0; i < 5; i++)
            ign_menu_sprite_set(&M.spr_flag[i], M.pic_flaggor + i * 0xca1 + (i == cursor ? 0x4bc6 : 0), 61, 53, 61, 0, 0);
    }

    /* 4. text layer */
    if (M.redraw) {
        M.redraw = 0;
        menu_draw_text_layer();
        if (pg == PAGE(16) && t < 1080.0) menu_draw_hiscores();
    }

    /* 5. background */
    if (intro == 0.0 || intro >= 1278.0) {
        if (pg == PAGE(21))
            memset(M.backbuf, 0, SCREEN_BYTES);
        else {
            M.bg_acc += M.dt;
            if (M.bg_acc >= 2.11764705882353) {
                while (M.bg_acc >= 2.11764705882353) M.bg_acc += -2.11764705882353;
                menu_bg_anim_step();
            }
            memcpy(M.backbuf, M.bgbuf, SCREEN_BYTES);
        }
    }
    if (pg != PAGE(21) && pg != PAGE(13) && pg != PAGE(14) && pg != PAGE(16) && pg != PAGE(8) &&
        (intro == 0.0 || intro >= 1306.8))
        put_sprite(&M.spr_logo, 75, 2);
    int s0 = ign_menu_slides[slide_index(t)][0];
    if (s0 == -2) put_sprite(&M.spr_sys_a, 160, 125);
    else if (s0 == -3) put_sprite(&M.spr_sys_b, 160, 125);
    if (pg == PAGE(16) && entered) menu_draw_hiscores();
    if (M.snd_on) sound_update();

    /* 6. translucent text layer */
    if (t > 1080.0)
        blend(10, 68, 319, timer_ms() % 809 < 500 ? 199 : 191);
    else if (pg == PAGE(16))
        blend(8, 0, 319, 180);
    else if (pg == PAGE(8))
        blend(10, 0, 319, 199);
    else if (pg == PAGE(22) || pg == PAGE(23) || pg == PAGE(24))
        blend(0, 64, 319, 159);
    else if (pg == PAGE(17) || pg == PAGE(18))
        blend(0, 65, 320, 168);
    else if (pg == PAGE(19) || pg == PAGE(20))
        blend(0, 65, 320, 188);
    else {
        const IgnMenuItem *it0 = ign_gui_item(pg, 0);
        if (ign_gui_page(pg)->nitems > 0 && it0->elems >= 0 && (intro == 0.0 || intro >= 1306.8)) {
            int y0 = ign_menu_elems[it0->elems].y + H / 2 - 4;
            blend(0, y0, 319, y0 + 5 + 19 * ign_gui_page(pg)->nitems);
        }
    }

    /* 7. page sprites and 3D cars */
    if (t < 1080.0) {
        if (pg == PAGE(21)) {
            for (int i = 0; i < 5; i++) {
                if (i == 1) continue; /* the German flag is never drawn */
                int col = i > 1 ? i - 1 : i;
                put_sprite(&M.spr_flag[i], col * 69 + 26, 74);
            }
            const char *s = "SELECT YOUR LANGUAGE";
            ign_gfx_text(font(F_MINI), s, 160 - ign_gfx_text_width(font(F_MINI), s) / 2, 191);
        }
        if (pg == PAGE(4) || pg == PAGE(5)) {
            double f = 1.0 - M.carpic_ms * 2.5 * 0.0025;
            if (f < 0) f = -f;
            if (f > 1.0) f = 1.0;
            if (M.carpic_ms == 0) put_sprite(&M.spr_carname, 160, 90);
            else {
                float sa[2] = {(float)f, 0.0f};
                ign_menu_sprite_put(&M.spr_carname, 160 << 8, 90 << 8, sa);
            }
            put_sprite(&M.spr_car_sel, 32, 122);
            if (page(0)->sel == 1 && page(2)->sel == 0) {
                const char *s = TEXT(pg == PAGE(4) ? 5 : 6);
                ign_gfx_text(font(F_MINI), s, 160 - ign_gfx_text_width(font(F_MINI), s) / 2, 192);
            }
        }
        if (pg == PAGE(6)) {
            double f = 1.0 - M.trkpic_ms * 2.0 * 0.0025;
            if (f < 0) f = -f;
            if (f > 1.0) f = 1.0;
            if (M.trkpic_ms == 0) put_sprite(&M.spr_trk, 251, 136);
            else {
                float sa[2] = {(float)f, 0.0f};
                ign_menu_sprite_put(&M.spr_trk, 251 << 8, 136 << 8, sa);
            }
            if (entered) {
                if (page(0)->sel == 1 && page(2)->sel == 0) M.anim_b = 1;
                else M.anim_a = 1;
            }
        }
        if (pg == PAGE(4) || pg == PAGE(5)) {
            int pl = pg == PAGE(4) ? 0 : 1;
            if (pl == 1 && entered) M.anim_b = 1;
            int sel = ign_cfg_get(pl ? CFG_P2_CAR : CFG_P1_CAR);
            int rs = (int)(M.dt * 1000.0 * (1.0 / 36) * 3600.0 * (1.0 / 3400));
            int rsc = (int)(M.dt * 1000.0 * (1.0 / 36) * 3600.0 * (1.0 / 1900));
            if (M.carpic_ms > 0) {
                double s = sin(M.carpic_ms * 1.5707963267949 * 0.0025);
                if (M.carpic_dir < 0) {
                    menu_car_render(wrap_car(sel - 1), rs, s + 0.01, 78, 158);
                    menu_car_render(wrap_car(sel + 2), rs, 1.01 - s, 240, 158);
                    menu_car_render(wrap_car(sel), rs, s * 0.6 + 1.01, (int)(s * 81 + 78), 158);
                    menu_car_render(wrap_car(sel + 1), rsc, 1.61 - s * 0.6, (int)(s * 81 + 159), 158);
                } else {
                    menu_car_render(wrap_car(sel + 1), rs, s + 0.01, 240, 158);
                    menu_car_render(wrap_car(sel), rs, s * 0.6 + 1.01, (int)(240 - s * 81), 158);
                    menu_car_render(wrap_car(sel - 2), rs, 1.01 - s, 78, 158);
                    menu_car_render(wrap_car(sel - 1), rsc, 1.61 - s * 0.6, (int)(159 - s * 81), 158);
                }
            } else {
                menu_car_render(wrap_car(sel - 1), rs, 1.01, 78, 158);
                menu_car_render(wrap_car(sel + 1), rs, 1.01, 240, 158);
                menu_car_render(wrap_car(sel), rsc, 1.61, 159, 158);
            }
        }
    }

    /* 8. tail */
    if (pg == PAGE(8)) {
        M.p8_frames++;
        menu_commit_settings();
    } else
        M.p8_frames = 0;
    if (intro == 0.0 || intro >= 1306.8) {
        put_sprite(&g_corner[0], 0, 0);
        put_sprite(&g_corner[1], W - 20, 0);
        put_sprite(&g_corner[2], 0, H - 20);
        put_sprite(&g_corner[3], W - 20, H - 20);
    }
}

/* ---- 0x160c4 page key callback --------------------------------------------------------------------------------- */

static void play_sel(int sample, int vol) { play(sample, vol, 22000); }

static int menu_page_key(int sc, int ev) {
    static const uint8_t locked_cars[4] = {10, 9, 6, 1}; /* 0xbf970 */
    int pg = CUR_PAGE, cur = g_gui_pub.sel;
    for (int i = 0; i < 7; i++)
        if (kbd_typed(ign_menu_cheat_words[i])) {
            g_cheat[i] ^= 1;
            play_sel(SND_SEL, 65000);
        }
    if (g_cheat[1]) { /* SURMULE: MIRROR difficulty */
        page_item(15, 0)->max = 3;
        M.redraw = 1;
        M.trkpic_dirty = 1;
    }
    if (g_cheat[2]) { /* SLASKTRATT: every car */
        for (int c = 0; c < 11; c++) M.car_list[c] = c;
        page_item(4, 0)->max = 10;
        page_item(5, 0)->max = 10;
        M.car_count = 11;
        M.redraw = 1;
    }
    if (M.idle_t != 0.0) {
        if (M.idle_t >= 1080.0) {
            M.idle_t = 0.0;
            M.redraw = 1;
            return IGN_GUI_CONSUMED;
        }
        M.idle_t = 0.0;
    }
    if (0.0 < M.intro_t && M.intro_t < 1278.0) { /* skip the intro */
        M.intro_t = 1278.0;
        set_palette(M.pal_white.rgb[0]);
        if (g_host.sound_stop_voice) {
            g_host.sound_stop_voice(M.intro_snd_a);
            g_host.sound_stop_voice(M.intro_snd_b);
        }
        if (g_host.sound_silence) g_host.sound_silence();
        return IGN_GUI_CONSUMED;
    }
    if (ev != 0 || g_gui_pub.editmode != 0) M.redraw = 1;
    if (pg == PAGE(15) && cur == 0 && page_item(15, 0)->max == 0 && abs(ev) == 3) {
        play_sel(SND_NO, 65000);
        return IGN_GUI_CONSUMED;
    }
    if (pg != PAGE(21)) {
        int a = abs(ev);
        if (a >= 1 && a <= 5) play_sel(SND_FLIP, 50000);
        else if (a == 6) play_sel(SND_SEL, 50000);
    }
    /* CONTROLS: only one player can use the analog port (values 1 JOYSTICK and 2 PEDALS). */
    int c1 = ign_cfg_get(CFG_P1_CONTROLS), c2 = ign_cfg_get(CFG_P2_CONTROLS);
    if (pg == PAGE(11) && cur == 2 && (c2 == 1 || c2 == 2)) {
        if (c1 == 0 && ev == 3) ign_cfg_set(CFG_P1_CONTROLS, 2);
        else if (c1 == 3 && ev == -3) ign_cfg_set(CFG_P1_CONTROLS, 1);
    }
    if (pg == PAGE(12) && cur == 2 && (c1 == 1 || c1 == 2)) {
        if (c2 == 0 && ev == 3) ign_cfg_set(CFG_P2_CONTROLS, 2);
        else if (c2 == 3 && ev == -3) ign_cfg_set(CFG_P2_CONTROLS, 1);
    }
    if (pg == PAGE(21)) {
        if (ev != 0) M.flags_dirty = 1;
        int lc = ign_cfg_get(CFG_LANGUAGE);
        if ((lc == 0 && ev == 3) || (lc == 2 && ev == -3)) ign_cfg_set(CFG_LANGUAGE, 1);
        if (ev == 6 && M.intro_t == 0.0) {
            M.lang = ign_cfg_get(CFG_LANGUAGE);
            g_gui.menu_by_id = ign_menu_by_id[M.lang];
            for (int i = 0; i < 25; i++) M.pages[i] = ign_menu_lang_pages[M.lang][i];
            int nlock = ign_cfg_get(CFG_BONUS) ? 3 - ign_cfg_get(CFG_UNLOCK) : 4 - ign_cfg_get(CFG_UNLOCK);
            int n = 0;
            for (int car = 0; car < 11; car++) {
                int k = 0;
                if (nlock > 0)
                    while (k < nlock && locked_cars[k] != car) k++;
                if (k == nlock || nlock <= 0) M.car_list[n++] = car;
            }
            M.car_count = n;
            page_item(4, 0)->max = n - 1;
            page_item(5, 0)->max = n - 1;
            if (ign_cfg_get(CFG_P1_CAR) > page_item(4, 0)->max) ign_cfg_set(CFG_P1_CAR, page_item(4, 0)->max);
            if (ign_cfg_get(CFG_P2_CAR) > page_item(5, 0)->max) ign_cfg_set(CFG_P2_CAR, page_item(5, 0)->max);
            if (g_first_menu) { /* first entry since start-up: the intro */
                M.intro_t = 0.01;
                M.intro_vid_acc = 0.0;
                M.intro_clip = 0;
                memset(M.backbuf, 0, SCREEN_BYTES);
                const char *s = TEXT(46); /* LOADING */
                ign_gfx_text(font(F_SMALL), s, 160 - ign_gfx_text_width(font(F_SMALL), s) / 2, 94);
                present();
                for (int k = 0; k < 6; k++) {
                    const char *path = data_path(ign_menu_intro_clips[k]);
                    M.intro_size[k] = (size_t)file_size(path);
                    M.intro_buf[k] = xcalloc(M.intro_size[k]);
                    read_file_into(path, M.intro_buf[k], M.intro_size[k], 0);
                }
                memset(M.backbuf, 0, SCREEN_BYTES);
                present();
                ign_cdp_open(&M.cdp, M.intro_buf[0], M.intro_size[0], M.backbuf);
            } else {
                M.fadein_t = 0.01;
                if (g_host.sound_free) g_host.sound_free(SND_INTRO);
                menu_load_car();
            }
        }
    }
    if (pg == PAGE(0) && ev == -6) return IGN_GUI_CONSUMED;
    if (pg == PAGE(0) && ev == 6) page_item(3, 0)->enabled = 1;
    if (pg == PAGE(2) && ev == 6) {
        page_item(3, 0)->enabled = (int16_t)(page(2)->sel ^ 1);
        if (page_item(3, 0)->enabled == 0 && page(3)->sel == 0) page(3)->sel = 1;
    }
    if (pg == PAGE(6) && (abs(ev) == 1 || abs(ev) == 2)) {
        if (M.trkpic_ms == 0) M.trkpic_ms = 1;
        else if ((double)M.trkpic_ms > 200.0) M.trkpic_dirty = 1;
    }
    for (int p = 0; p < 2; p++) { /* KEY ALREADY ASSIGNED */
        int mine = PAGE(13 + p), other = PAGE(14 - p);
        if (pg != mine || g_gui_pub.editmode != 3) continue;
        for (int i = 0; i < ign_gui_page(mine)->nitems; i++) {
            const IgnMenuItem *a = ign_gui_item(mine, i), *b = ign_gui_item(other, i);
            if ((i != ign_gui_page(pg)->sel && g_cfg[ign_menu_edits[a->edit].dest] == sc) ||
                g_cfg[ign_menu_edits[b->edit].dest] == sc) {
                M.key_msg_ms = 1;
                ign_gui_set_input_lock(1100);
                play_sel(SND_NO, 65000);
                return IGN_GUI_CONSUMED;
            }
        }
    }
    if ((pg == PAGE(4) || pg == PAGE(5)) && (abs(ev) == 3 || abs(ev) == 4)) {
        M.carpic_ms = 1;
        ign_gui_set_input_lock(400);
        M.carpic_dir = ev < 0 ? -1 : 1;
    }
    return 0;
}

/* ---- 0x11504 menu_load_gfx ------------------------------------------------------------------------------------- */

static uint8_t *load_pic_pixels(const char *name, size_t size) {
    uint8_t *p = xcalloc(size);
    read_file_into(data_path(name), p, size, 0x34e);
    return p;
}

int ign_frontend_load(void) {
    memset(&M, 0, sizeof M);
    M.abort = 0;
    read_file_into(data_path("baltazar\\data\\menu.col"), M.menu_pal.rgb, 0x300, 8);
    ign_frame_init(&g_back_frame, W, H);
    ign_frame_init(&g_text_frame, W, H);
    memset(g_back_frame.color, 0, sizeof g_back_frame.color);
    memset(g_text_frame.color, 0, sizeof g_text_frame.color);
    M.backbuf = g_back_frame.color;
    set_palette(M.menu_pal.rgb[0]);
    if (g_first_menu) {
        int32_t ver = 0;
        uint8_t lang[4] = {0};
        read_file_into(btz_read_path(), &ver, 4, 0);
        if (ver == IGN_CFG_VERSION) {
            read_file_into(btz_read_path(), lang, 4, 0x593);
            M.lang = (int)(lang[0] | lang[1] << 8 | lang[2] << 16 | (uint32_t)lang[3] << 24);
        } else
            M.lang = 0;
        if (M.lang < 0 || M.lang > 5) M.lang = 0;
        for (int f = 0; f < 8; f++) g_font_slot[f]->proportional = 1, g_font_slot[f]->spacing = 0;
    }
    M.menu_tab = xcalloc(0x10000);
    read_file_into(data_path("baltazar\\data\\menu.tab"), M.menu_tab, 0x10000, 0);
    ign_gfx_set_lut(M.menu_tab);
    ign_gfx_set_target(&g_back_frame);
    memset(M.backbuf, 0, SCREEN_BYTES);
    {
        const char *s = TEXT(46); /* LOADING */
        ign_gfx_text(font(F_SMALL), s, 160 - ign_gfx_text_width(font(F_SMALL), s) / 2, 94);
    }
    present();

    /* sound_init {5, 100, 2}; the "NO SOUNDCARD FOUND" wait for Enter is not reproduced (SDL has no such state). */
    int r = g_host.sound_init ? g_host.sound_init() : 0;
    if (r == 1) {
        M.snd_present = M.snd_on = 1;
        M.sfx_vol_applied = 0xffff;
        if (g_host.sound_volume) g_host.sound_volume(0xffff);
    } else
        M.snd_present = M.snd_on = 0;
    (void)g_nosound_msg_shown;

    /* menu_init 0x14000 (inlined) */
    M.exit = 0;
    M.frames = 0;
    M.fadein_t = M.quit_t = M.race_t = 0.0;
    M.page7_autosel = 0;
    M.result = IGN_FRONTEND_STAY;
    memset(&M.pal_black, 0, sizeof M.pal_black);
    memset(&M.pal_white, 0xff, sizeof M.pal_white);
    M.bgbuf = xcalloc(MENU_BUF);
    M.textbuf = g_text_frame.color;
    M.pic_bilar = load_pic_pixels("baltazar\\data\\bilar.pic", 0x1e786);
    M.pic_trk_spr = load_pic_pixels("baltazar\\data\\trk_spr.pic", 0x1374f);
    M.pic_ign_logo = load_pic_pixels("baltazar\\data\\ign_logo.pic", 0x2685);
    M.pic_car_sel = load_pic_pixels("baltazar\\data\\car_sel.pic", 0x49b6);
    M.pic_flaggor = load_pic_pixels("baltazar\\data\\flaggor.pic", 0x978c);

    /* menu_setup 0x19ac8 (inlined); the track-name labels are static data (gen_menu_data.py). */
    for (int k = 0; k < 25; k++) M.pages[k] = ign_menu_lang_pages[0][k];
    ign_cfg_defaults();
    for (int k = 0; k < 25; k++) ign_cfg_set(CFG_PAGE_CURSOR + 4 * k, page(k)->sel);
    ign_cfg_default_records();
    ign_cfg_load(btz_read_path());
    ign_cfg_set(CFG_SCREEN_SIZE, 3 - ign_cfg_get(CFG_SCREEN_INV));
    if (ign_cfg_get(CFG_DIFFICULTY) > ign_cfg_get(CFG_UNLOCK)) ign_cfg_set(CFG_DIFFICULTY, ign_cfg_get(CFG_UNLOCK));
    for (int l = 0; l < 6; l++) {
        for (int k = 0; k < 25; k++) ign_gui_page(ign_menu_lang_pages[l][k])->sel = ign_cfg_get(CFG_PAGE_CURSOR + 4 * k);
        ign_gui_page(ign_menu_lang_pages[l][0])->sel = 0;
        ign_gui_item(ign_menu_lang_pages[l][15], 0)->max = ign_cfg_get(CFG_UNLOCK);
    }
    static IgnFont *fonts[8];
    for (int i = 0; i < 8; i++) fonts[i] = g_font_slot[i];
    memset(&g_gui, 0, sizeof g_gui);
    g_gui.w = W;
    g_gui.h = H;
    g_gui.root = ign_menu_root;
    g_gui.callbacks = g_callbacks;
    g_gui.key_latch = g_key_latch;
    g_gui.bind_allowed = ign_menu_bind_allowed;
    g_gui.menu_by_id = ign_menu_by_id[0];
    g_gui.sound_pump = M.snd_on;
    g_gui.fonts = fonts;
    g_gui.remember_sel = 1;
    g_gui.key = menu_page_key;
    g_gui.draw = menu_page_draw;
    g_gui.sound_update = sound_update;
    g_gui.timer_ms = timer_ms;
    memcpy(ign_menu_keymap, ign_menu_keymap_pristine, sizeof ign_menu_keymap);
    ign_gui_start();

    M.intro_t = M.intro_vid_acc = M.idle_t = 0.0;
    M.intro_clip = 0;
    M.intro_bufs_live = 1;
    M.cd_poll_ms = 0;
    M.cd_track_applied = M.cd_vol_applied = -99;
    M.intro_snd_a = M.intro_snd_b = -1;
    M.key_msg_ms = 0;
    uint8_t *sys = g_sysgfx + 0x34e;
    ign_menu_sprite_set(&M.spr_logo, M.pic_ign_logo, 173, 57, 173, 0, 0);
    ign_menu_sprite_set(&M.spr_sys_a, sys + 248 * 256, 253, 58, 256, 0x7e80, 0x1d00);
    ign_menu_sprite_set(&M.spr_sys_b, sys + 306 * 256, 126, 106, 256, 0x3f00, 0x3500);
    for (int row = 0; row < 3; row++)
        for (int c = 0; c < 4; c++)
            ign_menu_sprite_set(&M.spr_cells[row * 4 + c], sys + row * 44 * 256 + c * 51, 51, 44, 256, 0x1900, 0x1600);
    ign_menu_sprite_set(&M.spr_car_sel, M.pic_car_sel, 255, 74, 255, 0, 0);
    M.redraw = 1;
    M.anim_a = M.anim_b = -1;
    M.cd_tracks = g_host.cd_tracks ? g_host.cd_tracks() : 0;
    M.clock = timer_ms() * 0.036;
    return 1;
}

/* ---- 0x13a5c menu_shutdown -------------------------------------------------------------------------------------- */

int ign_frontend_shutdown(void) {
    if (g_host.sound_silence) g_host.sound_silence();
    if (g_host.cd_stop) g_host.cd_stop();
    ign_gui_free();
    if (g_host.sound_shutdown) g_host.sound_shutdown();
    M.snd_on = 0;
    menu_free_car();
    IgnMenuSprite *spr[] = {&M.spr_carname, &M.spr_trk, &M.spr_logo, &M.spr_sys_a, &M.spr_sys_b, &M.spr_car_sel};
    for (size_t i = 0; i < sizeof spr / sizeof spr[0]; i++) ign_menu_sprite_free(spr[i]);
    for (int i = 0; i < 5; i++) ign_menu_sprite_free(&M.spr_flag[i]);
    for (int i = 0; i < 12; i++) ign_menu_sprite_free(&M.spr_cells[i]);
    for (int k = 0; k < 6; k++) free(M.intro_buf[k]);
    free(M.bgbuf);
    free(M.pic_bilar);
    free(M.pic_trk_spr);
    free(M.pic_ign_logo);
    free(M.pic_car_sel);
    free(M.pic_flaggor);
    free(M.menu_tab);
    ign_gfx_set_lut(ign_gfx_identity_lut());
    ign_menubox_clear();
    memset(&M, 0, sizeof M);
    return 1;
}

/* ---- 0x12bb8 menu_frame ----------------------------------------------------------------------------------------- */

static int pick_cd_track(int sel) {
    if (sel == -1) return (int)ign_random15(ign_random_state()) * M.cd_tracks / 32768 + 1;
    return sel == 0 ? 7 : sel;
}

static void cd_restart_music(void) {
    if (g_host.sound_silence) g_host.sound_silence();
    M.cd_tracks = g_host.cd_tracks ? g_host.cd_tracks() : 0;
    if (M.cd_tracks > 0 && g_host.cd_play) g_host.cd_play(pick_cd_track(ign_cfg_get(CFG_CD_TRACK)));
}

static void save_settings(void) {
    menu_commit_settings();
    ign_cfg_save(save_path(IGN_BTZ_NAME));
}

int ign_frontend_frame(void) {
    if (M.abort) return M.result;
    double now = timer_ms() * 0.036;
    double dt = now - M.clock;
    M.clock = now;
    if (dt == 0.0) dt = 0.01;
    else if (dt > 10.8) dt = 10.8;
    M.dt = dt;
    int steps = (int)M.dt;
    for (M.tick_i = 0; M.tick_i <= steps; M.tick_i++) {
        M.step = M.tick_i < steps ? 1.0 : M.dt - M.tick_i;
        /* (joysticks: not supported by the port) */
        if (M.intro_t != 0.0) {
            M.intro_t += M.step;
            if (M.intro_t > 28.8 && M.intro_t < 1278.0) {
                M.intro_vid_acc += M.step;
                if (M.intro_vid_acc >= 2.393103448275862) {
                    while (M.intro_vid_acc >= 2.393103448275862) M.intro_vid_acc += -2.393103448275862;
                    if (M.snd_on) sound_update();
                    if (ign_cdp_next(&M.cdp) == 0) {
                        if (++M.intro_clip == 6) M.intro_t = 1278.0;
                        else ign_cdp_open(&M.cdp, M.intro_buf[M.intro_clip], M.intro_size[M.intro_clip], M.backbuf);
                    }
                    if (M.cdp.cur == 2) set_palette(ign_cdp_palette(&M.cdp));
                }
            }
            if (M.intro_t > 1312.56) ign_gui_set_input_lock(0);
            if (M.intro_t >= 1335.6) {
                M.intro_t = 0.0;
                set_palette(M.menu_pal.rgb[0]);
                if (g_host.sound_stop_voice) {
                    g_host.sound_stop_voice(M.intro_snd_a);
                    g_host.sound_stop_voice(M.intro_snd_b);
                }
                if (g_host.sound_silence) g_host.sound_silence();
                if (g_host.sound_free) g_host.sound_free(SND_INTRO);
                M.idle_t = 1079.9;
            }
        }
        if (M.intro_t == 0.0 && CUR_PAGE != PAGE(21) && CUR_PAGE != PAGE(8)) {
            M.idle_t += M.step;
            if (M.idle_t >= 3780.0) M.idle_t = 1080.0;
            if (M.idle_t >= 1080.0) {
                int n = (int)(M.idle_t + -1080.0);
                if (n % 168 == 0 && n / 168 < 16) M.redraw = 1;
            }
        }
        if (M.fadein_t != 0.0) {
            M.fadein_t += M.step;
            if (M.fadein_t >= 28.8) M.fadein_t = 0.0;
        }
        if (M.quit_t != 0.0) {
            M.quit_t += M.step;
            if (M.quit_t >= 28.8) {
                save_settings();
                M.exit = 1;
                M.result = IGN_FRONTEND_QUIT;
            }
        }
        if (M.race_t != 0.0) {
            M.race_t += M.step;
            if (M.race_t >= 28.8) {
                save_settings();
                M.exit = 1;
                M.result = IGN_FRONTEND_RACE;
                g_first_menu = 0;
            }
        }
        if (M.carpic_ms > 0) M.carpic_ms += 27;
        if (CUR_PAGE != PAGE(4) && CUR_PAGE != PAGE(5)) M.carpic_ms = M.carpic_dir = 0;
        else if (M.carpic_ms >= 160 && M.carpic_ms < 187) M.carpic_dirty = 1;
        else if (M.carpic_ms >= 400) M.carpic_ms = M.carpic_dir = 0;
        if (M.anim_a > 0) M.anim_a += 27;
        if (M.anim_a >= 600) M.anim_a = 0;
        if (M.anim_b > 0) M.anim_b += 27;
        if (M.anim_b >= 600) M.anim_b = 0;
        if (M.trkpic_ms > 0) M.trkpic_ms += 27;
        if (CUR_PAGE != PAGE(6)) M.trkpic_ms = 0;
        else if (M.trkpic_ms >= 200 && M.trkpic_ms < 227) M.trkpic_dirty = 1;
        else if (M.trkpic_ms >= 400) M.trkpic_ms = 0;
        if (ign_cfg_get(CFG_MUSIC_VOL) != 0 && (M.intro_t == 0.0 || M.intro_t >= 1278.0) && CUR_PAGE != PAGE(21)) {
            M.cd_poll_ms += 27;
            if (M.cd_poll_ms > 2000) {
                M.cd_poll_ms = 0;
                if (!(g_host.cd_busy && g_host.cd_busy())) cd_restart_music();
            }
        }
        if (M.key_msg_ms != 0) {
            M.key_msg_ms += 27;
            if (M.key_msg_ms > 1100) {
                M.key_msg_ms = 0;
                M.redraw = 1;
            }
        }
        if (M.exit || M.abort) return M.result;
    }

    if (M.intro_t != 0.0) {
        const uint8_t *video = ign_cdp_palette(&M.cdp);
        if (M.intro_t >= 28.8 && M.intro_t < 57.6 && video) {
            ign_palette_lerp(&M.pal_white, (const IgnPalette *)video, (int)((M.intro_t - 28.8) * 270.0 * (1 / 28.8)), &M.pal_work);
            set_palette(M.pal_work.rgb[0]);
        } else if (M.intro_t >= 1249.2 && M.intro_t < 1278.0 && video) {
            ign_palette_lerp((const IgnPalette *)video, &M.pal_white, (int)((M.intro_t - 1249.2) * 270.0 * (1 / 28.8)), &M.pal_work);
            set_palette(M.pal_work.rgb[0]);
        }
        if (M.intro_t < 28.8) {
            ign_palette_lerp(&M.pal_black, &M.pal_white, (int)(M.intro_t * 270.0 * (1 / 28.8)), &M.pal_work);
            set_palette(M.pal_work.rgb[0]);
        } else if (M.intro_t >= 1306.8) {
            ign_palette_lerp(&M.pal_white, &M.menu_pal, (int)((M.intro_t - 1306.8) * 270.0 * (1 / 28.8)), &M.pal_work);
            set_palette(M.pal_work.rgb[0]);
        } else if (g_intro_snd_once) {
            g_intro_snd_once = 0;
            memset(M.textbuf, 0, SCREEN_BYTES);
            memset(M.backbuf, 0, SCREEN_BYTES);
            M.intro_snd_a = play(SND_INTRO, 65000, 22050);
            M.intro_snd_b = play(SND_INTRO, 65000, 22050);
        }
    }
    if (ign_cfg_get(CFG_MUSIC_VOL) != M.cd_vol_applied) {
        M.cd_vol_applied = ign_cfg_get(CFG_MUSIC_VOL);
        if (g_host.cd_volume) g_host.cd_volume((int)(M.cd_vol_applied * 25.5));
    }
    if (ign_cfg_get(CFG_MUSIC_VOL) != 0 && (M.intro_t == 0.0 || M.intro_t > 1278.0) && CUR_PAGE != PAGE(21) &&
        ign_cfg_get(CFG_CD_TRACK) != M.cd_track_applied) {
        M.cd_track_applied = ign_cfg_get(CFG_CD_TRACK);
        cd_restart_music();
    }
    {
        int v = ign_cfg_get(CFG_SFX_VOL) * 0x1999;
        if (v != M.sfx_vol_applied) {
            M.sfx_vol_applied = v;
            if (v == 0) M.snd_on = 0;
            else if (M.snd_present) M.snd_on = 1;
            if (M.snd_on && M.snd_present && g_host.sound_volume) g_host.sound_volume(v);
        }
    }
    if (CUR_PAGE == PAGE(21) && !g_first_menu) g_key_latch[SC_ENTER] = 1;
    if (M.intro_t >= 1278.0 && M.intro_bufs_live) {
        M.intro_bufs_live = 0;
        for (int k = 0; k < 6; k++) {
            free(M.intro_buf[k]);
            M.intro_buf[k] = NULL;
        }
        memset(&M.cdp, 0, sizeof M.cdp);
        menu_load_car();
    }
    if (ign_gui_tick() == -1) {
        act_quit(0, 6);
        return M.result;
    }
    if (CUR_PAGE == PAGE(7)) {
        if (M.page7_autosel) {
            page(7)->sel = M.page7_autosel - 1;
            g_key_latch[SC_ENTER] = 1;
            ign_gui_tick();
            M.page7_autosel = 0;
        } else
            g_key_latch[SC_ESC] = 1;
    }
    if (M.fadein_t != 0.0) {
        double r = M.fadein_t * 50.0 * (1 / 28.8) + -10.0;
        menu_iris(r < 0 ? 0 : r);
    }
    if (M.quit_t != 0.0) {
        double r = (28.8 - M.quit_t) * 50.0 * (1 / 28.8) + -10.0;
        menu_iris(r < 0 ? 0 : r);
    }
    if (M.race_t != 0.0) {
        double r = (28.8 - M.race_t) * 50.0 * (1 / 28.8) + -10.0;
        menu_iris(r < 0 ? 0 : r);
    }
    for (int k = 0; k < 10; k++) {
        if (!(M.messages.time[k] > 0.0)) continue;
        M.messages.time[k] -= M.dt;
        if (M.messages.time[k] < 0.0) M.messages.time[k] = 0.0;
        ign_gfx_text(font(F_RED_DARK), M.messages.text[k], 0, k * 16);
    }
    present();
    M.frames++;
    return M.result;
}
