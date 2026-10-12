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
    memset(t, 0, sizeof *t);
}

static void object_gate(const IgnTrack *t, const IgnLevel *lv, int o, float g[4]) {
    const IgnObject *obj = &lv->track.objs[o];
    const float *c = t->center[o];
    int va = -1, vb = -1;
    size_t at = 4 + (size_t)o * 500;
    if (lv->tri && at + 0x19 <= lv->tri_size) {
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
