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

// cg_remotepredict.cpp: remote prediction (Added). Project: Omaha's (its
// cg_remotepredict.cpp, copied with notuern's permission); the model itself
// is remote_predict.cpp.
//
// Stock servers have no lag compensation: a shot is traced on the server
// frame after the shooter's command arrives, against where everyone is then,
// so a moving player has to be led by about the ping. Snapshots acking our
// commands tell when they arrive: other players are drawn where the model has
// them at cg.time plus that lead, and half a frame for where in the frame the
// command lands. Every snapshot is read as it arrives (the cgame itself reads
// them only when its clock reaches them). Collision keeps the stock position
// (netLerpOrigin): only the drawing moves.
//
// With the lagometer up, where each enemy was drawn for each server frame is
// kept, and the update for that frame tells how far off it was: the lagometer
// shows the lead and that error (cgi.Lagometer_Prediction). Only enemies in
// view are measured, so it never tells of one behind a wall, and one whose own
// connection put them off (their commands stalled at the server, or landed in
// a burst) isn't charged to the prediction.

#include "cg_local.h"
#include "cg_remotepredict.h"
#include "remote_predict.h"

#include <stdint.h>

static cvar_t *cg_remotePrediction;
static cvar_t *cg_remotePredictionMaxLead;
static cvar_t *cg_remotePredictionPush;
static cvar_t *cg_remotePredictionSmooth;

#define RP_MAX_ENTS  MAX_CLIENTS // players are entities 0..MAX_CLIENTS-1
#define RP_DRAWN     16          // server frames of where a player was drawn (800 ms at 20 Hz)
#define RP_STEPSIZE  18          // bg_pmove's STEPSIZE
#define RP_SNAPS     32          // the snapshots the client keeps (PACKET_BACKUP)

// where a player was drawn for a server frame: the frame whose cg.time + lead
// came nearest it
typedef struct {
    int    serverTime;
    float  dist;       // how far the frame's target was from it (ms)
    vec3_t origin;     // where it was drawn, the fade included
    int    fromTime;   // the update the prediction started from: its server time
    int    fromTrTime; // and the player's last command time in it
} rpDrawn_t;

typedef struct {
    rpPlayer_t rp;
    int        lastServerTime; // newest snapshot the player was in
    int        legs[2];        // last legs frameInfo, for the newest slot
    float      legsW[2];
    int        legsSlot;
    int        haveLegs;
    float      prevVel[2];
    int        modelindex;
    int        usable; // alive, solid, not on a ladder, a turret or in water
    vec3_t     mins, maxs;

    // what was drawn
    int    shownValid;
    int    shownFrom; // serverTime of the record the drawn prediction came from
    vec3_t shown;
    vec3_t shownPrev;
    vec3_t offset; // a new snapshot's correction, being faded out

    rpDrawn_t drawn[RP_DRAWN];
    int       drawnHead;
    int       drawnCount;
} rpEnt_t;

static rpEnt_t    s_ents[RP_MAX_ENTS];
static rpServer_t s_server;
static rpLearn_t  s_learn;
static rpLead_t   s_lead;
static byte       s_isWeapon[MAX_MODELS]; // 0 unknown, 1 weapon, 2 not

// legs animation classes, cached per (model, anim)
#define RP_ANIM_CACHE 4096
static uint32_t s_animKey[RP_ANIM_CACHE];
static rpAnim_t s_animVal[RP_ANIM_CACHE];

static snapshot_t s_snap;           // the snapshot being read (too big for the stack)
static int        s_lastSnapNum;    // last snapshot number read
static int        s_lastServerTime; // only newer snapshots are used, so rewinds are skipped
static int        s_latestTime;     // the newest snapshot's server time
static qboolean   s_readGap;        // snapshots went unread (prediction off): the histories are stale

// per frame (CG_RP_BeginFrame): only a snapshot changes them
static qboolean s_frameActive;
static float    s_frameLead;

