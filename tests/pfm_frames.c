/* Renders frames of BALTAZAR/DATA/TEST2.PFM with the menu's texture pages and shade tables (menu_load_car 0x1aaf8)
 * over a zero background and writes them raw (320x200 bytes), or compares them with reference raw files.
 *
 *   pfm_frames DATA_DIR OUT_DIR FRAME...       -> OUT_DIR/pfm_FRAME.raw
 *   pfm_frames DATA_DIR --check REF_DIR FRAME... compares with REF_DIR/py_FRAME.raw
 *   pfm_frames DATA_DIR --hash                  FNV-1a of every frame drawn over a zero background
 *
 * The reference frames come from a Python model checked pixel for pixel against the original routines (0x549b0,
 * 0x54df8, 0x60e3d) run in an emulator; the --hash value in CMakeLists.txt was taken after --check passed for all
 * 209 frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/pfm.h"
#include "game/assets.h"

static uint8_t *load(const char *dir, const char *name, size_t *size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    uint8_t *d = ign_read_file(path, size);
    if (!d) fprintf(stderr, "cannot read %s\n", path);
    return d;
}

int main(int argc, char **argv) {
    if (argc < 3 || (argc < 4 && strcmp(argv[2], "--hash"))) return 2;
    static const char *const tex[8] = {"CANADA", "CARS", "DIVSPR", "LIGHT", "SMOKE", "DARKSMOK", "BOOM", "EXSMOKE"};
    uint8_t *pages[32] = {0};
    int n = 0;
    for (int f = 0; f < 8; f++) {
        char name[128];
        size_t size;
        snprintf(name, sizeof name, "BALTAZAR/DATA/TEXTURES/%s.TEX", tex[f]);
        uint8_t *d = load(argv[1], name, &size);
        if (!d) return 1;
        if (size > 0x100000) size = 0x100000;
        for (size_t o = 0; o < size && n < 31; o += 0x10000) {
            pages[n] = calloc(1, 0x10000);
            memcpy(pages[n++], d + o, size - o < 0x10000 ? size - o : 0x10000);
        }
        free(d);
    }
    size_t size;
    uint8_t *shd = load(argv[1], "LEVELS/CANADA/CANADA.SHD", &size), solid[65536];
    uint8_t *tab = load(argv[1], "BALTAZAR/DATA/MENU.TAB", &size);
    uint8_t *pfm = load(argv[1], "BALTAZAR/DATA/TEST2.PFM", &size);
    if (!shd || !tab || !pfm) return 1;
    for (int r = 0; r < 256; r++)
        for (int c = 0; c < 256; c++) solid[r * 256 + c] = (uint8_t)(r ? r : c);
    const uint8_t *shades[3] = {shd, solid, NULL};
    IgnPfm *p = ign_pfm_load(pfm, size, pages, shades);
    if (!p) return 1;
    if (!strcmp(argv[2], "--hash")) {
        uint32_t h = 2166136261u;
        static uint8_t fb[64000];
        for (int f = 0; f < ign_pfm_frames(p); f++) {
            memset(fb, 0, sizeof fb);
            if (ign_pfm_draw(p, f, fb, 320, 200, tab)) return 1;
            for (int k = 0; k < 64000; k++) h = (h ^ fb[k]) * 16777619u;
        }
        printf("frames %d hash %08x\n", ign_pfm_frames(p), h);
        return 0;
    }
    int check = !strcmp(argv[2], "--check"), first = check ? 4 : 3, bad = 0;
    for (int i = first; i < argc; i++) {
        static uint8_t fb[64000];
        memset(fb, 0, sizeof fb);
        int f = atoi(argv[i]);
        if (ign_pfm_draw(p, f, fb, 320, 200, tab)) return 1;
        char path[1024];
        if (check) {
            snprintf(path, sizeof path, "%s/py_%d.raw", argv[3], f);
            FILE *r = fopen(path, "rb");
            static uint8_t ref[64000];
            if (!r || fread(ref, 1, sizeof ref, r) != sizeof ref) return 1;
            fclose(r);
            int diff = 0;
            for (int k = 0; k < 64000; k++) diff += fb[k] != ref[k];
            printf("frame %d: %d differing pixels of %d frames\n", f, diff, ign_pfm_frames(p));
            bad |= diff != 0;
        } else {
            snprintf(path, sizeof path, "%s/pfm_%d.raw", argv[2], f);
            FILE *w = fopen(path, "wb");
            if (!w) return 1;
            fwrite(fb, 1, sizeof fb, w);
            fclose(w);
        }
    }
    return bad;
}
