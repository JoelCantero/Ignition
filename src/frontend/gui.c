#include "gui.h"

#include <stdlib.h>
#include <string.h>

#include "settings.h"

IgnGuiConfig g_gui;
IgnGuiPublic g_gui_pub;

static struct {
    int first_tick;            /* 0x208070 */
    double last_t, dt;         /* 0x207e08, 0x207e10: 25 Hz ticks */
    const int8_t *keymap;      /* 0x207e48 */
    int menu_stack[16];        /* 0x208148 */
    int sel_stack[16];         /* 0x208188 */
    int depth;                 /* 0x2081c8 */
    int last_row;              /* 0x2081cc */
    int sel, prev_sel;         /* 0x2081d0, 0x2081d4 */
    int cur, prev_menu;        /* 0x2081d8, 0x2081dc */
    int input_lock;            /* 0x208370 */
    int exit_req;              /* 0x208374 */
    int16_t editmode;          /* 0x208378: 0 none, 1 text, 2 auto text, 3 key capture */
    int edit_len;              /* 0x20837c */
    char edit_buf[258];        /* 0x208380 */
} s;

static int32_t value_of(const IgnMenuItem *it) { return ign_cfg_get(it->value); }
static void set_value(const IgnMenuItem *it, int32_t v) { ign_cfg_set(it->value, v); }
static char *edit_dest(const IgnMenuEdit *e) { return (char *)g_cfg + e->dest; }
static IgnMenuItem *item(int i) { return ign_gui_item(s.cur, i); }

/* sub_00053390 (inlined everywhere) */
static void publish(void) {
    for (int i = 0; i < 16; i++) {
        g_gui_pub.menu_stack[i] = s.menu_stack[i];
        g_gui_pub.sel_stack[i] = s.sel_stack[i];
    }
    g_gui_pub.cur = s.cur;
    g_gui_pub.prev_menu = s.prev_menu;
    g_gui_pub.depth = s.depth;
    g_gui_pub.sel = s.sel;
    g_gui_pub.prev_sel = s.prev_sel;
    g_gui_pub.editmode = s.editmode;
}

/* sub_000539f8: every shipped element has NULL sprite pixels, so no sprite is ever built. */
static int build_sprite(int i, int slot, int selected) {
    (void)slot;
    (void)selected;
    IgnMenuItem *it = item(i);
    if (it->elems < 0 || i < 0 || i >= ign_menu_pages[s.cur].nitems) return 0;
    return 1;
}

/* sub_000534cc: +1 select (push or transition callback), -1 back (pop), 0 enter the current page. */
static int navigate(int dir) {
    IgnMenuPage *cur = ign_gui_page(s.cur);
    if (dir < 0 && s.depth == 0) {
        s.exit_req = 1;
        return 0;
    }
    if (dir > 0 && !item(s.sel)->enabled) return 0;
    cur->sel = s.sel;
    s.sel_stack[s.depth] = s.sel;
    for (int r = 0; r < 25; r++) {
        const int16_t(*t)[2] = ign_menu_transitions[r];
        int j;
        for (j = 0; j <= s.depth; j++)
            if (t[j][0] != 0 && t[j][0] != s.sel_stack[j] + 1) break;
        if (j <= s.depth) continue;
        s.last_row = r;
        int act = t[s.depth][1];
        if (act < 0) {
            void (*f)(int, int) = g_gui.callbacks[-act - 1];
            if (f) {
                publish();
                f(0, dir < 0 ? -6 : 6);
            }
            break;
        }
        if (dir > 0) {
            s.prev_menu = s.cur;
            s.cur = g_gui.menu_by_id[act];
            s.menu_stack[s.depth + 1] = s.cur;
            if (!g_gui.remember_sel) ign_gui_page(s.cur)->sel = 0;
            s.sel = s.prev_sel = ign_gui_page(s.cur)->sel;
            goto common;
        }
    }
    if (dir > 0) return 0;
    if (dir < 0) {
        s.prev_menu = s.cur;
        s.cur = s.menu_stack[s.depth - 1];
        ign_gui_page(s.cur)->sel = g_gui.remember_sel ? s.sel_stack[s.depth - 1] : 0;
        s.sel = s.prev_sel = ign_gui_page(s.cur)->sel;
    }
common:
    s.depth += dir;
    g_gui_pub.page_entered = 1;
    s.keymap = ign_menu_keymap; /* every menu's keymap is 0xbee1c */
    {
        const IgnMenuItem *it = item(s.sel);
        if (it->edit >= 0 && ign_menu_edits[it->edit].type == 2) {
            s.editmode = 2;
            strcpy(s.edit_buf, edit_dest(&ign_menu_edits[it->edit]));
            s.edit_len = (int)strlen(s.edit_buf);
        }
    }
    for (int i = 0; i < ign_gui_page(s.cur)->nitems; i++) {
        build_sprite(i, 0, i == s.sel);
        build_sprite(i, 1, i == s.sel);
    }
    return 1;
}

