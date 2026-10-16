#include "physics.h"

#include <math.h>

#define G 9.81
#define HALF_PI 1.570798 /* the original constant, not M_PI_2 */

/* 0xc4e20 */
const int32_t ign_car_mass[12] = {500, 570, 750, 570, 600, 1000, 1000, 500, 1000, 600, 500, 900};

/* 0xc4fd0: per gear {x0, y0, x1, y1, x2, y2, x3, y3}; gear 0 is reverse. */
const int32_t ign_car_gears[12][3][8] = {
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 30, 50, 60, 90, 0, 199, -50}, {0, 15, 80, 50, 131, 0, 199, -34}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 20, 50, 45, 100, 0, 199, -50}, {0, 5, 90, 50, 135, 0, 199, -30}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 35, 50, 50, 85, 0, 199, -50}, {0, 20, 80, 40, 122, 0, 199, -30}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 15, 60, 50, 105, 0, 199, -50}, {0, 5, 100, 50, 130, 0, 199, -40}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 25, 50, 50, 95, 0, 199, -50}, {0, 5, 90, 50, 131, 0, 199, -37}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 95, 50, 80, 90, 0, 199, -50}, {0, 60, 90, 60, 129, 0, 199, -35}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 80, 60, 60, 100, 0, 199, -50}, {0, 30, 90, 60, 136, 0, 199, -35}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 20, 60, 40, 100, 0, 199, -50}, {0, 10, 100, 50, 126, 0, 199, -35}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 110, 50, 85, 95, 0, 199, -50}, {0, 60, 80, 50, 124, 0, 199, -40}},
    {{0, 10, 30, 20, 50, 0, 199, -50}, {0, 30, 50, 50, 105, 0, 199, -50}, {0, 5, 90, 50, 137, 0, 199, -30}},
    {{0, 10, 30, 20, 50, 0, 199, -50}, {0, 60, 70, 80, 110, 0, 199, -50}, {0, 50, 90, 90, 140, 0, 199, -25}},
    {{0, 30, 30, 30, 60, 0, 199, -50}, {0, 70, 60, 50, 100, 0, 199, -50}, {0, 30, 90, 60, 135, 0, 199, -40}},
};

/* 0xc4e80 / 0xc4ee0 / 0xc4f40 */
const double ign_car_steer_speed[12] = {20, 40, 40, 40, 40, 130, 40, 40, 130, 40, 40, 40};
const double ign_car_steer_div[12] = {90, 90, 90, 80, 80, 205, 90, 80, 205, 80, 80, 90};
const int32_t ign_car_steer_rate[12] = {14, 6, 16, 11, 9, 7, 10, 8, 9, 5, 5, 1};

/* 0xc4dc0 */
const double ign_car_grip[12] = {1.3, 1.0, 1.3, 1.1, 1.1, 1.2, 1.01, 1.2, 1.5, 1.0, 1.45, 1.3};

/* 0xc4e50 / 0xc4f70: turbo force per charge point and recharge per tick */
const int32_t ign_car_turbo_force[12] = {110, 250, 125, 280, 220, 210, 370, 200, 190, 375, 380, 145};
const double ign_car_turbo_rate[12] = {0.55, 0.4, 0.6, 0.4, 0.45, 0.4, 0.4, 0.5, 0.55, 0.4, 0.4, 0.4};

/* 0xc33f0 */
const double ign_surface_grip[30] = {
    4.5, 4.0, 4.5, 4.5, 3.5, 3.5, 3.5, 4.5, 4.5, 0.01, 4.5, 4.0, 4.5, 4.5, 3.5,
    3.5, 3.5, 4.5, 4.5, 0.01, 4.5, 4.0, 4.5, 4.5, 3.5, 3.5, 3.5, 4.5, 4.5, 0.01,
};

static void hermite(int16_t *out, int n, double y, double c, double bq, double a) {
    for (int t = 0; t < n; t++) {
        /* sub_00055f9c truncates toward zero before fistp. */
        out[t] = (int16_t)trunc(a * t * t * t + bq * t * t + c * t + y);
    }
}