void CG_RP_RegisterCvars(void)
{
    cg_remotePrediction = cgi.Cvar_Get("cg_remotePrediction", "1", CVAR_ARCHIVE);
    // the most ping compensated for, ms (0: all of it); the rest of the lead, the
    // interpolation buffer and the wait for the server frame, is always in
    cg_remotePredictionMaxLead = cgi.Cvar_Get("cg_remotePredictionMaxLead", "0", CVAR_ARCHIVE);
    // run a player whose commands reach the server late further ahead, by half
    // their backlog (0: the lead only)
    cg_remotePredictionPush = cgi.Cvar_Get("cg_remotePredictionPush", "1", CVAR_ARCHIVE);
    // a new snapshot's correction fades out: a third of it left after that many ms (0: none)
    cg_remotePredictionSmooth = cgi.Cvar_Get("cg_remotePredictionSmooth", "15", CVAR_ARCHIVE);
}

void CG_RP_Init(void)
{
    const int fps = atoi(Info_ValueForKey(CG_ConfigString(CS_SERVERINFO), "sv_fps"));

    memset(s_ents, 0, sizeof(s_ents));
    memset(s_isWeapon, 0, sizeof(s_isWeapon));
    memset(s_animKey, 0, sizeof(s_animKey));
    RP_LearnReset(&s_learn);
    RP_LeadReset(&s_lead);
    s_server.oldCmdScale = 0;
    s_server.frameMsec   = fps > 0 ? 1000 / fps : 50;

    s_lastSnapNum    = 0;
    s_lastServerTime = 0;
    s_latestTime     = 0;
    s_readGap        = qfalse;
    s_frameActive    = qfalse;
    s_frameLead      = -1;
}

/*
==================
Feeding the model
==================
*/

// the serverTime of our first command after commandTime: with commandTime it
// brackets when our commands reach the server
static int CG_RP_NextCmdTime(int commandTime)
{
    usercmd_t cmd;
    const int cur  = cgi.GetCurrentCmdNumber();
    int       next = -1;

    for (int i = 0; i < CMD_BACKUP - 1; i++) {
        if (!cgi.GetUserCmd(cur - i, &cmd) || cmd.serverTime <= commandTime) {
            break;
        }
        next = cmd.serverTime;
    }
    return next;
}

static void CG_RP_Anim(int modelindex, int anim, rpAnim_t *out)
{
    const uint32_t key  = ((uint32_t)modelindex << 16 | (uint32_t)(anim & 0xffff)) + 1;
    uint32_t       slot = (key * 2654435761u) & (RP_ANIM_CACHE - 1);
    const char    *name = NULL;

    for (int i = 0; i < 8; i++, slot = (slot + 1) & (RP_ANIM_CACHE - 1)) {
        if (s_animKey[slot] == key) {
            *out = s_animVal[slot];
            return;
        }
        if (!s_animKey[slot]) {
            break;
        }
    }
    if (modelindex > 0 && modelindex < MAX_MODELS) {
        dtiki_t *tiki = cgi.R_Model_GetHandle(cgs.model_draw[modelindex]);

        if (tiki && anim >= 0 && anim < cgi.TIKI_NumAnims(tiki)) {
            name = cgi.Anim_NameForNum(tiki, anim);
        }
    }
    RP_ClassifyAnim(name, out);
    s_animKey[slot] = key;
    s_animVal[slot] = *out;
}

// the legs slot (frameInfo 0 or 1) holding the newest animation: the one whose
// index just changed, else the one whose weight is rising, else the heavier one
static int CG_RP_LegsSlot(rpEnt_t *e, const entityState_t *es)
{
    int slot = e->legsSlot;

    if (e->haveLegs) {
        const int c0 = es->frameInfo[0].index != e->legs[0];
        const int c1 = es->frameInfo[1].index != e->legs[1];

        if (c0 != c1) {
            slot = c0 ? 0 : 1;
        } else if (es->frameInfo[0].weight > e->legsW[0] + 1e-3f) {
            slot = 0;
        } else if (es->frameInfo[1].weight > e->legsW[1] + 1e-3f) {
            slot = 1;
        } else if (es->frameInfo[slot].weight < 0.5f) {
            slot ^= 1;
        }
    } else {
        slot = es->frameInfo[0].weight >= es->frameInfo[1].weight ? 0 : 1;
    }
    e->legs[0]  = es->frameInfo[0].index;
    e->legs[1]  = es->frameInfo[1].index;
    e->legsW[0] = es->frameInfo[0].weight;
    e->legsW[1] = es->frameInfo[1].weight;
    e->legsSlot = slot;
    e->haveLegs = 1;
    return slot;
}

