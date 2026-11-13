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

// cg_demoaim.c -- Added in OPM
//  Where a player's crosshair was, drawn from their eyes in a demo
//  (cg_demoaim 1): the recorder playing, a player followed (cg_followcamera
//  2) or one the demo doesn't show (cg_unspeccedcamera 2). Their screen
//  showed their aim, as the demo has it 20 times a second (their view for
//  the first two, their entity for the last), plus the kick their own game
//  added after each shot, which no demo has; each shot of theirs went along
//  that, give or take its weapon's scatter. So, around their shots, as the
//  client's index of the demo has them (cgi.DemoShots):
//  - a shot is drawn as the snapshot before its own comes, the one its
//    message comes with (its tracer a frame before), while it was fired with
//    the aim of its own: of 450 first shots of sniper rifles, half are 0.23
//    deg from that, 0.38 from the one before. So the crosshair is held at the
//    aim of its own snapshot while it's drawn, and runs that far ahead of the
//    snapshots between the shots of a run, from the aims the index has
//  - the kick is the weapon's (its tiki's viewkick, with the middle of each
//    random part), on the shooter's screen 50 msec after the snapshot of the
//    shot, as they got it back: of 3,400 shots of the StG 44, half went 0.97
//    deg from that aim and kick, 3.05 from the aim alone. It's the one drawn
//  - what's still left at each shot, to where it ended (on the player it went
//    through as they're drawn, ahead of where the server had them while they
//    move), is added from shot to shot: all of it for a sniper rifle's or a
//    Kar98's while still, what goes beyond its scatter for the others, as the
//    crosshair is where they aimed, not where each bullet flew
//  At the frame its gun fires, half of the shots of each weapon are now that
//  close to where they went (before), in 26 kills of players not shown, then
//  35 of the recorder and players followed, half each: sniper rifles 0.00 deg
//  (0.00 with the scope made up), 0.00 (0.09), 0.03 (0.91); other rifles 0.42
//  (2.16), 0.16 (1.20), 0.13 (0.74); the StG 44 and the BAR 0.63 (0.86), 0.87
//  (2.11), 0.68 (1.33); submachine guns 1.37 (2.54), 1.34 (2.90), 1.49
//  (4.33); pistols 1.81 (4.40), 2.07 (4.46), 1.45 (3.70)

#include "cg_local.h"

// the shots looked at around the time drawn
#define DEMO_AIM_MAX_SHOTS 64
#define DEMO_AIM_BEFORE    2000
#define DEMO_AIM_AFTER     1000
// msec at most between the shots of a run
#define DEMO_AIM_RUN_GAP 700
// msec to close in before the first shot of a run, and back after the last
#define DEMO_AIM_RAMP_IN  150
#define DEMO_AIM_RAMP_OUT 250
// msec before a shot is drawn that the crosshair is where it was fired: its
// tracer is drawn a frame before its gun fires (the client reads a snapshot
// a frame ahead)
#define DEMO_AIM_EARLY 20
// msec after the snapshot of a shot that its kick showed on the shooter's
// screen, the one that fits the shots after it best
#define DEMO_AIM_KICK_DELAY 50
// the step the kick is worked out in, msec (a frame at 125 fps)
#define DEMO_AIM_KICK_STEP 8
// a shot after another this close is in a burst, which scatters more
#define DEMO_AIM_BURST 300
// the speed scatter is the widest at
#define DEMO_AIM_RUN_SPEED 250
// a shot further from the aim and kick is taken for another's, deg
#define DEMO_AIM_FAR 10
// how far a player drawn can be from where the server had them, to aim at
// them as they're drawn
#define DEMO_AIM_DRAWN_NEAR 64
// how far across from a player's origin a shot goes through them
#define DEMO_AIM_HIT_ACROSS 20
// a shot that ends nearer the camera tells nothing of the aim
#define DEMO_AIM_NEAREST 64