void ign_torque_curve(const int32_t p[8], int16_t out[IGN_TORQUE_POINTS]) {
    int x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
    float s1 = (float)(y2 - y0) / (float)(x2 - x0);
    float s2 = (float)(y3 - y1) / (float)(x3 - x1);
    int d0 = x1 - x0, d1 = x2 - x1, d2 = x3 - x2;
    float a0 = ((s1 + 1.2f) * d0 + 2 * (y0 - y1)) / (float)(d0 * d0 * d0);
    float b0 = (3 * (y1 - y0) - (s1 + 2.4f) * d0) / (float)(d0 * d0);
    float a1 = ((s1 + s2) * d1 + 2 * (y1 - y2)) / (float)(d1 * d1 * d1);
    float b1 = (3 * (y2 - y1) - (2.0f * s1 + s2) * d1) / (float)(d1 * d1);
    float a2 = (s2 * d2 + 2 * (y2 - y3)) / (float)(d2 * d2 * d2);
    float b2 = (3 * (y3 - y2) - 2.0f * s2 * d2) / (float)(d2 * d2);
    int i = 0;
    if (d0 > 0 && i + d0 <= IGN_TORQUE_POINTS) hermite(out + i, d0, y0, 1.2f, b0, a0), i += d0;
    if (d1 > 0 && i + d1 <= IGN_TORQUE_POINTS) hermite(out + i, d1, y1, s1, b1, a1), i += d1;
    if (d2 > 0 && i + d2 <= IGN_TORQUE_POINTS) hermite(out + i, d2, y2, s2, b2, a2), i += d2;
    if (i < IGN_TORQUE_POINTS) out[i] = (int16_t)y3;
}

static int sign(double v) {
    return v < 0 ? -1 : v > 0 ? 1 : 0;
}

/* sub_0001f3fc: impulse against a wall through point (cx, cz) with direction theta. */
static int wall_impulse(IgnBody *b, double cx, double cz, double theta) {
    const double e = 0.6 * 0.3;
    double rx = cx - b->px, rz = cz - b->pz;
    double a = (rx != 0 || rz != 0) ? atan2(rz, rx) : 0.0;
    double r = sqrt(rx * rx + rz * rz);
    double ct = cos(theta), st = sin(theta);
    int side = sign(ct * rz - st * rx);
    double w = b->yaw_rate * r;
    double cvx = b->vx - w * sin(a), cvz = b->vz + w * cos(a);
    double vn = ct * cvz - st * cvx;
    if (side != sign(vn)) return 0;

    b->impact = fabs(vn);
    double k = cos(theta - a) * r * 2.0 * b->mass / b->inertia * 0.5;
    if (k > 0.1) k = 0.1;
    if (k < -0.1) k = -0.1;
    double j = -(1.0 + e) * vn / (1.0 + k * r * cos(a - theta));
    b->yaw_rate += k * j;
    b->vx -= st * j;
    b->vz += ct * j;
    b->px -= sign(j) * 0.25 * st;
    b->pz += sign(j) * 0.25 * ct;
    return 1;
}

static double cross(double ax, double az, double bx, double bz) {
    return ax * bz - az * bx;
}

