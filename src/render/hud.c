#include "hud.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game/race.h"

/* Sprite rectangles {x, y, w, h} per layout (sub_00028000 [0x1fef44]: 0xc3618, 0xc3858, 0xc3a98). */
static const int16_t RECTS[3][36][4] = {
    {{0, 0, 55, 8}, {0, 8, 42, 8}, {0, 16, 23, 8}, {0, 24, 41, 33}, {0, 57, 13, 22}, {13, 57, 13, 22},
     {26, 57, 13, 22}, {39, 57, 13, 22}, {52, 57, 13, 22}, {65, 57, 13, 22}, {78, 57, 13, 22}, {91, 57, 13, 22},
     {104, 57, 13, 22}, {0, 75, 24, 22}, {54, 0, 16, 16}, {70, 0, 16, 16}, {86, 0, 16, 16}, {102, 0, 16, 16},
     {118, 0, 16, 16}, {54, 16, 16, 16}, {70, 16, 16, 16}, {86, 16, 16, 16}, {54, 32, 16, 16}, {70, 32, 16, 16},
     {86, 32, 16, 16}, {102, 32, 16, 16}, {0, 79, 8, 8}, {21, 89, 10, 29}, {21, 79, 10, 10}, {31, 79, 10, 10},
     {21, 89, 10, 29}, {0, 87, 7, 9}, {0, 0, 1, 1}, {123, 139, 104, 33}, {0, 0, 14, 14}, {127, 0, 20, 18}},
    {{0, 0, 83, 12}, {0, 12, 70, 12}, {0, 24, 27, 12}, {0, 36, 78, 64}, {0, 100, 22, 39}, {22, 100, 22, 39},
     {44, 100, 22, 39}, {66, 100, 22, 39}, {88, 100, 22, 39}, {110, 100, 22, 39}, {132, 100, 22, 39},
     {154, 100, 22, 39}, {176, 100, 22, 39}, {0, 139, 46, 44}, {83, 0, 32, 32}, {115, 0, 32, 32}, {147, 0, 32, 32},
     {179, 0, 32, 32}, {211, 0, 32, 32}, {104, 32, 32, 32}, {136, 32, 32, 32}, {168, 32, 32, 32}, {104, 64, 32, 32},
     {136, 64, 32, 32}, {168, 64, 32, 32}, {200, 64, 32, 32}, {109, 139, 14, 14}, {46, 159, 21, 58},
     {46, 139, 21, 20}, {88, 139, 21, 20}, {0, 139, 9, 100}, {9, 139, 12, 17}, {9, 207, 10, 19},
     {123, 139, 104, 33}, {0, 0, 28, 28}, {127, 0, 20, 18}},
    {{0, 0, 161, 24}, {0, 24, 129, 24}, {0, 48, 64, 24}, {0, 72, 157, 130}, {0, 56, 45, 76}, {45, 56, 45, 76},
     {90, 56, 45, 76}, {135, 56, 45, 76}, {180, 56, 45, 76}, {0, 132, 45, 76}, {45, 132, 45, 76},
     {90, 132, 45, 76}, {135, 132, 45, 76}, {0, 139, 46, 44}, {83, 0, 32, 32}, {115, 0, 32, 32}, {147, 0, 32, 32},
     {179, 0, 32, 32}, {211, 0, 32, 32}, {104, 32, 32, 32}, {136, 32, 32, 32}, {168, 32, 32, 32}, {104, 64, 32, 32},
     {136, 64, 32, 32}, {168, 64, 32, 32}, {200, 64, 32, 32}, {0, 202, 29, 29}, {161, 88, 45, 111},
     {161, 0, 45, 44}, {161, 44, 45, 44}, {0, 139, 9, 100}, {9, 139, 12, 17}, {9, 207, 10, 19},
     {123, 139, 104, 33}, {0, 0, 56, 56}, {83, 0, 44, 38}},
};