// the model of the weapon the player holds (an attached entity), or 0
static int CG_RP_Weapon(const snapshot_t *snap, int number)
{
    for (int i = 0; i < snap->numEntities; i++) {
        const entityState_t *c = &snap->entities[i];
        const int            m = c->modelindex;

        if (c->parent != number || m <= 0 || m >= MAX_MODELS) {
            continue;
        }
        if (!s_isWeapon[m]) {
            s_isWeapon[m] = !Q_stricmpn(CG_ConfigString(CS_MODELS + m), "models/weapons/", 15) ? 1 : 2;
        }
        if (s_isWeapon[m] == 1) {
            return m;
        }
    }
    return 0;
}

/*
==================
The lagometer's error
==================
*/

// where a player was drawn for the server frame serverTime, NULL if not
static const rpDrawn_t *CG_RP_DrawnAt(const rpEnt_t *e, int serverTime)
{
    for (int i = 0; i < e->drawnCount; i++) {
        const rpDrawn_t *d = &e->drawn[(e->drawnHead - i + RP_DRAWN) % RP_DRAWN];

        if (d->serverTime == serverTime) {
            return d;
        }
    }
    return NULL;
}

// an enemy: anyone else, but a teammate in a team game
static qboolean CG_RP_Enemy(const entityState_t *es, const snapshot_t *snap)
{
    const int team = snap->ps.stats[STAT_TEAM];

    if (cgs.gametype < GT_TEAM) {
        return qtrue;
    }
    return !((team == TEAM_ALLIES && (es->eFlags & EF_ALLIES)) || (team == TEAM_AXIS && (es->eFlags & EF_AXIS)));
}

// a player drawn there is in view: in front, nothing of the world between
static qboolean CG_RP_InView(const vec3_t origin)
{
    vec3_t  chest, d;
    trace_t tr;

    VectorCopy(origin, chest);
    chest[2] += 40;
    VectorSubtract(chest, cg.refdef.vieworg, d);
    if (DotProduct(d, cg.refdef.viewaxis[0]) <= 0) {
        return qfalse;
    }
    CG_Trace(&tr, cg.refdef.vieworg, vec3_origin, vec3_origin, chest, ENTITYNUM_NONE, CONTENTS_SOLID, qfalse, qfalse, "CG_RP_InView");
    return tr.fraction == 1.0f ? qtrue : qfalse;
}

/*
 * For the lagometer: how far across the ground from where this snapshot has
 * them we drew each enemy in view for its server frame, their average and the
 * worst. Not one whose own commands put them off: between the update the
 * prediction started from and this one the server ran a frame's worth or more
 * less of their commands than time went by (it held them still) or more (their
 * late ones landed at once).
 */
static void CG_RP_Measure(const snapshot_t *snap)
{
    float sum = 0.0f, worst = 0.0f;
    int   players = 0;

    for (int i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];
        const rpDrawn_t     *d;
        float                dx, dy, error;

        if (es->eType != ET_PLAYER || es->number < 0 || es->number >= RP_MAX_ENTS || es->number == snap->ps.clientNum
            || (es->eFlags & EF_DEAD) || !CG_RP_Enemy(es, snap)) {
            continue;
        }
        d = CG_RP_DrawnAt(&s_ents[es->number], snap->serverTime);
        if (!d || !CG_RP_InView(d->origin)) {
            continue;
        }
        if (es->pos.trTime && d->fromTrTime) {
            const int cmdSpan = es->pos.trTime - d->fromTrTime, srvSpan = snap->serverTime - d->fromTime;

            if (abs(cmdSpan - srvSpan) >= s_server.frameMsec) {
                continue;
            }
        }
        dx    = d->origin[0] - es->netorigin[0];
        dy    = d->origin[1] - es->netorigin[1];
        error = sqrtf(dx * dx + dy * dy);
        sum += error;
        worst = Q_max(worst, error);
        players++;
    }
    cgi.Lagometer_Prediction(snap->serverTime, s_frameLead, players, players ? sum / players : 0.0f, worst);
}

