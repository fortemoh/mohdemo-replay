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

// Remote prediction's model (remote_predict.cpp; Project: Omaha's test, copied
// with notuern's permission): the physics must match bg_pmove, the animation
// names must parse, keys must be recovered from velocities, and the lead must
// come out of command acks.

#include "q_shared.h"
#include "bg_public.h"
#include "../remote_predict.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static void expect_near(double value, double expected, double epsilon, const char *message)
{
    if (value < expected - epsilon || value > expected + epsilon) {
        fprintf(stderr, "FAIL: %s (got %f expected %f)\n", message, value, expected);
        g_failures++;
    }
}

// the world is the half space below z = 0
static void Flat_Trace(
    trace_t     *tr,
    const vec3_t start,
    const vec3_t mins,
    const vec3_t maxs,
    const vec3_t end,
    int          passEntityNum,
    int          contentMask,
    int          capsule,
    qboolean     traceDeep
)
{
    float startBottom = start[2] + mins[2];
    float endBottom   = end[2] + mins[2];

    memset(tr, 0, sizeof(*tr));
    tr->entityNum = ENTITYNUM_NONE;
    tr->fraction  = 1.0f;
    VectorCopy(end, tr->endpos);
    if (startBottom < 0) {
        tr->startsolid = qtrue;
        tr->allsolid   = endBottom < 0 ? qtrue : qfalse;
        tr->fraction   = 0;
        VectorCopy(start, tr->endpos);
    } else if (endBottom < 0) {
        float f = (startBottom - 0.125f) / (startBottom - endBottom);
        tr->fraction = f < 0 ? 0 : f;
        for (int i = 0; i < 3; i++) {
            tr->endpos[i] = start[i] + (end[i] - start[i]) * tr->fraction;
        }
    } else {
        return;
    }
    tr->plane.normal[2] = 1.0f;
    tr->contents        = CONTENTS_SOLID;
    tr->entityNum       = ENTITYNUM_WORLD;
}

static int Flat_PointContents(const vec3_t point, int passEntityNum)
{
    return point[2] < 0 ? CONTENTS_SOLID : 0;
}

// RP_PmoveStep against the real Pmove on a flat floor, with the server's
// floor projection between commands (Player::GetMoveInfo)
static void test_physics_matches_pmove(void)
{
    playerState_t ps;
    pmove_t       pm;
    float         st[4] = {0, 0, 0, 0};
    double        worst = 0;
    int           t     = 1000;

    srand(7);
    memset(&ps, 0, sizeof(ps));
    ps.commandTime        = t;
    ps.gravity            = 800;
    ps.pm_type            = PM_NORMAL;
    ps.groundEntityNum    = ENTITYNUM_WORLD;
    ps.walking            = qtrue;
    ps.stats[STAT_HEALTH] = 100;

    for (int i = 0; i < 2000; i++) {
        static const int moves[3] = {-127, 0, 127};
        static int       fwd, right, speed = 264;
        static float     yaw;
        int              msec = 1 + rand() % 16;

        if (i % 25 == 0) {
            fwd   = moves[rand() % 3];
            right = moves[rand() % 3];
            speed = (rand() % 2) ? 264 : 158;
        }
        yaw += (float)(rand() % 9 - 4);
        t += msec;

        memset(&pm, 0, sizeof(pm));
        pm.ps              = &ps;
        pm.trace           = Flat_Trace;
        pm.pointcontents   = Flat_PointContents;
        pm.tracemask       = MASK_PLAYERSOLID;
        pm.protocol        = 8;
        pm.cmd.serverTime  = t;
        pm.cmd.forwardmove = (signed char)fwd;
        pm.cmd.rightmove   = (signed char)right;
        pm.cmd.angles[YAW] = (short)ANGLE2SHORT(yaw);
        ps.speed           = speed;
        Pmove(&pm);
        if (ps.groundEntityNum != ENTITYNUM_NONE) {
            ps.velocity[2] = 0;
        }

        RP_PmoveStep(st, msec, fwd, right, (float)SHORT2ANGLE((short)ANGLE2SHORT(yaw)), speed, 0);
        worst = fmax(worst, fmax(fabs(st[0] - ps.origin[0]), fabs(st[1] - ps.origin[1])));
    }
    expect_near(worst, 0, 1e-3, "flat-ground physics matches bg_pmove");
}