/* Placement table [0x1fef20] (0xc3d48, 0xc3db8, 0xc3e28), offsets from the viewport corners. */
enum {
    P_LAP_PANEL_Y,                  /* 0: y0 + */
    P_POS_X, P_POS_Y,               /* 1-2: x0 +, y1 + */
    P_GEAR_X, P_GEAR_Y,             /* 3-4: x1 +, y1 + */
    P_DIAL_X, P_DIAL_Y,             /* 5-6 */
    P_CAP_X, P_CAP_Y,               /* 7-8 */
    P_NEEDLE_X, P_NEEDLE_Y,         /* 9-10: needle pivot */
    P_NEEDLE_HALF, P_NEEDLE_LEN,    /* 11-12 */
    P_TURBO_X, P_TURBO_Y,           /* 13-14: frame */
    P_LIGHT_X, P_LIGHT_Y,           /* 15-16 */
    P_BAR_X0, P_BAR_Y, P_BAR_X1,    /* 17-19: turbo bar left, bottom, right */
    P_COUNT_W = 24, P_COUNT_Y = 25, /* centring width and y0 + offset of the start lights */
    P_SIGN_Y = 27                   /* y0 + offset of the signs */
};
static const int16_t PLACE[3][28] = {
    {24, 4, -27, -13, -37, -41, -32, -27, -19, -24, -16, 1, 12, -12,
     -70, -12, -78, -10, -44, -6, 14, -101, 14, -50, 96, 30, -197, 1},
    {33, 8, -55, -28, -73, -88, -69, -62, -43, -56, -37, 3, 26, -23,
     -130, -23, -150, -19, -79, -7, 14, -101, 14, -50, 96, 30, -176, 1},
    {66, 12, -110, -56, -146, -176, -138, -125, -87, -110, -72, 6, 52, -46,
     -260, -46, -304, -38, -156, -10, 28, -202, 28, -50, 96, 60, -352, 2},
};

/* Text table [0x1fef1c] (0xc3ce8, 0xc3d08, 0xc3d28): y0 + rows of total/lap 1-3 times, position (x0 +, y1 +),
 * speed (x1 +, y1 +). */
static const int16_t TEXT[3][8] = {
    {15, 32, 42, 52, 4, -20, -18, -11},
    {20, 46, 59, 72, 8, -40, -44, -29},
    {40, 98, 124, 150, 16, -80, -88, -56},
};

/* 0xc3e94 + (difficulty * 20 + kind) * 4: sign sprite of the PLC kind (type >> 24, at most 16). Difficulty 3
 * swaps the left/right signs. */
static const int8_t SIGN_MAP[4][17] = {
    {2, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {19, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {19, 1, 0, 3, 2, 5, 4, 7, 6, 8, 9, 11, 10, 12, 13, 14, 15},
};

/* 0xc3350: player names of the computer cars by model (sub_00022d08 0x22e6d). */
static const char *const CAR_NAMES[11] = {"COO", "BAN", "VAC", "ENF", "RED", "BUS", "MON", "BUG", "SMO", "VEG", "IGN"};

static const char *const LAST_WARNING[6] = {
    "WARNING, YOU'RE LAST!", "SIE SIND LETZTER!", "ATTENZIONE, SEI ULTIMO",
    "AVISO, ERES EL ULTIMO", "VARNING, DU LIGGER SIST!", "ATTENTION, VOUS ETES DERNIER!",
};
/* 0xa8df4..0xa8e40 (race_elimination 0x2e085); the Italian and Swedish strings use the fonts' own codes. */
static const char *const OUT_MESSAGE[6] = {
    "%s IS OUT", "%s IST AUSGESCHIEDEN", "%s \xc8 FUORI", "%s ESTA FUERA", "%s bR UTE", "%s EST ELIMINE",
};
static const char *const LAP_RECORD[6] = {
    "LAP RECORD", "RUNDENREKORD", "RECORD SUL GIRO", "RECORD DE VUELTA", "VARV REKORD", "RECORD DU TOUR",
};

#define TWO_PI 6.2831853071796

static int load_pic(IgnPic *pic, const char *data_dir, const char *name) {
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", data_dir, name) >= (int)sizeof path) return -1;
    return ign_load_pic(path, pic);
}

static int load_font(IgnFont *font, const char *data_dir, const char *name) {
    char path[1024];
    if (snprintf(path, sizeof path, "%s/FONTS/%s", data_dir, name) >= (int)sizeof path) return -1;
    return ign_font_load_lft(font, path);
}

static int rect_sprite(IgnSprite *s, const IgnPic *pic, const int16_t r[4]) {
    return ign_sprite_from_pic(s, pic, r[0], r[1], r[2], r[3]);
}

/* sub_00028000 for the current layout. */
static int build_sprites(IgnHud *hud) {
    const int16_t(*r)[4] = RECTS[hud->layout];
    const IgnPic *pic1 = hud->layout == IGN_HUD_LOW ? &hud->pangfx_s
                         : hud->layout == IGN_HUD_MEDIUM ? &hud->pangfx_n : &hud->pan1;
    const IgnPic *pic2 = hud->layout == IGN_HUD_HIGH ? &hud->pan2 : pic1;
    int fail = 0;
    fail |= rect_sprite(&hud->panel_total, pic1, r[0]);
    fail |= rect_sprite(&hud->panel_lap, pic1, r[1]);
    fail |= rect_sprite(&hud->panel_pos, pic1, r[2]);
    fail |= rect_sprite(&hud->dial, pic1, r[3]);
    fail |= rect_sprite(&hud->needle_cap, pic1, r[26]);
    for (int i = 0; i < 9; i++) fail |= rect_sprite(&hud->gear[i], pic2, r[4 + i]);
    fail |= rect_sprite(&hud->turbo_frame, pic1, r[27]);
    for (int i = 0; i < 2; i++) fail |= rect_sprite(&hud->turbo_light[i], pic1, r[28 + i]);
    /* 0x28378: the start lights always come from N_PANGFX, stacked under entry 33. */
    for (int k = 0; k < 4; k++)
        fail |= ign_sprite_from_pic(&hud->countdown[k], &hud->pangfx_n, r[33][0], r[33][1] + r[33][3] * k,
                                    r[33][2], r[33][3]);
    /* 0x283ee: 4 x 4 signs from entry 34 of the layout's SIGNS picture. */
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++)
            fail |= ign_sprite_from_pic(&hud->sign[row * 4 + col], &hud->signs, r[34][0] + r[34][2] * col,
                                        r[34][1] + r[34][3] * row, r[34][2], r[34][3]);
    /* 0x28577: car markers 12 x 17 at (9 + 12 r, 139 + 17 c), index 4 r + c. */
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 4; col++)
            fail |= ign_sprite_from_pic(&hud->marker[row * 4 + col], &hud->pangfx_n, 9 + 12 * row, 139 + 17 * col,
                                        12, 17);
    /* 0x28606: explosion frames 24 x 23 at (83 + 24 c, 23 r). */
    for (int row = 0; row < 2; row++)
        for (int col = 0; col < 5; col++)
            fail |= ign_sprite_from_pic(&hud->blast[row * 5 + col], &hud->pangfx_n, 83 + 24 * col, 23 * row, 24, 23);
    fail |= ign_sprite_from_pic(&hud->bar[0], &hud->pangfx_n, 0, 139, 7, 120); /* 0x286c9 */
    fail |= ign_sprite_from_pic(&hud->bar[1], &hud->pan2, 225, 0, 7, 300);     /* 0x28708 */
    fail |= ign_sprite_from_pic(&hud->crown, &hud->pangfx_n, 11, 207, 17, 12); /* 0x28747 */
    fail |= ign_sprite_from_pic(&hud->medal[1], &hud->sysg2, 153, 0, 89, 84);  /* 0x28801 */
    fail |= ign_sprite_from_pic(&hud->medal[2], &hud->sysg2, 0, 88, 89, 84);   /* 0x2883c */
    fail |= ign_sprite_from_pic(&hud->medal[3], &hud->sysg2, 89, 88, 89, 84);  /* 0x28875 */
    return fail ? -1 : 0;
}