typedef struct {
    const char *model; // in their hand
    // the tiki's viewkick: pitch, then yaw (of the pitch for a "V"), at least
    // and at most, how fast it comes back, at most, and the scatter the pitch
    // adds beyond
    float       pitch[2];
    float       yaw[2];
    float       recenter;
    qboolean    yawOfPitch;
    float       pitchMax;
    float       yawMax;
    float       minDecay;
    float       maxDecay;
    // how far a shot can go from the aim and kick by scatter alone, deg each
    // way, first shots then in a burst, still then at full speed: about one
    // and a half times as far as half of the shots of 12 demos go
    float       scatter[2][2];
} demoAimWeapon_t;

static const demoAimWeapon_t demoAimWeapons[] = {
    {"models/weapons/springfield.tik",    {-4.5f, -5.0f},   {-1.0f, 1.0f},    6.0f,  qfalse, 8.0f,  8.0f, 12, 25, {{0, 1.5f}, {0, 1.5f}}},
    {"models/weapons/kar98sniper.tik",    {-5.0f, -5.0f},   {0.07f, 0.07f},   0.5f,  qtrue,  8.0f,  8.0f, 12, 25, {{0, 1.5f}, {0, 1.5f}}},
    {"models/weapons/kar98.tik",          {-3.5f, -4.0f},   {-1.0f, -1.05f},  2.5f,  qfalse, 5.0f,  2.0f, 12, 25, {{0.05f, 0.9f}, {0.05f, 0.9f}}},
    {"models/weapons/m1_garand.tik",      {-2.5f, -2.5f},   {-0.1f, -0.1f},   0.15f, qfalse, 8.0f,  8.2f, 12, 25, {{0.15f, 1.2f}, {0.3f, 1.4f}}},
    {"models/weapons/mp40.tik",           {-1.7f, -1.7f},   {-0.08f, 0.08f},  1.0f,  qtrue,  8.5f,  8.6f, 12, 25, {{0.5f, 0.8f}, {1.2f, 1.4f}}},
    {"models/weapons/thompsonsmg.tik",    {-1.58f, -1.58f}, {-0.16f, -0.16f}, 0.9f,  qtrue,  8.5f,  8.6f, 12, 25, {{0.5f, 0.8f}, {1.3f, 1.5f}}},
    {"models/weapons/mp44.tik",           {-1.8f, -1.8f},   {-0.2f, 0.05f},   3.2f,  qfalse, 12.0f, 0.2f, 12, 25, {{0.25f, 0.7f}, {0.65f, 2.0f}}},
    {"models/weapons/bar.tik",            {-1.96f, -1.96f}, {-1.2f, 1.2f},    3.2f,  qfalse, 10.0f, 0.6f, 12, 25, {{0.3f, 1.0f}, {0.45f, 1.6f}}},
    {"models/weapons/colt45.tik",         {-1.2f, -1.2f},   {-0.2f, 0.2f},    1.0f,  qtrue,  3.0f,  1.0f, 12, 25, {{1.4f, 2.5f}, {2.2f, 3.5f}}},
    {"models/weapons/p38.tik",            {-1.0f, -1.0f},   {-0.16f, 0.16f},  1.2f,  qtrue,  2.8f,  0.8f, 12, 25, {{1.4f, 2.5f}, {2.2f, 3.5f}}},
    {"models/weapons/silencedpistol.tik", {-0.2f, -0.2f},   {0, 0},           1.0f,  qtrue,  3.0f,  1.0f, 12, 25, {{0.4f, 2.1f}, {0.6f, 2.1f}}},
    // one pellet of the shotgun's, the last
    {"models/weapons/shotgun.tik",        {-20.0f, -20.0f}, {0.05f, 0.05f},   15.2f, qfalse, 12.0f, 5.0f, 12, 25, {{5.0f, 6.0f}, {5.0f, 6.0f}}},
};

// for a gun not known: no kick, each shot near enough
static const demoAimWeapon_t demoAimOther = {"", {0, 0}, {0, 0}, 1, qfalse, 0, 0, 12, 25, {{1, 2}, {1, 2}}};

static struct {
    int            client;
    int            numShots;
    demoShotInfo_t shots[DEMO_AIM_MAX_SHOTS];
    // where the camera was the frame before, of whom, and when
    vec3_t         eye;
    int            eyeClient;
    int            eyeTime;
} aim = {-1};

