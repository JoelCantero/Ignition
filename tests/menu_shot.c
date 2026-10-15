/* Headless frontend driver: runs menu_frame with a simulated 70 Hz clock, injects DOS scancodes and writes the
 * 320x200 menu frames as PPM.
 *
 *   menu_shot DATA_DIR OUT_PREFIX STEP...
 *
 * STEP is `wN` (run N frames), `kHEX` (press the scancode), `sNAME` (write OUT_PREFIX-NAME.ppm) or `r` (expect the
 * frontend to have returned IGN_FRONTEND_RACE). Starting a race or quitting writes IGN_DOS.BTZ into the user
 * folder IGN_SAVE_DIR (created if needed; default ./menu-shot-save/). The game folder is only read. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "frontend/frontend.h"

static int now_ms;
static uint8_t palette[768];
static const uint8_t *last;

static int timer(void) { return now_ms; }
static void set_palette(const uint8_t rgb[768]) { memcpy(palette, rgb, 768); }
static void present(const uint8_t *buf) { last = buf; }

static int write_ppm(const char *path, const uint8_t *buf) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", IGN_FRONTEND_W, IGN_FRONTEND_H);
    for (int i = 0; i < IGN_FRONTEND_W * IGN_FRONTEND_H; i++) fwrite(palette + buf[i] * 3, 1, 3, f);
    return fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s DATA_DIR OUT_PREFIX STEP...\n", argv[0]);
        return 2;
    }
    const char *save = getenv("IGN_SAVE_DIR");
    if (!save || !*save) save = "menu-shot-save/";
    mkdir(save, 0777);
    IgnFrontendHost host = {.timer_ms = timer, .set_palette = set_palette, .present = present, .save_dir = save};
    if (ign_frontend_init(argv[1], &host)) {
        fprintf(stderr, "cannot initialise the frontend from %s\n", argv[1]);
        return 1;
    }
    ign_frontend_load();
    int result = IGN_FRONTEND_STAY;
    for (int i = 3; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == 'w') {
            for (int n = atoi(a + 1); n > 0 && result == IGN_FRONTEND_STAY; n--) {
                now_ms += 14;
                result = ign_frontend_frame();
            }
        } else if (a[0] == 'k') {
            ign_frontend_key_down((int)strtol(a + 1, NULL, 16));
        } else if (a[0] == 's') {
            char path[1024];
            snprintf(path, sizeof path, "%s-%s.ppm", argv[2], a + 1);
            if (!last || write_ppm(path, last)) {
                fprintf(stderr, "cannot write %s\n", path);
                return 1;
            }
        } else if (a[0] == 'r') {
            if (result != IGN_FRONTEND_RACE) {
                fprintf(stderr, "expected a race start, got %d\n", result);
                return 1;
            }
        }
    }
    printf("result %d\n", result);
    ign_frontend_shutdown();
    ign_frontend_destroy();
    return 0;
}