int ign_gui_start(void) {
    s.exit_req = 0;
    s.edit_len = 0;
    s.depth = 0;
    s.sel_stack[0] = 0;
    s.cur = s.menu_stack[0] = s.prev_menu = g_gui.root;
    s.editmode = 0;
    s.sel = ign_gui_page(s.cur)->sel;
    s.input_lock = 0;
    s.last_t = (double)g_gui.timer_ms() * 0.04;
    navigate(0);
    publish();
    s.first_tick = 1;
    return 1;
}

int ign_gui_set_input_lock(int ms) {
    s.input_lock = ms;
    return 1;
}

int ign_gui_free(void) { return 1; }

/* sub_00051c94 */
int ign_gui_tick(void) {
    double now = (double)g_gui.timer_ms() * 0.04;
    s.sel = ign_gui_page(s.cur)->sel; /* the game may move the cursor between calls */
    double dt = now - s.last_t;
    s.last_t = now;
    if (dt == 0.0) dt = 0.01;
    else if (dt > 12.0) dt = 12.0;
    s.dt = dt;
    if (!s.first_tick) g_gui_pub.page_entered = 0;
    int n_iter = (int)s.dt;
    for (int n = 0; n <= n_iter; n++) {
        if (s.input_lock > 0) {
            s.input_lock -= 25;
            goto tick_end;
        }
        for (int k = 0; k < 256; k++) {
            if (!g_gui.key_latch[k]) continue;
            g_gui.key_latch[k] = 0;
            if (g_gui.key) {
                publish();
                if (g_gui.key(k, s.keymap[k]) == IGN_GUI_CONSUMED) continue;
            }
            IgnMenuItem *it = item(s.sel);
            const IgnMenuEdit *ed = it->edit >= 0 ? &ign_menu_edits[it->edit] : NULL;
            if (ed && (s.editmode == 1 || s.editmode == 2) && k < 0x54) {
                uint8_t c = ign_menu_ascii[k];
                if (c != 0xff) {
                    if (s.edit_len >= ed->maxlen - 1) s.edit_len = ed->maxlen - 2;
                    s.edit_buf[s.edit_len++] = (char)c;
                    s.edit_buf[s.edit_len] = 0;
                } else if (k == 0x0e && s.edit_len > 0)
                    s.edit_buf[--s.edit_len] = 0;
            } else if (ed && s.editmode == 3) {
                /* A forbidden key, or the captured one, ends this whole key scan. */
                if (g_gui.bind_allowed && !g_gui.bind_allowed[k]) goto tick_end;
                *edit_dest(ed) = (char)k;
                s.editmode = 0;
                goto tick_end;
            }
            int a = s.keymap[k];
            if (a == 0) continue;
            s.prev_sel = s.sel;
            int old_v = it->value >= 0 ? value_of(it) : 0;
            if (it->cb > 0 && g_gui.callbacks[it->cb - 1]) {
                publish();
                g_gui.callbacks[it->cb - 1](it->arg, a);
            }
            if (it->value < 0 && abs(a) >= 3 && abs(a) <= 5) goto changed;
            if (s.editmode != 0 && abs(a) != 6) goto changed;
            IgnMenuPage *cur = ign_gui_page(s.cur);
            switch (a) {
            case -6:
                if (s.editmode == 0) navigate(-1);
                else if (s.editmode == 1) s.editmode = 0;
                else if (s.editmode == 2) {
                    s.editmode = 0;
                    navigate(-1);
                }
                break;
            case -5:
                set_value(it, it->min);
                break;
            case -4:
                if (it->enabled) set_value(it, value_of(it) - 1 < it->min ? it->min : value_of(it) - 1);
                break;
            case -3:
                if (it->enabled) set_value(it, value_of(it) - 1 < it->min ? it->max : value_of(it) - 1);
                break;
            case -2: {
                int s0 = s.sel, i;
                for (i = 0; i < cur->nitems; i++) {
                    if (--s.sel < 0) s.sel = 0;
                    if (item(s.sel)->enabled) break;
                }
                if (i == cur->nitems) s.sel = s0;
                break;
            }
            case -1:
                for (int i = 0; i < cur->nitems; i++) {
                    if (--s.sel < 0) s.sel = cur->nitems - 1;
                    if (item(s.sel)->enabled) break;
                }
                break;
            case 1:
                for (int i = 0; i < cur->nitems; i++) {
                    if (++s.sel > cur->nitems - 1) s.sel = 0;
                    if (item(s.sel)->enabled) break;
                }
                break;
            case 2: {
                int s0 = s.sel, i;
                for (i = 0; i < cur->nitems; i++) {
                    if (++s.sel > cur->nitems - 1) s.sel = cur->nitems - 1;
                    if (item(s.sel)->enabled) break;
                }
                if (i == cur->nitems) s.sel = s0;
                break;
            }
            case 3:
                if (it->enabled) set_value(it, value_of(it) + 1 > it->max ? it->min : value_of(it) + 1);
                break;
            case 4:
                if (it->enabled) set_value(it, value_of(it) + 1 > it->max ? it->max : value_of(it) + 1);
                break;
            case 5:
                set_value(it, it->max);
                break;
            case 6:
                if (ed) {
                    if (s.editmode == 0) {
                        if (!it->enabled) break;
                        if (ed->type == 1) {
                            s.editmode = 1;
                            strcpy(s.edit_buf, edit_dest(ed));
                            s.edit_len = (int)strlen(s.edit_buf);
                        } else if (ed->type == 3) {
                            s.edit_buf[0] = 0;
                            s.editmode = 3;
                            s.edit_len = 0;
                        }
                    } else if (s.editmode == 1) {
                        s.editmode = 0;
                        s.edit_buf[s.edit_len] = 0;
                        strcpy(edit_dest(ed), s.edit_buf);
                    } else if (s.editmode == 2) {
                        s.edit_buf[s.edit_len] = 0;
                        s.editmode = 0;
                        strcpy(edit_dest(ed), s.edit_buf);
                        navigate(it->cb == -1 ? -1 : 1);
                    }
                } else
                    navigate(it->cb == -1 ? -1 : 1);
                break;
            default:
                break;
            }
        changed:
            if (s.sel != s.prev_sel) {
                ign_gui_page(s.cur)->sel = s.sel;
                build_sprite(s.prev_sel, 0, 0);
                build_sprite(s.prev_sel, 1, 0);
                build_sprite(s.sel, 0, 1);
                build_sprite(s.sel, 1, 1);
            }
            {
                IgnMenuItem *ns = item(s.sel);
                if (ns->value >= 0 && value_of(ns) != old_v) build_sprite(s.sel, 1, 1);
            }
        }
    tick_end:
        if (s.input_lock < 0) s.input_lock = 0;
    }
    if (s.exit_req) {
        s.exit_req = 0;
        return -1;
    }
    if (g_gui.sound_pump && g_gui.sound_update) g_gui.sound_update();
    if (g_gui.draw) {
        publish();
        g_gui.draw();
    }
    publish();
    s.first_tick = 0;
    return 1;
}