static int parse_language(const char *s) {
    static const char *const names[6] = {"en", "de", "it", "es", "sv", "fr"};
    if (!s || !*s) return 0;
    if (isdigit((unsigned char)s[0])) {
        int n = atoi(s);
        return n >= 0 && n < 6 ? n : 0;
    }
    for (int i = 0; i < 6; i++)
        if (tolower((unsigned char)s[0]) == names[i][0] && tolower((unsigned char)s[1]) == names[i][1]) return i;
    return 0;
}

int ign_hud_init(IgnHud *hud, const char *data_dir, const char *level, int w, int h) {
    memset(hud, 0, sizeof *hud);
    (void)h;
    /* race_setup_video: 320x200 -> S_*, 640x480 -> N_*, 800x600 -> H_*. Other sizes use the next larger layout. */
    hud->layout = w <= 320 ? IGN_HUD_LOW : w <= 640 ? IGN_HUD_MEDIUM : IGN_HUD_HIGH;
    hud->language = parse_language(getenv("IGN_LANG"));
    hud->rng = 0x1fb4e8u;
    static const char *const SIGNS[3] = {"S_SIGNS.PIC", "N_SIGNS.PIC", "H_SIGNS.PIC"};
    static const char *const SUFFIX[3] = {"_S", "", "_H"};
    char name[64], path[1024];
    size_t pan_size = 0;
    if (snprintf(path, sizeof path, "%s/LEVELS/%s/%s.PAN", data_dir, level, level) >= (int)sizeof path) goto fail;
    hud->pan = ign_read_file(path, &pan_size);
    if (!hud->pan || pan_size < 65536) goto fail;
    if (load_pic(&hud->pangfx_s, data_dir, "S_PANGFX.PIC") || load_pic(&hud->pangfx_n, data_dir, "N_PANGFX.PIC") ||
        load_pic(&hud->pan1, data_dir, "H_PAN1.PIC") || load_pic(&hud->pan2, data_dir, "H_PAN2.PIC") ||
        load_pic(&hud->signs, data_dir, SIGNS[hud->layout]) || load_pic(&hud->sysg2, data_dir, "N_SYSG_2.PIC"))
        goto fail;
    snprintf(name, sizeof name, "YELLOW%s.LFT", SUFFIX[hud->layout]);
    if (load_font(&hud->time_font, data_dir, name)) goto fail;
    snprintf(name, sizeof name, "POS%s.LFT", SUFFIX[hud->layout]);
    if (load_font(&hud->pos_font, data_dir, name)) goto fail;
    snprintf(name, sizeof name, "SPEED%s.LFT", SUFFIX[hud->layout]);
    if (load_font(&hud->speed_font, data_dir, name)) goto fail;
    /* load_system_gfx 0x21985: the menu and message fonts are proportional with one pixel of spacing. */
    if (load_font(&hud->message_font, data_dir, hud->layout == IGN_HUD_LOW ? "MINI.LFT" : "SMALL.LFT")) goto fail;
    hud->message_font.proportional = 1;
    hud->message_font.spacing = 1;
    if (snprintf(path, sizeof path, "%s/FONTS/IGNITION.FNT", data_dir) >= (int)sizeof path ||
        ign_fnt_load(&hud->debug_font, path))
        goto fail;
    if (build_sprites(hud)) goto fail;
    return 0;

fail:
    ign_hud_destroy(hud);
    return -1;
}

