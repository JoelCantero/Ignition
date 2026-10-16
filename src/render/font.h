#ifndef IGN_FONT_H
#define IGN_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "sprite.h"

#define IGN_FONT_GLYPHS 224 /* characters 32..255 */

/* One of the 30 font slots of 0x24c214 (0x63e bytes), filled by sub_00061399 from an LFT file. */
typedef struct {
    uint8_t *data;    /* the whole LFT file; glyph sprites point into it */
    size_t size;
    int align;        /* +0x04: 0 left, 1 centred on x, 2 right-aligned at x */
    int disabled;     /* +0x08: draw and width return 2 without doing anything */
    int proportional; /* +0x0c: 0 = fixed cells of `width`, 1 = glyph widths */
    int spacing;      /* +0x10: added after every glyph; 1 after sub_00061220 */
    int count;        /* +0x18: LFT +6, glyphs present */
    int width;        /* +0x1a: LFT +8, cell width */
    int height;       /* +0x1c: LFT +0x0a */
    uint8_t present[IGN_FONT_GLYPHS]; /* +0x1e: offset != -1 */
    int16_t glyph_w[IGN_FONT_GLYPHS];  /* +0x47e: LFT +0x68c, signed */
    IgnSprite glyph[IGN_FONT_GLYPHS];  /* +0xfe: sprite handles over LFT +0x84c + offset */
} IgnFont;

/* sub_00061399 on a file already in memory: "LFT\0", u16 100, then the header and glyph tables. Takes ownership of
 * `data` on success. Returns 0, or -1 for a malformed file (glyph rectangles past the end are rejected). */
int ign_font_parse_lft(IgnFont *font, uint8_t *data, size_t size);
/* sub_000615eb: load_file + sub_00061399. */
int ign_font_load_lft(IgnFont *font, const char *path);
void ign_font_free(IgnFont *font);
/* 0x61319's occupied-slot sweep over caller-owned records. No DOS global registry or shutdown callback;
 * each owner passes its own bank. Glyphs borrow LFT bytes, so free 0x61653 releases the backing file once. */
void ign_font_free_all(IgnFont *fonts, size_t count);

/* sub_000616db: advance of the text in pixels (fixed cells: width + spacing per glyph or space; proportional:
 * glyph width + spacing, spaces trunc(width * 0.35)). Characters without a glyph are skipped. */
int ign_font_text_width(const IgnFont *font, const char *text);
/* sub_00061959: draws the text with its top-left (alignment 0), centre (1) or right end (2) at x. Fixed cells centre
 * narrower glyphs ((width - glyph) / 2). Glyphs are sprites, so they go through the blend table like any sprite. */
void ign_font_draw(IgnFrame *fb, const IgnClip *clip, const IgnFont *font, const char *text, int x, int y,
                   const uint8_t *lut);

/* FONTS\IGNITION.FNT (load_fonts 0x240f4, [0x1feff4]), drawn by sub_00053d60 for the debug overlay 0x49fdc:
 * u16 glyph count, u16 height, i16 spacing, u8 widths[192] at +6, u8 map[256] at +0xc6 (character -> glyph),
 * u32 offsets[224] at +0x1c6, bitmaps at +0x546 (width x height bytes per glyph). */
typedef struct {
    uint8_t *data;
    size_t size;
    int count, height, spacing;
} IgnFnt;

int ign_fnt_parse(IgnFnt *fnt, uint8_t *data, size_t size);
int ign_fnt_load(IgnFnt *fnt, const char *path);
void ign_fnt_free(IgnFnt *fnt);
int ign_fnt_text_width(const IgnFnt *fnt, const char *text);
/* sub_00053d60: nonzero texels become texel + color; the pen advances glyph width + spacing. */
void ign_fnt_draw(IgnFrame *fb, const IgnFnt *fnt, const char *text, int x, int y, uint8_t color);

#endif
