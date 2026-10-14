#ifndef IGN_TRACK_H
#define IGN_TRACK_H

#include "assets.h"

#define IGN_NO_SECTION 10000

#define IGN_AI_ROUTE_PATHS 2
#define IGN_AI_ROUTE_RECORDS 2

typedef struct {
    int32_t point[6]; /* load_tri +0x02..+0x16: left xyz followed by right xyz */
    int object;
    int plc;
    uint8_t plc_type;
    uint8_t tri_type;
    uint8_t valid;
} IgnAiRouteRecord;

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
    IgnAiRouteRecord (*ai_records)[IGN_AI_ROUTE_PATHS][IGN_AI_ROUTE_RECORDS];
    uint8_t (*ai_record_count)[IGN_AI_ROUTE_PATHS];
} IgnTrack;

/* Road edges per section from the TRI blocks (vertex indices at +1 and +0x17). */
void ign_track_gates(IgnTrack *t, const IgnLevel *lv);
/* load_tri 0x1ce30: rebuilds the ordered gate records, including TRI split types 1 and 2. */
int ign_track_ai_records(IgnTrack *t, const IgnLevel *lv);
/* Object of a signed section: g_sections+0 on the main route, +0xc (branch B object) for negative sections. */
int ign_track_section_object(const IgnTrack *t, int s);
/* ai_find_section 0x1decc: select a route record, applying branch fallback and invalid-record advancement. */
int ign_track_find_section(const IgnTrack *t, int section, int branch, int step, int track_id,
                           int *out_section, int *out_branch, int *out_object);
/* build_sections 0x2970f-0x2a30a: g_sections+4 (+0x10 on branch B) = -atan2(x - x_next, z - z_next) of the PLC
 * origins of the section object and the next one (atan2 0x647ef swaps st0/st1 before fpatan), in [0, 2pi). */
int ign_track_section_angle(const IgnTrack *t, const IgnLevel *lv, int s, double *out);

/* sub_000247b0 + sub_00028f08. Returns 0 on success. */
int ign_build_track(const IgnModel *m, IgnTrack *t);
void ign_free_track(IgnTrack *t);

#endif