void ign_hud_destroy(IgnHud *hud) {
    free(hud->pan);
    ign_free_pic(&hud->pangfx_s);
    ign_free_pic(&hud->pangfx_n);
    ign_free_pic(&hud->pan1);
    ign_free_pic(&hud->pan2);
    ign_free_pic(&hud->signs);
    ign_free_pic(&hud->sysg2);
    ign_font_free(&hud->time_font);
    ign_font_free(&hud->pos_font);
    ign_font_free(&hud->speed_font);
    ign_font_free(&hud->message_font);
    ign_fnt_free(&hud->debug_font);
    memset(hud, 0, sizeof *hud);
}

void ign_hud_format_time(char out[16], double t) {
    int minutes = (int)(t * 0.016666666666666666);
    if (minutes > 99) minutes = 99;
    double whole = trunc(t);
    int seconds = (int)whole - minutes * 60;
    int hundredths = (int)((t - whole) * 100.0);
    snprintf(out, 16, "%.2d:%.2d:%.2d", minutes, seconds, hundredths);
}

int ign_hud_message(IgnHud *hud, const char *text, int duration, int period, int viewport) {
    int slot = 0;
    while (slot < IGN_HUD_MESSAGES && hud->msg[slot].active) slot++;
    if (slot == IGN_HUD_MESSAGES) return -1;
    IgnHudMessage *m = &hud->msg[slot];
    m->active = 1;
    snprintf(m->text, sizeof m->text, "%s", text);
    m->timer = 0;
    m->period = period;
    m->viewport = viewport;
    m->duration = duration;
    return slot;
}

static double wrap_angle(double a) {
    while (a < 0.0) a += TWO_PI;
    while (a >= TWO_PI) a -= TWO_PI;
    return a;
}

static const char *car_name(const IgnCar *car) {
    return car->id >= 0 && car->id < 11 ? CAR_NAMES[car->id] : "IGN";
}