static void test_classify(void)
{
    rpAnim_t a;

    RP_ClassifyAnim("mp40_stand_run_fwd", &a);
    expect_true(a.cls == RPL_MOVE && a.dir == RPD_FWD && a.run && !a.crouch, "mp40_stand_run_fwd");
    RP_ClassifyAnim("unarmed_crouch_walk_left", &a);
    expect_true(a.cls == RPL_MOVE && a.dir == RPD_LEFT && !a.run && a.crouch, "unarmed_crouch_walk_left");
    RP_ClassifyAnim("enfieldl42a1_crouch_run_back", &a);
    expect_true(a.cls == RPL_MOVE && a.dir == RPD_BACK && a.run && a.crouch, "a long weapon prefix");
    RP_ClassifyAnim("rifle_stand_idle", &a);
    expect_true(a.cls == RPL_IDLE && !a.crouch, "rifle_stand_idle");
    RP_ClassifyAnim("pistol_jump_run_takeoff", &a);
    expect_true(a.cls == RPL_JUMP && a.run, "pistol_jump_run_takeoff");
    RP_ClassifyAnim("mp44_fall", &a);
    expect_true(a.cls == RPL_FALL, "mp44_fall");
    RP_ClassifyAnim("mp40_land_hard", &a);
    expect_true(a.cls == RPL_LAND, "mp40_land_hard");
    RP_ClassifyAnim("smg_stand_run_into_wall", &a);
    expect_true(a.cls == RPL_WALLRUN, "run into wall is not a run");
    RP_ClassifyAnim("death_run", &a);
    expect_true(a.cls == RPL_DEAD, "death_run");
    RP_ClassifyAnim("mp40_stand_raise", &a);
    expect_true(a.cls == RPL_OTHER, "a torso animation");
    RP_ClassifyAnim(NULL, &a);
    expect_true(a.cls == RPL_NONE, "no name");
}

// a player runs forward, then strafes right, as a server would show them
static void test_keys_from_velocity(void)
{
    rpPlayer_t p;
    rpServer_t srv     = {0, 50};
    float      st[4]   = {0, 0, 0, 0};
    int        trTime  = 10000;
    int        correct = 0, total = 0;

    RP_Reset(&p);
    for (int s = 0; s < 40; s++) {
        int     fwd   = s < 20 ? 127 : 0;
        int     right = s < 20 ? 0 : 127;
        rpObs_t o;

        for (int c = 0; c < 6; c++) {
            RP_PmoveStep(st, 8, fwd, right, 30.0f, 264, 0);
        }
        trTime += 48;
        memset(&o, 0, sizeof(o));
        o.serverTime  = 20000 + s * 50;
        o.trTime      = trTime;
        o.origin[0]   = st[0];
        o.origin[1]   = st[1];
        o.velocity[0] = st[2];
        o.velocity[1] = st[3];
        o.yaw         = 30.0f;
        o.runSpeed    = 264;
        o.legs.cls    = RPL_MOVE;
        o.legs.run    = 1;
        o.legs.dir    = s < 20 ? RPD_FWD : RPD_RIGHT;
        RP_Ingest(&p, &o, &srv);
        if (s > 0) {
            const rpRecord_t *r   = RP_Newest(&p);
            int               key = fwd > 0 ? 7 : 5; // (127, 0) and (0, 127) in the key table
            total++;
            correct += r->key == key;
        }
    }
    expect_true(correct == total, "keys recovered from velocities");

    // steady strafing: the held keys keep the strafe speed, and the blend (which mixes
    // in a little "keys released") stays close to it and points the same way
    {
        float d[2], h;
        int   ok   = RP_Predict(&p, &srv, 20000 + 39 * 50 + 100, -1, d, &h);
        float v    = sqrtf(st[2] * st[2] + st[3] * st[3]);
        float held = sqrtf(p.keep[10][0] * p.keep[10][0] + p.keep[10][1] * p.keep[10][1]);
        float dist = sqrtf(d[0] * d[0] + d[1] * d[1]);
        expect_true(ok, "a prediction");
        expect_near(held, v * 0.080f, 0.05, "the held keys keep the strafe speed");
        expect_true(dist > 0.9f * v * h / 1000.0f && dist <= v * h / 1000.0f + 0.01f, "blend close to the held path");
        expect_true(d[0] * st[2] + d[1] * st[3] > 0.99f * dist * v, "same direction");
    }

    // a target before the newest record predicts no lead, and never reads
    // before the paths
    {
        float d[2] = {1, 1}, h = 1;
        int   ok   = RP_Predict(&p, &srv, 20000 + 39 * 50 - 50, -1, d, &h);
        expect_true(ok, "a prediction for a past target");
        expect_near(h, 0, 0.0001, "a past target: no horizon");
        expect_near(d[0], 0, 0.0001, "a past target: no move (x)");
        expect_near(d[1], 0, 0.0001, "a past target: no move (y)");
    }
    // a far target stays within the paths
    {
        float d[2], h;
        int   ok = RP_Predict(&p, &srv, 20000 + 39 * 50 + 100000, -1, d, &h);
        expect_true(ok && d[0] == d[0] && d[1] == d[1], "a prediction for a far target");
    }
}

