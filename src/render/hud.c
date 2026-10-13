#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LFT_BITMAPS 0x84c
#define HUD_FONT_GLYPHS 224

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int load_font(IgnHudFont *font, const char *data_dir, const char *name) {
    char path[1024];
    if (snprintf(path, sizeof path, "%s/FONTS/%s.LFT", data_dir, name) >= (int)sizeof path) return -1;
    font->data = ign_read_file(path, &font->size);
    if (!font->data) return -1;
    if (font->size < LFT_BITMAPS || memcmp(font->data, "LFT\0", 4) || read_u16(font->data + 4) != 100) return -1;

    font->height = read_u16(font->data + 10);
    if (!font->height || 12 + HUD_FONT_GLYPHS * 4 > font->size ||
        0x68c + HUD_FONT_GLYPHS * 2 > font->size)
        return -1;
    for (int i = 0; i < HUD_FONT_GLYPHS; i++) {
        uint32_t offset = read_u32(font->data + 12 + i * 4);
        uint16_t width = read_u16(font->data + 0x68c + i * 2);
        if (offset != UINT32_MAX &&
            ((size_t)offset > font->size - LFT_BITMAPS ||
             (size_t)width * font->height > font->size - LFT_BITMAPS - offset))
            return -1;
        font->offset[i] = offset;
        font->width[i] = width;
    }
    return 0;
}

static void map_palette(const IgnPalette *source, const IgnPalette *target, uint8_t map[256]) {
    for (int i = 0; i < 256; i++) {
        unsigned best_distance = UINT32_MAX;
        int best = 0;
        for (int j = 0; j < 256; j++) {
            int dr = (int)source->rgb[i][0] - target->rgb[j][0];
            int dg = (int)source->rgb[i][1] - target->rgb[j][1];
            int db = (int)source->rgb[i][2] - target->rgb[j][2];
            unsigned distance = (unsigned)(dr * dr + dg * dg + db * db);
            if (distance < best_distance) best_distance = distance, best = j;
        }
        map[i] = (uint8_t)best;
    }
}

int ign_hud_init(IgnHud *hud, const char *data_dir, const IgnPalette *palette) {
    memset(hud, 0, sizeof *hud);
    char path[1024];
    if (snprintf(path, sizeof path, "%s/H_PAN1.PIC", data_dir) >= (int)sizeof path ||
        ign_load_pic(path, &hud->panel))
        goto fail;
    map_palette(&hud->panel.palette, palette, hud->palette_map);
    if (load_font(&hud->yellow, data_dir, "YELLOW") ||
        load_font(&hud->speed, data_dir, "SPEED") ||
        load_font(&hud->pos, data_dir, "POS_S"))
        goto fail;
    return 0;

fail:
    ign_hud_destroy(hud);
    return -1;
}

void ign_hud_destroy(IgnHud *hud) {
    ign_free_pic(&hud->panel);
    free(hud->yellow.data);
    free(hud->speed.data);
    free(hud->pos.data);
    memset(hud, 0, sizeof *hud);
}

static void draw_sprite(const IgnHud *hud, IgnFrame *frame, int sx, int sy, int sw, int sh,
                        int dx, int dy, float scale_x, float scale_y) {
    int dw = (int)lroundf(sw * scale_x), dh = (int)lroundf(sh * scale_y);
    if (dw < 1 || dh < 1) return;
    for (int y = 0; y < dh; y++) {
        int py = dy + y;
        if (py < 0 || py >= frame->h) continue;
        int source_y = sy + y * sh / dh;
        for (int x = 0; x < dw; x++) {
            int px = dx + x;
            if (px < 0 || px >= frame->w) continue;
            uint8_t color = hud->panel.pixels[source_y * hud->panel.width + sx + x * sw / dw];
            if (color) frame->color[py * frame->w + px] = hud->palette_map[color];
        }
    }
}

static const IgnHudFont *font_for_char(const IgnHud *hud, const IgnHudFont *font, unsigned char ch) {
    if (ch >= 32 && font->offset[ch - 32] != UINT32_MAX) return font;
    if (font != &hud->yellow && ch >= 32 &&
        hud->yellow.offset[ch - 32] != UINT32_MAX)
        return &hud->yellow;
    return NULL;
}

static int text_width(const IgnHud *hud, const char *text, const IgnHudFont *font, float scale_x) {
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == ' ') {
            width += (int)lroundf(font->height * scale_x / 2.0f);
            continue;
        }
        const IgnHudFont *selected = font_for_char(hud, font, *p);
        if (selected) width += (int)lroundf((selected->width[*p - 32] + 1) * scale_x);
    }
    return width;
}