int ign_body_collide(IgnBody *b, const double (*walls)[4], int nwalls) {
    IgnBodyCorners corners;
    ign_body_corners(b, 1.0, &corners);
    const double *cx = corners.x, *cz = corners.z;
    double hx[8], hz[8];
    int nh = 0;
    for (int w = 0; w < nwalls && nh < 8; w++) {
        double wx0 = walls[w][0], wz0 = walls[w][1], wx1 = walls[w][2], wz1 = walls[w][3];
        double dwx = wx0 - wx1, dwz = wz0 - wz1;
        for (int e = 0; e < 4 && nh < 8; e++) {
            int i = e, j = (e + 1) % 4; /* edge table 0xbfe60: 0-1, 1-2, 2-3, 3-0 */
            double ex = cx[i] - cx[j], ez = cz[i] - cz[j];
            if (sign(cross(dwx, dwz, wx0 - cx[i], wz0 - cz[i])) ==
                sign(cross(dwx, dwz, wx0 - cx[j], wz0 - cz[j])))
                continue;
            if (sign(cross(ex, ez, cx[i] - wx0, cz[i] - wz0)) ==
                sign(cross(ex, ez, cx[i] - wx1, cz[i] - wz1)))
                continue;
            double den = cross(ex, ez, dwx, dwz);
            if (den == 0) continue;
            double t = cross(cx[i] - wx0, cz[i] - wz0, dwx, dwz) / den;
            hx[nh] = cx[i] - t * ex;
            hz[nh] = cz[i] - t * ez;
            nh++;
        }
    }
    if (nh < 2) return 0;

    double mx = 0, mz = 0;
    for (int i = 0; i < nh; i++) mx += hx[i], mz += hz[i];
    mx /= nh;
    mz /= nh;
    int bi = 0, bj = 1;
    double best = -1;
    for (int i = 0; i < nh - 1; i++)
        for (int j = i + 1; j < nh; j++) {
            double d = pow(hx[j] - hx[i], 2.0) + pow(hz[j] - hz[i], 2.0);
            if (d > best) best = d, bi = i, bj = j;
        }
    double dz = hz[bj] - hz[bi], dx = hx[bj] - hx[bi];
    double theta = (dx != 0 || dz != 0) ? atan2(dz, dx) : 0.0;
    return wall_impulse(b, mx, mz, theta);
}

/* sub_0001fdb4: preserve the separate products before scaling and translating. */
void ign_body_corners(const IgnBody *b, double scale, IgnBodyCorners *corners) {
    double ch = cos(b->heading), sh = sin(b->heading);
    for (int i = 0; i < 4; i++) {
        double xc = b->corner_x[i] * ch, zs = b->corner_z[i] * sh;
        double xs = b->corner_x[i] * sh, zc = b->corner_z[i] * ch;
        corners->x[i] = (xc - zs) + b->px;
        corners->z[i] = (xs + zc) + b->pz;
        corners->scaled_x[i] = (xc * scale - zs * scale) + b->px;
        corners->scaled_z[i] = (xs * scale + zc * scale) + b->pz;
    }
}

static void contact_frame(IgnBody *b, double cx, double cz, double *r, double *a, double *vx,
                          double *vz) {
    double rx = cx - b->px, rz = cz - b->pz;
    *a = (rx != 0 || rz != 0) ? atan2(rz, rx) : 0.0;
    *r = sqrt(rx * rx + rz * rz);
    double w = b->yaw_rate * *r;
    *vx = b->vx - w * sin(*a);
    *vz = b->vz + w * cos(*a);
}

/* sub_0001e638: mutual impulse, mass-weighted, restitution = product of both bodies. */
int ign_body_pair_impulse(IgnBody *A, IgnBody *B, double cx, double cz, double theta) {
    double e = A->restitution * B->restitution;
    double ct = cos(theta), st = sin(theta);
    double ra, aa, vax, vaz, rb, ab, vbx, vbz;
    contact_frame(A, cx, cz, &ra, &aa, &vax, &vaz);
    contact_frame(B, cx, cz, &rb, &ab, &vbx, &vbz);
    int side = sign(ct * (cz - A->pz) - st * (cx - A->px));
    double vn = ct * (vaz - vbz) - st * (vax - vbx);
    if (side != sign(vn)) return 0;

    double normal = (vbx * st - vbz * ct) - (vax * st - vaz * ct);
    A->impact = B->impact = fabs(normal);
    double ka = cos(theta - aa) * ra * 2.0 * A->mass / A->inertia;
    double kb = cos(theta - ab) * rb * 2.0 * B->mass / B->inertia;
    double inv_mass = 1.0 / (A->mass + B->mass);
    double n = -(1.0 + e) * normal;
    double ja = (B->mass * inv_mass) * n /
                (1.0 + (ka * ra) * (sin(aa) * st + cos(aa) * ct));
    double jb = (A->mass * inv_mass) * n /
                (1.0 + (kb * rb) * (sin(ab) * st + cos(ab) * ct));
    A->yaw_rate += ka * ja;
    A->vx -= st * ja;
    A->vz += ct * ja;
    A->px -= sign(ja) * 0.25 * st;
    A->pz += sign(ja) * 0.25 * ct;
    B->yaw_rate -= kb * jb;
    B->vx += st * jb;
    B->vz -= ct * jb;
    B->px += sign(jb) * 0.25 * st;
    B->pz -= sign(jb) * 0.25 * ct;
    return 1;
}