/* One 1/36 s frame of the per-frame state the original keeps in the car structures and globals. */
static void step_frame(IgnHud *hud, const IgnHudState *st) {
    const double dt = 1.0;
    int n = st->ncars < IGN_HUD_MAX_CARS ? st->ncars : IGN_HUD_MAX_CARS;
    /* sub_0004c858 (collisions pass): message timers. */
    for (int i = 0; i < IGN_HUD_MESSAGES; i++) {
        IgnHudMessage *m = &hud->msg[i];
        if (m->active == 1 && ++m->timer > m->duration) m->active = 0;
    }
    for (int i = 0; i < n; i++) {
        const IgnCar *car = st->cars[i];
        IgnHudCar *hc = &hud->car[i];
        /* car_update 0x2fbb2: the previous section is kept whenever the navigation section changes. */
        if (car->section != hc->section) hc->prev_section = hc->section, hc->section = car->section;
        /* cars_body_sway 0x41038: the explosion frame runs from the elimination to 10. */
        if (car->blow > 0.0 && hc->blow == 0.0) hc->blow = car->blow;
        else if (hc->blow > 0.0 && hc->blow < 10.0) hc->blow += 0.3;
        if (car->out && !hc->was_out) {
            hc->was_out = 1;
            /* race_elimination 0x2e085: the car last at the end of a lap is out. */
            if (st->mode == IGN_MODE_ELIMINATION && car->blow > 0.0) {
                char text[100];
                snprintf(text, sizeof text, OUT_MESSAGE[hud->language], car_name(car));
                ign_hud_message(hud, text, 0x50, 8, -1);
            }
            /* car_update 0x2f38b: the player's finish restarts [0x1fb4d0] and [0x1fed9c]. */
            if (i == 0) hud->finish_clock = 0.0, hud->finish_frames = 0, hc->medal_zoom = 0;
        }
    }
    const IgnCar *player = st->cars[0];
    IgnHudCar *pc = &hud->car[0];
    /* car_update 0x2f40f: a human lap under [0x1fb390] is announced (computer records are silent). */
    if (st->best_lap < hud->best_lap && player->last_lap == st->best_lap)
        ign_hud_message(hud, LAP_RECORD[hud->language], 0x50, 8, -1);
    hud->best_lap = st->best_lap;
    /* sub_000420cc 0x4280c / 0x4288d / 0x428b0: clocks after the player's finish. */
    if (player->out) {
        if (hud->finish_frames >= 0 && hud->finish_frames < 120) hud->finish_frames = (int)(hud->finish_frames + dt);
        hud->finish_clock += dt;
        if (pc->medal_zoom < 25) pc->medal_zoom = (int)(pc->medal_zoom + dt);
    }
    /* sub_0004a150 (main loop 0x21089): frames spent facing back along the section, 0..120. */
    double angle;
    if (st->track && st->level && ign_track_section_angle(st->track, st->level, player->section, &angle)) {
        double a = wrap_angle(angle - 1.5707963267949), h = wrap_angle(player->body.heading);
        double d = fabs(a - h);
        if (d > 2.0943951023932 && d < 4.1887902047864) {
            pc->wrong_way = (int)(pc->wrong_way + dt);
            if (pc->wrong_way > 120) pc->wrong_way = 120;
        } else if (pc->wrong_way > 0) {
            pc->wrong_way = (int)(pc->wrong_way - dt);
        }
    }
    /* race_hud: gear lever (0x4a907), rev needle (0x4bfa6) and turbo light (0x4ae7d) advance once per frame. */
    double target = player->gear * 4;
    if (target > pc->gear_anim) {
        pc->gear_anim += dt * 0.5;
        if (target < pc->gear_anim) pc->gear_anim = target;
    }
    if (target < pc->gear_anim) {
        pc->gear_anim -= dt * 0.5;
        if (target > pc->gear_anim) pc->gear_anim = target;
    }
    pc->needle = (player->revs * 2.7 - pc->needle) * 0.25 + pc->needle;
    if (pc->needle > 270.0) pc->needle = 270.0;
    if (player->turbo_mode == 0 && player->turbo_charge >= 100.0) {
        if (++pc->turbo_blink > 14) pc->turbo_blink = 0;
    } else if (player->turbo_mode > 0 || player->turbo_charge < 20.0) {
        if (++pc->turbo_blink > 1) pc->turbo_blink = 0;
    }
    /* 0x4b117: the sign of the section blinks for 24 frames, then stays. */
    if (!player->out && st->clock == -1.0 && st->track && st->level &&
        !(pc->wrong_way > 0x19 && pc->section < pc->prev_section)) {
        int obj = ign_track_section_object(st->track, player->section), kind = 0;
        if (obj >= 0 && obj < st->level->track.nobj) kind = (int)(st->level->track.objs[obj].type >> 24);
        pc->sign_blink = kind > 0 ? pc->sign_blink + 1 : 0;
    }
    /* race_hud 0x4b246: the last car of an elimination race is warned every frame (duration 0, period 1). */
    if (st->mode == IGN_MODE_ELIMINATION && st->remaining == player->position && !player->out)
        ign_hud_message(hud, LAST_WARNING[hud->language], 0, 1, -1);
}

void ign_hud_update(IgnHud *hud, const IgnHudState *st) {
    if (!st->ncars || !st->cars || !st->cars[0]) return;
    if (!hud->started) {
        /* First frame: the animated gauges start at their targets (the original starts them at 0 with the car). */
        const IgnCar *player = st->cars[0];
        hud->started = 1;
        hud->frame_clock = floor(st->total);
        hud->best_lap = st->best_lap;
        hud->car[0].gear_anim = player->gear * 4;
        hud->car[0].needle = fmin(player->revs * 2.7, 270.0);
        for (int i = 0; i < st->ncars && i < IGN_HUD_MAX_CARS; i++) {
            hud->car[i].section = hud->car[i].prev_section = st->cars[i]->section;
            hud->car[i].was_out = st->cars[i]->out;
            hud->car[i].blow = st->cars[i]->blow;
        }
        hud->finish_frames = st->cars[0]->out ? 120 : 0;
        hud->finish_clock = st->cars[0]->out ? 121.0 : 0.0;
        hud->car[0].medal_zoom = st->cars[0]->out ? 25 : 0;
        return;
    }
    while (hud->frame_clock + 1.0 <= st->total) {
        hud->frame_clock += 1.0;
        step_frame(hud, st);
    }
}

