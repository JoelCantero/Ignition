/* Phase-four resource/message/sizing helpers. --vectors feeds phase4_helpers_original.py; otherwise
 * phase4_helpers_test [GAME_DIR [INCOMPLETE_GAME_DIR]] runs synthetic and optional real-asset checks. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/frontend.h"
#include "frontend/pfm.h"
#include "render/hud.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}

static uint64_t hash(const void *ptr, size_t size) {
    const uint8_t *p = ptr;
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}

static void message_fixture(IgnFrontendMessages *m, unsigned mask) {
    memset(m, 0, sizeof *m);
    for (int i = 0; i < IGN_FRONTEND_MESSAGES; i++) {
        m->time[i] = mask & (1u << i) ? (double)(i & 1 ? -i - 1 : i + 1) : -0.0;
        strcpy(m->text[i], "occupied");
    }
    if (mask == 1023) m->time[0] = NAN;
}

static void vectors(void) {
    static const uint32_t lengths[] = {0, 1, 4, 64, 0x15555555u, 0x40000000u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    static const int32_t maxima[] = {-17, 0, 12, INT32_MAX};
    for (size_t i = 0; i < sizeof lengths / sizeof *lengths; i++)
        for (size_t j = 0; j < sizeof maxima / sizeof *maxima; j++) {
            uint8_t header[8] = {'l', 'i', '0', '0'};
            put32(header + 4, lengths[i]);
            IgnPfmWorkspace w = {maxima[j], maxima[j]};
            ign_pfm_workspace_include(&w, header);
            header[3] = '1';
            ign_pfm_workspace_include(&w, header);
            printf("workspace %u %d %d %d\n", lengths[i], maxima[j], w.primitives, w.vertices);
        }
    for (int mode = 0; mode < 3; mode++)
        for (int view = 0; view < 4; view++)
            for (int split = 0; split < 2; split++)
                printf("layout %d %d %d %d\n", mode, view, split, ign_hud_select_layout(mode, view, split));
    for (unsigned mask = 0; mask < 1024; mask += 11) {
        IgnFrontendMessages m;
        message_fixture(&m, mask);
        ign_frontend_messages_post(&m, "new", 3.25);
        printf("message %u %016llx\n", mask, (unsigned long long)hash(&m, sizeof m));
    }
    IgnFrontendMessages m;
    message_fixture(&m, 1023);
    ign_frontend_messages_post(&m, "new", 3.25);
    printf("message 1023 %016llx\n", (unsigned long long)hash(&m, sizeof m));
    for (int layout = 0; layout < 3; layout++) {
        IgnHud hud = {.layout = layout};
        IgnPic *pics[] = {&hud.pangfx_s, &hud.pangfx_n, &hud.pan1, &hud.pan2, &hud.signs};
        for (int i = 0; i < 5; i++) {
            pics[i]->width = i == 0 ? 123 : i == 1 ? 227 : i == 4 ? (layout == 0 ? 56 : layout == 1 ? 112 : 256) : 256;
            pics[i]->height = 1024;
            pics[i]->pixels = calloc((size_t)pics[i]->width, 1024);
            if (!pics[i]->pixels) exit(1);
        }
        if (ign_hud_rebuild_sprites(&hud)) exit(1);
        IgnSprite sprites[37] = {hud.panel_total, hud.panel_lap, hud.panel_pos, hud.dial, hud.needle_cap};
        int n = 5;
        for (int i = 0; i < 9; i++) sprites[n++] = hud.gear[i];
        sprites[n++] = hud.turbo_frame;
        for (int i = 0; i < 2; i++) sprites[n++] = hud.turbo_light[i];
        for (int i = 0; i < 4; i++) sprites[n++] = hud.countdown[i];
        for (int i = 0; i < 16; i++) sprites[n++] = hud.sign[i];
        for (int i = 0; i < n; i++) {
            int bank = -1;
            size_t offset = 0;
            for (int k = 0; k < 5; k++) {
                uintptr_t base = (uintptr_t)pics[k]->pixels, pixel = (uintptr_t)sprites[i].pixels;
                if (pixel >= base && pixel - base < (size_t)pics[k]->width * 1024) {
                    bank = k;
                    offset = pixel - base;
                }
            }
            printf("sprite %d %d %d %zu %d %d %d\n", layout, i, bank, offset, sprites[i].w, sprites[i].h,
                   sprites[i].stride);
        }
        for (int i = 0; i < 5; i++) ign_free_pic(pics[i]);
    }
}

static void messages(void) {
    IgnFrontendMessages m = {0};
    CHECK(ign_frontend_messages_post(&m, "first", 1) == 0);
    CHECK(ign_frontend_messages_post(&m, "second", -1) == 1);
    m.time[0] = -0.0;
    CHECK(ign_frontend_messages_post(&m, "reuse", 4) == 0);
    CHECK(!strcmp(m.text[0], "reuse") && m.time[0] == 4);
    for (int i = 2; i < 10; i++) CHECK(ign_frontend_messages_post(&m, "fill", i) == i);
    CHECK(ign_frontend_messages_post(&m, "replace", 0) == 9);
    CHECK(ign_frontend_messages_post(&m, "replace again", 1) == 9);
    char long_text[512];
    memset(long_text, 'x', sizeof long_text);
    long_text[sizeof long_text - 1] = 0;
    CHECK(ign_frontend_messages_post(&m, long_text, 2) == 9);
    CHECK(strlen(m.text[9]) == 255 && m.time[8] == 8 && m.time[9] == 2);
}

static void fonts(void) {
    IgnFont bank[30] = {0};
    for (int k = 0; k < 30; k += 7) {
        size_t size = 0x84e;
        uint8_t *data = calloc(1, size);
        CHECK(data != NULL);
        if (!data) return;
        memcpy(data, "LFT", 4);
        data[4] = 100;
        data[6] = data[8] = data[10] = 1;
        for (int i = 0; i < IGN_FONT_GLYPHS; i++) put32(data + 12 + 4 * i, UINT32_MAX);
        put32(data + 12, 0);
        data[0x68c] = 1;
        data[0x84c] = 17;
        CHECK(ign_font_parse_lft(&bank[k], data, size) == 0);
        CHECK(bank[k].present[0] && bank[k].glyph[0].pixels[0] == 17);
    }
    ign_font_free(&bank[0]);
    ign_font_free(&bank[0]);
    ign_font_free_all(bank, 30);
    ign_font_free_all(bank, 30);
    const IgnFont empty = {0};
    for (int i = 0; i < 30; i++) CHECK(!memcmp(&bank[i], &empty, sizeof empty));
}

static void workspace(void) {
    uint8_t data[8 + 12 + 8 + 20 + 8 + 4 + 8 + 4 + 8] = {0};
    size_t o = 0;
    static const char *const tags[] = {"li01", "li00", "li01", "li00", "lixx"};
    static const int sizes[] = {12, 20, 4, 4, 0};
    for (int i = 0; i < 5; i++) {
        memcpy(data + o, tags[i], 4);
        put32(data + o + 4, (uint32_t)sizes[i]);
        o += (size_t)sizes[i] + 8;
    }
    uint8_t *pages[] = {NULL};
    const uint8_t *shades[] = {NULL};
    IgnPfm *p = ign_pfm_load(data, sizeof data, pages, shades);
    CHECK(p != NULL && ign_pfm_frames(p) == 2);
    IgnPfmWorkspace w = ign_pfm_workspace(p);
    CHECK(w.primitives == 120 && w.vertices == 24);
    uint8_t unknown[8] = {'l', 'i', '0', '2'};
    put32(unknown + 4, 1024);
    ign_pfm_workspace_include(&w, unknown);
    CHECK(w.primitives == 120 && w.vertices == 24);
    ign_pfm_free(p);
    CHECK(ign_pfm_workspace(NULL).primitives == 0);
}

static void layouts(void) {
    static const int table[3][8] = {{1, 0, 0, 0, 0, 0, 0, 0}, {2, 1, 1, 1, 1, 1, 1, 0},
                                  {2, 1, 1, 1, 1, 1, 1, 0}};
    for (int mode = 0; mode < 3; mode++)
        for (int view = 0; view < 4; view++)
            for (int split = 0; split < 2; split++)
                CHECK(ign_hud_select_layout(mode, view, split) == table[mode][view * 2 + split]);
    CHECK(ign_hud_select_layout(-1, 0, 0) == -1);
    CHECK(ign_hud_select_layout(3, 0, 0) == -1);
    CHECK(ign_hud_select_layout(0, 4, 0) == -1);
    CHECK(ign_hud_select_layout(0, 0, 2) == -1);
}

static void assets(const char *dir, const char *incomplete_dir) {
    IgnFrontendHost host = {0};
    CHECK(ign_frontend_init(dir, &host) == 0);
    const IgnMenuSprite *loading = ign_frontend_loading_sprite();
    const IgnPic *sysg2 = ign_frontend_system_picture();
    CHECK(loading && loading->s.w == 61 && loading->s.h == 58);
    CHECK(sysg2 && sysg2->pixels && ign_frontend_system_palette());
    CHECK(!ign_frontend_font(-1) && !ign_frontend_font(8));
    for (int slot = 0; slot < 8; slot++)
        CHECK(ign_frontend_font(slot) && ign_frontend_font(slot)->proportional == 1 &&
              ign_frontend_font(slot)->spacing == 1);
    char path[1400];
    snprintf(path, sizeof path, "%s/N_SYSGFX.PIC", dir);
    size_t size;
    uint8_t *raw = ign_read_file(path, &size);
    CHECK(raw && size >= 0xc1e7 + 58 * 256);
    if (raw && loading)
        for (int row = 0; row < 58; row++)
            CHECK(!memcmp(loading->px + row * 61, raw + 0xc1e7 + row * 256, 61));
    free(raw);
    snprintf(path, sizeof path, "%s/SYS.COL", dir);
    raw = ign_read_file(path, &size);
    IgnPalette pal;
    CHECK(raw && !ign_parse_col(raw, size, &pal));
    if (raw && ign_frontend_system_palette()) CHECK(!memcmp(&pal, ign_frontend_system_palette(), sizeof pal));
    free(raw);
    const uint8_t *lut = ign_gfx_identity_lut();
    for (int s = 0; s < 256; s++)
        for (int d = 0; d < 256; d++) CHECK(lut[s * 256 + d] == s);
    static uint8_t frame[320 * 200];
    for (int repeat = 0; repeat < 3; repeat++) {
        for (int i = 0; i < 100; i++) ign_menubox_add(10, i % 6, 70, 0, i & 1);
        ign_menubox_clear();
        memset(frame, 73, sizeof frame);
        ign_menubox_draw(frame, 320, 320, 200);
        for (size_t i = 0; i < sizeof frame; i++) CHECK(frame[i] == 73);
    }
    ign_menubox_add(10, 0, 70, 0, 0);
    snprintf(path, sizeof path, "%s/MENUBKG.DAT", dir);
    CHECK(!ign_menubox_init(path)); /* reinitialisation must release old nodes */
    memset(frame, 73, sizeof frame);
    ign_menubox_draw(frame, 320, 320, 200);
    for (size_t i = 0; i < sizeof frame; i++) CHECK(frame[i] == 73);
    CHECK(ign_frontend_init(dir, &host) == 0);
    ign_frontend_destroy();
    ign_frontend_destroy();
    CHECK(!ign_frontend_loading_sprite() && !ign_frontend_system_picture() && !ign_frontend_system_palette());
    for (int slot = 0; slot < 8; slot++) CHECK(!ign_frontend_font(slot));
    if (incomplete_dir) {
        CHECK(ign_frontend_init(incomplete_dir, &host) == -1);
        CHECK(!ign_frontend_loading_sprite() && !ign_frontend_system_picture() && !ign_frontend_system_palette());
        ign_frontend_destroy();
        CHECK(ign_frontend_init(dir, &host) == 0);
        ign_frontend_destroy();
    }
    for (int mode = 0; mode < 3; mode++)
        for (int view = 0; view < 4; view++)
            for (int players = 1; players <= 2; players++) {
                IgnHud hud;
                int rc = ign_hud_init_view(&hud, dir, "USA", mode, view, players);
                CHECK(rc == 0);
                if (rc) continue;
                CHECK(hud.layout == ign_hud_select_layout(mode, view, players - 1));
                const uint8_t *panel = hud.panel_total.pixels, *marker = hud.marker[0].pixels;
                const uint8_t *font = hud.message_font.data;
                snprintf(path, sizeof path, "%s/FONTS/%s.LFT", dir, mode ? "SMALL" : "MINI");
                raw = ign_read_file(path, &size);
                CHECK(raw && size == hud.message_font.size);
                if (raw && size == hud.message_font.size) CHECK(!memcmp(raw, font, size));
                free(raw);
                hud.msg[0].active = 1;
                hud.car[0].needle = 123;
                memset(&hud.panel_total, 0, sizeof hud.panel_total);
                CHECK(!ign_hud_rebuild_sprites(&hud));
                CHECK(hud.panel_total.pixels == panel && hud.marker[0].pixels == marker);
                CHECK(hud.msg[0].active == 1 && hud.car[0].needle == 123 && hud.message_font.data == font);
                CHECK(hud.sign[15].pixels && hud.countdown[3].pixels && hud.gear[8].pixels);
                ign_hud_destroy(&hud);
                ign_hud_destroy(&hud);
                CHECK(!hud.pan && !hud.panel_total.pixels && !hud.sign[15].pixels && !hud.time_font.data);
            }
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--vectors")) {
        vectors();
        return 0;
    }
    messages();
    fonts();
    workspace();
    layouts();
    if (argc > 1) assets(argv[1], argc > 2 ? argv[2] : NULL);
    printf("phase4 helpers: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