/*
===============
CG_DemoAimWeapon

The weapon of the gun in the hand of client, whose body is body (NULL if it
isn't in the snapshot: any gun of theirs)
===============
*/
static const demoAimWeapon_t *CG_DemoAimWeapon(int client, const entityState_t *body)
{
    const char *model;
    int         i, j;

    for (i = 0; i < cg.snap->numEntities; i++) {
        if (body ? !CG_FollowedGun(&cg.snap->entities[i], body) : cg.snap->entities[i].parent != client) {
            continue;
        }
        model = CG_ConfigString(CS_MODELS + cg.snap->entities[i].modelindex);
        for (j = 0; j < (int)ARRAY_LEN(demoAimWeapons); j++) {
            if (!Q_stricmp(model, demoAimWeapons[j].model)) {
                return &demoAimWeapons[j];
            }
        }
        break;
    }
    return &demoAimOther;
}

/*
===============
CG_DemoAimViewOf

The player whose aim snap shows from their eyes in a demo, -1 for none: one
the demo doesn't show (CG_UnspeccedFromEyes), one followed from their eyes
(cg_followcamera 2) or the recorder playing, unless seen from behind
(cg_recordercamera), on a turret or dead
===============
*/
static int CG_DemoAimViewOf(const snapshot_t *snap)
{
    const playerState_t *ps = &snap->ps;

    if (!(snap->snapFlags & (SNAPFLAG_UNSPECCED_FROM_EYES | SNAPFLAG_FOLLOWED_FROM_EYES))
        && ((ps->pm_flags & (PMF_SPECTATING | PMF_CAMERA_VIEW)) || CG_RecorderBehind())) {
        return -1;
    }
    if ((ps->pm_flags & PMF_TURRET) || ps->pm_type == PM_DEAD || ps->stats[STAT_HEALTH] <= 0) {
        return -1;
    }
    return ps->clientNum;
}

/*
===============
CG_DemoAimKickAt

The kick on the shooter's screen at time (as the snapshots'), from their
shots before it, each DEMO_AIM_KICK_DELAY after its snapshot, as
EventViewKick and CG_CalcViewValues do it
===============
*/
static void CG_DemoAimKickAt(const demoAimWeapon_t *weapon, int time, vec2_t kick)
{
    int   now = 0, until, next, msec, i, j;
    float decay;

    kick[0] = kick[1] = 0;
    for (i = 0; i <= aim.numShots; i++) {
        next = i < aim.numShots ? aim.shots[i].time + DEMO_AIM_KICK_DELAY : time;
        if (next > time) {
            next = time;
        }
        if (i == 0) {
            now = next;
        }
        // back, as it does a frame at a time
        for (; now < next; now = until) {
            until = Q_min(now + DEMO_AIM_KICK_STEP, next);
            msec  = until - now;
            for (j = 0; j < 2; j++) {
                if (!kick[j]) {
                    continue;
                }
                decay = kick[j] * weapon->recenter;
                decay = Q_clamp_float(decay, -weapon->maxDecay, weapon->maxDecay);
                if (fabs(decay) < weapon->minDecay) {
                    decay = decay > 0 ? weapon->minDecay : -weapon->minDecay;
                }
                if (kick[j] > 0) {
                    kick[j] = Q_max(0, kick[j] - decay * msec / 1000.0f);
                } else {
                    kick[j] = Q_min(0, kick[j] - decay * msec / 1000.0f);
                }
            }
        }
        if (i == aim.numShots || aim.shots[i].time + DEMO_AIM_KICK_DELAY > time) {
            break;
        }
        // its kick, the middle of what it can be
        kick[0] += (weapon->pitch[0] + weapon->pitch[1]) / 2;
        if (weapon->yawOfPitch) {
            kick[1] += kick[0] * (weapon->yaw[0] + weapon->yaw[1]) / 2;
        } else {
            kick[1] += (weapon->yaw[0] + weapon->yaw[1]) / 2;
        }
        kick[0] = Q_clamp_float(kick[0], -weapon->pitchMax, weapon->pitchMax);
        kick[1] = Q_clamp_float(kick[1], -weapon->yawMax, weapon->yawMax);
    }
}

