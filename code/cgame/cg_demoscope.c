/*
===========================================================================
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

// cg_demoscope.c -- Added in OPM
//  A demo holds nothing of whether a player it shows from behind (CG_DemoBehind)
//  has a scope on, nor when they put it on. Far enough, a sniper's shot almost
//  always has one: of 9,813 kills whose killer's view is in the demo, 94% at
//  1000 to 1500 units, 98.5% at 1500 to 2000, and 61% at 500 to 750. So one is
//  made up for a sniper kill far enough (cg_demoscope 0 turns that off), for a
//  moment before the shot (cg_demoscopetime) and just after, as a spectator
//  following them sees it: the camera behind them zoomed in. Where they stand
//  and how their aim moves come from their entity, where it ends up from the
//  shot in the demo. From their eyes (cg_unspeccedcamera 2), the scope is
//  theirs. A shot of theirs that kills nobody gets one too, if it
//  goes far enough: the client tells where from and to (cl_demoScopeShot).

#include "cg_local.h"

// the zoom of the sniper rifles
#define DEMO_SCOPE_FOV 20
// what a spectator's zoom takes, from a snapshot to the next
#define DEMO_SCOPE_ZOOM_TIME 50
// a shot's message is the one of the kill: it's read, and its tracer drawn, as
// the time drawn reaches the snapshot before
#define DEMO_SCOPE_SHOT_AHEAD 50
// as long after the kill as a flick's stays on: 150 msec after the shot, when a
// tenth of real scopes are off within 100 and a quarter within 200. Many aim
// elsewhere, or duck, right after
#define DEMO_SCOPE_AFTER 100
// how far from the player a shot starts, and passes from its victim
#define DEMO_SCOPE_SHOT_NEAR 96

typedef struct {
    int      kill;     // its time, as the snapshots', 0 for none (or of a shot that kills nobody)
    qboolean decided;  // whether it has one is settled
    qboolean on;       // it has one
    qboolean shot;     // the shot's line came
    int      shotTime; // the time drawn when it came
    vec3_t   hit;      // where it passes the victim, from their origin
    vec3_t   offset;   // what's added to the aim of their entity
    qboolean missed;   // for a shot that kills nobody, which ends at end
    vec3_t   end;
} demoScope_t;

static demoScope_t scope;

/*
===============
CG_DemoScopeMiddle

How far above a player's origin to aim at them
===============
*/
static float CG_DemoScopeMiddle(const entityState_t *es)
{
    vec3_t mins, maxs;

    IntegerToBoundingBox(es->solid, mins, maxs);
    return maxs[2] * 0.55f;
}

/*
===============
CG_DemoScope

The player a scope is made up for now, -1 for none, of client, who the demo
shows from behind or from their eyes while it doesn't show them
===============
*/
static int CG_DemoScope(int client)
{
    int kill, start, victim;

    if (client < 0 || !cg_demoscope->integer) {
        return -1;
    }
    kill = cl_demoScopeKill->integer;
    if (!kill) {
        return -1;
    }

    start = kill - DEMO_SCOPE_SHOT_AHEAD - cg_demoscopetime->integer;
    if (kill != scope.kill || cg.time < start) {
        memset(&scope, 0, sizeof(scope));
        scope.kill = kill;
    }
    if (cg.time < start || cg.time >= kill + DEMO_SCOPE_AFTER) {
        return -1;
    }

    if (!scope.decided) {
        vec3_t from;

        scope.decided = qtrue;
        victim        = cl_demoScopeVictim->integer;
        if (victim >= 0 && victim < MAX_CLIENTS && cg_entities[victim].currentValid) {
            float dist = Distance(cg_entities[client].currentState.origin, cg_entities[victim].currentState.origin);

            scope.on = dist >= cg_demoscopedist->value;
            cgi.DPrintf("Scope for a sniper kill %.0f units away: %s\n", dist, scope.on ? "made up" : "none");
        } else if (victim < 0
                   && sscanf(
                          cl_demoScopeShot->string,
                          "%f %f %f %f %f %f",
                          &from[0],
                          &from[1],
                          &from[2],
                          &scope.end[0],
                          &scope.end[1],
                          &scope.end[2]
                      ) == 6) {
            // as far as it went, where it hit
            float dist = Distance(from, scope.end);

            scope.on     = dist >= cg_demoscopedist->value;
            scope.missed = qtrue;
            cgi.DPrintf("Scope for a sniper shot %.0f units long: %s\n", dist, scope.on ? "made up" : "none");
        }
    }
    return scope.on ? client : -1;
}

