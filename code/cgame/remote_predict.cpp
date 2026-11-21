/*
===========================================================================
Copyright (C) 2026 Project: Omaha
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// remote_predict.cpp: remote prediction's model (Added; see
// remote_predict.h). Project: Omaha's, copied with notuern's permission; its
// blend's weights were fitted on demos by Omaha's research tools, which are
// not part of either repository.

#include "remote_predict.h"

#include <math.h>
#include <string.h>
#include <ctype.h>

// the fitted blend: softmax weights over the three paths (keys held, velocity
// held, keys released) for each feature
#define RP_BLEND_FEATURES 14

static const double rpBlend[RP_BLEND_FEATURES][3] = {
    { 0.354543673,  0.707762615, -0.747359490}, // bias
    {-0.178556287,  0.083577361,  0.162844408}, // horizon / 100 ms
    { 0.648679050, -1.083100394,  0.375305004}, // speed / wish speed
    {-0.331907694,  0.972918746, -0.759180405}, // inference residual / 8
    { 0.163261025, -0.048376593, -0.114455664}, // snapshots keys held / 10
    {-0.716969395,  1.524770118, -0.794455834}, // keys just changed
    { 0.406907188, -0.764787181,  0.361986222}, // acceleration
    {-0.226642526,  0.274944025,  0.001397759}, // turn rate
    {-1.161297878,  0.095147474,  1.065171488}, // keys none
    { 0.666543790,  0.155718831, -0.820804384}, // keys forward
    { 0.067915012,  0.213606685, -0.282468385}, // keys back
    { 0.121507581, -0.020691480, -0.119107064}, // keys strafe
    { 0.420175820,  0.239939927, -0.682972174}, // keys diagonal
    { 0.000000000,  0.000000000,  0.000000000}, // air or unknown
};

#define RP_SWITCH_COST 1.0 // u/s a key switch must gain to be believed
#define RP_ANIM_COST   3.0 // u/s for keys the legs animation contradicts
#define RP_YAW_TAU     150.0 // ms: how fast an extrapolated turn dies out
#define RP_OFFSET_MED  8     // snapshots in the median of the player's clock offset
// Frames in a row on which the server ran none of the player's commands before
// the prediction holds them where the server has them. Measured on a public
// server (2026-11-09): after 1 or 2 silent frames most stalls end within the
// next 100 ms; after 3 (150 ms), 70-75% are still frozen 100 ms later.
#define RP_STALL_FRAMES 3

static const int rpKeys[9][2] = {
    {-127, -127}, {-127, 0}, {-127, 127}, {0, -127}, {0, 0}, {0, 127}, {127, -127}, {127, 0}, {127, 127},
};
#define RP_KEY_NONE 4

/*
==================
Movement physics: flat ground, XY, float32 as bg_pmove (PM_Friction,
PM_GetMove, PM_CmdScale, PM_Accelerate, PM_WalkMove, then
Player::GetMoveInfo's floor projection, which leaves no z speed between
commands).
==================
*/

static float RP_ShortYaw(double yaw)
{
    int s = (int)(yaw * 65536.0 / 360.0) & 65535;
    if (s >= 32768) {
        s -= 65536;
    }
    return (float)(s * (360.0 / 65536.0));
}

static float RP_Normalize2(float *x, float *y)
{
    float length = *x * *x + *y * *y;
    if (length > 1.1920929e-07f) {
        length = (float)sqrt((double)length);
        float il = 1.0f / length;
        *x *= il;
        *y *= il;
    }
    return length;
}

