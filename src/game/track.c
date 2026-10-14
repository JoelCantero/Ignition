#include "track.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void face_sum(const IgnObject *o, const IgnFace *f, double s[3]) {
    for (int k = 0; k < 3; k++)
        for (int c = 0; c < 3; c++) s[c] += o->verts[f->v[k] * 3 + c] + o->pos[c];
}

/* Mean of the road faces of an object (first pass of sub_00028f08). */
static void object_center(const IgnObject *o, float out[3]) {
    for (int pass = 0; pass < 3; pass++) {
        double s[3] = {0, 0, 0};
        int n = 0;
        for (int i = 0; i < o->nf; i++) {
            int type = (int)(o->faces[i].flags >> 16);
            int take = pass == 2 || (type < 0x28 && (pass == 1 || type % 10 < 5 || type % 10 == 6));
            if (take) face_sum(o, &o->faces[i], s), n += 3;
        }
        if (n) {
            for (int c = 0; c < 3; c++) out[c] = (float)(s[c] / n);
            return;
        }
    }
    for (int c = 0; c < 3; c++) out[c] = (float)o->pos[c];
}

static double dist(const IgnTrack *t, int a, int b) {
    double dx = t->center[a][0] - t->center[b][0];
    double dy = (t->center[a][1] - t->center[b][1]) * 2.0; /* height differences count double */
    double dz = t->center[a][2] - t->center[b][2];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static int nearest(const IgnTrack *t, const unsigned char *visited, int from, const int *classes,
                   int nclasses, int skip_visited) {
    int best = -1;
    double best_d = 1e8;
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
    t->center = calloc((size_t)m->nobj + 1, sizeof *t->center);
    unsigned char *visited = calloc((size_t)m->nobj + 1, 1);
    if (!t->klass || !t->section || !t->center || !visited) goto fail;

    int road = 0;
    t->start_obj = t->dir_obj = -1;
    for (int i = 0; i < m->nobj; i++) {
        t->klass[i] = (int)(m->objs[i].type & 0xfff);
        t->section[i] = IGN_NO_SECTION;
        if (t->klass[i] <= 50) object_center(&m->objs[i], t->center[i]);
        else for (int c = 0; c < 3; c++) t->center[i][c] = (float)m->objs[i].pos[c];
        if (t->klass[i] < 50 && t->klass[i] != 8) road++;
        if (t->klass[i] == 0 && t->start_obj < 0) t->start_obj = i;
        if (t->klass[i] == 1 && t->dir_obj < 0) t->dir_obj = i;
    }
    if (t->start_obj < 0 || t->dir_obj < 0) goto fail;

    static const int main_road[] = {2, 3, 7, 9}, branch_a[] = {4, 6}, branch_b[] = {5, 6};
    static const int jump[] = {7};
    int s = 0, cur = t->dir_obj;
    t->section[t->start_obj] = s++;
    visited[t->start_obj] = 1;

    for (int step = 0; step < road - 3; step++) {
        int next = nearest(t, visited, cur, main_road, 4, 1);
        visited[cur] = 1;
        t->section[cur] = s++;
        if (next < 0) break;
        cur = next;

        if (t->klass[cur] == 3) {
            /* Fork: branch A keeps positive sections, branch B reuses them negated, both end at class 6. */
            int fork = cur, s_fork = s, merge = -1;
            while (t->klass[cur] != 6) {
                int n = nearest(t, visited, cur, branch_a, 2, 1);
                visited[cur] = 1;
                t->section[cur] = s++;
                if (n < 0) break;
                cur = n;
                step++;
            }
            merge = cur;
            int s_end = s;
            s = s_fork;
            cur = fork;
            while (t->klass[cur] != 6) {
                int n = nearest(t, visited, cur, branch_b, 2, 1);
                visited[cur] = 1;
                t->section[cur] = -s;
                s++;
                if (n < 0) break;
                cur = n;
                step++;
            }
            s = s_end;
            cur = merge;
        }
        if (t->klass[cur] == 7) {
            int n = nearest(t, visited, cur, jump, 1, 1);
            if (n >= 0) {
                visited[cur] = 1;
                t->section[cur] = s++;
                cur = n;
            }
        }
    }
    visited[cur] = 1;
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
    free(t->center);
    free(t->section_obj);
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
    t->section_obj = malloc(sizeof *t->section_obj * (size_t)(t->count + 1));
    t->gate = calloc((size_t)t->count + 1, sizeof *t->gate);
    t->gate_b = calloc((size_t)t->count + 1, sizeof *t->gate_b);
    t->has_b = calloc((size_t)t->count + 1, 1);
    if (!t->section_obj || !t->gate || !t->gate_b || !t->has_b) return;
    for (int s = 0; s < t->count; s++) t->section_obj[s] = -1;
    for (int i = 0; i < t->nobj; i++) {
        int s = t->section[i];
        if (s >= 0 && s < t->count) t->section_obj[s] = i;
        else if (s < 0 && s != IGN_NO_SECTION && -s < t->count) {
            object_gate(t, lv, i, t->gate_b[-s]);
            t->has_b[-s] = 1;
        }
    }
    for (int s = 0; s < t->count; s++)
        if (t->section_obj[s] >= 0) object_gate(t, lv, t->section_obj[s], t->gate[s]);
    /* Sections without an object reuse the previous gate. */
    for (int s = 1; s < t->count; s++)
        if (t->section_obj[s] < 0) memcpy(t->gate[s], t->gate[s - 1], sizeof t->gate[s]);
}

static int route_record_for_object(const IgnLevel *lv, int object, IgnAiRouteRecord *record) {
    if (object < 0 || object >= lv->track.nobj || !lv->tri || !lv->surface.engine_to_plc ||
        object >= lv->surface.nengine)
        return 0;
    int plc = lv->surface.engine_to_plc[object];
    if (plc < 0 || plc >= lv->track.nobj || lv->tri_size < 4 + 0x6e ||
        (size_t)plc > (lv->tri_size - 4 - 0x6e) / 500)
        return 0;

    const IgnObject *obj = &lv->track.objs[object];
    const uint8_t *tri = lv->tri + 4 + (size_t)plc * 500;
    int left = tri[1] | (int)tri[2] << 8;
    int right = tri[0x17] | (int)tri[0x18] << 8;
    if (!obj->verts || left >= obj->nv || right >= obj->nv || left == right)
        return 0;

    record->point[0] = obj->verts[left * 3] + obj->pos[0] + IGN_SRF_ORIGIN;
    record->point[1] = obj->verts[left * 3 + 1] + obj->pos[1];
    record->point[2] = obj->verts[left * 3 + 2] + obj->pos[2] + IGN_SRF_ORIGIN;
    record->point[3] = obj->verts[right * 3] + obj->pos[0] + IGN_SRF_ORIGIN;
    record->point[4] = obj->verts[right * 3 + 1] + obj->pos[1];
    record->point[5] = obj->verts[right * 3 + 2] + obj->pos[2] + IGN_SRF_ORIGIN;
    record->object = object;
    record->plc = plc;
    record->plc_type = (uint8_t)obj->type;
    record->tri_type = tri[0x6d];
    record->valid = 1;
    return 1;
}

static int32_t midpoint(int32_t a, int32_t b) {
    return (int32_t)((int64_t)a + ((int64_t)b - a) / 2);
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

int ign_track_ai_records(IgnTrack *t, const IgnLevel *lv) {
    if (!t || !lv) return -1;
    free(t->ai_records);
    free(t->ai_record_count);
    t->ai_records = NULL;
    t->ai_record_count = NULL;
    if (t->count <= 0 || !t->section || !t->section_obj || !lv->tri ||
        !lv->surface.engine_to_plc || !lv->track.objs)
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
        for (int i = 0; i < t->nobj; i++)
            if (t->section[i] == -s && s != 0) {
                objects[1] = i;
                break;
            }
        for (int path = 0; path < 2; path++) {
            IgnAiRouteRecord record = {0};
            if (!route_record_for_object(lv, objects[path], &record)) continue;
            if (t->ai_record_count[s][path] >= IGN_AI_ROUTE_RECORDS) goto fail;
            t->ai_records[s][path][t->ai_record_count[s][path]++] = record;
            if (record.tri_type == 1 || record.tri_type == 2) {
                if (t->ai_record_count[s][path] >= IGN_AI_ROUTE_RECORDS) goto fail;
                IgnAiRouteRecord split = record;
                split_route_record(&t->ai_records[s][path][t->ai_record_count[s][path] - 1], &split);
                t->ai_records[s][path][t->ai_record_count[s][path]++] = split;
            }
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
    if (t->count <= 0) return -1;
    if (s >= 0) return t->section_obj ? t->section_obj[s % t->count] : -1;
    for (int i = 0; i < t->nobj; i++)
        if (t->section[i] == s) return i;
    return -1;
}

static int route_object(const IgnTrack *t, int section, int branch) {
    if (branch) {
        if (!t->has_b || !t->has_b[section]) return -1;
        return ign_track_section_object(t, -section);
    }
    return t->section_obj ? t->section_obj[section] : -1;
}

static int route_record_valid(const IgnTrack *t, int section, int branch) {
    return t->ai_records && t->ai_record_count &&
           t->ai_record_count[section][branch] > 0 &&
           t->ai_records[section][branch][0].valid;
}

int ign_track_find_section(const IgnTrack *t, int section, int branch, int step, int track_id,
                           int *out_section, int *out_branch, int *out_object) {
    if (!t || t->count <= 0 || !t->section_obj || (branch != 0 && branch != 1) || step == 0)
        return 0;

    int candidate = (int16_t)section;
    int count = (int16_t)t->count;
    if (candidate >= count) candidate -= count;
    if (candidate < 0) candidate += count;
    if (candidate < 0 || candidate >= count) return 0;

    for (int searched = 0; searched < count; searched++) {
        int object = route_object(t, candidate, branch);
        if (object < 0) {
            branch = !branch;
            object = route_object(t, candidate, branch);
        }
        if (object < 0) return 0;

        if (route_record_valid(t, candidate, branch)) {
            if (out_section) *out_section = candidate;
            if (out_branch) *out_branch = branch;
            if (out_object) *out_object = object;
            return 1;
        }

        int previous = candidate > 0 ? candidate - 1 : 0;
        int current_has_branch = t->has_b && t->has_b[candidate];
        int previous_has_branch = t->has_b && t->has_b[previous];
        int may_accept_invalid = current_has_branch || !previous_has_branch;
        if (track_id == 0 && candidate >= 0xab && candidate <= 0xad)
            may_accept_invalid = 0;
        if (may_accept_invalid) {
            if (out_section) *out_section = candidate;
            if (out_branch) *out_branch = branch;
            if (out_object) *out_object = object;
            return 1;
        }

        candidate += step;
        if (candidate >= count) candidate -= count;
        if (candidate < 0) candidate += count;
        if (candidate < 0 || candidate >= count) return 0;
    }
    return 0;
}

int ign_track_section_angle(const IgnTrack *t, const IgnLevel *lv, int s, double *out) {
    int o0 = ign_track_section_object(t, s), o1 = ign_track_section_object(t, s >= 0 ? s + 1 : s - 1);
    /* The last branch B section leads to the merge object (class 6) closest to it. */
    if (s < 0 && o0 >= 0 && o1 < 0) {
        double best = INFINITY;
        for (int i = 0; i < t->nobj; i++) {
            if (t->klass[i] != 6 || t->section[i] < 0 || t->section[i] == IGN_NO_SECTION) continue;
            double dx = t->center[i][0] - t->center[o0][0], dz = t->center[i][2] - t->center[o0][2];
            if (dx * dx + dz * dz < best) best = dx * dx + dz * dz, o1 = i;
        }
    }
    if (s < 0 && o1 < 0) o1 = ign_track_section_object(t, -s + 1);
    if (o0 < 0 || o1 < 0) return 0;
    const int32_t *a = lv->track.objs[o0].pos, *b = lv->track.objs[o1].pos;
    double ang = -atan2((double)a[0] - b[0], (double)a[2] - b[2]);
    if (ang < 0) ang += 2 * 3.14159265358979323846;
    if (ang >= 2 * 3.14159265358979323846) ang -= 2 * 3.14159265358979323846;
    *out = ang;
    return 1;
}