/*
===============
CG_DemoScopeBody

Where a player stands and aims, from the snapshot to the next, as the camera
behind them goes
===============
*/
static void CG_DemoScopeBody(const centity_t *cent, vec3_t origin, vec3_t angles, float *height, float *lean)
{
    playerState_t ps;
    float         frac = cg.frameInterpolation;
    int           i;

    CG_DemoBehindState(&cent->currentState, &ps);
    VectorCopy(ps.origin, origin);
    VectorCopy(ps.viewangles, angles);
    *height = ps.viewheight;
    // the pelvis (bone controller 3) rolls by 0.8 times the lean
    *lean = AngleNormalize180(cent->currentState.bone_angles[3][2]) / 0.8f;

    if (!cent->interpolate) {
        return;
    }

    CG_DemoBehindState(&cent->nextState, &ps);
    for (i = 0; i < 3; i++) {
        origin[i] += frac * (ps.origin[i] - origin[i]);
        angles[i] = LerpAngle(angles[i], ps.viewangles[i], frac);
    }
    *height += frac * (ps.viewheight - *height);
    *lean += frac * (AngleNormalize180(cent->nextState.bone_angles[3][2]) / 0.8f - *lean);
}

/*
===============
CG_DemoScopeEye

Where they look from, as CG_OffsetFirstPersonView works it out: the fatal shots
of 60 sniper kills start within 2 units of it, but for the odd lean
===============
*/
static void CG_DemoScopeEye(const vec3_t origin, const vec3_t angles, float height, float lean, vec3_t eye)
{
    vec3_t flat, forward, left, pivot, delta, moved;

    VectorSet(flat, angles[PITCH], angles[YAW], 0);
    AngleVectorsLeft(flat, forward, left, NULL);

    VectorCopy(origin, eye);
    eye[2] += height;

    VectorCopy(eye, pivot);
    pivot[2] -= height * (angles[PITCH] > 0 ? 0.4f : 0.2f);
    VectorSubtract(eye, pivot, delta);
    RotatePointAroundVector(moved, left, delta, angles[PITCH] * 0.4f);
    VectorAdd(pivot, moved, eye);

    if (lean) {
        VectorCopy(eye, pivot);
        pivot[2] -= 28.7f;
        VectorSubtract(eye, pivot, delta);
        RotatePointAroundVector(moved, forward, delta, lean);
        VectorAdd(pivot, moved, eye);
    }
}

/*
===============
CG_DemoScopeAim

The aim with a made up scope of client, if there's one (scope.offset, added to
the aim of their entity): closing in on the victim until the kill (where the
shot goes, for one that kills nobody) and off by the same after it; and the
zoom, as a spectator's goes
===============
*/
static qboolean CG_DemoScopeAim(int client, float *fov)
{
    int        victim = cl_demoScopeVictim->integer;
    int        start;
    centity_t *other;
    vec3_t     bodyOrigin, bodyAngles, eye, target, dir, aim;
    float      height, lean, frac;

    client = CG_DemoScope(client);
    if (client < 0) {
        return qfalse;
    }

    start = scope.kill - DEMO_SCOPE_SHOT_AHEAD - cg_demoscopetime->integer;

    if (cg.time < scope.kill
        && (scope.missed || (victim >= 0 && victim < MAX_CLIENTS && cg_entities[victim].currentValid))) {
        CG_DemoScopeBody(&cg_entities[client], bodyOrigin, bodyAngles, &height, &lean);

        if (scope.missed) {
            // closing in on where the shot goes
            VectorCopy(scope.end, target);
        } else {
            other = &cg_entities[victim];
            // where they're drawn this frame, which cg_smoothClients moves
            // ahead of their entity
            CG_CalcEntityLerpPositions(other);
            VectorCopy(other->lerpOrigin, target);
            if (scope.shot) {
                frac = (float)(cg.time - scope.shotTime) / DEMO_SCOPE_SHOT_AHEAD;
                frac = Q_clamp_float(frac, 0, 1);
                target[0] += frac * scope.hit[0];
                target[1] += frac * scope.hit[1];
                target[2] += CG_DemoScopeMiddle(&other->currentState) * (1 - frac) + frac * scope.hit[2];
            } else {
                target[2] += CG_DemoScopeMiddle(&other->currentState);
            }
        }

        CG_DemoScopeEye(bodyOrigin, bodyAngles, height, lean, eye);
        VectorSubtract(target, eye, dir);
        vectoangles(dir, aim);

        frac = (float)(cg.time - start) / Q_max(1, cg_demoscopetime->integer);
        frac = Q_clamp_float(frac, 0, 1);
        frac = frac * frac * (3 - 2 * frac);

        scope.offset[PITCH] = frac * AngleSubtract(aim[PITCH], bodyAngles[PITCH]);
        scope.offset[YAW]   = frac * AngleSubtract(aim[YAW], bodyAngles[YAW]);
    }

    frac = (float)Q_min(cg.time - start, scope.kill + DEMO_SCOPE_AFTER - cg.time) / DEMO_SCOPE_ZOOM_TIME;
    frac = Q_clamp_float(frac, 0, 1);
    *fov = cg_fov->value + frac * (DEMO_SCOPE_FOV - cg_fov->value);
    return qtrue;
}