void RP_PmoveStep(float st[4], int msec, int forwardmove, int rightmove, float yaw, int speed, int oldCmdScale)
{
    float frametime, fx, fy, lx, ly, vx, vy;
    float fmove, smove, scale, wx, wy, wishspeed;
    float angle;
    int   mx;

    if (msec < 1) {
        msec = 1;
    } else if (msec > 200) {
        msec = 200;
    }
    frametime = (float)(msec * 0.001);
    angle     = (float)(yaw * (3.14159265358979323846 * 2 / 360));
    fy        = (float)sin((double)angle);
    fx        = (float)cos((double)angle);
    lx        = -fy;
    ly        = fx;
    vx        = st[2];
    vy        = st[3];

    // PM_Friction
    {
        float spd = (float)sqrt((double)(vx * vx + vy * vy));
        if (spd < 1) {
            vx = 0;
            vy = 0;
        } else {
            float control  = spd < 50.0f ? 50.0f : spd;
            float drop     = 0.0f + control * 6.0f * frametime;
            float newspeed = spd - drop;
            if (newspeed < 0) {
                newspeed = 0;
            }
            newspeed /= spd;
            vx = vx * newspeed;
            vy = vy * newspeed;
        }
    }

    // PM_GetMove, PM_CmdScale
    fmove = (float)forwardmove;
    if (fmove < 0) {
        fmove *= 0.80f;
    }
    smove = (float)rightmove * 0.85f;
    mx    = (int)fabsf(fmove);
    if (fabsf(smove) > mx) {
        mx = (int)fabsf(smove);
    }
    if (!mx) {
        scale = 0;
    } else {
        float total;
        if (oldCmdScale) {
            total = (float)sqrt((double)(fmove * fmove + smove * smove + 0.0f));
        } else {
            total = (float)sqrt((double)(float)(forwardmove * forwardmove + rightmove * rightmove));
        }
        scale = (float)((double)((float)speed * (float)mx) / (127.0 * (double)total));
    }

    // PM_WalkMove: renormalized flat axes, wish direction and speed
    RP_Normalize2(&fx, &fy);
    RP_Normalize2(&lx, &ly);
    wx        = fx * fmove - lx * smove;
    wy        = fy * fmove - ly * smove;
    wishspeed = RP_Normalize2(&wx, &wy);
    wishspeed *= scale;

    // PM_Accelerate
    {
        float px = wx * wishspeed - vx;
        float py = wy * wishspeed - vy;
        float plen = RP_Normalize2(&px, &py);
        float can  = 8.0f * frametime * wishspeed;
        if (can > plen) {
            can = plen;
        }
        vx = vx + can * px;
        vy = vy + can * py;
    }

    // PM_ClipVelocity on a floor stops a crawl; PM_SlideMove moves flat
    if ((double)(vx * vx + vy * vy) < 0.005) {
        vx = 0;
        vy = 0;
    }
    if (vx != 0 || vy != 0) {
        st[0] = st[0] + vx * frametime;
        st[1] = st[1] + vy * frametime;
    }
    st[2] = vx;
    st[3] = vy;
}

/*
==================
Animations
==================
*/

