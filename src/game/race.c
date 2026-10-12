#include "race.h"

#include <stdlib.h>

void ign_race_init(IgnRace *r, int mode, int ncars) {
    r->mode = mode;
    r->tick = 0;
    r->finished = 0;
    r->remaining = ncars;
}

static void finish(IgnRace *r, IgnCar *c) {
    c->out = 1;
    c->finish_tick = r->tick;
    c->place = ++r->finished;
}

int ign_race_step(IgnRace *r, IgnCar *const *cars, int n) {
    r->tick++;
    for (int i = 0; i < n; i++)
        if (!cars[i]->out && r->mode != IGN_MODE_ELIMINATION && cars[i]->lap >= IGN_RACE_LAPS)
            finish(r, cars[i]);

    if (r->mode == IGN_MODE_ELIMINATION) {
        /* sub_0002de30: with exactly one racing car a lap behind the leader, it is out. */
        int max_lap = 0, behind = 0, last = -1;
        for (int i = 0; i < n; i++)
            if (!cars[i]->out && cars[i]->lap > max_lap) max_lap = cars[i]->lap;
        for (int i = 0; i < n; i++)
            if (!cars[i]->out && cars[i]->lap < max_lap) behind++, last = i;
        if (behind == 1 && cars[last]->blow == 0.0) {
            IgnCar *c = cars[last];
            c->wrecked = 1;
            c->blow = 1.0;
            finish(r, c);
            if (--r->remaining == 1)
                for (int i = 0; i < n; i++)
                    if (!cars[i]->out) finish(r, cars[i]);
        }
    }
    int racing = 0;
    for (int i = 0; i < n; i++) racing += !cars[i]->out;
    return racing;
}

int ign_race_position(const IgnCar *const *cars, int n, int index) {
    const IgnCar *me = cars[index];
    int pos = 1;
    for (int i = 0; i < n; i++) {
        if (i == index) continue;
        const IgnCar *o = cars[i];
        int ahead;
        /* Finishers rank first, eliminated cars last (the earliest eliminated is the lowest). */
        int ro = o->wrecked ? -1 : o->out, rm = me->wrecked ? -1 : me->out;
        if (ro != rm) ahead = ro > rm;
        else if (o->out) ahead = o->wrecked ? o->place > me->place : o->place < me->place;
        else if (o->lap != me->lap) ahead = o->lap > me->lap;
        else if (o->phase != me->phase) ahead = o->phase > me->phase;
        else ahead = abs(o->section) > abs(me->section);
        pos += ahead;
    }
    return pos;
}