/*
===============
CG_DemoScopeView

The view with a made up scope, if there's one, from behind the player shown
there (CG_DemoBehind), as a spectator's: where their camera is, zooming as
they do
===============
*/
qboolean CG_DemoScopeView(vec3_t origin, vec3_t angles, float *fov)
{
    int client = CG_DemoBehind();

    if (!CG_DemoScopeAim(client, fov)) {
        return qfalse;
    }
    CG_DemoBehindCamera(client, scope.offset, origin, angles);
    return qtrue;
}

/*
===============
CG_DemoScopeEyes

The made up scope from the eyes of a player not shown (CG_UnspeccedEyes), set
in the playerstate drawn: their aim closing in as from behind, and once
zoomed in half way, their scope on, as a player's own is, with no gun nor
crosshair
===============
*/
void CG_DemoScopeEyes(void)
{
    int      client = CG_UnspeccedEyes();
    float    fov;
    qboolean zoomed = qfalse;

    if (client < 0) {
        return;
    }

    if (CG_DemoScopeAim(client, &fov)) {
        cg.predicted_player_state.viewangles[PITCH] += scope.offset[PITCH];
        cg.predicted_player_state.viewangles[YAW] += scope.offset[YAW];
        zoomed = fov < (cg_fov->value + DEMO_SCOPE_FOV) / 2;
    }

    cg.snap->ps.stats[STAT_INZOOM]    = zoomed ? DEMO_SCOPE_FOV : 0;
    cg.snap->ps.stats[STAT_CROSSHAIR] = zoomed ? 0 : CG_UnspeccedCrosshair();
    cg.predicted_player_state.stats[STAT_INZOOM]    = cg.snap->ps.stats[STAT_INZOOM];
    cg.predicted_player_state.stats[STAT_CROSSHAIR] = cg.snap->ps.stats[STAT_CROSSHAIR];
    cg.camera_fov = zoomed ? DEMO_SCOPE_FOV : cg_fov->value;
}

/*
===============
CG_DemoScopeShot

A shot of the demo: the one of the kill tells where it hit
===============
*/
void CG_DemoScopeShot(const vec3_t start, const vec3_t end)
{
    static snapshot_t    snap;
    const entityState_t *killer = NULL, *victim = NULL;
    vec3_t               line, middle, to;
    float                along;
    int                  number, time, i;

    if (!scope.on || scope.shot || scope.kill != cl_demoScopeKill->integer) {
        return;
    }

    cgi.GetCurrentSnapshotNumber(&number, &time);
    if (time != scope.kill || !cgi.GetSnapshot(number, &snap)) {
        return;
    }
    for (i = 0; i < snap.numEntities; i++) {
        if (snap.entities[i].number == cl_demoBehind->integer) {
            killer = &snap.entities[i];
        } else if (snap.entities[i].number == cl_demoScopeVictim->integer) {
            victim = &snap.entities[i];
        }
    }
    if (!killer || !victim
        || Square(start[0] - killer->origin[0]) + Square(start[1] - killer->origin[1]) > Square(DEMO_SCOPE_SHOT_NEAR)) {
        return;
    }

    VectorSubtract(end, start, line);
    if (VectorNormalize(line) < 1) {
        return;
    }
    VectorCopy(victim->origin, middle);
    middle[2] += CG_DemoScopeMiddle(victim);
    VectorSubtract(middle, start, to);
    along = DotProduct(to, line);
    VectorMA(start, along, line, scope.hit);
    if (Distance(scope.hit, middle) > DEMO_SCOPE_SHOT_NEAR) {
        return;
    }
    VectorSubtract(scope.hit, victim->origin, scope.hit);

    scope.shot     = qtrue;
    scope.shotTime = cg.time;
}