static double hud_random(IgnHud *hud) {
    hud->rng = hud->rng * 1103515245u + 12345u;
    return (hud->rng >> 8 & 0xffff) / 65536.0;
}

/* race_hud 0x4a37c / sub_0004be7c 0x4be9b: while the camera shakes (+0x38) the panel moves by shake * 0.04, the
 * direction chosen by a random draw. */
static int jitter(IgnHud *hud, const IgnHudState *st, double *d) {
    if (st->shake == 0.0) return 0;
    *d = st->shake * 0.04;
    if (hud_random(hud) < 0.5) *d = -*d;
    return 1;
}

static void draw_text(IgnHud *hud, IgnFrame *fb, const IgnClip *clip, const IgnFont *font, const char *text,
                      int x, int y) {
    ign_font_draw(fb, clip, font, text, x, y, hud->pan);
}

static void draw_sprite(IgnHud *hud, IgnFrame *fb, const IgnClip *clip, const IgnSprite *s, int x, int y) {
    ign_sprite_draw(fb, clip, s, x, y, hud->pan);
}

/* sub_0004be7c: rev dial, needle and speed digits. */
static void draw_dial(IgnHud *hud, IgnFrame *fb, const IgnClip *vp, const IgnHudState *st) {
    const int16_t *p = PLACE[hud->layout];
    const IgnCar *car = st->cars[0];
    int x1 = vp->x1, y1 = vp->y1;
    double d;
    if (jitter(hud, st, &d)) x1 = (int)(vp->x1 + d), y1 = (int)(vp->y1 + d);
    draw_sprite(hud, fb, vp, &hud->dial, x1 + p[P_DIAL_X], y1 + p[P_DIAL_Y]);
    /* 0x4bff7: a thin triangle, half width p[11] across the pivot and p[12] long, turned by the needle angle. */
    double n = hud->car[0].needle, half = p[P_NEEDLE_HALF], len = p[P_NEEDLE_LEN];
    double cx = x1 + p[P_NEEDLE_X], cy = y1 + p[P_NEEDLE_Y];
    double a = (n + 180.0) * 3.1415926535898 * 0.005555555555555555;
    double b = (n + 90.0) * 3.1415926535898 * 0.005555555555555555;
    double c = n * 3.1415926535898 * 0.005555555555555555;
    int tri[6] = {(int)(cx + cos(a) * half), (int)(cy + sin(a) * half), (int)(cx + cos(b) * len),
                  (int)(cy + sin(b) * len), (int)(cx + cos(c) * half), (int)(cy + sin(c) * half)};
    ign_fill_triangle(fb, vp, tri, 0xc4);
    draw_sprite(hud, fb, vp, &hud->needle_cap, x1 + p[P_CAP_X], y1 + p[P_CAP_Y]);
    char speed[16];
    int v = (int)(car->body.v_fwd * 3.6);
    snprintf(speed, sizeof speed, "%.3d", v < 0 ? -v : v);
    draw_text(hud, fb, vp, &hud->speed_font, speed, vp->x1 + TEXT[hud->layout][6], vp->y1 + TEXT[hud->layout][7]);
}

static void draw_messages(IgnHud *hud, IgnFrame *fb, const IgnClip *vp) {
    /* sub_0004c89c: centred, one line of (feeb0 - 5) * 8 pixels per slot above the bottom of viewport 0. */
    int line = hud->layout == IGN_HUD_LOW ? 8 : 16;
    hud->message_font.align = 1;
    for (int i = 0; i < IGN_HUD_MESSAGES; i++) {
        const IgnHudMessage *m = &hud->msg[i];
        if (m->active != 1) continue;
        if (!(fmod((double)m->timer, (double)(m->period * 2)) < (double)m->period)) continue;
        int x = m->viewport == -1 ? fb->w / 2 : vp->x0 + (vp->x1 - vp->x0) / 2;
        draw_text(hud, fb, vp, &hud->message_font, m->text, x, vp->y1 - (i + 1) * line);
    }
    hud->message_font.align = 0;
}

