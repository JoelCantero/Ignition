#ifndef IGN_TRACK_H
#define IGN_TRACK_H

#include "assets.h"

#define IGN_NO_SECTION 10000

typedef struct IgnTrack {
    int nobj;
    int *klass;        /* PLC type & 0xfff (0 start, 1 start direction, 2/3/7/9 road, 4/5/6 branches) */
    int *section;      /* per object: section index, negative on the second branch, IGN_NO_SECTION if none */
    float (*center)[3];
    int count;         /* [0x1ff004] */
    int start_obj, dir_obj;
    int *section_obj;  /* main route: object of each section */
    float (*gate_b)[4]; /* branch B gates (negative sections), valid where has_b[s] */
    unsigned char *has_b;
    float (*gate)[4];  /* per section: left x,z and right x,z road edges (TRI vertices) */
} IgnTrack;

/* Road edges per section from the TRI blocks (vertex indices at +1 and +0x17). */
void ign_track_gates(IgnTrack *t, const IgnLevel *lv);

/* sub_000247b0 + sub_00028f08. Returns 0 on success. */
int ign_build_track(const IgnModel *m, IgnTrack *t);
void ign_free_track(IgnTrack *t);

#endif
