#include "game/ai.h"

#include <stdio.h>

static int check(int ok, const char *message) {
    if (!ok) fprintf(stderr, "FAIL: %s\n", message);
    return !ok;
}

int main(void) {
    int failed = 0;
    IgnAi ai;
    IgnFx fx, expected_fx;

    ign_ai_set_track("CANADA");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_update_route_choice(&ai, 10, &fx);
    failed |= check(ai.route_selector == 0 && ai.route_choice == 1, "section 10 threshold selects route 1");
    failed |= check(ai.route_random == 16838.0 / 32768.0, "rand() uses the original 15-bit scale");

    ign_ai_set_track("AUSTRIA");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_update_route_choice(&ai, 5, &fx);
    failed |= check(ai.route_selector == 0 && ai.route_choice == 1, "100-percent threshold forces route 1");

    ign_ai_set_track("JAPAN");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_update_route_choice(&ai, 190, &fx);
    failed |= check(ai.route_selector == 1 && ai.route_choice == 0, "zero threshold forces route 0 and retains selector");

    ign_ai_set_track("CANADA");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_fx_init(&expected_fx);
    ign_fx_rand15(&expected_fx);
    unsigned second = ign_fx_rand15(&expected_fx);
    ign_ai_update_route_choice(&ai, 2, &fx);
    failed |= check(fx.seed == expected_fx.seed, "duplicate section rows each consume one random value");
    failed |= check(ai.route_random == (double)second / 32768.0, "duplicate section rows retain the last random value");

    unsigned seed = fx.seed;
    ign_ai_update_route_choice(&ai, 3, &fx);
    failed |= check(fx.seed == seed, "unmatched section does not consume random state");
    return failed;
}