void ign_hud_draw(IgnHud *hud, IgnFrame *fb, const IgnHudState *st) {
    if (!st->ncars || !st->cars || !st->cars[0]) return;
    const IgnCar *car = st->cars[0];
    const IgnHudCar *hc = &hud->car[0];
    /* sub_000420cc 0x4310c: no HUD 120 frames after the player's finish. */
    if (car->out && hud->finish_clock > 120.0) return;
    const int16_t *p = PLACE[hud->layout], *t = TEXT[hud->layout];
    const IgnClip vp = {0, 0, fb->w, fb->h}; /* [0x1fefd0]+0x2c..0x38 of player 0 */
    int x0 = vp.x0, y0 = vp.y0, x1 = vp.x1, y1 = vp.y1;
    double d;
    if (jitter(hud, st, &d)) x0 = (int)(vp.x0 + d), y0 = (int)(vp.y0 + d), x1 = (int)(vp.x1 + d), y1 = (int)(vp.y1 + d);
    int racing = st->clock == -1.0;
    char text[64];

    if (st->mode != IGN_MODE_ELIMINATION) {
        /* 0x4a4b4: total time, then the lap panel with one row per lap (live time on the current lap). */
        draw_sprite(hud, fb, &vp, &hud->panel_total, x0 + 7, y0 + 7);
        if (racing)
            ign_hud_format_time(text, car->race_elapsed);
        else
            snprintf(text, sizeof text, "--:--:--");
        draw_text(hud, fb, &vp, &hud->time_font, text, x0 + 7, y0 + t[0]);
        draw_sprite(hud, fb, &vp, &hud->panel_lap, x0 + 7, y0 + p[P_LAP_PANEL_Y]);
        if (racing)
            ign_hud_format_time(text, car->lap == 0 ? car->lap_elapsed : car->lap_times[0]);
        else
            snprintf(text, sizeof text, "--:--:--");
        draw_text(hud, fb, &vp, &hud->time_font, text, x0 + 7, y0 + t[1]);
        for (int lap = 1; lap < 3; lap++) {
            if (car->lap < lap) break;
            ign_hud_format_time(text, car->lap == lap ? car->lap_elapsed : car->lap_times[lap]);
            draw_text(hud, fb, &vp, &hud->time_font, text, x0 + 7, y0 + t[1 + lap]);
        }
    }
    if (st->ncars > 1) {
        /* 0x4a805: place; elimination races show place / cars left while racing. */
        if (st->mode != IGN_MODE_ELIMINATION) {
            draw_sprite(hud, fb, &vp, &hud->panel_pos, x0 + p[P_POS_X], y1 + p[P_POS_Y]);
            snprintf(text, sizeof text, "%d", car->out ? car->finish_position : car->position);
        } else if (!car->out) {
            snprintf(text, sizeof text, "%d/%d", car->position, st->remaining);
        } else {
            snprintf(text, sizeof text, "%d", car->position);
        }
        draw_text(hud, fb, &vp, &hud->pos_font, text, x0 + t[4], y1 + t[5]);
    }
    draw_dial(hud, fb, &vp, st);
    int gear = (int)hc->gear_anim;
    if (gear < 0) gear = 0;
    if (gear > 8) gear = 8;
    draw_sprite(hud, fb, &vp, &hud->gear[gear], x1 + p[P_GEAR_X], y1 + p[P_GEAR_Y]);

    /* 0x4aa6e: turbo charge as two flat triangles, height charge / {4.2, 2.1, 1}, colour 0xd7. */
    static const double TURBO_DIV[3] = {4.2, 2.1, 1.0};
    double k = 1.0 / TURBO_DIV[hud->layout];
    int bottom = y1 + p[P_BAR_Y], top = (int)(bottom - car->turbo_charge * k);
    int left = x1 + p[P_BAR_X0], right = x1 + p[P_BAR_X1];
    int quad_a[6] = {left, bottom, left, top, right, bottom}, quad_b[6] = {right, bottom, left, top, right, top};
    ign_fill_triangle(fb, &vp, quad_a, 0xd7);
    ign_fill_triangle(fb, &vp, quad_b, 0xd7);
    draw_sprite(hud, fb, &vp, &hud->turbo_frame, x1 + p[P_TURBO_X], y1 + p[P_TURBO_Y]);
    const IgnSprite *light = NULL;
    if (car->turbo_mode == 0 && car->turbo_charge >= 20.0 && car->turbo_charge < 100.0)
        light = &hud->turbo_light[0];
    else if (car->turbo_mode == 0 && car->turbo_charge >= 100.0)
        light = &hud->turbo_light[hc->turbo_blink < 7 ? 1 : 0];
    else if (car->turbo_mode > 0 || car->turbo_charge < 20.0)
        light = &hud->turbo_light[hc->turbo_blink ? 1 : 0];
    if (light) draw_sprite(hud, fb, &vp, light, x1 + p[P_LIGHT_X], y1 + p[P_LIGHT_Y]);

    if (!car->out && racing && st->track && st->level) {
        /* 0x4b03d: WRONG WAY after 25 frames facing back with the section going down, else the section's sign. */
        int sign_x = vp.x0 + (vp.x1 - vp.x0) / 2 - RECTS[hud->layout][34][2] / 2;
        int obj = ign_track_section_object(st->track, car->section);
        int kind = obj >= 0 && obj < st->level->track.nobj ? (int)(st->level->track.objs[obj].type >> 24) : 0;
        if (hc->wrong_way > 0x19 && hc->section < hc->prev_section) {
            draw_sprite(hud, fb, &vp, &hud->sign[15], sign_x, y0 + p[P_SIGN_Y]);
        } else if (kind > 0 && (hc->sign_blink / 4 % 2 == 0 || hc->sign_blink >= 24)) {
            int diff = st->difficulty < 0 ? 0 : st->difficulty > 3 ? 3 : st->difficulty;
            int index = SIGN_MAP[diff][kind > 16 ? 16 : kind];
            if (index >= 0 && index < 16) draw_sprite(hud, fb, &vp, &hud->sign[index], sign_x, y0 + p[P_SIGN_Y]);
        }
    }

    if (st->mode == IGN_MODE_ELIMINATION) {
        /* 0x4b327: progress bar with a marker per car (the leader crowned) and the explosions of eliminated
         * cars. Before 250 frames every car sits at the bottom. */
        int hires = hud->layout != IGN_HUD_LOW, len = hires ? 300 : 120;
        draw_sprite(hud, fb, &vp, &hud->bar[hires], vp.x0 + 8, vp.y0 + 8);
        int n = st->ncars < IGN_HUD_MAX_CARS ? st->ncars : IGN_HUD_MAX_CARS;
        double frac[IGN_HUD_MAX_CARS];
        for (int i = n - 1; i >= 0; i--) {
            const IgnCar *c = st->cars[i];
            frac[i] = st->total >= 250.0 && st->track && st->track->count > 0
                          ? fabs((double)c->section) / st->track->count : 0.0;
            if (c->out) continue;
            int h = (int)(frac[i] * len);
            int model = c->id >= 0 && c->id < 12 ? c->id : 0;
            draw_sprite(hud, fb, &vp, &hud->marker[model], vp.x0 + 5, vp.y0 + len + 5 - h);
            if (c->position == 1) draw_sprite(hud, fb, &vp, &hud->crown, vp.x0 + 19, vp.y0 + len + 8 - h);
        }
        for (int i = n - 1; i >= 0; i--) {
            const IgnCar *c = st->cars[i];
            double blow = hud->car[i].blow;
            if (!c->out || !(blow < 10.0) || c->finish_position == 1) continue;
            int frame = (int)blow;
            if (frame < 0 || frame > 9) continue;
            draw_sprite(hud, fb, &vp, &hud->blast[frame], vp.x0, vp.y0 + len - (int)(len * frac[i]));
        }
    }

    /* 0x4b717: start lights 3 in [50, 100), 2 in [100, 150), 1 in [150, 200), GO for the first 250 frames. */
    int light_x = vp.x0 + (vp.x1 - vp.x0) / 2 - p[P_COUNT_W] / 2, light_y = y0 + p[P_COUNT_Y];
    int lights = -1;
    if (st->clock >= 50.0 && st->clock < 100.0) lights = 0;
    else if (st->clock >= 100.0 && st->clock < 150.0) lights = 1;
    else if (st->clock >= 150.0 && st->clock < 200.0) lights = 2;
    else if (racing && st->total < 250.0) lights = 3;
    if (lights >= 0) draw_sprite(hud, fb, &vp, &hud->countdown[lights], light_x, light_y);

    /* 0x4ba79: finishing place for 120 frames, growing for 25 (scale zoom * 0.04). */
    int place_ok = st->mode != IGN_MODE_ELIMINATION ? car->finish_position < 4 : car->finish_position < 2;
    if (car->out && hud->finish_clock >= 0.0 && hud->finish_frames < 120 && !(st->mode == 2 && st->ncars <= 1) &&
        place_ok && hc->medal_zoom > 0 && car->finish_position >= 1 && car->finish_position < 4) {
        int mx = vp.x0 + (vp.x1 - vp.x0) / 2, my = (y1 - y0) / 2;
        const IgnSprite *medal = &hud->medal[car->finish_position];
        if (hc->medal_zoom < 25)
            ign_sprite_draw_scaled(fb, &vp, medal, mx, my, (float)(hc->medal_zoom * 0.04), hud->pan);
        else
            draw_sprite(hud, fb, &vp, medal, mx, my);
    }
    draw_messages(hud, fb, &vp);
}