static int segment_hit(double p0x, double p0z, double p1x, double p1z, double q0x, double q0z,
                       double q1x, double q1z, double *hx, double *hz);

int ign_body_collide_pair(IgnBody *a, IgnBody *b) {
    a->impact = b->impact = 0;
    a->contact_x = a->contact_z = b->contact_x = b->contact_z = 0;
    double dx = a->px - b->px, dz = a->pz - b->pz, rr = a->radius + b->radius;
    if (dx * dx + dz * dz > rr * rr) return 0;
    IgnBodyCorners ac, bc;
    ign_body_corners(a, 1.0, &ac);
    ign_body_corners(b, 1.0, &bc);
    const double *ax = ac.x, *az = ac.z, *bx = bc.x, *bz = bc.z;
    double hx[8], hz[8];
    int nh = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double x, z;
            if (!segment_hit(ax[i], az[i], ax[(i + 1) % 4], az[(i + 1) % 4], bx[j], bz[j],
                             bx[(j + 1) % 4], bz[(j + 1) % 4], &x, &z))
                continue;
            if (nh == 8) return 0;
            hx[nh] = x;
            hz[nh] = z;
            nh++;
        }
    if (nh < 1) return 0;

    double mx = 0, mz = 0;
    for (int i = 0; i < nh; i++) mx += hx[i], mz += hz[i];
    double inv_count = 1.0 / nh;
    mx *= inv_count;
    mz *= inv_count;
    /* Original indices are undefined for coincident hits; keep the first point in the native port. */
    int bi = 0, bj = 0;
    double best = 0;
    for (int i = 0; i < nh - 1; i++)
        for (int j = i; j < nh; j++) {
            double d = pow(hx[j] - hx[i], 2.0) + pow(hz[j] - hz[i], 2.0);
            if (d > best) best = d, bi = i, bj = j;
        }
    double tz = hz[bi] - hz[bj], tx = hx[bi] - hx[bj];
    double theta = (tx != 0 || tz != 0) ? atan2(tz, tx) : 0.0;
    a->contact_x = b->contact_x = mx;
    a->contact_z = b->contact_z = mz;
    ign_body_pair_impulse(a, b, mx, mz, theta);
    return 1;
}

/* Proper intersection of segments p0-p1 and q0-q1 (sign tests as in sub_0001ebb4). */
static int segment_hit(double p0x, double p0z, double p1x, double p1z, double q0x, double q0z,
                       double q1x, double q1z, double *hx, double *hz) {
    double px = p0x - p1x, pz = p0z - p1z, qx = q0x - q1x, qz = q0z - q1z;
    if (sign(cross(px, pz, p0x - q0x, p0z - q0z)) == sign(cross(px, pz, p0x - q1x, p0z - q1z)))
        return 0;
    if (sign(cross(qx, qz, q0x - p0x, q0z - p0z)) == sign(cross(qx, qz, q0x - p1x, q0z - p1z)))
        return 0;
    double den = cross(px, pz, qx, qz);
    if (den == 0) return 0;
    *hx = ((px * qx) * (p0z - q0z) - (p0x * pz) * qx + (q0x * qz) * px) / den;
    *hz = ((pz * qz) * (p0x - q0x) - (p0z * px) * qz + (q0z * qx) * pz) / -den;
    return 1;
}

