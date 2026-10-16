#include "track.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void face_sum(const IgnObject *o, const IgnFace *f, uint32_t s[3]) {
    for (int k = 0; k < 3; k++)
        for (int c = 0; c < 3; c++)
            s[c] += (uint32_t)o->verts[f->v[k] * 3 + c] + (uint32_t)o->pos[c];
}

/* Mean of the road faces of an object (first pass of sub_00028f08). */
static void object_center(const IgnObject *o, int32_t out[3]) {
    for (int pass = 0; pass < 3; pass++) {
        uint32_t s[3] = {0, 0, 0};
        int n = 0;
        for (int i = 0; i < o->nf; i++) {
            int type = (int)(o->faces[i].flags >> 16);
            int take = pass == 2 || (type < 0x28 && (pass == 1 || type % 10 < 5 || type % 10 == 6));
            if (take) face_sum(o, &o->faces[i], s), n += 3;
        }
        if (n) {
            for (int c = 0; c < 3; c++) out[c] = (int32_t)s[c] / n;
            return;
        }
    }
    for (int c = 0; c < 3; c++) out[c] = o->pos[c];
}

/* sub_00028f08: normalize mutable PLC/MSH origins before either engine grid is created. */
int ign_track_recenter_model(IgnModel *m) {
    if (!m) return -1;
    if (m->track_recentered) return 0;
    if (!m->plc || !m->msh || m->cell || m->stamp) return -1;
    for (int i = 0; i < m->nobj; i++) {
        IgnObject *o = &m->objs[i];
        if ((o->type & 0xfff) > 50) continue;
        int32_t center[3], pos[3];
        object_center(o, center);
        for (int c = 0; c < 3; c++)
            pos[c] = c == 1 ? (int32_t)(0u - (uint32_t)center[c]) : center[c];
        int32_t *verts = (int32_t *)o->verts;
        for (int v = 0; v < o->nv; v++)
            for (int c = 0; c < 3; c++)
                verts[v * 3 + c] = (int32_t)((uint32_t)verts[v * 3 + c] +
                                             (uint32_t)o->pos[c] - (uint32_t)pos[c]);
        for (int c = 0; c < 3; c++) {
            o->pos[c] = pos[c];
            uint32_t raw = (uint32_t)center[c];
            uint8_t *p = m->plc + 12 + (size_t)i * 20 + c * 4;
            for (int b = 0; b < 4; b++) p[b] = (uint8_t)(raw >> (b * 8));
        }
    }
    m->track_recentered = 1;
    return 0;
}

