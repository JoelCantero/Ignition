#ifndef IGN_MENU_DATA_H
#define IGN_MENU_DATA_H

/* Static data of the Baltazar menu framework (0x51b20..0x539f8) and of the frontend, extracted from MAINDOS.EXE by
 * tools/gen_menu_data.py into menu_data.c. Pointers of the original are indices here; value and edit destinations
 * are offsets in the settings block g_cfg (settings.h). */

#include <stddef.h>
#include <stdint.h>

#define IGN_MENU_COUNT 145

/* Element (0x18 bytes): an unused 16x16 sprite slot (pixels always NULL in the shipped data) and a text. */
typedef struct {
    int16_t x, y, w, h, pitch;
    const char *text; /* +0x0e */
    int16_t tx, ty;   /* +0x12, +0x14: top-left, relative to the screen centre */
    int16_t font;     /* +0x16: font slot */
} IgnMenuElem;

/* EditDesc (0x16 bytes); the extra element pointer +0x00 is NULL in all data. */
typedef struct {
    int dest; /* +0x04: g_cfg offset of the string (type 1, 2) or of the scancode byte (type 3) */
    int16_t x, y, font_sel, font_unsel;
    uint8_t maxlen, cursor;
    int16_t blink_ms;
    int16_t type; /* 1 text, 2 text edited on page entry, 3 key binding */
} IgnMenuEdit;

/* Item (0x1c bytes). */
typedef struct {
    int value;          /* g_cfg offset, -1 = none */
    int32_t min, max;   /* max is patched at run time (difficulty, cars, CD track) */
    int elems;          /* index in ign_menu_elems, -1 = none; label pair, value pairs, then the disabled group */
    int16_t cb;         /* > 0: callbacks[cb - 1] on every key; -1: BACK item */
    int32_t arg;
    int edit;           /* index in ign_menu_edits, -1 = none */
    int16_t enabled;
} IgnMenuItem;

/* Menu (0x18 bytes); draw (0x170ac), key callback (0x160c4) and keymap (0xbee1c) are the same for every menu. */
typedef struct {
    int nitems;
    int items; /* index in ign_menu_items */
    int sel;   /* +0x14 */
} IgnMenuPage;

extern IgnMenuElem ign_menu_elems[];
extern const IgnMenuEdit ign_menu_edits[];
extern IgnMenuItem ign_menu_items[];
extern IgnMenuPage ign_menu_pages[IGN_MENU_COUNT];
extern const int16_t ign_menu_lang_pages[6][25]; /* 0xbe2bc: menu per language and page slot k */
extern const int16_t ign_menu_by_id[6][128];     /* 0xaeb6c + lang * 0x3110 */
extern const int ign_menu_root;                  /* 0xaeb54, the language page shared by all languages */
extern const int16_t ign_menu_transitions[25][16][2]; /* 0xbe514: {match, action} per depth */

extern int8_t ign_menu_keymap[256];               /* 0xbee1c, the live keymap of every menu (patched by 0x170ac) */
extern const int8_t ign_menu_keymap_pristine[256]; /* 0xbed1c, its original contents */
extern const int8_t ign_menu_keymap_default[256]; /* 0xc4860 */
extern const uint8_t ign_menu_bind_allowed[256];  /* 0xbef1c */
extern const uint8_t ign_menu_ascii[0x54];        /* 0xc4960, 0xff = none */

extern const char *const ign_menu_text[6][50];    /* 0xbf088 */
extern const char *const ign_menu_key_names[256]; /* 0xc49b4 */
extern const char *const ign_menu_credit_names[17]; /* 0xbf538 */
extern const int16_t ign_menu_slides[16][4];      /* 0xbf57c: {title text, name a, b, c} */
extern const char *const ign_menu_track_names[8]; /* 0xbf04c; [7] = "MOOSEJAW FALLS" */
extern const char *const ign_menu_driver_tags[11]; /* 0xbf5fc */
extern const char *const ign_menu_intro_clips[6]; /* 0xbed00 */
extern const char *const ign_menu_cheat_words[7]; /* 0xbf030 */
extern const int32_t ign_menu_track_thumbs[7];    /* 0xbf06c: TRK_SPR.PIC offsets per track item */

#endif