/*
===============
CG_DemoAimAt

Their aim at time (as the snapshots'), from the snapshot drawn and the next,
or past them from the index's of their shots (the snapshot before each,
its own and the one after), held at the last one known
===============
*/
static void CG_DemoAimAt(int time, vec2_t angles)
{
    const playerState_t *from = &cg.snap->ps, *to = &cg.snap->ps;
    int                  fromTime = cg.snap->serverTime, toTime = cg.snap->serverTime;
    float                frac, fromAim[2], toAim[2];
    int                  i, j;

    if (cg.nextSnap && CG_DemoAimViewOf(cg.nextSnap) == aim.client
        && !((cg.nextSnap->snapFlags ^ cg.snap->snapFlags) & (SNAPFLAG_UNSPECCED_FROM_EYES | SNAPFLAG_FOLLOWED_FROM_EYES))
        && cg.nextSnap->serverTime > cg.snap->serverTime) {
        to     = &cg.nextSnap->ps;
        toTime = cg.nextSnap->serverTime;
    }

    fromAim[0] = from->viewangles[PITCH];
    fromAim[1] = from->viewangles[YAW];
    toAim[0]   = to->viewangles[PITCH];
    toAim[1]   = to->viewangles[YAW];

    if (time > toTime) {
        // past the snapshots: from the last of them to the first of the
        // index's after it, at time or after
        fromTime   = toTime;
        fromAim[0] = toAim[0];
        fromAim[1] = toAim[1];
        toTime     = 0;
        for (i = 0; i < aim.numShots; i++) {
            for (j = 0; j < 3; j++) {
                const int t = aim.shots[i].aimTimes[j];

                if (!t || t <= fromTime) {
                    continue;
                }
                if (t <= time) {
                    fromTime   = t;
                    fromAim[0] = aim.shots[i].aims[j][0];
                    fromAim[1] = aim.shots[i].aims[j][1];
                } else if (!toTime || t < toTime) {
                    toTime   = t;
                    toAim[0] = aim.shots[i].aims[j][0];
                    toAim[1] = aim.shots[i].aims[j][1];
                }
            }
        }
        if (!toTime) {
            angles[0] = fromAim[0];
            angles[1] = fromAim[1];
            return;
        }
    }

    frac      = toTime > fromTime ? (float)(time - fromTime) / (toTime - fromTime) : 0;
    frac      = Q_clamp_float(frac, 0, 1);
    angles[0] = LerpAngle(fromAim[0], toAim[0], frac);
    angles[1] = LerpAngle(fromAim[1], toAim[1], frac);
}

/*
===============
CG_DemoAimHoldEnd

When shot is drawn: its gun fires as the snapshot before its own is, the one
its message comes with, a frame after its tracer. The crosshair is held where
it was fired from DEMO_AIM_EARLY before
===============
*/
static int CG_DemoAimHoldEnd(const demoShotInfo_t *shot)
{
    return shot->aimTimes[0] ? shot->aimTimes[0] : shot->time - 50;
}

static int CG_DemoAimHoldStart(const demoShotInfo_t *shot)
{
    return CG_DemoAimHoldEnd(shot) - DEMO_AIM_EARLY;
}