static double dist(const IgnTrack *t, int a, int b) {
    /* The original first recentres PLC origins and compensates the mesh vertices. */
    double dx = (double)t->center[a][0] - t->center[b][0];
    double dy = ((double)t->center[a][1] - t->center[b][1]) * 2.0;
    double dz = (double)t->center[a][2] - t->center[b][2];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static int nearest(const IgnTrack *t, const unsigned char *visited, int from,
                   const int *classes, int nclasses, int skip_visited) {
    int best = from;
    double best_d = 99999999.0;
    for (int i = 0; i < t->nobj; i++) {
        if (i == from || (skip_visited && visited[i])) continue;
        int ok = 0;
        for (int k = 0; k < nclasses; k++) ok |= t->klass[i] == classes[k];
        if (!ok) continue;
        double d = dist(t, from, i);
        if (d < best_d) best_d = d, best = i;
    }
    return best;
}

int ign_build_track(const IgnModel *m, IgnTrack *t) {
    memset(t, 0, sizeof *t);
    t->nobj = m->nobj;
    t->klass = calloc((size_t)m->nobj + 1, sizeof *t->klass);
    t->section = calloc((size_t)m->nobj + 1, sizeof *t->section);
    t->section_b = calloc((size_t)m->nobj + 1, sizeof *t->section_b);
    t->center = calloc((size_t)m->nobj + 1, sizeof *t->center);
    t->section_obj = malloc(sizeof *t->section_obj * ((size_t)m->nobj + 1));
    t->section_obj_b = malloc(sizeof *t->section_obj_b * ((size_t)m->nobj + 1));
    unsigned char *visited = calloc((size_t)m->nobj + 1, 1);
    if (!t->klass || !t->section || !t->section_b || !t->center || !t->section_obj ||
        !t->section_obj_b || !visited)
        goto fail;

    int road = 0;
    t->start_obj = t->dir_obj = -1;
    for (int i = 0; i < m->nobj; i++) {
        t->klass[i] = (int)(m->objs[i].type & 0xfff);
        t->section[i] = IGN_NO_SECTION;
        t->section_b[i] = IGN_NO_SECTION;
        int32_t center[3];
        if (t->klass[i] <= 50) object_center(&m->objs[i], center);
        else for (int c = 0; c < 3; c++) center[c] = m->objs[i].pos[c];
        for (int c = 0; c < 3; c++) t->center[i][c] = (float)center[c];
        if (t->klass[i] < 50 && t->klass[i] != 8) road++;
        if (t->klass[i] == 0 && t->start_obj < 0) t->start_obj = i;
        if (t->klass[i] == 1 && t->dir_obj < 0) t->dir_obj = i;
    }
    if (t->start_obj < 0 || t->dir_obj < 0) goto fail;

    t->section_next_obj = malloc(sizeof *t->section_next_obj * ((size_t)m->nobj + 1));
    t->section_next_obj_b = malloc(sizeof *t->section_next_obj_b * ((size_t)m->nobj + 1));
    if (!t->section_next_obj || !t->section_next_obj_b) goto fail;
    for (int i = 0; i <= m->nobj; i++)
        t->section_next_obj[i] = t->section_next_obj_b[i] = -1;
    for (int i = 0; i <= m->nobj; i++) t->section_obj[i] = t->section_obj_b[i] = -1;

    static const int main_road[] = {2, 3, 7, 9}, branch_a[] = {4, 6}, branch_b[] = {5, 6};
    static const int jump[] = {7};
    int s = 0, cur = t->dir_obj;
    t->section[t->start_obj] = s++;
    t->section_obj[0] = t->start_obj;
    t->section_next_obj[0] = t->dir_obj;
    visited[t->start_obj] = 1;

    for (int step = 0; step < road - 3; step++) {
        if (s > m->nobj) goto fail;
        int next = nearest(t, visited, cur, main_road, 4, 1);
        visited[cur] = 1;
        t->section_next_obj[s] = next;
        t->section_obj[s] = cur;
        t->section_obj_b[s] = -1;
        t->section[cur] = s++;
        if (next < 0) break;
        cur = next;

        if (t->klass[cur] == 3) {
            /* The original stores branch B alongside the main route at each logical section. */
            int fork = cur, s_fork = s;
            while (t->klass[cur] != 6) {
                if (s > m->nobj) goto fail;
                int n = nearest(t, visited, cur, branch_a, 2, 1);
                visited[cur] = 1;
                t->section_next_obj[s] = n;
                t->section_obj[s] = cur;
                t->section_obj_b[s] = -2;
                t->section[cur] = s++;
                if (n == cur) goto fail;
                cur = n;
                if (t->klass[cur] != 6) step++;
            }
            int s_end = s;
            s = s_fork;
            cur = fork;
            while (t->klass[cur] != 6) {
                if (s > m->nobj) goto fail;
                int n = nearest(t, visited, cur, branch_b, 2, 1);
                visited[cur] = 1;
                t->section_next_obj_b[s] = n;
                t->section_obj_b[s] = cur;
                t->section_b[cur] = -s;
                t->section[cur] = -s;
                s++;
                if (n == cur) goto fail;
                cur = n;
                if (t->klass[cur] != 6) step++;
            }
            s = s_end;
        }
        if (t->klass[cur] == 7) {
            int n = nearest(t, visited, cur, jump, 1, 1);
            if (n >= 0) {
                if (s > m->nobj) goto fail;
                visited[cur] = 1;
                if (s > m->nobj) goto fail;
                t->section_next_obj[s] = n;
                t->section_obj[s] = cur;
                t->section_obj_b[s] = -1;
                t->section[cur] = s++;
                cur = n;
            }
        }
    }
    visited[cur] = 1;
    t->section_obj[s] = cur;
    t->section_obj_b[s] = -1;
    t->section_next_obj[s] = t->start_obj;
    t->section[cur] = s++;
    t->count = s;
    free(visited);
    return 0;
fail:
    free(visited);
    ign_free_track(t);
    return -1;
}

void ign_free_track(IgnTrack *t) {
    free(t->klass);
    free(t->section);
    free(t->section_b);
    free(t->center);
    free(t->section_obj);
    free(t->section_next_obj);
    free(t->section_next_obj_b);
    free(t->section_obj_b);
    free(t->gate);
    free(t->gate_b);
    free(t->has_b);
    free(t->ai_records);
    free(t->ai_record_count);
    memset(t, 0, sizeof *t);
}

static void object_gate(const IgnTrack *t, const IgnLevel *lv, int o, float g[4]) {
    const IgnObject *obj = &lv->track.objs[o];
    const float *c = t->center[o];
    int va = -1, vb = -1;
    int plc = o < lv->surface.nengine && lv->surface.engine_to_plc
                  ? lv->surface.engine_to_plc[o]
                  : -1;
    if (lv->tri && plc >= 0 && plc < lv->track.nobj && lv->tri_size >= 4 + 0x19 &&
        (size_t)plc <= (lv->tri_size - 4 - 0x19) / 500) {
        size_t at = 4 + (size_t)plc * 500;
        va = lv->tri[at + 1] | lv->tri[at + 2] << 8;
        vb = lv->tri[at + 0x17] | lv->tri[at + 0x18] << 8;
    }
    if (va >= 0 && vb >= 0 && va < obj->nv && vb < obj->nv && va != vb) {
        g[0] = (float)(obj->verts[va * 3] + obj->pos[0]);
        g[1] = (float)(obj->verts[va * 3 + 2] + obj->pos[2]);
        g[2] = (float)(obj->verts[vb * 3] + obj->pos[0]);
        g[3] = (float)(obj->verts[vb * 3 + 2] + obj->pos[2]);
    } else {
        g[0] = g[2] = c[0];
        g[1] = g[3] = c[2];
    }
}

void ign_track_gates(IgnTrack *t, const IgnLevel *lv) {
    int map_main = !t->section_obj;
    int map_branch = !t->section_obj_b;
    if (map_main) t->section_obj = malloc(sizeof *t->section_obj * (size_t)(t->count + 1));
    if (map_branch) t->section_obj_b = malloc(sizeof *t->section_obj_b * (size_t)(t->count + 1));
    t->gate = calloc((size_t)t->count + 1, sizeof *t->gate);
    t->gate_b = calloc((size_t)t->count + 1, sizeof *t->gate_b);
    t->has_b = calloc((size_t)t->count + 1, 1);
    if (!t->section_obj || !t->section_obj_b || !t->gate || !t->gate_b || !t->has_b) return;
    if (map_main)
        for (int s = 0; s < t->count; s++) t->section_obj[s] = -1;
    if (map_branch)
        for (int s = 0; s < t->count; s++) t->section_obj_b[s] = -1;
    if (map_main || map_branch) {
        for (int i = 0; i < t->nobj; i++) {
            int s = t->section[i];
            if (map_main && s >= 0 && s < t->count) t->section_obj[s] = i;
            int section_b = t->section_b ? t->section_b[i] : IGN_NO_SECTION;
            if (section_b == IGN_NO_SECTION && s < 0) section_b = s;
            if (map_branch && section_b < 0 && section_b != IGN_NO_SECTION &&
                -section_b < t->count)
                t->section_obj_b[-section_b] = i;
        }
    }
    for (int s = 0; s < t->count; s++) {
        if (t->section_obj[s] >= 0) object_gate(t, lv, t->section_obj[s], t->gate[s]);
        if (t->section_obj_b[s] >= 0) {
            object_gate(t, lv, t->section_obj_b[s], t->gate_b[s]);
            t->has_b[s] = 1;
        }
    }
    /* Sections without an object reuse the previous gate. */
    for (int s = 1; s < t->count; s++)
        if (t->section_obj[s] < 0) memcpy(t->gate[s], t->gate[s - 1], sizeof t->gate[s]);
}

static int route_record_for_object(const IgnTrack *t, const IgnLevel *lv, int object,
                                   IgnAiRouteRecord *record) {
    if (object < 0 || object >= lv->track.nobj || !lv->tri)
        return 0;
    /* g_sections indexes g_level_plc (0x1d246..0x1d261), not engine objects. */
    int plc = object;
    if (plc < 0 || plc >= lv->track.nobj || lv->tri_size < 4 + 0x6e ||
        (size_t)plc > (lv->tri_size - 4 - 0x6e) / 500)
        return 0;

    const IgnObject *obj = &lv->track.objs[object];
    const uint8_t *tri = lv->tri + 4 + (size_t)plc * 500;
    int left = (int16_t)(tri[1] | (int)tri[2] << 8);
    int right = (int16_t)(tri[0x17] | (int)tri[0x18] << 8);
    if (!obj->verts || left < 0 || right < 0 || left >= obj->nv || right >= obj->nv)
        return 0;

    record->point[0] = obj->verts[left * 3] + obj->pos[0] + IGN_SRF_ORIGIN;
    record->point[1] = obj->verts[left * 3 + 1] + obj->pos[1];
    record->point[2] = obj->verts[left * 3 + 2] + obj->pos[2] + IGN_SRF_ORIGIN;
    record->point[3] = obj->verts[right * 3] + obj->pos[0] + IGN_SRF_ORIGIN;
    record->point[4] = obj->verts[right * 3 + 1] + obj->pos[1];
    record->point[5] = obj->verts[right * 3 + 2] + obj->pos[2] + IGN_SRF_ORIGIN;
    /* build_sections adds the new PLC y to local vertices; load_tri adds it again. */
    if (t->center) {
        record->point[1] += 2 * (int32_t)t->center[object][1];
        record->point[4] += 2 * (int32_t)t->center[object][1];
    }
    record->object = object;
    record->plc = plc;
    record->surface = (int8_t)tri[0]; /* 0x1d2eb..0x1d2f1 */
    record->tri_type = tri[0x6d];
    record->valid = 1;
    return 1;
}

static int32_t midpoint(int32_t a, int32_t b) {
    return (int32_t)((int64_t)a + ((int64_t)b - a) / 2);
}

/* 0x1d9d0..0x1da22 under the runtime control word 0x127f: fcos/fsin are not rounded by the precision
 * control, but the following fmul/faddp round to binary64 before __CHP truncates. */
static int32_t inset_coordinate(int32_t origin, double angle, int sine) {
    double value = (double)origin + 18 * (sine ? sin(angle) : cos(angle));
    double integer = trunc(value);
    return integer < INT32_MIN || integer > INT32_MAX ? INT32_MIN : (int32_t)integer;
}

void ign_track_ai_geometry(IgnAiRouteRecord *record, const IgnAiRouteRecord *next) {
    int32_t current[3][3], following[3][3];
    for (int axis = 0; axis < 3; axis++) {
        current[0][axis] = record->point[axis];
        current[1][axis] = midpoint(record->point[axis], record->point[axis + 3]);
        current[2][axis] = record->point[axis + 3];
        following[0][axis] = next->point[axis];
        following[1][axis] = midpoint(next->point[axis], next->point[axis + 3]);
        following[2][axis] = next->point[axis + 3];
    }
    for (int edge = 0; edge < 3; edge++) {
        double dx = (double)following[edge][0] - current[edge][0];
        double dy = (double)following[edge][1] - current[edge][1];
        double dz = (double)following[edge][2] - current[edge][2];
        record->heading[edge] = (float)atan2(dz, dx);
        record->slope[edge] = (float)atan2(dy, dx);
    }
    double dx = (double)record->point[3] - record->point[0];
    double dz = (double)record->point[5] - record->point[2];
    record->across[0] = (float)atan2(dz, dx);
    record->across[1] = (float)atan2((double)record->point[2] - record->point[5],
                                   (double)record->point[0] - record->point[3]);
    for (int edge = 0; edge < 2; edge++) {
        double angle = record->across[edge];
        record->inset[edge * 2] = inset_coordinate(record->point[edge * 3], angle, 0);
        record->inset[edge * 2 + 1] = inset_coordinate(record->point[edge * 3 + 2], angle, 1);
    }
    record->geometry_valid = 1;
}

static void split_route_record(IgnAiRouteRecord *first, IgnAiRouteRecord *second) {
    *second = *first;
    int32_t original[6];
    memcpy(original, first->point, sizeof original);
    for (int axis = 0; axis < 3; axis++) {
        int a = axis, b = axis + 3;
        int32_t middle = midpoint(original[a], original[b]);
        if (first->tri_type == 1) {
            first->point[a] = middle;
            second->point[a] = original[a];
            second->point[b] = middle;
        } else {
            first->point[b] = middle;
            second->point[a] = middle;
            second->point[b] = original[b];
        }
    }
}

const IgnAiRouteRecord *ign_track_ai_record(const IgnTrack *t, int section, int slot) {
    if (!t->ai_records || !t->ai_record_count || section < 0 || section >= t->count ||
        (slot != 0 && slot != 1))
        return NULL;
    int path = slot, record = 0;
    if (t->ai_record_count[section][0] > 1) path = 0, record = slot;
    return t->ai_record_count[section][path] > record ?
               &t->ai_records[section][path][record] : NULL;
}

int ign_track_ai_records(IgnTrack *t, const IgnLevel *lv, int track_id) {
    if (!t || !lv) return -1;
    free(t->ai_records);
    free(t->ai_record_count);
    t->ai_records = NULL;
    t->ai_record_count = NULL;
    if (t->count <= 0 || !t->section || !t->section_obj || !lv->tri ||
        !lv->track.objs)
        return -1;
    t->ai_records = calloc((size_t)t->count, sizeof *t->ai_records);
    t->ai_record_count = calloc((size_t)t->count, sizeof *t->ai_record_count);
    if (!t->ai_records || !t->ai_record_count) {
        free(t->ai_records);
        free(t->ai_record_count);
        t->ai_records = NULL;
        t->ai_record_count = NULL;
        return -1;
    }

    for (int s = 0; s < t->count; s++) {
        int objects[2] = {t->section_obj ? t->section_obj[s] : -1, -1};
        if (t->section_obj_b) objects[1] = t->section_obj_b[s];
        else if (s > 0) objects[1] = ign_track_section_object(t, -s);
        for (int path = 0; path < 2; path++) {
            IgnAiRouteRecord record = {0};
            if (objects[path] == -2) {
                record.object = record.plc = -2;
                t->ai_records[s][path][0] = record;
                t->ai_record_count[s][path] = 1;
                continue;
            }
            if (!route_record_for_object(t, lv, objects[path], &record)) continue;
            if (t->ai_record_count[s][path] >= IGN_AI_ROUTE_RECORDS) goto fail;
            t->ai_records[s][path][t->ai_record_count[s][path]++] = record;
            if (path == 0 && (record.tri_type == 1 || record.tri_type == 2)) {
                if (t->ai_record_count[s][path] >= IGN_AI_ROUTE_RECORDS) goto fail;
                IgnAiRouteRecord split = record;
                split_route_record(&t->ai_records[s][path][t->ai_record_count[s][path] - 1], &split);
                t->ai_records[s][path][t->ai_record_count[s][path]++] = split;
                break; /* The main split already owns both original slots. */
            }
        }
    }
    for (int s = 0; s < t->count; s++) {
        for (int slot = 0; slot < 2; slot++) {
            int path = t->ai_record_count[s][0] > 1 ? 0 : slot;
            int r = path == slot ? 0 : slot;
            if (t->ai_record_count[s][path] <= r) continue;
            IgnAiRouteRecord *record = &t->ai_records[s][path][r];
            int next_section, next_slot;
            if (!record->valid || !ign_track_find_section(t, s + 1, slot, 1, track_id,
                                                         &next_section, &next_slot, NULL))
                continue;
            const IgnAiRouteRecord *next = ign_track_ai_record(t, next_section, next_slot);
            if (next && next->valid) ign_track_ai_geometry(record, next);
        }
    }
    return 0;
fail:
    free(t->ai_records);
    free(t->ai_record_count);
    t->ai_records = NULL;
    t->ai_record_count = NULL;
    return -1;
}

int ign_track_section_object(const IgnTrack *t, int s) {
    if (!t || t->count <= 0) return -1;
    if (s >= 0) return t->section_obj ? t->section_obj[s % t->count] : -1;
    int section = -s;
    if (section < t->count && t->section_obj_b)
        return t->section_obj_b[section];
    for (int i = 0; i < t->nobj; i++)
        if ((t->section_b && t->section_b[i] == s) || t->section[i] == s) return i;
    return -1;
}

static int route_object(const IgnTrack *t, int section, int branch) {
    if (branch) {
        return t->section_obj_b ? t->section_obj_b[section] :
               section > 0 ? ign_track_section_object(t, -section) : -1;
    }
    return t->section_obj ? t->section_obj[section] : -1;
}

int ign_track_find_section(const IgnTrack *t, int section, int branch, int step, int track_id,
                           int *out_section, int *out_branch, int *out_object) {
    if (!t || t->count <= 0 || !t->section_obj || (branch != 0 && branch != 1))
        return 0;

    int candidate = (int16_t)section;
    int count = (int16_t)t->count;
    if (candidate >= count) candidate -= count;
    else if (candidate < 0) candidate += count;
    if (candidate < 0 || candidate >= count) return 0;

    for (int searched = 0; searched < count; searched++) {
        const IgnAiRouteRecord *record = ign_track_ai_record(t, candidate, branch);
        if (!record) {
            branch = !branch;
            record = ign_track_ai_record(t, candidate, branch);
        }
        if (!record) return 0;

        int previous = candidate > 0 ? candidate - 1 : 0;
        int current_has_branch = ign_track_ai_record(t, candidate, 1) != NULL;
        int previous_has_branch = ign_track_ai_record(t, previous, 1) != NULL;
        int accept = record->valid && (current_has_branch || !previous_has_branch);
        if (track_id == 0 && candidate >= 0xab && candidate <= 0xad)
            accept = 0;
        if (accept) {
            if (out_section) *out_section = candidate;
            if (out_branch) *out_branch = branch;
            if (out_object) *out_object = route_object(t, candidate, branch);
            return 1;
        }

        candidate = (int16_t)((uint32_t)candidate + (uint32_t)step);
        if (candidate >= count) candidate -= count;
        else if (candidate < 0) candidate += count;
        if (candidate < 0 || candidate >= count) return 0;
    }
    return 0;
}

int ign_track_section_angle(const IgnTrack *t, const IgnLevel *lv, int s, double *out) {
    if (!t || !lv || !out) return 0;
    int section = s < 0 ? -s : s;
    int o0 = ign_track_section_object(t, s);
    int o1 = -1;
    if (section >= 0 && section < t->count) {
        int *next = s < 0 ? t->section_next_obj_b : t->section_next_obj;
        if (next) o1 = next[section];
        if (o1 < 0 && s >= 0)
            o1 = ign_track_section_object(t, (section + 1) % t->count);
    }
    if (o0 < 0) {
        *out = o0;
        return 1;
    }
    if (o1 < 0) return 0;
    double dx = t->center ? (double)t->center[o0][0] - t->center[o1][0] :
                            (double)lv->track.objs[o0].pos[0] - lv->track.objs[o1].pos[0];
    double dz = t->center ? (double)t->center[o0][2] - t->center[o1][2] :
                            (double)lv->track.objs[o0].pos[2] - lv->track.objs[o1].pos[2];
    double ang = -atan2(dx, dz);
    if (ang < 0) ang += 6.2831853071796;
    if (ang >= 6.2831853071796) ang -= 6.2831853071796;
    *out = ang;
    return 1;
}