static void test_reset_on_teleport(void)
{
    rpPlayer_t p;
    rpServer_t srv = {0, 50};
    rpObs_t    o;

    RP_Reset(&p);
    memset(&o, 0, sizeof(o));
    o.serverTime = 1000;
    o.trTime     = 900;
    o.runSpeed   = 264;
    RP_Ingest(&p, &o, &srv);
    o.serverTime = 1050;
    o.trTime     = 950;
    o.origin[0]  = 2000; // no one runs 2000 units in 50 ms
    RP_Ingest(&p, &o, &srv);
    expect_true(p.count == 1, "a jump across the map starts a new history");
}

static void test_stall(void)
{
    rpPlayer_t p;
    rpServer_t srv = {0, 50};
    rpObs_t    o;
    float      d[2], h;

    RP_Reset(&p);
    memset(&o, 0, sizeof(o));
    o.runSpeed    = 264;
    o.velocity[0] = 264;
    // running along x, their commands reaching the server 100 ms behind its time
    for (int i = 0; i < 6; i++) {
        o.serverTime = 1000 + i * 50;
        o.trTime     = o.serverTime - 100;
        o.origin[0]  = 264 * 0.05f * i;
        RP_Ingest(&p, &o, &srv);
    }
    expect_true(RP_Predict(&p, &srv, o.serverTime + 100, -1, d, &h), "a prediction while they run");
    expect_near(h, 100, 0.01, "horizon: the 100 ms to the target, their commands on time");
    expect_true(RP_Predict(&p, &srv, o.serverTime + 200, -1, d, &h), "a prediction with a 200 ms lead");
    expect_near(h, 200, 0.01, "the whole lead, whatever its length");

    // their packets stop: the server holds them, their last command time stays
    for (int i = 0; i < 2; i++) {
        o.serverTime += 50;
        RP_Ingest(&p, &o, &srv);
    }
    expect_true(RP_Predict(&p, &srv, o.serverTime + 100, -1, d, &h), "a prediction after two silent frames");
    expect_near(h, 150, 0.01, "two silent frames: runs on by half the backlog (jitter)");
    expect_true(RP_Predict(&p, &srv, o.serverTime + 100, 10, d, &h), "a prediction with 10 ms of push allowed");
    expect_near(h, 110, 0.01, "the push takes what cg_remotePredictionMaxLead leaves: 10 ms");
    expect_true(RP_Predict(&p, &srv, o.serverTime + 100, 0, d, &h), "a prediction with no push allowed");
    expect_near(h, 100, 0.01, "no room: the lead only");
    o.serverTime += 50;
    RP_Ingest(&p, &o, &srv);
    expect_true(RP_Predict(&p, &srv, o.serverTime + 100, -1, d, &h), "a prediction after three silent frames");
    expect_near(h, 0, 0.001, "three silent frames: held where the server has them");
    expect_near(d[0], 0, 0.001, "no displacement");
    expect_near(d[1], 0, 0.001, "none sideways either");

    // their commands land: 200 ms of running in one frame, then on time again
    o.serverTime += 50;
    o.trTime     = o.serverTime - 100;
    o.origin[0] += 264 * 0.2f;
    RP_Ingest(&p, &o, &srv);
    expect_true(p.count > 1, "a burst their commands explain keeps the history");
    expect_true(RP_Predict(&p, &srv, o.serverTime + 100, -1, d, &h), "a prediction after the burst");
    expect_near(h, 100, 0.01, "the stalled records don't drag the median: the full horizon again");
}

static void test_lead(void)
{
    rpLead_t l;

    RP_LeadReset(&l);
    expect_true(RP_LeadMean(&l, 50) < 0, "no lead before any ack");
    // commands every 10 ms reach the server 90-100 ms after their serverTime
    for (int s = 0; s < 40; s++) {
        int S = 100000 + s * 50;
        RP_LeadPush(&l, S, S - 95, S - 85);
    }
    expect_near(RP_LeadMean(&l, 50), 90 + 25, 0.01, "lead = arrival delay + half a frame");
}

static void test_learn(void)
{
    rpLearn_t  l;
    rpServer_t srv = {0, 50};
    rpAnim_t   fwd = {RPL_MOVE, RPD_FWD, 0, 1};
    rpAnim_t   str = {RPL_MOVE, RPD_LEFT, 0, 1};

    RP_LearnReset(&l);
    for (int i = 0; i < 8; i++) {
        RP_LearnObserve(&l, &srv, 51, &fwd, 263.875f);
    }
    expect_true(RP_LearnRunSpeed(&l, 51) == 264, "run speed learned per weapon");
    for (int i = 0; i < 10; i++) {
        RP_LearnObserve(&l, &srv, 51, &str, 264 * 0.843f);
    }
    expect_true(srv.oldCmdScale == 1, "fast strafing means the old PM_CmdScale");
}

int main(void)
{
    test_physics_matches_pmove();
    test_classify();
    test_keys_from_velocity();
    test_reset_on_teleport();
    test_stall();
    test_lead();
    test_learn();

    if (g_failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", g_failures);
        return 1;
    }

    printf("test_remote_predict: all tests passed\n");
    return 0;
}