/*
===============
CG_DemoAimLeft

What the aim and kick don't tell of shot, as far as the crosshair goes, from
eye: where it went, beyond what its weapon's scatter can do, as the player it
went through is drawn (ahead of where the server had them while they move),
and with a hit's kick, which the client makes up (CL_DemoFollowedHits) and
the view takes off after
===============
*/
static void CG_DemoAimLeft(const demoAimWeapon_t *weapon, int index, const vec3_t eye, vec2_t left)
{
    const demoShotInfo_t *shot = &aim.shots[index];
    vec3_t                target, dir, angles;
    vec2_t                kick;
    float                 scatter, speed, dist;
    qboolean              burst;
    int                   i;

    left[0] = left[1] = 0;
    if (!shot->aimTimes[1]) {
        return;
    }

    // where it hit, on the player it went through as they're drawn
    VectorCopy(shot->end, target);
    if (shot->hitClient >= 0 && shot->hitClient < MAX_CLIENTS && cg_entities[shot->hitClient].currentValid
        && Square(shot->hit[0]) + Square(shot->hit[1]) <= Square(DEMO_AIM_HIT_ACROSS)) {
        centity_t *cent = &cg_entities[shot->hitClient];
        vec3_t     origin, drawn;

        // their origin then, and as they're drawn
        CG_CalcEntityLerpPositions(cent);
        VectorSubtract(shot->point, shot->hit, origin);
        VectorSubtract(cent->lerpOrigin, origin, drawn);
        if (VectorLength(drawn) <= DEMO_AIM_DRAWN_NEAR) {
            VectorAdd(target, drawn, target);
        }
    }

    VectorSubtract(target, eye, dir);
    dist = VectorLength(dir);
    if (dist < DEMO_AIM_NEAREST) {
        return;
    }
    vectoangles(dir, angles);
    CG_DemoAimKickAt(weapon, shot->time, kick);

    for (i = 0; i < 2; i++) {
        left[i] = AngleSubtract(
            angles[i ? YAW : PITCH], shot->aims[1][i] + kick[i] - cg.predicted_player_state.damage_angles[i ? YAW : PITCH]
        );
    }
    // as far as the camera can be from their eyes
    if (Square(left[0]) + Square(left[1]) > Square(DEMO_AIM_FAR + RAD2DEG(atan2(16, dist)))) {
        // a shot of another player beside them
        left[0] = left[1] = 0;
        return;
    }

    burst   = index > 0 && shot->time - aim.shots[index - 1].time <= DEMO_AIM_BURST;
    speed   = Q_min(1, shot->speed / DEMO_AIM_RUN_SPEED);
    scatter = weapon->scatter[burst][0] + speed * (weapon->scatter[burst][1] - weapon->scatter[burst][0]);
    for (i = 0; i < 2; i++) {
        if (left[i] > scatter) {
            left[i] -= scatter;
        } else if (left[i] < -scatter) {
            left[i] += scatter;
        } else {
            left[i] = 0;
        }
    }
}

