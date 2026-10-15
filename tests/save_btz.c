/* IGN_DOS.BTZ and time-trial persistence: byte round trips, record insertion (render_main 0x459c6) and the ghost
 * rules of 0x2adf8/0x2ccc8. Usage: save_btz_test GAME_DIR SCRATCH_DIR (the real file is optional). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/ghost.h"
#include "game/save.h"

static int failures;

static void check(int condition, const char *message) {
    if (condition) return;
    fprintf(stderr, "FAIL %s\n", message);
    failures++;
}

static int read_all(const char *path, unsigned char *buf, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    return (int)n;
}

static void round_trip(const char *path, const char *scratch) {
    static unsigned char orig[IGN_BTZ_SIZE + 16], back[IGN_BTZ_SIZE + 16];
    IgnBtz b;
    if (ign_btz_load(path, &b)) {
        printf("skip real-file round trip: %s not readable\n", path);
        return;
    }
    int n = read_all(path, orig, sizeof orig);
    check(n == IGN_BTZ_SIZE, "real BTZ size");
    /* Decode and re-encode every record table: the image must not change. */
    for (int t = 0; t < IGN_BTZ_TRACKS; t++) {
        IgnTrackRecords r;
        check(!ign_btz_records(&b, t, &r), "records read");
        check(!ign_btz_set_records(&b, t, &r), "records write");
    }
    check(!memcmp(b.raw, orig, IGN_BTZ_SIZE), "records decode/encode keeps the image");
    char out[1100];
    snprintf(out, sizeof out, "%s/roundtrip.btz", scratch);
    check(!ign_btz_save(out, &b), "save");
    check(read_all(out, back, sizeof back) == IGN_BTZ_SIZE && !memcmp(back, orig, IGN_BTZ_SIZE),
          "load/save is byte exact");
    /* Values decoded from the shipped file (tools/btz.py info). */
    IgnTrackRecords r;
    ign_btz_records(&b, 0, &r);
    check(!strcmp(r.race_name[0], "JCP") && r.race[0] > 135.4 && r.race[0] < 135.5, "CANADA race record");
    check(r.lap[0] > 41.8 && r.lap[0] < 41.9, "CANADA lap record");
    ign_btz_records(&b, 6, &r);
    check(!strcmp(r.race_name[0], "KOT") && r.race[0] == 233.0 && r.lap[4] == 84.0, "JAPAN defaults");
    char name[IGN_BTZ_NAME_LEN + 1];
    ign_btz_name(&b, 0, name);
    check(!strcmp(name, "JCP"), "participant 0 name");
    check(ign_btz_i32(&b, IGN_BTZ_NCARS) == 6 && ign_btz_i32(&b, IGN_BTZ_LANGUAGE) == 3, "cars and language");
    printf("real-file round trip: %d bytes identical\n", IGN_BTZ_SIZE);
}

static void insertion(void) {
    IgnTrackRecords r;
    const double t[5] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) r.race[i] = r.lap[i] = t[i], snprintf(r.race_name[i], 4, "N%d", i);
    check(ign_records_insert_race(&r, "AAA", 50.0) == -1, "a tie with slot 4 is rejected (jae)");
    check(ign_records_insert_race(&r, "BBB", 25.0) == 2, "25 s lands in slot 2");
    check(r.race[2] == 25.0 && !strcmp(r.race_name[2], "BBB") && r.race[3] == 30.0 && r.race[4] == 40.0 &&
              !strcmp(r.race_name[4], "N3"),
          "slots shift down and the old slot 4 is dropped");
    check(ign_records_insert_race(&r, "CCC", 10.0) == 1, "a tie with slot 0 stays below it");
    check(ign_records_insert_race(&r, "LONGNAME", 1.0) == 0 && !strcmp(r.race_name[0], "LON"), "new record, 3 chars");
    check(ign_records_insert_lap(&r, "DDD", 5.0) == 0 && r.lap[0] == 5.0 && r.lap[4] == 40.0, "lap table");

    IgnBtz b;
    ign_btz_default(&b);
    static IgnCar car;
    memset(&car, 0, sizeof car);
    car.out = 1;
    car.race_elapsed = 100.0;
    car.lap_times[0] = 33.0, car.lap_times[1] = 34.0, car.lap_times[2] = 0;
    int bits = ign_records_commit(&b, 1, "PL1", &car);
    check((bits & IGN_SAVED_TRACK_RECORD) && (bits & IGN_SAVED_LAP_RECORD), "commit into empty tables");
    ign_btz_records(&b, 1, &r);
    check(r.race[0] == 100.0 && r.lap[0] == 33.0 && r.lap[1] == 34.0 && r.lap[2] == 999.99, "commit skips 0 laps");
    car.wrecked = 1;
    check(ign_records_commit(&b, 1, "PL1", &car) == 0, "wrecked car is not recorded");
}

