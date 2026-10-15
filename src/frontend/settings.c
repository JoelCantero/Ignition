#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "menu_data.h"

uint8_t g_cfg[IGN_CFG_SIZE];

int32_t ign_cfg_get(int off) {
    uint8_t *p = g_cfg + off;
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

void ign_cfg_set(int off, int32_t v) {
    uint8_t *p = g_cfg + off;
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)((uint32_t)v >> (8 * i));
}

void ign_cfg_default_records(uint8_t out[7 * 0x78]) { memcpy(out, ign_menu_default_records, 7 * 0x78); }

void ign_cfg_defaults(void) {
    static const int32_t slots[8] = {0, 2, 3, 4, 5, 7, 8, 8};
    static const uint8_t p1_keys[8] = {0xcb, 0xcd, 0xc8, 0xd0, 0x35, 0x34, 0x36, 0x28};
    static const uint8_t p2_keys[8] = {0x2e, 0x30, 0x21, 0x2f, 0x2b, 0x2a, 0x2c, 0x1f};
    ign_cfg_set(CFG_VERSION, IGN_CFG_VERSION);
    ign_cfg_set(CFG_TWO_PLAYERS, 0);
    ign_cfg_set(CFG_GAME_MODE, 0);
    ign_cfg_set(CFG_DIFFICULTY, 0);
    ign_cfg_set(CFG_GHOST, 1);
    ign_cfg_set(CFG_OBSTACLES, 1);
    ign_cfg_set(CFG_FLAG_018, 1);
    for (int i = 0; i < 8; i++) ign_cfg_set(CFG_CAR_SLOT + 4 * i, slots[i]);
    ign_cfg_set(CFG_UNK_03C, 0);
    ign_cfg_set(CFG_P1_CAR, 0);
    ign_cfg_set(CFG_P2_CAR, 0);
    for (int i = 0; i < 9; i++) ign_cfg_set(CFG_GEARBOX + 4 * i, 1);
    ign_cfg_set(CFG_SKIDS, 1);
    ign_cfg_set(CFG_SMOKE, 1);
    memcpy(g_cfg + CFG_NAMES, "PL1", 4);
    memcpy(g_cfg + CFG_NAMES + 8 * 15, "PL2", 4);
    ign_cfg_set(CFG_TRACK, 0);
    ign_cfg_set(CFG_CD_TRACK, 0);
    ign_cfg_set(CFG_SCREEN_INV, 0);
    ign_cfg_set(CFG_RESOLUTION, 0);
    ign_cfg_set(CFG_MIPMAP, 0);
    ign_cfg_set(CFG_PERSP, 0);
    ign_cfg_set(CFG_P1_CONTROLS, 0);
    ign_cfg_set(CFG_P2_CONTROLS, 0);
    ign_cfg_set(CFG_P1_AUTOACC, 0);
    ign_cfg_set(CFG_P2_AUTOACC, 0);
    ign_cfg_set(CFG_SFX_VOL, 10);
    ign_cfg_set(CFG_SCREEN_SIZE, 3);
    ign_cfg_set(CFG_MUSIC_VOL, 5);
    ign_cfg_set(CFG_UNK_11B, 2);
    ign_cfg_set(CFG_UNK_11F, 2);
    for (int i = 0; i < 8; i++) ign_cfg_set(CFG_GRID + 4 * i, i);
    ign_cfg_set(CFG_RACERS, 1);
    ign_cfg_set(CFG_UNK_157, 0);
    ign_cfg_set(CFG_CONTROL, 0);
    for (int i = 1; i < 8; i++) ign_cfg_set(CFG_CONTROL + 4 * i, 1);
    memcpy(g_cfg + CFG_P1_KEYS, p1_keys, 8);
    memcpy(g_cfg + CFG_P2_KEYS, p2_keys, 8);
    ign_cfg_set(CFG_UNK_18F, 0);
    ign_cfg_set(CFG_UNK_193, 0);
    ign_cfg_set(CFG_HS_TRACK, 0);
    ign_cfg_set(CFG_HS_LAP, 0);
    ign_cfg_set(CFG_UNLOCK, 0);
    ign_cfg_set(CFG_BONUS, 0);
    ign_cfg_set(CFG_UNK_58F, 0);
    ign_cfg_set(CFG_LANGUAGE, 0);
    memcpy(g_cfg + CFG_STR_19B, "G1", 3);
    memcpy(g_cfg + CFG_STR_1AA, "12", 3);
}

int ign_cfg_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t buf[IGN_CFG_SIZE];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    /* The version int alone decides; a short read leaves the rest of the block as it was (read_file_into). */
    if (n < 4 || buf[0] != IGN_CFG_VERSION || buf[1] || buf[2] || buf[3]) return -1;
    memcpy(g_cfg, buf, n);
    return 0;
}

int ign_cfg_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(g_cfg, 1, IGN_CFG_SIZE, f);
    return fclose(f) || n != IGN_CFG_SIZE ? -1 : 0;
}