/* sub_0005383c: coordinates are pixels from the screen centre; text goes through the identity LUT. */
static void draw_element(const IgnMenuElem *e, const char *text_override, int use_override) {
    if (!e && !use_override) return;
    int cx = g_gui.w / 2, cy = g_gui.h / 2;
    const char *text = use_override ? text_override : e->text;
    if (!text) return;
    const uint8_t *saved = ign_gfx_lut();
    ign_gfx_set_lut(ign_gfx_identity_lut());
    ign_font_draw(g_gui.fonts[e->font], text, e->tx + cx, e->ty + cy);
    ign_gfx_set_lut(saved);
}

/* sub_000527c8 */
int ign_gui_draw_items(void) {
    const IgnMenuPage *cur = ign_gui_page(s.cur);
    for (int i = 0; i < cur->nitems; i++) {
        const IgnMenuItem *it = item(i);
        int sel = i == s.sel, l = sel ^ 1;
        if (!it->enabled) {
            l += 2;
            if (it->value >= 0) l += 2 * (it->max + 1 - it->min);
        }
        if (it->elems >= 0) draw_element(&ign_menu_elems[it->elems + l], NULL, 0);
        if (it->value >= 0 && it->elems >= 0)
            draw_element(&ign_menu_elems[it->elems + l + 2 + 2 * (value_of(it) - it->min)], NULL, 0);
        if (it->edit >= 0) {
            const IgnMenuEdit *ed = &ign_menu_edits[it->edit];
            const char *text;
            if (ed->type == 3 && !(s.editmode != 0 && sel))
                text = ign_menu_key_names[(uint8_t)*edit_dest(ed)];
            else
                text = edit_dest(ed);
            if (sel) {
                if (s.editmode != 0) text = s.edit_buf;
                if (ed->blink_ms == 0 || g_gui.timer_ms() % ed->blink_ms < ed->blink_ms / 2) {
                    s.edit_buf[s.edit_len] = (char)ed->cursor;
                    s.edit_buf[s.edit_len + 1] = 0;
                } else
                    s.edit_buf[s.edit_len] = 0;
            }
            IgnMenuElem tmp = {0};
            tmp.tx = ed->x;
            tmp.ty = ed->y;
            tmp.font = sel ? ed->font_sel : ed->font_unsel;
            draw_element(&tmp, text, 1);
        }
    }
    return 1;
}
