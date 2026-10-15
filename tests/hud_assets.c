/* Parsers and drawing of the 2D race overlay: LFT/FNT fonts, geputget sprites, the race_hud time format and,
 * with the game data, every original font/panel plus offscreen HUD frames (countdown, racing, after the finish,
 * elimination messages). Usage: hud_assets_test [GAME_DIR [PPM_OUTPUT_DIR]] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/race.h"
#include "render/font.h"
#include "render/hud.h"
#include "render/sprite.h"

static int failures;

#define CHECK(cond, ...)                                                                                           \
    do {                                                                                                           \
        if (!(cond)) {                                                                                             \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                                   \
            fprintf(stderr, __VA_ARGS__);                                                                          \
            fputc('\n', stderr);                                                                                   \
            failures++;                                                                                            \
        }                                                                                                          \
    } while (0)

static IgnFrame frame;

static void put16(uint8_t *p, int v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* An LFT with cell width 4, height 2: '0' 2 pixels wide (colour 7), '1' 4 wide (colour 9), 'j' width -6. */
static uint8_t *make_lft(size_t *size) {
    *size = 0x84c + 4 + 8;
    uint8_t *d = calloc(1, *size);
    memcpy(d, "LFT", 4);
    put16(d + 4, 100);
    put16(d + 6, 3);
    put16(d + 8, 4);
    put16(d + 10, 2);
    for (int i = 0; i < IGN_FONT_GLYPHS; i++) put32(d + 0x0c + i * 4, UINT32_MAX);
    put32(d + 0x0c + ('0' - 32) * 4, 0);
    put16(d + 0x68c + ('0' - 32) * 2, 2);
    put32(d + 0x0c + ('1' - 32) * 4, 4);
    put16(d + 0x68c + ('1' - 32) * 2, 4);
    put32(d + 0x0c + ('j' - 32) * 4, 12);
    put16(d + 0x68c + ('j' - 32) * 2, 0xfffa);
    memset(d + 0x84c, 7, 4);
    memset(d + 0x84c + 4, 9, 8);
    return d;
}

static int count_colour(uint8_t colour, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += frame.color[y * frame.w + x] == colour;
    return n;
}

static void test_lft(void) {
    size_t size;
    uint8_t *d = make_lft(&size);
    IgnFont f;
    CHECK(ign_font_parse_lft(&f, d, size) == 0, "synthetic LFT parses");
    CHECK(f.count == 3 && f.width == 4 && f.height == 2 && f.spacing == 1 && !f.proportional && !f.align,
          "header and sub_00061220 defaults");
    CHECK(f.present['0' - 32] && f.present['1' - 32] && f.present['j' - 32] && !f.present['2' - 32],
          "present glyphs follow the offsets");
    CHECK(f.glyph_w['j' - 32] == -6 && f.glyph['j' - 32].pixels == NULL, "negative width has no bitmap");
    /* Fixed cells: width + spacing per glyph or space, unknown characters skipped. */
    CHECK(ign_font_text_width(&f, "01 1?") == 4 * 5, "fixed width %d", ign_font_text_width(&f, "01 1?"));
    f.proportional = 1;
    /* Proportional: glyph + spacing, space trunc(4 * 0.35) = 1, 'j' moves back 6 - 1. */
    CHECK(ign_font_text_width(&f, "01 1") == 3 + 5 + 1 + 5, "proportional width %d", ign_font_text_width(&f, "01 1"));
    CHECK(ign_font_text_width(&f, "1j") == 5 - 5, "signed glyph width");
    f.proportional = 0;

    ign_frame_init(&frame, 320, 200);
    memset(frame.color, 0, sizeof frame.color);
    ign_font_draw(&frame, NULL, &f, "0", 10, 10, NULL);
    /* A 2-wide glyph in a 4-wide cell is centred: (4 - 2) / 2 = 1. */
    CHECK(count_colour(7, 11, 10, 13, 12) == 4 && count_colour(7, 0, 0, 320, 200) == 4, "fixed cell centring");
    memset(frame.color, 0, sizeof frame.color);
    f.align = 1;
    ign_font_draw(&frame, NULL, &f, "11", 50, 20, NULL); /* width 10: starts at 50 - 5 */
    CHECK(frame.color[20 * 320 + 45] == 9 && frame.color[20 * 320 + 44] == 0 && frame.color[20 * 320 + 54] == 0 &&
              frame.color[20 * 320 + 53] == 9,
          "centred text");
    memset(frame.color, 0, sizeof frame.color);
    f.align = 2;
    ign_font_draw(&frame, NULL, &f, "1", 50, 20, NULL); /* width 5: starts at 45 */
    CHECK(frame.color[20 * 320 + 45] == 9 && frame.color[20 * 320 + 48] == 9 && frame.color[20 * 320 + 49] == 0,
          "right-aligned text");
    f.disabled = 1;
    CHECK(ign_font_text_width(&f, "11") == 2, "disabled slot returns 2");
    ign_font_free(&f);

    /* Malformed files are refused. */
    d = make_lft(&size);
    d[0] = 'X';
    CHECK(ign_font_parse_lft(&f, d, size) != 0, "bad magic");
    memcpy(d, "LFT", 4);
    put16(d + 4, 101);
    CHECK(ign_font_parse_lft(&f, d, size) != 0, "bad version");
    put16(d + 4, 100);
    put32(d + 0x0c + ('1' - 32) * 4, 6);
    CHECK(ign_font_parse_lft(&f, d, size) != 0, "glyph past the end");
    CHECK(ign_font_parse_lft(&f, d, 0x84b) != 0, "truncated header");
    free(d);
}