/*
===============
CG_DemoAimView

Sets the aim drawn from a player's eyes in a demo (CG_DemoAimViewOf) as their
crosshair was (cg_demoaim), if it can tell it
===============
*/
void CG_DemoAimView(void)
{
    int                    client;
    const entityState_t   *body   = NULL;
    const demoAimWeapon_t *weapon;
    const demoShotInfo_t  *a, *z;
    vec3_t                 eye;
    vec2_t                 angles, kick, left, nextLeft;
    float                  frac;
    int                    first = -1, last = -1, runFirst = 0, time, i;

    aim.client = -1;
    if (!cg_demoaim->integer || !cg.demoPlayback || !cg.snap) {
        return;
    }
    client = CG_DemoAimViewOf(cg.snap);
    if (client < 0) {
        return;
    }
    for (i = 0; i < cg.snap->numEntities; i++) {
        if (cg.snap->entities[i].number == client) {
            body = &cg.snap->entities[i];
        }
    }
    // from their entity alone, it aims as it goes on a ladder or a turret
    if (cg.snap->snapFlags & SNAPFLAG_UNSPECCED_FROM_EYES && (!body || CG_DemoHeadTurn(body))) {
        return;
    }

    aim.client   = client;
    aim.numShots = cgi.DemoShots(client, cg.time - DEMO_AIM_BEFORE, cg.time + DEMO_AIM_AFTER, aim.shots, DEMO_AIM_MAX_SHOTS);
    weapon       = CG_DemoAimWeapon(client, body);

    // the last run of shots about now: from closing in before its first to
    // coming back after its last
    for (i = 0; i < aim.numShots; i++) {
        if (i > 0 && aim.shots[i].time - aim.shots[i - 1].time > DEMO_AIM_RUN_GAP) {
            runFirst = i;
        }
        if ((i == aim.numShots - 1 || aim.shots[i + 1].time - aim.shots[i].time > DEMO_AIM_RUN_GAP)
            && CG_DemoAimHoldStart(&aim.shots[runFirst]) - DEMO_AIM_RAMP_IN <= cg.time
            && cg.time <= CG_DemoAimHoldEnd(&aim.shots[i]) + DEMO_AIM_RAMP_OUT) {
            first = runFirst;
            last  = i;
        }
    }

    // where the camera is, as the frame before had it
    if (aim.eyeTime && aim.eyeClient == client && cg.time - aim.eyeTime >= 0 && cg.time - aim.eyeTime <= 100) {
        VectorCopy(aim.eye, eye);
    } else {
        VectorCopy(cg.predicted_player_state.origin, eye);
        eye[2] += cg.predicted_player_state.viewheight;
    }

    // the time of the snapshots the aim drawn is from: ahead of the time
    // drawn as a shot is drawn before its own, held while it's drawn
    time    = cg.time;
    left[0] = left[1] = 0;
    if (first >= 0) {
        a = &aim.shots[first];
        z = &aim.shots[last];

        if (cg.time < CG_DemoAimHoldStart(a)) {
            // closing in on the first
            frac = (float)(cg.time - (CG_DemoAimHoldStart(a) - DEMO_AIM_RAMP_IN)) / DEMO_AIM_RAMP_IN;
            frac = Q_clamp_float(frac, 0, 1);
            frac = frac * frac * (3 - 2 * frac);
            time += (int)((a->time - CG_DemoAimHoldStart(a)) * frac);
            CG_DemoAimLeft(weapon, first, eye, left);
            left[0] *= frac;
            left[1] *= frac;
        } else if (cg.time > CG_DemoAimHoldEnd(z)) {
            // back after the last
            frac = (float)(cg.time - CG_DemoAimHoldEnd(z)) / DEMO_AIM_RAMP_OUT;
            frac = Q_clamp_float(frac, 0, 1);
            frac = frac * frac * (3 - 2 * frac);
            time += (int)((z->time - CG_DemoAimHoldEnd(z)) * (1 - frac));
            CG_DemoAimLeft(weapon, last, eye, left);
            left[0] *= 1 - frac;
            left[1] *= 1 - frac;
        } else {
            for (i = first; i < last && CG_DemoAimHoldStart(&aim.shots[i + 1]) <= cg.time; i++) {
            }
            CG_DemoAimLeft(weapon, i, eye, left);
            if (cg.time <= CG_DemoAimHoldEnd(&aim.shots[i])) {
                // held where it was fired
                time = aim.shots[i].time;
            } else {
                // from one to the next
                frac = (float)(cg.time - CG_DemoAimHoldEnd(&aim.shots[i]))
                     / Q_max(1, CG_DemoAimHoldStart(&aim.shots[i + 1]) - CG_DemoAimHoldEnd(&aim.shots[i]));
                frac = Q_clamp_float(frac, 0, 1);
                time = aim.shots[i].time + (int)(frac * (aim.shots[i + 1].time - aim.shots[i].time));
                CG_DemoAimLeft(weapon, i + 1, eye, nextLeft);
                left[0] += frac * (nextLeft[0] - left[0]);
                left[1] += frac * (nextLeft[1] - left[1]);
            }
        }
    }

    CG_DemoAimAt(time, angles);
    CG_DemoAimKickAt(weapon, time, kick);

    cg.predicted_player_state.viewangles[PITCH] = AngleNormalize180(angles[0] + kick[0] + left[0]);
    cg.predicted_player_state.viewangles[YAW]   = AngleNormalize360(angles[1] + kick[1] + left[1]);
    // the kick is this one, when their shots are known
    if (aim.numShots) {
        cg.viewkick[0] = cg.viewkick[1] = 0;
    }
}

/*
===============
CG_DemoAimed

Whether the aim drawn this frame is the one CG_DemoAimView worked out
===============
*/
qboolean CG_DemoAimed(void)
{
    return aim.client >= 0;
}

/*
===============
CG_DemoAimEye

Where the camera is drawn from, for the next frame's aim
===============
*/
void CG_DemoAimEye(void)
{
    if (aim.client >= 0) {
        VectorCopy(cg.refdef.vieworg, aim.eye);
        aim.eyeClient = aim.client;
        aim.eyeTime   = cg.time;
    } else {
        aim.eyeTime = 0;
    }
}
