#ifndef IGN_TRACK_H
#define IGN_TRACK_H

#include "assets.h"

#define IGN_NO_SECTION 10000

#define IGN_AI_ROUTE_PATHS 2
#define IGN_AI_ROUTE_RECORDS 2

typedef struct {
    int32_t point[6]; /* load_tri +0x02..+0x16: left xyz followed by right xyz */
    int object; /* section object: PLC index, not an engine-pool index */
    int plc;    /* same PLC identity retained for asset validation */
    int8_t surface;   /* +0x01: byte 0 of the TRI block ([0xeb780][plc]), signed t_surface_grip index */
    uint8_t tri_type;
    uint8_t valid;
    float heading[3]; /* load_tri +0x2a/+0x2e/+0x32: left, integer centre, right */
    float slope[3];   /* +0x36/+0x3a/+0x3e: atan2(dy, dx), not a slope magnitude */
    float across[2];  /* +0x42/+0x46 */
    int32_t inset[4]; /* +0x1a..+0x26: left x/z, right x/z, 18 units inward */
    uint8_t geometry_valid;
} IgnAiRouteRecord;

typedef struct IgnTrack {
    int nobj;
    int *klass;        /* PLC type & 0xfff (0 start, 1 start direction, 2/3/7/9 road, 4/5/6 branches) */
    int *section;      /* per object: section index, negative on the second branch, IGN_NO_SECTION if none */
    int *section_b;    /* branch B section index; also maps the shared fork without replacing its main section */
    float (*center)[3];
    int count;         /* [0x1ff004] */
    int start_obj, dir_obj;
    int *section_obj;  /* main route: PLC object of each section */
    int *section_next_obj; /* exact next PLC object on the main route */
    int *section_next_obj_b; /* exact next PLC object on branch B */
    int *section_obj_b; /* branch B object of each signed section */
    float (*gate_b)[4]; /* branch B gates (negative sections), valid where has_b[s] */
    unsigned char *has_b;
    float (*gate)[4];  /* per section: left x,z and right x,z road edges (TRI vertices) */
    IgnAiRouteRecord (*ai_records)[IGN_AI_ROUTE_PATHS][IGN_AI_ROUTE_RECORDS];
    uint8_t (*ai_record_count)[IGN_AI_ROUTE_PATHS];
} IgnTrack;

/* Road edges per section from the TRI blocks (vertex indices at +1 and +0x17). */
void ign_track_gates(IgnTrack *t, const IgnLevel *lv);
/* load_tri 0x1ce30: rebuilds the ordered gate records, including TRI split types 1 and 2. */
int ign_track_ai_records(IgnTrack *t, const IgnLevel *lv, int track_id);
/* Two original slots: a split main gate, otherwise main gate and unsplit branch B. */
const IgnAiRouteRecord *ign_track_ai_record(const IgnTrack *t, int section, int slot);
/* load_tri 0x1d80a..0x1da22 / 0x1da75..0x1dc8c, for two valid records. */
void ign_track_ai_geometry(IgnAiRouteRecord *record, const IgnAiRouteRecord *next);
/* PLC object of a signed section: g_sections+0 on main, +0xc (branch B) for negative sections. */
int ign_track_section_object(const IgnTrack *t, int s);
/* ai_find_section 0x1decc: select a route record, applying branch fallback and invalid-record advancement. */
int ign_track_find_section(const IgnTrack *t, int section, int branch, int step, int track_id,
                           int *out_section, int *out_branch, int *out_object);
/* build_sections 0x2970f-0x2a30a: route adjacency and g_sections+4 (+0x10 on branch B) use PLC origins. */
int ign_track_section_angle(const IgnTrack *t, const IgnLevel *lv, int s, double *out);

/* sub_000247b0 + sub_00028f08. Returns 0 on success. */
int ign_build_track(const IgnModel *m, IgnTrack *t);
void ign_free_track(IgnTrack *t);

#endif