static void test_fnt(void) {
    size_t size = 0x546 + 6 + 4;
    uint8_t *d = calloc(1, size);
    put16(d, 2);
    put16(d + 2, 2);
    put16(d + 4, 0xffff); /* spacing -1, as IGNITION.FNT */
    d[6] = 3;
    d[7] = 2;
    memset(d + 0xc6, 255, 256);
    d[0xc6 + 'A'] = 0;
    d[0xc6 + 'B'] = 1;
    put32(d + 0x1c6, 0);
    put32(d + 0x1c6 + 4, 6);
    uint8_t a[6] = {1, 0, 1, 0, 2, 0}, b[4] = {3, 3, 3, 3};
    memcpy(d + 0x546, a, 6);
    memcpy(d + 0x546 + 6, b, 4);
    IgnFnt fnt;
    CHECK(ign_fnt_parse(&fnt, d, size) == 0, "synthetic FNT parses");
    CHECK(fnt.spacing == -1 && fnt.height == 2, "FNT header");
    CHECK(ign_fnt_text_width(&fnt, "AB?") == (3 - 1) + (2 - 1), "FNT width skips unmapped characters");
    ign_frame_init(&frame, 320, 200);
    memset(frame.color, 0, sizeof frame.color);
    ign_fnt_draw(&frame, &fnt, "AB", 5, 5, 0x10);
    CHECK(frame.color[5 * 320 + 5] == 0x11 && frame.color[5 * 320 + 6] == 0 && frame.color[6 * 320 + 6] == 0x12,
          "texel + colour, zero transparent");
    CHECK(frame.color[5 * 320 + 7] == 0x13 && frame.color[6 * 320 + 8] == 0x13, "advance width + spacing");
    uint8_t *bad = calloc(1, size);
    memcpy(bad, fnt.data, size);
    ign_fnt_free(&fnt);
    put32(bad + 0x1c6 + 4, 8);
    CHECK(ign_fnt_parse(&fnt, bad, size) != 0, "FNT glyph past the end");
    free(bad);
}

static int differs_from_zero(int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += frame.color[y * frame.w + x] != 0;
    return n;
}

