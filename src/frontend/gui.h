#ifndef IGN_GUI_H
#define IGN_GUI_H

/* The game-independent "Baltazar" menu framework of MAINDOS.EXE: menu_start 0x51b20, menu_tick 0x51c94,
 * menu_draw_items 0x527c8, menu_free 0x52a0c, menu_set_input_lock 0x52a5c, menu_navigate 0x534cc,
 * menu_draw_element 0x5383c and menu_item_build_sprite 0x539f8 (a no-op: no element has sprite pixels). Menus, items
 * and transitions are the static data of menu_data.h. */

#include <stdint.h>

#include "gfx2d.h"
#include "menu_data.h"

#define IGN_GUI_CONSUMED 0x441

typedef struct {
    int w, h;                       /* 0x208074, 0x208078 */
    int root;                       /* 0x20807c: menu index */
    void (*const *callbacks)(int arg, int action); /* 0x208080: 1-based */
    uint8_t *key_latch;             /* 0x208084: u8[256] "pressed" latches, consumed (game injects keys here) */
    const uint8_t *bind_allowed;    /* 0x208088 */
    const int16_t *menu_by_id;      /* 0x20808c: 128 menu indices */
    int sound_pump;                 /* 0x208098 */
    IgnFont *const *fonts;          /* 0x20809c: font slot per element font index */
    int remember_sel;               /* 0x2080a8 */
    int (*key)(int sc, int action); /* page +4 (0x160c4) */
    void (*draw)(void);             /* page +0 (0x170ac) */
    void (*sound_update)(void);     /* snd_update 0x624cc */
    int (*timer_ms)(void);          /* g_timer_ms 0xab5dc */
} IgnGuiConfig;

/* Public snapshot 0x2080ac, published before every callback. */
typedef struct {
    int menu_stack[16], sel_stack[16];
    int cur, prev_menu, depth, sel, prev_sel; /* 0x20812c .. 0x20813c */
    int16_t editmode;                         /* 0x208140 */
    int page_entered;                         /* 0x208142: set by navigate, cleared at the next tick */
} IgnGuiPublic;

extern IgnGuiConfig g_gui;
extern IgnGuiPublic g_gui_pub;

static inline IgnMenuPage *ign_gui_page(int m) { return &ign_menu_pages[m]; }
static inline IgnMenuItem *ign_gui_item(int m, int i) { return &ign_menu_items[ign_menu_pages[m].items + i]; }

int ign_gui_start(void);             /* 0x51b20 */
int ign_gui_tick(void);              /* 0x51c94: -1 when BACK is selected at depth 0 */
int ign_gui_draw_items(void);        /* 0x527c8 */
int ign_gui_free(void);              /* 0x52a0c */
int ign_gui_set_input_lock(int ms);  /* 0x52a5c */

#endif