static void CG_RP_Ingest(const snapshot_t *snap)
{
    // our own lead: which of our commands this snapshot had run
    if (!cg.demoPlayback && snap->ps.clientNum == cg.clientNum) {
        RP_LeadPush(&s_lead, snap->serverTime, snap->ps.commandTime, CG_RP_NextCmdTime(snap->ps.commandTime));
    }

    for (int i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];
        rpEnt_t             *e;
        rpObs_t              o;
        int                  weapon, steady;
        float                speed;

        if (es->eType != ET_PLAYER || es->number < 0 || es->number >= RP_MAX_ENTS) {
            continue;
        }
        e = &s_ents[es->number];
        memset(&o, 0, sizeof(o));
        CG_RP_Anim(es->modelindex, es->frameInfo[CG_RP_LegsSlot(e, es)].index, &o.legs);
        o.serverTime  = snap->serverTime;
        o.trTime      = es->pos.trTime;
        o.yaw         = es->netangles[YAW];
        o.teleportBit = es->eFlags & EF_TELEPORT_BIT;
        VectorCopy(es->netorigin, o.origin);
        VectorCopy(es->pos.trDelta, o.velocity);

        weapon     = CG_RP_Weapon(snap, es->number);
        o.runSpeed = RP_LearnRunSpeed(&s_learn, weapon);
        if (!o.runSpeed) {
            // the stock run speed with an SMG until the server shows its own
            o.runSpeed = cg_target_game >= TG_MOHTA ? 302 : 264;
        }
        e->usable = !(es->eFlags & (EF_DEAD | EF_CLIMBWALL | EF_PLAYER_ARTILLERY)) && es->solid && o.legs.cls != RPL_DEAD;
        if (e->usable) {
            vec3_t waist;

            VectorCopy(es->netorigin, waist);
            waist[2] += 32;
            if (CG_PointContents(waist, -1) & MASK_WATER) {
                e->usable = 0;
            }
        }
        o.reset = !e->usable || es->modelindex != e->modelindex;

        // a steady speed teaches the server's run speeds and PM_CmdScale
        speed  = sqrtf(o.velocity[0] * o.velocity[0] + o.velocity[1] * o.velocity[1]);
        steady = e->lastServerTime == snap->serverTime - s_server.frameMsec
              && fabsf(o.velocity[0] - e->prevVel[0]) < 0.5f && fabsf(o.velocity[1] - e->prevVel[1]) < 0.5f;
        if (steady && e->usable) {
            RP_LearnObserve(&s_learn, &s_server, weapon, &o.legs, speed);
        }

        RP_Ingest(&e->rp, &o, &s_server);
        e->lastServerTime = snap->serverTime;
        e->prevVel[0]     = o.velocity[0];
        e->prevVel[1]     = o.velocity[1];
        e->modelindex     = es->modelindex;
        IntegerToBoundingBox(es->solid, e->mins, e->maxs);
    }

    if (s_frameActive && cg_lagometer->integer) {
        CG_RP_Measure(snap);
    }
}

/*
==================
Drawing
==================
*/

// how far ahead of cg.time to draw, in server time; -1 when unknown
static float CG_RP_Lead(void)
{
    const int max  = cg_remotePredictionMaxLead->integer;
    float     lead = RP_LeadMean(&s_lead, s_server.frameMsec);

    if (lead < 0) {
        // until our commands have been acked a few times: the snapshot round trip,
        // the client clock running behind snapshots, and half a frame
        lead = cg.snap->ping + 55 + 0.5f * s_server.frameMsec;
    }
    // cg_remotePredictionMaxLead: compensate at most that much ping. The rest of
    // the lead stays, so 60 at 82 ms ping leads as a 60 ms ping needs
    if (max > 0 && cg.snap->ping > max) {
        lead = Q_max(0.0f, lead - (cg.snap->ping - max));
    }
    return lead;
}

// the most the push for a player's late commands may add to the lead (ms): 0
// with cg_remotePredictionPush off, else the ping compensation
// cg_remotePredictionMaxLead leaves unused; -1 no limit
static int CG_RP_MaxPush(void)
{
    const int max = cg_remotePredictionMaxLead->integer;

    if (!cg_remotePredictionPush->integer) {
        return 0;
    }
    if (max <= 0) {
        return -1;
    }
    return cg.snap->ping < max ? max - cg.snap->ping : 0;
}