static void integrate_position(IgnBody *b) {
    double tp = tan(b->pitch), tr = tan(b->roll);
    double ch = cos(b->heading), sh = sin(b->heading);
    double ax = atan(tp * ch + tr * sh);
    double az = atan(tp * sh + tr * ch);
    b->px += cos(ax) * b->vx * IGN_PHYS_DT;
    b->pz += cos(az) * b->vz * IGN_PHYS_DT;
}

void ign_body_step(IgnBody *b, int airborne, double slope_k) {
    double drive = b->drive;
    double h0 = b->heading;

    if (airborne) {
        double hp = h0 + HALF_PI;
        b->v_lat = cos(hp) * b->vx + sin(hp) * b->vz;
        b->v_fwd = cos(h0) * b->vx + sin(h0) * b->vz;
        b->heading += b->yaw_rate * IGN_PHYS_DT;
        integrate_position(b);
        b->fmax_f = b->fmax_r = 0;
        b->saturated = 1;
        return;
    }

    double tilt = cos(b->pitch) * cos(b->roll);
    b->fmax_f = G * b->grip_f * b->mass * tilt;
    b->fmax_r = G * b->grip_r * b->mass * tilt;
    b->v_fwd = cos(h0) * b->vx + sin(h0) * b->vz;

    double target, max_dw, vel_angle = 0, speed = 0;
    if (!b->sliding) {
        target = tan(b->steer) * b->v_fwd / (b->a + b->b);
        max_dw = b->fmax_f * 3.0;
    } else {
        if (b->vx != 0 || b->vz != 0) vel_angle = atan2(b->vz, b->vx);
        speed = sqrt(b->vx * b->vx + b->vz * b->vz);
        target = sin(h0 - vel_angle) * speed * 0.2;
        max_dw = b->fmax_f * 2.0;
    }
    max_dw = max_dw / b->inertia * IGN_PHYS_DT;
    if (fabs(b->yaw_rate - target) <= max_dw)
        b->yaw_rate = target;
    else
        b->yaw_rate += target > b->yaw_rate ? max_dw : -max_dw;
    b->heading += b->yaw_rate * IGN_PHYS_DT;
    b->saturated = 0;

    double h = b->heading, hp = h + HALF_PI;
    if (!b->sliding) {
        if (drive >= b->fmax_f) {
            drive = b->fmax_f;
            b->saturated = 1;
        }
        double along = drive / b->mass + sin(b->pitch) * G * slope_k;
        double side = sin(b->roll) * G * slope_k;
        b->vx += (along * cos(h) + side * cos(hp)) * IGN_PHYS_DT;
        b->vz += (along * sin(h) + side * sin(hp)) * IGN_PHYS_DT;

        double lat = b->vx * cos(hp) + b->vz * sin(hp);
        b->v_lat = lat;
        double grip = b->fmax_r / b->mass * IGN_PHYS_DT;
        if (grip > fabs(lat)) grip = fabs(lat);
        if (lat < 0) grip = -grip;
        b->vx -= cos(hp) * grip;
        b->vz -= sin(hp) * grip;
    } else {
        double gx = cos(hp) * G * sin(b->roll) + cos(h) * G * sin(b->pitch);
        double gz = sin(hp) * G * sin(b->roll) + sin(h) * G * sin(b->pitch);
        b->vx += gx * slope_k * IGN_PHYS_DT;
        b->vz += gz * slope_k * IGN_PHYS_DT;
        double fric = b->fmax_r / b->mass * IGN_PHYS_DT;
        if (fric > speed) fric = speed;
        b->vx -= cos(vel_angle) * fric;
        b->vz -= sin(vel_angle) * fric;
    }
    integrate_position(b);
}