static void draw_text(const IgnHud *hud, IgnFrame *frame, const char *text, const IgnHudFont *font,
                      int x, int y, float scale_x, float scale_y) {
    int pen_x = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == ' ') {
            pen_x += (int)lroundf(font->height * scale_x / 2.0f);
            continue;
        }
        const IgnHudFont *selected = font_for_char(hud, font, *p);
        if (!selected) continue;
        int index = *p - 32;
        int glyph_width = selected->width[index];
        const uint8_t *pixels = selected->data + LFT_BITMAPS + selected->offset[index];
        int dest_width = (int)lroundf(glyph_width * scale_x);
        int dest_height = (int)lroundf(selected->height * scale_y);
        if (dest_width < 1) dest_width = 1;
        if (dest_height < 1) dest_height = 1;
        for (int gy = 0; gy < dest_height; gy++) {
            int py = y + gy;
            if (py < 0 || py >= frame->h) continue;
            int source_y = gy * selected->height / dest_height;
            for (int gx = 0; gx < dest_width; gx++) {
                int px = pen_x + gx;
                if (px < 0 || px >= frame->w) continue;
                uint8_t color = pixels[source_y * glyph_width + gx * glyph_width / dest_width];
                if (color) frame->color[py * frame->w + px] = hud->palette_map[color];
            }
        }
        pen_x += (int)lroundf((glyph_width + 1) * scale_x);
    }
}

static void format_time(char out[16], double frames) {
    int centiseconds = (int)(frames * (100.0 / 36.0));
    if (centiseconds < 0) centiseconds = 0;
    int minutes = centiseconds / 6000;
    int seconds = centiseconds / 100 % 60;
    centiseconds %= 100;
    snprintf(out, 16, "%02d:%02d:%02d", minutes, seconds, centiseconds);
}

void ign_hud_draw(IgnHud *hud, IgnFrame *frame, const IgnCar *car, const IgnRace *race, int car_count) {
    /* sub_0004a320 lays the race HUD over the completed scene; sprite pixel 0 is the panel key color. */
    float scale_x = frame->w / 640.0f;
    float scale_y = frame->h / 480.0f;
    int origin_x = (int)lroundf(8 * scale_x);
    int origin_y = frame->h - (int)lroundf(256 * scale_y) - (int)lroundf(8 * scale_y);
    if (origin_y < 0) origin_y = 0;

    draw_sprite(hud, frame, 0, 0, 158, 22, origin_x, origin_y, scale_x, scale_y);
    draw_sprite(hud, frame, 0, 22, 132, 22, origin_x, origin_y + (int)lroundf(22 * scale_y),
                scale_x, scale_y);
    draw_sprite(hud, frame, 0, 44, 64, 24, origin_x, origin_y + (int)lroundf(44 * scale_y),
                scale_x, scale_y);
    draw_sprite(hud, frame, 160, 0, 45, 45, origin_x + (int)lroundf(160 * scale_x), origin_y,
                scale_x, scale_y);
    draw_sprite(hud, frame, 160, 46, 45, 44, origin_x + (int)lroundf(160 * scale_x),
                origin_y + (int)lroundf(46 * scale_y), scale_x, scale_y);
    draw_sprite(hud, frame, 0, 72, 160, 128, origin_x, origin_y + (int)lroundf(72 * scale_y),
                scale_x, scale_y);
    draw_sprite(hud, frame, 160, 91, 44, 132, origin_x + (int)lroundf(160 * scale_x),
                origin_y + (int)lroundf(91 * scale_y), scale_x, scale_y);

    if (ign_race_started(race)) {
        if (!hud->race_started) {
            hud->lap_start = race->total;
            hud->observed_lap = car->lap;
            hud->race_started = 1;
        } else if (car->lap != hud->observed_lap) {
            hud->last_lap = race->total - hud->lap_start;
            hud->lap_start = race->total;
            hud->observed_lap = car->lap;
        }
    }

    char total_time[16], lap_time[16], place[16], speed[8];
    format_time(total_time, race->total);
    format_time(lap_time, hud->race_started ? race->total - hud->lap_start : 0);
    int place_number = car->position > 0 ? car->position : 1;
    if (car_count < 1) car_count = 1;
    snprintf(place, sizeof place, "%d/%d", place_number, car_count);
    int kmh = (int)lroundf(car->kmh);
    if (kmh < 0) kmh = 0;
    snprintf(speed, sizeof speed, "%d", kmh);

    int text_x = origin_x + (int)lroundf(207 * scale_x);
    draw_text(hud, frame, total_time, &hud->yellow, text_x, origin_y + (int)lroundf(13 * scale_y),
              scale_x, scale_y);
    draw_text(hud, frame, lap_time, &hud->yellow, text_x, origin_y + (int)lroundf(59 * scale_y),
              scale_x, scale_y);
    int place_width = text_width(hud, place, &hud->pos, scale_x);
    draw_text(hud, frame, place, &hud->pos,
              origin_x + (int)lroundf(72 * scale_x) - place_width / 2,
              origin_y + (int)lroundf(43 * scale_y), scale_x, scale_y);

    int speed_width = text_width(hud, speed, &hud->speed, scale_x);
    draw_text(hud, frame, speed, &hud->speed,
              origin_x + (int)lroundf(112 * scale_x) - speed_width / 2,
              origin_y + (int)lroundf(159 * scale_y), scale_x, scale_y);
}
