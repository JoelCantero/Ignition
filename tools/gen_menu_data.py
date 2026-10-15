#!/usr/bin/env python3
"""Extracts the static frontend data of MAINDOS.EXE into src/frontend/menu_data.c.

  gen_menu_data.py [--exe MAINDOS.EXE] [-o src/frontend/menu_data.c]

What is extracted (addresses of the original data object):
  0xbe2bc  Menu* [6 languages][25 pages]; menus are 0x18-byte {draw, key, keymap, nitems, items, sel}
  0xaeb6c  Menu* [128] menu-id table per language (stride 0x3110)
  items    0x1c-byte {int *value, min, max, Element *elems, i16 cb, i32 arg, EditDesc *edit, i16 enabled}
  elements 0x18-byte {u8 *pixels, i16 x, y, w, h, pitch, char *text, i16 tx, ty, font}
  edits    0x16-byte {Element *elem, char *dest, i16 x, y, font_sel, font_unsel, u8 maxlen, cursor, i16 blink, type}
  0xbe514  transition table, 25 rows x 16 {i16 match, i16 action}
  0xbee1c / 0xc4860 key maps, 0xbef1c key-binding allow table, 0xc4960 scancode -> ASCII, 0xc49b4 key names
  0xbf088  char* [6][50] frontend strings; 0xbf538 credit names; 0xbf57c credit slides; 0xbf628 default records
  0xbf04c  track names; 0xbf5fc driver tags; 0xbf06c track thumbnail offsets; 0xbed00 intro CDP names;
  0xbf030  cheat words
Value and edit-destination pointers point into the settings block 0xe9bfc and are stored as offsets in it.
The track names that menu_load_gfx (0x11504) pokes into the track-select and best-times elements through its
once-only static initialisers (guard bytes 0xe9bd4..0xe9bd9) are applied here, as equivalent static data.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from disasm import DEFAULT_EXE, Image  # noqa: E402

CFG = 0xe9bfc
CFG_SIZE = 0x597
DRAW_CB, KEY_CB, KEYMAP = 0x170ac, 0x160c4, 0xbee1c


class Mem:
    def __init__(self, exe):
        self.img = Image(exe)

    def raw(self, addr, n):
        base, data, _ = self.img.find(addr)
        return data[addr - base:addr - base + n]

    def u8(self, a): return self.raw(a, 1)[0]
    def i16(self, a): return struct.unpack("<h", self.raw(a, 2))[0]
    def u32(self, a): return struct.unpack("<I", self.raw(a, 4))[0]
    def i32(self, a): return struct.unpack("<i", self.raw(a, 4))[0]

    def cstr(self, a):
        if not a or not self.img.find(a):
            return None
        out = bytearray()
        while True:
            c = self.u8(a + len(out))
            if c == 0:
                return bytes(out)
            out += bytes([c])


def c_string(b):
    if b is None:
        return "NULL"
    s = '"'
    for c in b:
        if c in (0x22, 0x5c):
            s += "\\" + chr(c)
        elif 32 <= c < 127:
            s += chr(c)
        else:
            s += "\\%03o" % c
    return s + '"'


def track_name_pokes(m):
    """Element text addresses (+0xe) written by the static initialisers of menu_load_gfx."""
    names = [m.u32(0xbf04c + 4 * t) for t in range(7)]
    long0 = m.u32(0xbf068)
    pokes = {}
    for lang, (base, lists) in enumerate(zip((0xadca2, 0xb0de6, 0xb3ef6, 0xb7006, 0xba116, 0xbd226),
                                             (0xae32e, 0xb1472, 0xb4582, 0xb7692, 0xba7a2, 0xbd8b2))):
        for t in range(7):
            for r in range(4):
                pokes[base + 0x60 * t + 0x18 * r] = long0 if t == 0 else names[t]
            for r in range(2):
                pokes[lists + 0x30 * t + 0x18 * r] = (names[0] if lang & 1 else long0) if t == 0 else names[t]
    return pokes


def cfg_offset(p, what):
    if not CFG <= p < CFG + CFG_SIZE:
        raise ValueError("%s pointer 0x%x outside the settings block" % (what, p))
    return p - CFG


class Gen:
    def __init__(self, m):
        self.m = m
        self.pokes = track_name_pokes(m)
        self.strings = {}
        self.elems = []      # (addr or None, fields)
        self.edits = []
        self.items = []
        self.menus = []
        self.menu_index = {}

    def element(self, a):
        m = self.m
        text = self.pokes.get(a + 0xe, m.u32(a + 0xe))
        font = m.i16(a + 0x16)
        s = m.cstr(text)
        if m.u32(a) or not 0 <= font < 8 or (text and s is None):
            return None
        return (m.i16(a + 4), m.i16(a + 6), m.i16(a + 8), m.i16(a + 0xa), m.i16(a + 0xc), s,
                m.i16(a + 0x12), m.i16(a + 0x14), font)

    def elem_array(self, a, count):
        first = len(self.elems)
        for k in range(count):
            e = self.element(a + 0x18 * k)
            if e is None:
                if k < 2:
                    raise ValueError("bad element at 0x%x" % (a + 0x18 * k))
                e = (0, 0, 0, 0, 0, None, 0, 0, 0)
            self.elems.append(e)
        return first

    def edit(self, a):
        m = self.m
        if m.u32(a):
            raise ValueError("edit element at 0x%x is not NULL" % a)
        self.edits.append((cfg_offset(m.u32(a + 4), "edit dest"), m.i16(a + 8), m.i16(a + 0xa), m.i16(a + 0xc),
                           m.i16(a + 0xe), m.u8(a + 0x10), m.u8(a + 0x11), m.i16(a + 0x12), m.i16(a + 0x14)))
        return len(self.edits) - 1

    def menu(self, a):
        if a in self.menu_index:
            return self.menu_index[a]
        m = self.m
        if (m.u32(a), m.u32(a + 4), m.u32(a + 8)) != (DRAW_CB, KEY_CB, KEYMAP):
            raise ValueError("menu 0x%x has unexpected callbacks/keymap" % a)
        n, items, sel = m.i32(a + 0xc), m.u32(a + 0x10), m.i32(a + 0x14)
        first = len(self.items)
        for j in range(n):
            it = items + 0x1c * j
            vp, mn, mx, el = m.u32(it), m.i32(it + 4), m.i32(it + 8), m.u32(it + 0xc)
            cb, arg, ed, en = m.i16(it + 0x10), m.i32(it + 0x12), m.u32(it + 0x16), m.i16(it + 0x1a)
            value = cfg_offset(vp, "value") if vp else -1
            # DIFFICULTY's max is raised at run time (unlock level, SURMULE): keep at least 4 value pairs.
            count = 2 + (2 * max(mx - mn + 1, 4) if vp else 0)
            elems = self.elem_array(el, 2 * count) if el else -1
            self.items.append((value, mn, mx, elems, cb, arg, self.edit(ed) if ed else -1, en))
        self.menu_index[a] = len(self.menus)
        self.menus.append((a, n, first, sel))
        return self.menu_index[a]

    def emit(self, out):
        m = self.m
        pages = [[self.menu(m.u32(0xbe2bc + 4 * (lang * 25 + k))) for k in range(25)] for lang in range(6)]
        by_id = []
        for lang in range(6):
            row = []
            for i in range(128):
                p = m.u32(0xaeb6c + lang * 0x3110 + 4 * i)
                row.append(self.menu(p) if p else -1)
            by_id.append(row)
        root = self.menu(0xaeb54)
        w = out.write
        w("/* Generated by tools/gen_menu_data.py from MAINDOS.EXE: do not edit. */\n")
        w('#include "menu_data.h"\n\n')
        w("IgnMenuElem ign_menu_elems[%d] = {\n" % len(self.elems))
        for e in self.elems:
            w("    {%d, %d, %d, %d, %d, %s, %d, %d, %d},\n" % (e[0], e[1], e[2], e[3], e[4], c_string(e[5]),
                                                            e[6], e[7], e[8]))
        w("};\n\nconst IgnMenuEdit ign_menu_edits[%d] = {\n" % len(self.edits))
        for e in self.edits:
            w("    {0x%x, %d, %d, %d, %d, %d, %d, %d, %d},\n" % e)
        w("};\n\nIgnMenuItem ign_menu_items[%d] = {\n" % len(self.items))
        for it in self.items:
            value = "0x%x" % it[0] if it[0] >= 0 else "-1"
            w("    {%s, %d, %d, %d, %d, %d, %d, %d},\n" % ((value,) + it[1:]))
        w("};\n\nIgnMenuPage ign_menu_pages[IGN_MENU_COUNT] = {\n")
        for a, n, first, sel in self.menus:
            w("    {%d, %d, %d}, /* 0x%x */\n" % (n, first, sel, a))
        w("};\n\nconst int16_t ign_menu_lang_pages[6][25] = {\n")
        for row in pages:
            w("    {%s},\n" % ", ".join(str(x) for x in row))
        w("};\n\nconst int16_t ign_menu_by_id[6][128] = {\n")
        for row in by_id:
            w("    {%s},\n" % ", ".join(str(x) for x in row))
        w("};\n\nconst int ign_menu_root = %d;\n\n" % root)
        w("const int16_t ign_menu_transitions[25][16][2] = {\n")
        for r in range(25):
            ents = ["{%d, %d}" % (m.i16(0xbe514 + r * 0x40 + 4 * j), m.i16(0xbe516 + r * 0x40 + 4 * j))
                    for j in range(16)]
            w("    {%s},\n" % ", ".join(ents))
        w("};\n\n")

        def bytes_table(name, ctype, addr, n, signed=False, const=True):
            vals = [m.u8(addr + i) for i in range(n)]
            if signed:
                vals = [v - 256 if v > 127 else v for v in vals]
            w("%s%s %s[%d] = {" % ("const " if const else "", ctype, name, n))
            for i, v in enumerate(vals):
                w(("\n    " if i % 16 == 0 else " ") + "%d," % v)
            w("\n};\n\n")

        bytes_table("ign_menu_keymap_pristine", "int8_t", 0xbed1c, 256, True)
        bytes_table("ign_menu_keymap", "int8_t", 0xbee1c, 256, True, False)
        bytes_table("ign_menu_keymap_default", "int8_t", 0xc4860, 256, True)
        bytes_table("ign_menu_bind_allowed", "uint8_t", 0xbef1c, 256)
        bytes_table("ign_menu_ascii", "uint8_t", 0xc4960, 0x54)
        bytes_table("ign_menu_default_records", "uint8_t", 0xbf628, 7 * 0x78)

        def str_table(name, addr, n, dims=""):
            w("const char *const %s%s = {" % (name, dims or "[%d]" % n))
            for i in range(n):
                w("\n    %s," % c_string(m.cstr(m.u32(addr + 4 * i))))
            w("\n};\n\n")

        w("const char *const ign_menu_text[6][50] = {\n")
        for lang in range(6):
            w("    {\n")
            for i in range(50):
                w("        %s,\n" % c_string(m.cstr(m.u32(0xbf088 + lang * 200 + 4 * i))))
            w("    },\n")
        w("};\n\n")
        str_table("ign_menu_key_names", 0xc49b4, 256)
        str_table("ign_menu_credit_names", 0xbf538, 17)
        w("const int16_t ign_menu_slides[16][4] = {\n")
        for s in range(16):
            w("    {%s},\n" % ", ".join(str(m.i16(0xbf57c + 8 * s + 2 * k)) for k in range(4)))
        w("};\n\n")
        str_table("ign_menu_track_names", 0xbf04c, 8)
        str_table("ign_menu_driver_tags", 0xbf5fc, 11)
        str_table("ign_menu_intro_clips", 0xbed00, 6)
        str_table("ign_menu_cheat_words", 0xbf030, 7)
        w("const int32_t ign_menu_track_thumbs[7] = {%s};\n" %
          ", ".join("0x%x" % m.u32(0xbf06c + 4 * i) for i in range(7)))
        return len(self.menus)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("-o", default=os.path.join(os.path.dirname(__file__), "..", "src", "frontend", "menu_data.c"))
    args = ap.parse_args()
    import io
    buf = io.StringIO()
    n = Gen(Mem(args.exe)).emit(buf)
    text = buf.getvalue()
    expected = "#define IGN_MENU_COUNT %d" % n
    with open(os.path.join(os.path.dirname(args.o), "menu_data.h")) as f:
        if expected not in f.read():
            sys.exit("menu_data.h must contain '%s'" % expected)
    with open(args.o, "w") as f:
        f.write(text)
    print("%s: %d menus" % (args.o, n))


if __name__ == "__main__":
    main()