static void test_sprites(void) {
    uint8_t pixels[6 * 4];
    for (int i = 0; i < 24; i++) pixels[i] = (uint8_t)i;
    IgnPic pic = {.width = 6, .height = 4, .pixels = pixels};
    IgnSprite s;
    CHECK(ign_sprite_from_pic(&s, &pic, 2, 1, 3, 2) == 0 && s.pixels == pixels + 8 && s.stride == 6,
          "sprite window");
    CHECK(ign_sprite_from_pic(&s, &pic, 4, 1, 3, 2) != 0 && !s.pixels, "window past the right edge");
    CHECK(ign_sprite_from_pic(&s, &pic, 0, 3, 1, 2) != 0, "window past the bottom");
    ign_sprite_from_pic(&s, &pic, 0, 0, 3, 2); /* 0 1 2 / 6 7 8 */
    static uint8_t lut[65536];
    for (int src = 0; src < 256; src++)
        for (int dst = 0; dst < 256; dst++) lut[src << 8 | dst] = src == 0 ? (uint8_t)dst : (uint8_t)(src + 100);
    ign_frame_init(&frame, 320, 200);
    memset(frame.color, 5, sizeof frame.color);
    ign_sprite_draw(&frame, NULL, &s, 10, 10, lut);
    CHECK(frame.color[10 * 320 + 10] == 5 && frame.color[10 * 320 + 11] == 101 && frame.color[11 * 320 + 12] == 108,
          "PAN blend table");
    memset(frame.color, 5, sizeof frame.color);
    IgnClip clip = {11, 0, 12, 11};
    ign_sprite_draw(&frame, &clip, &s, 10, 10, NULL);
    CHECK(frame.color[10 * 320 + 11] == 1 && frame.color[10 * 320 + 12] == 5 && frame.color[11 * 320 + 11] == 5,
          "clip window");
    memset(frame.color, 5, sizeof frame.color);
    ign_sprite_draw(&frame, NULL, &s, -1, -1, NULL);
    CHECK(frame.color[0] == 7 && frame.color[1] == 8 && frame.color[2] == 5, "frame edges clip");
    memset(frame.color, 0, sizeof frame.color);
    ign_sprite_draw_scaled(&frame, NULL, &s, 20, 20, 2.0f, NULL);
    /* The matrix path (exact against the original in tests/hud_original.py) covers about 6 x 4 pixels here. */
    int scaled = differs_from_zero(0, 0, 320, 200);
    CHECK(scaled >= 12 && scaled <= 24 && differs_from_zero(19, 19, 27, 25) == scaled, "scaled sprite (%d)", scaled);
    memset(frame.color, 0, sizeof frame.color);
    int tri[6] = {10, 10, 10, 20, 20, 10};
    ign_fill_triangle(&frame, NULL, tri, 0xd7, NULL);
    CHECK(frame.color[11 * 320 + 11] == 0xd7 && frame.color[18 * 320 + 18] == 0 && frame.color[10 * 320 + 9] == 0,
          "flat triangle");
}

static void test_time(void) {
    char t[16];
    ign_hud_format_time(t, 0.0);
    CHECK(!strcmp(t, "00:00:00"), "zero %s", t);
    ign_hud_format_time(t, 83.456);
    CHECK(!strcmp(t, "01:23:45"), "truncated hundredths %s", t);
    ign_hud_format_time(t, 59.999);
    CHECK(!strcmp(t, "00:59:99"), "no rounding up %s", t);
    ign_hud_format_time(t, 100.0 * 60.0 + 5.0);
    CHECK(!strcmp(t, "99:65:00"), "minutes stop at 99, seconds keep the rest %s", t);
}

static int write_ppm(const char *dir, const char *name, const IgnPalette *pal) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", frame.w, frame.h);
    for (int i = 0; i < frame.w * frame.h; i++) fwrite(pal->rgb[frame.color[i]], 1, 3, f);
    fclose(f);
    return 0;
}

static int differs_from(uint8_t background, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += frame.color[y * frame.w + x] != background;
    return n;
}

