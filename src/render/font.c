#include "font.h"

#include <stdlib.h>
#include <string.h>

#define LFT_OFFSETS 0x0c
#define LFT_WIDTHS 0x68c
#define LFT_BITMAPS 0x84c
#define FNT_WIDTHS 0x06
#define FNT_MAP 0xc6
#define FNT_OFFSETS 0x1c6
#define FNT_BITMAPS 0x546
#define FNT_MAX_WIDTHS (FNT_MAP - FNT_WIDTHS)

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int ign_font_parse_lft(IgnFont *font, uint8_t *data, size_t size) {
    memset(font, 0, sizeof *font);
    /* sub_00061399: strcmp(file, "LFT") and version 100 (errors 0x41a and 0x424). */
    if (!data || size < LFT_BITMAPS || memcmp(data, "LFT", 4) || rd16(data + 4) != 100) return -1;
    font->count = (int16_t)rd16(data + 6);
    font->width = (int16_t)rd16(data + 8);
    font->height = (int16_t)rd16(data + 10);
    if (font->height <= 0) return -1;
    size_t bitmaps = size - LFT_BITMAPS;
    for (int i = 0; i < IGN_FONT_GLYPHS; i++) {
        uint32_t offset = rd32(data + LFT_OFFSETS + i * 4);
        font->glyph_w[i] = (int16_t)rd16(data + LFT_WIDTHS + i * 2);
        if (offset == UINT32_MAX) continue;
        font->present[i] = 1;
        /* Widths are signed (movsx at 0x61558): MINI.LFT 'j' is -6 with its offset at the end of the file, so it
         * only moves the pen back. Such glyphs get no bitmap; other bitmaps must lie inside the file. */
        int w = font->glyph_w[i];
        if (w <= 0) continue;
        if (offset > bitmaps || (size_t)w * (size_t)font->height > bitmaps - offset) return -1;
        font->glyph[i] = (IgnSprite){data + LFT_BITMAPS + offset, w, font->height, w};
    }
    /* sub_00061220 slot defaults: left-aligned, enabled, fixed cells, one pixel of spacing. */
    font->spacing = 1;
    font->data = data;
    font->size = size;
    return 0;
}

int ign_font_load_lft(IgnFont *font, const char *path) {
    size_t size = 0;
    uint8_t *data = ign_read_file(path, &size);
    if (!data) {
        memset(font, 0, sizeof *font);
        return -1;
    }
    if (ign_font_parse_lft(font, data, size)) {
        free(data);
        return -1;
    }
    return 0;
}

void ign_font_free(IgnFont *font) {
    free(font->data);
    memset(font, 0, sizeof *font);
}

static int glyph_index(unsigned char ch) {
    return ch >= 32 ? ch - 32 : -1;
}

/* 0x61906 / 0x61d29: a proportional space is trunc((width << 8) * 0.35 * (1 / 256.0)). */
static int proportional_space(const IgnFont *font) {
    return (int)((double)(font->width << 8) * 0.35 * 0.00390625);
}

int ign_font_text_width(const IgnFont *font, const char *text) {
    if (font->disabled) return 2;
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int g = glyph_index(*p);
        if (g >= 0 && font->present[g])
            width += (font->proportional ? font->glyph_w[g] : font->width) + font->spacing;
        else if (*p == ' ')
            width += font->proportional ? proportional_space(font) : font->width + font->spacing;
    }
    return width;
}

void ign_font_draw(IgnFrame *fb, const IgnClip *clip, const IgnFont *font, const char *text, int x, int y,
                   const uint8_t *lut) {
    if (font->disabled) return;
    if (font->align) {
        int width = ign_font_text_width(font, text);
        x = font->align == 1 ? x - width / 2 : font->align == 2 ? x - width : x;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int g = glyph_index(*p);
        if (g >= 0 && font->present[g]) {
            int w = font->glyph_w[g];
            if (font->proportional) {
                ign_sprite_draw(fb, clip, &font->glyph[g], x, y, lut);
                x += w + font->spacing;
            } else {
                int centre = w != font->width ? (font->width - w) / 2 : 0;
                ign_sprite_draw(fb, clip, &font->glyph[g], x + centre, y, lut);
                x += font->width + font->spacing;
            }
        } else if (*p == ' ') {
            x += font->proportional ? proportional_space(font) : font->width + font->spacing;
        }
    }
}

int ign_fnt_parse(IgnFnt *fnt, uint8_t *data, size_t size) {
    memset(fnt, 0, sizeof *fnt);
    if (!data || size < FNT_BITMAPS) return -1;
    int count = rd16(data), height = (int16_t)rd16(data + 2);
    if (count <= 0 || count > FNT_MAX_WIDTHS || height <= 0) return -1;
    size_t bitmaps = size - FNT_BITMAPS;
    for (int c = 0; c < 256; c++) {
        int g = data[FNT_MAP + c];
        if (g >= count) continue; /* the map sends unused characters past the table; they are not drawable */
        uint32_t offset = rd32(data + FNT_OFFSETS + g * 4);
        size_t w = data[FNT_WIDTHS + g];
        if (offset > bitmaps || w * (size_t)height > bitmaps - offset) return -1;
    }
    fnt->count = count;
    fnt->height = height;
    fnt->spacing = (int16_t)rd16(data + 4);
    fnt->data = data;
    fnt->size = size;
    return 0;
}

int ign_fnt_load(IgnFnt *fnt, const char *path) {
    size_t size = 0;
    uint8_t *data = ign_read_file(path, &size);
    if (!data) {
        memset(fnt, 0, sizeof *fnt);
        return -1;
    }
    if (ign_fnt_parse(fnt, data, size)) {
        free(data);
        return -1;
    }
    return 0;
}

void ign_fnt_free(IgnFnt *fnt) {
    free(fnt->data);
    memset(fnt, 0, sizeof *fnt);
}

int ign_fnt_text_width(const IgnFnt *fnt, const char *text) {
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int g = fnt->data[FNT_MAP + *p];
        if (g < fnt->count) width += fnt->data[FNT_WIDTHS + g] + fnt->spacing;
    }
    return width;
}

void ign_fnt_draw(IgnFrame *fb, const IgnFnt *fnt, const char *text, int x, int y, uint8_t color) {
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int g = fnt->data[FNT_MAP + *p];
        if (g >= fnt->count) continue;
        int w = fnt->data[FNT_WIDTHS + g];
        const uint8_t *bitmap = fnt->data + FNT_BITMAPS + rd32(fnt->data + FNT_OFFSETS + g * 4);
        for (int row = 0; row < fnt->height; row++) {
            int py = y + row;
            if (py < 0 || py >= fb->h) continue;
            for (int col = 0; col < w; col++) {
                int px = x + col;
                uint8_t t = bitmap[row * w + col];
                if (t && px >= 0 && px < fb->w) fb->color[py * fb->w + px] = (uint8_t)(t + color);
            }
        }
        x += w + fnt->spacing;
    }
}