void RP_ClassifyAnim(const char *name, rpAnim_t *out)
{
    char        buf[64];
    const char *tok[8];
    int         n = 0;
    char       *s;

    memset(out, 0, sizeof(*out));
    if (!name || !name[0]) {
        return;
    }
    strncpy(buf, name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (s = buf; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
    s = buf;
    tok[n++] = s;
    for (; *s && n < 8; s++) {
        if (*s == '_') {
            *s       = 0;
            tok[n++] = s + 1;
        }
    }
#define TOK(i) (n - (i) >= 0 ? tok[n - (i)] : "")
    for (int i = 0; i < n; i++) {
        if (!strncmp(tok[i], "death", 5)) {
            out->cls = RPL_DEAD;
            return;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!strcmp(tok[i], "jump") || !strcmp(tok[i], "takeoff")) {
            out->cls = RPL_JUMP;
            for (int j = 0; j < n; j++) {
                if (!strcmp(tok[j], "run")) {
                    out->run = 1;
                }
            }
            return;
        }
    }
    if (!strcmp(TOK(1), "fall")) {
        out->cls = RPL_FALL;
        return;
    }
    if (!strcmp(TOK(1), "land") || (n >= 2 && !strcmp(TOK(2), "land"))) {
        out->cls = RPL_LAND;
        return;
    }
    {
        int into = 0, wall = 0;
        for (int i = 0; i < n; i++) {
            into |= !strcmp(tok[i], "into");
            wall |= !strcmp(tok[i], "wall");
        }
        if (into && wall) {
            out->cls = RPL_WALLRUN;
            out->run = 1;
            return;
        }
    }
    if (n >= 3 && (!strcmp(TOK(3), "stand") || !strcmp(TOK(3), "crouch"))
        && (!strcmp(TOK(2), "run") || !strcmp(TOK(2), "walk"))) {
        const char *d = TOK(1);
        int         dir = !strcmp(d, "fwd") ? RPD_FWD
                        : !strcmp(d, "back") ? RPD_BACK
                        : !strcmp(d, "left") ? RPD_LEFT
                        : !strcmp(d, "right") ? RPD_RIGHT
                                              : RPD_NONE;
        if (dir != RPD_NONE) {
            out->cls    = RPL_MOVE;
            out->crouch = !strcmp(TOK(3), "crouch");
            out->run    = !strcmp(TOK(2), "run");
            out->dir    = dir;
            return;
        }
    }
    if (n >= 2 && (!strcmp(TOK(2), "stand") || !strcmp(TOK(2), "crouch")) && !strcmp(TOK(1), "idle")) {
        out->cls    = RPL_IDLE;
        out->crouch = !strcmp(TOK(2), "crouch");
        return;
    }
#undef TOK
    out->cls = RPL_OTHER;
}

/*
==================
History
==================
*/

void RP_Reset(rpPlayer_t *p)
{
    memset(p, 0, sizeof(*p));
}

static rpRecord_t *RP_At(rpPlayer_t *p, int back)
{
    if (back >= p->count) {
        return NULL;
    }
    return &p->rec[(p->head - back + RP_HISTORY) % RP_HISTORY];
}

static const rpRecord_t *RP_AtC(const rpPlayer_t *p, int back)
{
    if (back >= p->count) {
        return NULL;
    }
    return &p->rec[(p->head - back + RP_HISTORY) % RP_HISTORY];
}

const rpRecord_t *RP_Newest(const rpPlayer_t *p)
{
    return RP_AtC(p, 0);
}

static double RP_Wrap180(double a)
{
    while (a > 180.0) {
        a -= 360.0;
    }
    while (a < -180.0) {
        a += 360.0;
    }
    return a;
}

static int RP_WishSpeed(const rpObs_t *o)
{
    double w = (double)o->runSpeed;
    double vxy, v3, vz;

    if (o->legs.cls == RPL_MOVE && !o->legs.run) {
        w *= 0.6;
    }
    if (o->legs.crouch) {
        w *= 0.6;
    }
    // on a slope the wish velocity follows the ground, so less of it is horizontal
    vxy = sqrt((double)o->velocity[0] * o->velocity[0] + (double)o->velocity[1] * o->velocity[1]);
    vz  = o->velocity[2];
    v3  = sqrt(vxy * vxy + vz * vz);
    if (v3 > 50 && fabs(vz) < 150) {
        return (int)floor((double)(int)w * (vxy / (v3 > 1e-6 ? v3 : 1e-6)));
    }
    return (int)w;
}

static double RP_AnimPenalty(int key, int adir)
{
    int f = rpKeys[key][0], r = rpKeys[key][1];

    if ((f == 0 && r == 0) || adir == RPD_NONE) {
        return 0;
    }
    switch (adir) {
    case RPD_FWD:
        return f > 0 ? 0 : RP_ANIM_COST;
    case RPD_BACK:
        return f < 0 ? 0 : RP_ANIM_COST;
    case RPD_LEFT:
        return r < 0 ? 0 : RP_ANIM_COST;
    default:
        return r > 0 ? 0 : RP_ANIM_COST;
    }
}

// ground physics over total ms: keysA until switchMs, then keysB; the yaw moves linearly by dyaw
static void RP_SimInterval(
    float v0x, float v0y, double yaw0, double dyaw, int total, int keysA, int keysB, int switchMs, int W, int old,
    float *vx, float *vy
)
{
    float st[4] = {0, 0, v0x, v0y};
    int   t     = 0;

    while (t < total) {
        int m = RP_PATH_STEP;
        if (t + m > total) {
            m = total - t;
        }
        t += m;
        int k = t <= switchMs ? keysA : keysB;
        RP_PmoveStep(st, m, rpKeys[k][0], rpKeys[k][1], RP_ShortYaw(yaw0 + dyaw * t / total), W, old);
    }
    *vx = st[2];
    *vy = st[3];
}

static void RP_Infer(rpPlayer_t *p, const rpServer_t *srv)
{
    rpRecord_t *cur  = RP_At(p, 0);
    rpRecord_t *prev = RP_At(p, 1);
    int         total;
    double      dyaw;
    double      best = 1e30;
    int         bk = -1, bs = 0;

    cur->key   = -1;
    cur->sw    = 0;
    cur->res   = 1e30f;
    cur->dwell = 0;
    if (!cur->trans) {
        return;
    }
    total = cur->obs.trTime - prev->obs.trTime;
    dyaw  = RP_Wrap180((double)cur->obs.yaw - (double)prev->obs.yaw);
    for (int k = 0; k < 9; k++) {
        double pen = RP_AnimPenalty(k, cur->obs.legs.dir);
        float  vx, vy;
        double e;

        RP_SimInterval(
            prev->obs.velocity[0], prev->obs.velocity[1], prev->obs.yaw, dyaw, total, k, k, total, cur->W,
            srv->oldCmdScale, &vx, &vy
        );
        e = hypot((double)(vx - cur->obs.velocity[0]), (double)(vy - cur->obs.velocity[1]));
        if (e + pen < best - 1e-6) {
            best = e + pen;
            bk   = k;
            bs   = 0;
        }
        if (prev->key >= 0 && prev->key != k) {
            // a switch from the previous keys; the new keys last at least one command
            for (int s = RP_PATH_STEP; s <= total - RP_PATH_STEP; s += RP_PATH_STEP) {
                RP_SimInterval(
                    prev->obs.velocity[0], prev->obs.velocity[1], prev->obs.yaw, dyaw, total, prev->key, k, s,
                    cur->W, srv->oldCmdScale, &vx, &vy
                );
                e = hypot((double)(vx - cur->obs.velocity[0]), (double)(vy - cur->obs.velocity[1]))
                  + RP_SWITCH_COST + pen;
                if (e < best - 1e-6) {
                    best = e;
                    bk   = k;
                    bs   = s;
                }
            }
        }
    }
    cur->key = bk;
    cur->sw  = bs;
    cur->res = (float)best;
    if (prev->key >= 0 && prev->key == bk) {
        cur->dwell = prev->dwell + 1;
    }
}

// keys held from the newest record forward, turning at omega deg/ms that dies out over RP_YAW_TAU
static void RP_BuildPath(float path[RP_PATH_STEPS + 1][2], const rpRecord_t *r, int key, double omega, int old)
{
    float st[4] = {0, 0, r->obs.velocity[0], r->obs.velocity[1]};

    path[0][0] = 0;
    path[0][1] = 0;
    for (int i = 1; i <= RP_PATH_STEPS; i++) {
        double t    = (double)(i * RP_PATH_STEP);
        double turn = omega * RP_YAW_TAU * (1.0 - exp(-t / RP_YAW_TAU));
        RP_PmoveStep(st, RP_PATH_STEP, rpKeys[key][0], rpKeys[key][1], RP_ShortYaw(r->obs.yaw + turn), r->W, old);
        path[i][0] = st[0];
        path[i][1] = st[1];
    }
}

void RP_Ingest(rpPlayer_t *p, const rpObs_t *obs, const rpServer_t *srv)
{
    rpRecord_t *prev = RP_At(p, 0);
    rpRecord_t *cur;
    float       vz;

    if (prev && !obs->reset) {
        // a jump no run could make: a respawn or teleport without the teleport bit
        float dx = obs->origin[0] - prev->obs.origin[0];
        float dy = obs->origin[1] - prev->obs.origin[1];
        float dt = (obs->serverTime - prev->obs.serverTime) * 0.001f;
        if (sqrtf(dx * dx + dy * dy) > 500.0f * dt + 32.0f) {
            RP_Reset(p);
            prev = NULL;
        }
    }
    if (obs->reset || (prev && obs->serverTime <= prev->obs.serverTime)) {
        RP_Reset(p);
        prev = NULL;
    }
    p->head = (p->head + 1) % RP_HISTORY;
    if (p->count < RP_HISTORY) {
        p->count++;
    }
    cur = &p->rec[p->head];
    memset(cur, 0, sizeof(*cur));
    cur->obs = *obs;
    cur->W   = RP_WishSpeed(obs);
    vz       = obs->velocity[2];
    cur->air = obs->legs.cls == RPL_JUMP || obs->legs.cls == RPL_FALL || fabsf(vz) > 150;
    if (prev) {
        int dtr = obs->trTime - prev->obs.trTime;
        cur->trans = obs->trTime && prev->obs.trTime && obs->teleportBit == prev->obs.teleportBit
                  && obs->serverTime - prev->obs.serverTime == srv->frameMsec && dtr > 0 && dtr <= 300 && !cur->air
                  && !prev->air;
    }
    RP_Infer(p, srv);

    // forward paths
    p->pathValid = 0;
    if (cur->key >= 0 && !cur->air) {
        double omega = 0;
        if (prev && obs->trTime > prev->obs.trTime) {
            omega = RP_Wrap180((double)obs->yaw - (double)prev->obs.yaw) / (obs->trTime - prev->obs.trTime);
        }
        RP_BuildPath(p->keep, cur, cur->key, omega, srv->oldCmdScale);
        RP_BuildPath(p->stop, cur, RP_KEY_NONE, omega, srv->oldCmdScale);
        p->pathValid = 1;
    }
}

static void RP_PathAt(const float path[RP_PATH_STEPS + 1][2], double h, float out[2])
{
    double f = h / RP_PATH_STEP;
    int    i;

    if (!(f > 0)) {
        out[0] = path[0][0];
        out[1] = path[0][1];
        return;
    }
    if (f >= RP_PATH_STEPS) {
        out[0] = path[RP_PATH_STEPS][0];
        out[1] = path[RP_PATH_STEPS][1];
        return;
    }
    i = (int)f;
    f -= i;
    out[0] = (float)(path[i][0] + (path[i + 1][0] - path[i][0]) * f);
    out[1] = (float)(path[i][1] + (path[i + 1][1] - path[i][1]) * f);
}

// a record on which the server ran none of the player's commands: their last
// command time is the one of the record a frame before (their packets weren't
// arriving, so the server held them still)
static int RP_Stalled(const rpRecord_t *r, const rpRecord_t *older, int frameMsec)
{
    return r && older && r->obs.trTime && older->obs.trTime == r->obs.trTime
        && r->obs.serverTime == older->obs.serverTime + frameMsec;
}

// frames in a row, up to the newest record, on which none of their commands ran
static int RP_StalledFrames(const rpPlayer_t *p, int frameMsec)
{
    int n = 0;

    while (RP_Stalled(RP_AtC(p, n), RP_AtC(p, n + 1), frameMsec)) {
        n++;
    }
    return n;
}

// median of the player's clock offset (snapshot time - command time) over recent records
static double RP_OffsetMedian(const rpPlayer_t *p, int frameMsec)
{
    int v[RP_OFFSET_MED];
    int n = 0;

    // records on which none of their commands ran would pull the median toward
    // the stall for 8 frames after they resume; without them it stays their usual
    // offset. When every record is one (a long stall), use them all.
    for (int pass = 0; pass < 2 && !n; pass++) {
        for (int i = 0; i < RP_OFFSET_MED; i++) {
            const rpRecord_t *r = RP_AtC(p, i);
            if (!r) {
                break;
            }
            if (!r->obs.trTime || (!pass && RP_Stalled(r, RP_AtC(p, i + 1), frameMsec))) {
                continue;
            }
            v[n++] = r->obs.serverTime - r->obs.trTime;
        }
    }
    if (!n) {
        return 0;
    }
    // insertion sort; n <= 8
    for (int i = 1; i < n; i++) {
        int x = v[i], j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int RP_Predict(const rpPlayer_t *p, const rpServer_t *srv, double targetTime, int maxPushMsec, float out[2], float *horizonMsec)
{
    const rpRecord_t *r = RP_AtC(p, 0);
    const rpRecord_t *q = RP_AtC(p, 1);
    double            cur, lead, push, h;
    float             keep[2], stop[2], cv[2];
    double            phi[RP_BLEND_FEATURES];
    double            z[3], w[3], zmax, sum;

    out[0] = out[1] = 0;
    if (horizonMsec) {
        *horizonMsec = 0;
    }
    if (!r || !r->obs.trTime) {
        return 0;
    }
    // their packets aren't reaching the server, which holds them where they are:
    // draw them there (out 0, horizon 0). A stall this long usually outlasts the
    // lead, and running them on misses both where they are and where the late
    // burst of their commands then puts them.
    if (RP_StalledFrames(p, srv->frameMsec) >= RP_STALL_FRAMES) {
        return 1;
    }
    cur  = (double)(r->obs.serverTime - r->obs.trTime);
    lead = targetTime - r->obs.serverTime;
    // the backlog push: their last command is older than usual, so some of what
    // is late probably lands by the target time. It adds at most maxPushMsec:
    // the ping compensation cg_remotePredictionMaxLead leaves unused, 0 with
    // cg_remotePredictionPush off, < 0 no limit. The lead itself is never cut.
    push = 0.5 * (cur - RP_OffsetMedian(p, srv->frameMsec));
    if (maxPushMsec >= 0 && push > maxPushMsec) {
        push = maxPushMsec;
    }
    h = lead + push;
    if (!(h > 0)) {
        h = 0; // a target before the newest record: nothing to run, and never read before the paths
    }
    if (horizonMsec) {
        *horizonMsec = (float)h;
    }

    cv[0] = (float)(r->obs.velocity[0] * (h / 1000.0));
    cv[1] = (float)(r->obs.velocity[1] * (h / 1000.0));
    if (!p->pathValid || !q) {
        out[0] = cv[0];
        out[1] = cv[1];
        return 1;
    }
    RP_PathAt(p->keep, h, keep);
    RP_PathAt(p->stop, h, stop);

    // the blend's features
    {
        int    f = rpKeys[r->key][0], s = rpKeys[r->key][1];
        int    kc;
        double sp  = sqrt((double)r->obs.velocity[0] * r->obs.velocity[0] + (double)r->obs.velocity[1] * r->obs.velocity[1]);
        double W   = r->W > 1 ? r->W : 1;
        double dvx = (double)r->obs.velocity[0] - q->obs.velocity[0];
        double dvy = (double)r->obs.velocity[1] - q->obs.velocity[1];
        double dtr = r->obs.trTime - q->obs.trTime;
        double om;

        if (dtr < 1) {
            dtr = 1;
        }
        om = fabs(RP_Wrap180((double)r->obs.yaw - (double)q->obs.yaw)) / dtr;
        kc = (f == 0 && s == 0) ? 0 : s == 0 ? (f > 0 ? 1 : 2) : f == 0 ? 3 : 4;
        memset(phi, 0, sizeof(phi));
        phi[0] = 1.0;
        phi[1] = h / 100.0;
        phi[2] = fmin(sp / W, 1.5);
        phi[3] = fmin(r->res / 8.0, 4.0);
        phi[4] = fmin(r->dwell / 10.0, 2.0);
        phi[5] = r->dwell == 0;
        phi[6] = fmin(sqrt(dvx * dvx + dvy * dvy) / dtr * 10.0, 4.0);
        phi[7] = fmin(om * 10.0, 4.0);
        phi[8 + kc] = 1.0;
        phi[13] = 0; // air and unknown keys never get here
    }
    for (int c = 0; c < 3; c++) {
        z[c] = 0;
        for (int i = 0; i < RP_BLEND_FEATURES; i++) {
            z[c] += phi[i] * rpBlend[i][c];
        }
    }
    zmax = fmax(z[0], fmax(z[1], z[2]));
    sum  = 0;
    for (int c = 0; c < 3; c++) {
        w[c] = exp(z[c] - zmax);
        sum += w[c];
    }
    out[0] = (float)((w[0] * keep[0] + w[1] * cv[0] + w[2] * stop[0]) / sum);
    out[1] = (float)((w[0] * keep[1] + w[1] * cv[1] + w[2] * stop[1]) / sum);
    return 1;
}

/*
==================
Server learning
==================
*/

void RP_LearnReset(rpLearn_t *l)
{
    memset(l, 0, sizeof(*l));
}

void RP_LearnObserve(rpLearn_t *l, rpServer_t *srv, int weaponModel, const rpAnim_t *legs, float speed)
{
    int run;

    if (weaponModel <= 0 || weaponModel >= 1024 || legs->cls != RPL_MOVE || legs->crouch || !legs->run || speed < 150) {
        return;
    }
    if (legs->dir == RPD_FWD) {
        l->runSpeed[weaponModel][l->runCount[weaponModel] % 8] = (int)(speed + 0.5f);
        l->runCount[weaponModel]++;
        return;
    }
    run = RP_LearnRunSpeed(l, weaponModel);
    if ((legs->dir == RPD_LEFT || legs->dir == RPD_RIGHT) && run > 0) {
        float ratio = speed / run;
        // stock strafes at 0.716 of run speed, upstream OpenMoHAA at 0.843
        if (fabsf(ratio - 0.716f) < 0.02f) {
            l->strafeStock += 1;
        } else if (fabsf(ratio - 0.843f) < 0.02f) {
            l->strafeOld += 1;
        }
        srv->oldCmdScale = l->strafeOld > l->strafeStock + 3;
    }
}

int RP_LearnRunSpeed(const rpLearn_t *l, int weaponModel)
{
    int v[8];
    int n;

    if (weaponModel <= 0 || weaponModel >= 1024 || !l->runCount[weaponModel]) {
        return 0;
    }
    n = l->runCount[weaponModel] < 8 ? l->runCount[weaponModel] : 8;
    memcpy(v, l->runSpeed[weaponModel], sizeof(int) * n);
    for (int i = 1; i < n; i++) {
        int x = v[i], j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    return v[n / 2];
}

/*
==================
Shooter lead
==================
*/

void RP_LeadReset(rpLead_t *l)
{
    memset(l, 0, sizeof(*l));
}

void RP_LeadPush(rpLead_t *l, int serverTime, int commandTime, int nextCmdTime)
{
    float hi, lo;

    if (commandTime <= 0) {
        return;
    }
    // the command at commandTime arrived by serverTime; the next one didn't
    hi = (float)(serverTime - commandTime);
    lo = nextCmdTime > commandTime ? (float)(serverTime - nextCmdTime) : hi - 10.0f;
    if (lo < 0) {
        lo = 0;
    }
    l->samples[l->head] = 0.5f * (lo + hi);
    l->head             = (l->head + 1) % 64;
    if (l->count < 64) {
        l->count++;
    }
}

float RP_LeadMean(const rpLead_t *l, int frameMsec)
{
    float v[64];
    int   n = l->count;

    if (n < 4) {
        return -1;
    }
    memcpy(v, l->samples, sizeof(float) * n);
    for (int i = 1; i < n; i++) {
        float x = v[i];
        int   j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    // a command fires on the frame after it arrives: on average half a frame later
    return v[n / 2] + 0.5f * frameMsec;
}