static void test_game_data(const char *dir, const char *out) {
    static const char *const LFT[] = {"MINI", "POS", "POS_H", "POS_S", "RESULT", "SMALL", "SPEED", "SPEED_H",
                                      "SPEED_S", "YELLOW", "YELLOW_H", "YELLOW_S"};
    for (size_t i = 0; i < sizeof LFT / sizeof *LFT; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/FONTS/%s.LFT", dir, LFT[i]);
        IgnFont f;
        CHECK(ign_font_load_lft(&f, path) == 0, "%s.LFT parses", LFT[i]);
        int present = 0;
        for (int g = 0; g < IGN_FONT_GLYPHS; g++) present += f.present[g];
        CHECK(present == f.count, "%s.LFT: %d glyphs present, header says %d", LFT[i], present, f.count);
        ign_font_free(&f);
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/FONTS/IGNITION.FNT", dir);
    IgnFnt fnt;
    CHECK(ign_fnt_load(&fnt, path) == 0 && fnt.count == 56 && fnt.height == 10 && fnt.spacing == -1,
          "IGNITION.FNT header");
    if (fnt.data) CHECK(ign_fnt_text_width(&fnt, "X 12") == 8 + 4 + 6 + 7 - 4, "FNT width");
    ign_fnt_free(&fnt);

    IgnLevel lv;
    if (ign_load_level(dir, "USA", &lv)) {
        CHECK(0, "cannot load USA");
        return;
    }
    static const int SIZES[3][2] = {{320, 200}, {640, 480}, {800, 600}};
    static const char *const NAMES[3] = {"low", "medium", "high"};
    for (int layout = 0; layout < 3; layout++) {
        static IgnHud hud;
        int w = SIZES[layout][0], h = SIZES[layout][1];
        if (ign_hud_init(&hud, dir, "USA", w, h)) {
            CHECK(0, "HUD init for %dx%d", w, h);
            continue;
        }
        CHECK(hud.layout == layout, "layout of %dx%d", w, h);
        /* The original indexes with fixed strides: 0x7b/0xe3/0x100 panels, 0x38/0x70/0x100 signs. */
        static const int PANEL_STRIDE[3] = {0x7b, 0xe3, 0x100}, SIGN_STRIDE[3] = {0x38, 0x70, 0x100};
        CHECK(hud.panel_total.stride == PANEL_STRIDE[layout] && hud.gear[0].stride == PANEL_STRIDE[layout],
              "panel stride");
        CHECK(hud.sign[0].stride == SIGN_STRIDE[layout], "sign stride");
        CHECK(hud.countdown[0].stride == 0xe3 && hud.countdown[3].h == 33 && hud.bar[1].stride == 0x100 &&
                  hud.medal[1].stride == 0xf2,
              "fixed N_PANGFX / H_PAN2 / N_SYSG_2 strides");

        IgnCar cars[4];
        memset(cars, 0, sizeof cars);
        for (int i = 0; i < 4; i++) cars[i].id = i, cars[i].position = i + 1, cars[i].turbo_charge = 100.0;
        const IgnCar *all[4] = {&cars[0], &cars[1], &cars[2], &cars[3]};
        IgnHudState st = {.cars = all, .ncars = 4, .level = &lv, .remaining = 4, .clock = 75.0, .total = 75.0,
                          .best_lap = 1e9};
        ign_frame_init(&frame, w, h);
        ign_hud_update(&hud, &st);
        /* Countdown: the first light of the four stacked in N_PANGFX. */
        memset(frame.color, 0, sizeof frame.color);
        ign_hud_draw(&hud, &frame, &st);
        int lx = w / 2 - 96 / 2, ly = (layout == 2 ? 60 : 30);
        CHECK(differs_from(0, lx, ly, lx + 104, ly + 33) > 500, "start lights drawn (%s)", NAMES[layout]);
        if (out) {
            char name[64];
            snprintf(name, sizeof name, "hud_%s_countdown", NAMES[layout]);
            write_ppm(out, name, &lv.palette);
        }
        /* Racing on lap 2 with a running turbo and the gauges moving. */
        st.clock = -1.0;
        st.total = 400.0;
        cars[0].lap = 1;
        cars[0].lap_times[0] = 61.25;
        cars[0].lap_elapsed = 12.5;
        cars[0].race_elapsed = 73.75;
        cars[0].gear = 2;
        cars[0].revs = 80.0;
        cars[0].turbo_charge = 55.0;
        cars[0].body.v_fwd = 40.0;
        ign_hud_update(&hud, &st);
        CHECK(hud.car[0].gear_anim == 8.0 && fabs(hud.car[0].needle - 216.0) < 1e-6, "gauges reach their targets");
        memset(frame.color, 0, sizeof frame.color);
        ign_hud_draw(&hud, &frame, &st);
        /* The bar (colour 0xd7) is seen through the glass of the turbo frame, so compare two charges. */
        static uint8_t full[IGN_MAX_W * IGN_MAX_H];
        memcpy(full, frame.color, sizeof full);
        cars[0].turbo_charge = 0.0;
        memset(frame.color, 0, sizeof frame.color);
        ign_hud_draw(&hud, &frame, &st);
        int bar_changed = 0;
        for (int i = 0; i < w * h; i++) bar_changed += full[i] != frame.color[i] && i % w >= w - 40;
        CHECK(bar_changed > 20, "turbo bar follows the charge (%d pixels)", bar_changed);
        cars[0].turbo_charge = 55.0;
        memcpy(frame.color, full, sizeof full);
        /* Primitive colours go through the shade table: PAN[0xc4 * 256] (146 in USA). */
        CHECK(count_colour(hud.pan[0xc4 << 8], w - 120, h - 120, w, h) > 0, "needle colour PAN[0xc400]");
        if (out) {
            char name[64];
            snprintf(name, sizeof name, "hud_%s_racing", NAMES[layout]);
            write_ppm(out, name, &lv.palette);
        }
        /* After the finish: first place, the medal grows for 25 frames and the HUD goes after 120. */
        cars[0].out = 1;
        cars[0].finish_position = 1;
        cars[0].lap = 3;
        cars[0].lap_times[1] = 60.5;
        cars[0].lap_times[2] = 59.75;
        st.total = 401.0;
        ign_hud_update(&hud, &st);
        st.total = 440.0;
        ign_hud_update(&hud, &st);
        CHECK(hud.car[0].medal_zoom == 25 && hud.finish_frames == 40, "finish clocks %d %d", hud.car[0].medal_zoom,
              hud.finish_frames);
        memset(frame.color, 0, sizeof frame.color);
        ign_hud_draw(&hud, &frame, &st);
        CHECK(differs_from(0, w / 2, h / 2, w / 2 + 89, h / 2 + 84) > 1000, "first place medal");
        if (out) {
            char name[64];
            snprintf(name, sizeof name, "hud_%s_finished", NAMES[layout]);
            write_ppm(out, name, &lv.palette);
        }
        st.total = 530.0;
        ign_hud_update(&hud, &st);
        memset(frame.color, 0, sizeof frame.color);
        ign_hud_draw(&hud, &frame, &st);
        CHECK(differs_from(0, 0, 0, w, h) == 0, "no HUD 120 frames after the finish");

        /* Elimination: a car goes out (message for 80 frames, blinking every 8) and the player is last. */
        static IgnHud elim;
        ign_hud_init(&elim, dir, "USA", w, h);
        memset(cars, 0, sizeof cars);
        for (int i = 0; i < 4; i++) cars[i].id = i, cars[i].position = 4 - i;
        st = (IgnHudState){.cars = all, .ncars = 4, .level = &lv, .mode = IGN_MODE_ELIMINATION, .remaining = 4,
                           .clock = -1.0, .total = 300.0, .best_lap = 1e9};
        ign_hud_update(&elim, &st);
        cars[3].out = 1;
        cars[3].wrecked = 1;
        cars[3].blow = 1.0;
        cars[3].position = 4;
        cars[0].position = 3;
        st.remaining = 3;
        st.total = 301.0;
        ign_hud_update(&elim, &st);
        CHECK(elim.msg[0].active && !strcmp(elim.msg[0].text, "ENF IS OUT") && elim.msg[0].duration == 80 &&
                  elim.msg[0].period == 8,
              "elimination message '%s'", elim.msg[0].text);
        CHECK(elim.msg[1].active && !strcmp(elim.msg[1].text, "WARNING, YOU'RE LAST!") && elim.msg[1].duration == 0,
              "last-place warning '%s'", elim.msg[1].text);
        memset(frame.color, 0, sizeof frame.color);
        ign_hud_draw(&elim, &frame, &st);
        int line = layout == 0 ? 8 : 16;
        CHECK(differs_from(0, w / 2 - 40, h - 2 * line, w / 2 + 40, h) > 50, "messages drawn above the bottom");
        if (out) {
            char name[64];
            snprintf(name, sizeof name, "hud_%s_elimination", NAMES[layout]);
            write_ppm(out, name, &lv.palette);
        }
        st.total = 309.0;
        ign_hud_update(&elim, &st);
        CHECK(elim.msg[0].active && elim.msg[0].timer == 8 && fmod(elim.msg[0].timer, 16.0) >= 8.0,
              "message hidden in the second half of its period");
        CHECK(fabs(elim.car[3].blow - (1.0 + 8 * 0.3)) < 1e-9, "explosion frame %.2f", elim.car[3].blow);
        st.total = 400.0;
        ign_hud_update(&elim, &st);
        int out_left = 0;
        for (int i = 0; i < IGN_HUD_MESSAGES; i++) out_left += elim.msg[i].active && strstr(elim.msg[i].text, "OUT");
        CHECK(!out_left, "message gone after 80 frames");
        ign_hud_destroy(&elim);
        ign_hud_destroy(&hud);
    }
    ign_free_level(&lv);
}

/* --triangles: reads "x0 y0 x1 y1 x2 y2 cx0 cy0 cx1 cy1" lines (clip x1/y1 exclusive) and prints the spans filled
 * in a 320x200 frame as "y:l-r ..." for tests/hud_original.py. */
static int triangles(void) {
    int t[6], c[4];
    ign_frame_init(&frame, 320, 200);
    while (scanf("%d %d %d %d %d %d %d %d %d %d", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &c[0], &c[1], &c[2],
                 &c[3]) == 10) {
        memset(frame.color, 0, sizeof frame.color);
        IgnClip clip = {c[0], c[1], c[2], c[3]};
        ign_fill_triangle(&frame, &clip, t, 1, NULL);
        for (int y = 0; y < 200; y++) {
            int l = -1, r = -1;
            for (int x = 0; x < 320; x++)
                if (frame.color[y * 320 + x]) {
                    if (l < 0) l = x;
                    r = x;
                }
            if (l >= 0) printf("%d:%d-%d ", y, l, r);
        }
        printf("\n");
    }
    return 0;
}

/* Deterministic page, blend table and background shared with tests/hud_original.py. */
static uint8_t mix(uint32_t v) {
    v ^= v >> 15;
    v *= 0x2c1b3c6du;
    v ^= v >> 12;
    v *= 0x297a2d39u;
    v ^= v >> 15;
    return (uint8_t)v;
}

/* --sprites: "u0 v0 w h m0 m1 m2 m3 x y cx0 cy0 cx1 cy1 seed" (x, y 8.8; clip x1/y1 exclusive); prints the FNV-1a
 * hash of the 320x200 frame after ign_sprite_draw_matrix. */
static int sprites(void) {
    static uint8_t page[65536], lut[65536];
    int u0, v0, w, h, m[4], x, y, c[4];
    unsigned seed;
    ign_frame_init(&frame, 320, 200);
    while (scanf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %u", &u0, &v0, &w, &h, &m[0], &m[1], &m[2], &m[3], &x, &y,
                 &c[0], &c[1], &c[2], &c[3], &seed) == 15) {
        for (uint32_t i = 0; i < 65536; i++) {
            page[i] = mix(i * 2654435761u + seed);
            if (page[i] < 40) page[i] = 0;
            lut[i] = (i >> 8) == 0 ? (uint8_t)i : mix(i + seed * 7919u);
        }
        for (int i = 0; i < 320 * 200; i++) frame.color[i] = (uint8_t)((i % 320) * 7 + (i / 320) * 13);
        IgnClip clip = {c[0], c[1], c[2], c[3]};
        const int32_t mat[4] = {m[0], m[1], m[2], m[3]};
        ign_sprite_draw_matrix(&frame, &clip, page, u0, v0, w, h, mat, x, y, lut);
        uint64_t hash = 1469598103934665603ull;
        for (int i = 0; i < 320 * 200; i++) hash = (hash ^ frame.color[i]) * 1099511628211ull;
        printf("%016llx\n", (unsigned long long)hash);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--triangles")) return triangles();
    if (argc > 1 && !strcmp(argv[1], "--sprites")) return sprites();
    test_lft();
    test_fnt();
    test_sprites();
    test_time();
    if (argc > 1) test_game_data(argv[1], argc > 2 ? argv[2] : NULL);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("hud assets: ok\n");
    return 0;
}