static qboolean CG_RP_Active(void)
{
    // not in single player; demos can't measure a lead; spectators don't shoot
    return cg_remotePrediction->integer && cgs.gametype != GT_SINGLE_PLAYER && !cg.demoPlayback
            && !(cg.snap->ps.pm_flags & PMF_SPECTATING) && cg.snap->ps.clientNum == cg.clientNum
             ? qtrue
             : qfalse;
}

// a box move that slides along what it hits, as PM_SlideMove does (up to three
// surfaces); the share of the move made, -1 when it starts stuck
static float CG_RP_Slide(rpEnt_t *e, const vec3_t from, const vec3_t move, int number, vec3_t out)
{
    trace_t tr;
    vec3_t  start, left, end;
    float   made = 0, total = VectorLength(move);

    VectorCopy(from, start);
    VectorCopy(move, left);
    VectorCopy(from, out);
    for (int bump = 0; bump < 3; bump++) {
        float into;

        VectorAdd(start, left, end);
        CG_Trace(&tr, start, e->mins, e->maxs, end, number, MASK_PLAYERSOLID, qfalse, qfalse, "CG_RP_Slide");
        if (tr.startsolid || tr.allsolid) {
            return bump ? made : -1;
        }
        VectorCopy(tr.endpos, out);
        made += VectorLength(left) * tr.fraction;
        if (tr.fraction == 1.0f) {
            break;
        }
        VectorCopy(tr.endpos, start);
        VectorScale(left, 1.0f - tr.fraction, left);
        into = DotProduct(left, tr.plane.normal);
        if (into < 0) {
            VectorMA(left, -into, tr.plane.normal, left);
        }
    }
    return total > 0 ? made / total : 1;
}

// from the player's server position toward the prediction: slides along walls,
// steps up stairs when blocked, and settles on the floor (or follows gravity
// in the air)
static void CG_RP_Place(rpEnt_t *e, const rpRecord_t *r, const float d[2], float horizon, int number, vec3_t out)
{
    trace_t tr;
    vec3_t  start, end, move, stepped;
    float   f;

    // sideways, a unit above the ground
    VectorCopy(r->obs.origin, start);
    start[2] += 1;
    VectorSet(move, d[0], d[1], 0);
    f = CG_RP_Slide(e, start, move, number, out);
    if (f < 0) {
        // the box already touches something here: don't second-guess the prediction
        VectorAdd(start, move, out);
    } else if (f < 0.999f) {
        // blocked: a stair step up may get further
        start[2] += RP_STEPSIZE;
        if (CG_RP_Slide(e, start, move, number, stepped) > f) {
            VectorCopy(stepped, out);
        }
    }

    VectorCopy(out, start);
    VectorCopy(out, end);
    if (r->air) {
        // a coast under gravity
        const float t = horizon * 0.001f;
        const float g = cg.snap->ps.gravity > 0 ? (float)cg.snap->ps.gravity : 800.0f;

        end[2] = r->obs.origin[2] + r->obs.velocity[2] * t - 0.5f * g * t * t;
    } else {
        end[2] = r->obs.origin[2] - RP_STEPSIZE - 2;
    }
    CG_Trace(&tr, start, e->mins, e->maxs, end, number, MASK_PLAYERSOLID, qfalse, qfalse, "CG_RP_Place");
    if (tr.startsolid || tr.allsolid || (!r->air && tr.fraction == 1.0f)) {
        // stuck, or walked off a ledge: keep the height, the next snapshot shows the fall
        out[2] = r->obs.origin[2];
    } else {
        out[2] = tr.endpos[2];
    }
}

// where we drew a player this frame, kept under the server frame its target
// time (cg.time + lead) is nearest, if nearer than before
static void CG_RP_RecordDrawn(rpEnt_t *e, const vec3_t pos, const rpRecord_t *r, float lead)
{
    const int   fm     = s_server.frameMsec > 0 ? s_server.frameMsec : 50;
    const float target = (float)cg.time + lead;
    const int   phase  = ((s_latestTime % fm) + fm) % fm; // server frames needn't start at 0
    const int   frame  = (int)floorf((target - (float)phase) / (float)fm + 0.5f) * fm + phase;
    const float dist   = fabsf(target - (float)frame);
    rpDrawn_t  *d      = &e->drawn[e->drawnHead];

    if (e->drawnCount && d->serverTime == frame) {
        if (dist >= d->dist) {
            return;
        }
    } else if (e->drawnCount && frame < d->serverTime) {
        // the lead fell: that frame is already kept
        return;
    } else {
        e->drawnHead = (e->drawnHead + 1) % RP_DRAWN;
        d            = &e->drawn[e->drawnHead];
        e->drawnCount = Q_min(e->drawnCount + 1, RP_DRAWN);
    }
    d->serverTime = frame;
    d->dist       = dist;
    VectorCopy(pos, d->origin);
    d->fromTime   = r->obs.serverTime;
    d->fromTrTime = r->obs.trTime;
}