static void ghost_rules(const char *scratch) {
    static IgnGhost g, rec, play;
    char path[1100];
    int record = 0;
    snprintf(path, sizeof path, "%s/missing.GST", scratch);
    ign_ghost_begin(&rec, 3, 1, "PL1");
    check(ign_ghost_trial_load(path, 1, &play, &rec, &record) == IGN_TRIAL_NONE && record == IGN_GHOST_NO_RECORD,
          "missing ghost -> 3000000");
    check(ign_ghost_trial_beats(&rec, record, 299.99), "any finish beats no ghost");

    ign_ghost_begin(&g, 3, 1, "PL1");
    g.count = 10;
    ign_ghost_finish(&g, 100.257);
    snprintf(path, sizeof path, "%s/GOLDRUSH.GST", scratch);
    check(!ign_ghost_save(path, &g), "ghost save");
    check(ign_ghost_trial_load(path, 1, &play, &rec, &record) == IGN_TRIAL_GHOST && record == 10025,
          "ghost of the track: record from header");
    check(!ign_ghost_trial_beats(&rec, record, 100.25) && ign_ghost_trial_beats(&rec, record, 100.24),
          "strictly faster beats");
    check(ign_ghost_trial_load(path, 0, &play, &rec, &record) == IGN_TRIAL_MISMATCH && rec.overflow,
          "other track: playback off, saving off");
    check(!ign_ghost_trial_beats(&rec, record, 1.0), "mismatch never saves");
}

static double fixed_random(void) {
    static const double seq[] = {0.0, 0.99, 0.5, 0.2};
    static int i;
    return seq[i++ % 4];
}

static void shuffle(void) {
    IgnBtz b;
    ign_btz_default(&b);
    for (int i = 0; i < 8; i++) ign_btz_set_i32(&b, IGN_BTZ_GRID + i * 4, i);
    ign_btz_set_i32(&b, IGN_BTZ_NCARS, 4);
    IgnBtz before = b;
    ign_btz_shuffle_grid(&b, fixed_random);
    check(!memcmp(&b, &before, sizeof b), "championship keeps the grid");
    ign_btz_set_i32(&b, IGN_BTZ_MODE, 2);
    ign_btz_shuffle_grid(&b, fixed_random);
    int seen = 0;
    for (int i = 0; i < 4; i++) seen |= 1 << ign_btz_i32(&b, IGN_BTZ_GRID + i * 4);
    check(seen == 0xf && ign_btz_i32(&b, IGN_BTZ_GRID + 16) == 4, "shuffle permutes the first ncars slots");
}

int main(int argc, char **argv) {
    const char *data = argc > 1 ? argv[1] : "/Users/Shared/Epic Games/Ignition/ignition";
    const char *scratch = argc > 2 ? argv[2] : ".";
    char path[1100];
    snprintf(path, sizeof path, "%s/%s", data, IGN_BTZ_NAME);
    round_trip(path, scratch);
    insertion();
    ghost_rules(scratch);
    shuffle();
    if (failures) return 1;
    printf("save_btz: ok\n");
    return 0;
}