void CG_RP_UpdateEntity(centity_t *cent)
{
    const entityState_t *es = &cent->currentState;
    const rpRecord_t    *r;
    rpEnt_t             *e;
    float                d[2], horizon, f;
    vec3_t               pos;

    VectorCopy(cent->lerpOrigin, cent->netLerpOrigin);
    if (es->eType != ET_PLAYER || es->number < 0 || es->number >= RP_MAX_ENTS || es->number == cg.snap->ps.clientNum) {
        return;
    }
    e = &s_ents[es->number];
    r = RP_Newest(&e->rp);
    if (!s_frameActive || !e->usable || !r || e->lastServerTime < s_latestTime || s_frameLead < 0
        || !RP_Predict(&e->rp, &s_server, cg.time + s_frameLead, CG_RP_MaxPush(), d, &horizon)) {
        e->shownValid = 0;
        return;
    }

    CG_RP_Place(e, r, d, horizon, es->number, pos);

    // a new snapshot moves the prediction: fade the jump out instead of popping
    if (e->shownValid && e->shownFrom != r->obs.serverTime) {
        vec3_t last;

        VectorSubtract(e->shown, e->shownPrev, last);
        VectorAdd(e->shown, last, last); // where we would have drawn it now
        VectorSubtract(last, pos, e->offset);
        if (VectorLength(e->offset) > 64) {
            VectorClear(e->offset); // a respawn or teleport: no fade
        }
    } else if (!e->shownValid) {
        VectorClear(e->offset);
    }
    f = cg_remotePredictionSmooth->value > 0 ? expf(-cg.frametime / cg_remotePredictionSmooth->value) : 0.0f;
    VectorScale(e->offset, f, e->offset);
    VectorAdd(pos, e->offset, pos);

    VectorCopy(e->shownValid ? e->shown : pos, e->shownPrev);
    VectorCopy(pos, e->shown);
    e->shownValid = 1;
    e->shownFrom  = r->obs.serverTime;
    VectorCopy(pos, cent->lerpOrigin);
    if (cg_lagometer->integer) {
        CG_RP_RecordDrawn(e, pos, r, s_frameLead);
    }
}

void CG_RP_BeginFrame(void)
{
    int latest, n;

    cgi.GetCurrentSnapshotNumber(&latest, &s_latestTime);
    if (latest < s_lastSnapNum) {
        // a new connection numbers snapshots from the start again
        s_lastSnapNum = latest - 1;
    }

    // snapshots aren't read while cg_remotePrediction is 0. Turned on again, it
    // starts from the newest snapshot (older ones are past the commands kept for
    // the lead) and falls back to the velocity until a second one comes
    if (!cg_remotePrediction->integer) {
        s_lastSnapNum = latest - 1;
        s_readGap     = qtrue;
        s_frameActive = qfalse;
        s_frameLead   = -1;
        return;
    }

    for (n = Q_max(s_lastSnapNum + 1, latest - RP_SNAPS + 1); n <= latest; n++) {
        if (!cgi.GetSnapshot(n, &s_snap) || s_snap.serverTime <= s_lastServerTime
            || (s_snap.snapFlags & SNAPFLAG_NOT_ACTIVE)) {
            continue;
        }
        s_lastServerTime = s_snap.serverTime;
        if (s_readGap) {
            // the histories and our lead are from before the gap
            memset(s_ents, 0, sizeof(s_ents));
            RP_LeadReset(&s_lead);
            s_readGap = qfalse;
        }
        CG_RP_Ingest(&s_snap);
    }
    s_lastSnapNum = latest;

    s_frameActive = CG_RP_Active();
    s_frameLead   = s_frameActive ? CG_RP_Lead() : -1;
}
