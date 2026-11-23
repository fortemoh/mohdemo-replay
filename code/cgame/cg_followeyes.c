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

// cg_followeyes.c -- Added in Reloaded: a followed player watched through
// their eyes on any server (cg_spectatefirstperson)
//
// Every server (Allied Assault 1.11, Spearhead, Breakthrough, OpenMoHAA) sends
// a spectator who follows a player a camera behind that player (fgame's
// Player::SetPlayerView and GetSpectateFollowOrientation, the same code in the
// original 1.11 game library):
//
//   ps.camera_angles = the player's view angles, the pitch plus
//                      g_spectatefollow_pitch (2) x the trace's fraction
//   ps.camera_origin = their eye + forward x -56 + up x 24 + right x 0.65 x
//                      their lean (g_spectatefollow_forward / _up), pulled in
//                      toward the top of their head when a wall is in the way
//
// Each snapshot read is rewritten into what a server of this code sends with
// g_spectatefollow_firstperson 1: the playerstate under the followed player's
// number, their origin, view angles, eye height and lean worked back from the
// camera, their weapon from the gun in their hands, its first-person animation
// from their upper body's (the server's state machine starts both together),
// their death seen through their eyes, and the view kick of a hit made from
// their health and the bullets the server sends for the effects. The rest of
// the cgame then draws that player's entity as its first-person model, as for
// our own player. What servers don't send to a spectator stays out: the ammo
// and the red flash. A server of this code seen showing followers the
// player's own view itself is asked for the view set here instead
// (spectatemode), which has all of it.

#include "cg_local.h"
#include "../corepp/tiki.h"

cvar_t *cg_spectatefirstperson;

// the servers' follow camera (g_spectatefollow_forward, _up, _pitch defaults; the lean's share)
#define FE_CAM_FORWARD -56.0f
#define FE_CAM_UP      24.0f
#define FE_CAM_PITCH   2.0f
#define FE_CAM_LEAN    0.65f

// the player's model's animation slots: the legs in 0 and 1, the upper body in 2 and 3
#define FE_TORSO_SLOT 2

// a stock server's frame (ms; they run 20 a second)
#define FE_SERVER_FRAME 50

// the hit's view kick, as the server's Player::DamageFeedback (g_viewkick_pitch, _yaw, _roll defaults),
// worked once a server frame
#define FE_KICK_PITCH 0.3f
#define FE_KICK_YAW   0.3f
#define FE_KICK_ROLL  0.15f

// the number the turret's view model of a followed player on a turret takes in the snapshots (see FE_Turret): one
// below the world's, which the servers leave to the last
#define FE_TURRET_VIEWMODEL (ENTITYNUM_WORLD - 1)

// the bullets and explosions the server sent lately, to tell where a hit came from
#define FE_MAX_SHOTS 32
#define FE_SHOT_TIME 300 // ms

// the turrets seen, as last sent (see FE_KeepTurrets)
#define FE_MAX_TURRETS 16

// a turret not sent (see FE_Turret): the bullets that start this near it are its shots, and each one's "fire"
// lasts this long
#define FE_TURRET_MUZZLE 64
#define FE_TURRET_FIRE   100 // ms

typedef struct {
    int   index;
    float time;
} feAnim_t;

typedef struct {
    vec3_t   start, end;
    int      time; // cgi.Milliseconds() when it came
    qboolean blast;
} feShot_t;

static struct {
    int      follow;     // the player watched through their eyes in the last snapshot read, -1 none
    int      serverTime; // that snapshot's
    vec3_t   origin;     // their origin in it
    float    lean;       // their lean, kept while the camera is pulled in onto their head
    float    eyeHeight;  // their eye height worked out last, and the top of their box then
    float    eyeBoxTop;
    float    boxTop; // the top of their box in the last snapshot read
    int      keptFrames; // snapshots in a row their view was kept as it was (see CG_FollowEyes_Snapshot)
    int      weaponEnt; // the gun in their hands, and its animation
    feAnim_t weaponAnim;
    feAnim_t torsoAnim; // their upper body's newest animation, and its weight then
    float    torsoWeight;
    qboolean torsoKnown; // seen to start a first-person animation (the stock state machine's names)
    int      vmAnim; // the first-person animation made from them
    int      vmAnimChanged;
    // their hits: their health in the last snapshot read, the kick and the damage it decays with, the red
    // flash, the damage's direction for the HUD (see FE_HitFeedback)
    int      health;
    vec3_t   kick;
    float    kickCount;
    float    flash;
    int      damageDir;
    qboolean airborne; // off the ground in the snapshot read (see FE_Airborne), and the last one they were in
    int      airTime;
    // the turret they are on: its animation and the slot it is in, and the shake its shots give their view
    // (see FE_Turret); for one not sent, the bullets read (fe_numShots then) and its last shot's slot and time
    feAnim_t     turretAnim;
    int          turretSlot;
    float        turretJitter;
    unsigned int turretShots;
    int          turretFireSlot;
    int          turretFireTime;
    // the entity the cgame draws as its first-person model, and for each player
    // what is added to their usageIndex (see FE_Salt)
    int      viewEnt;
    int      salt[MAX_CLIENTS];
    // the server shows followers the player's own view itself (see CG_FollowEyes_Snapshot), and the
    // spectatemode last asked of it (-1 none)
    qboolean serverMode;
    int      modeSent;
} fe;

static feShot_t     fe_shots[FE_MAX_SHOTS];
static unsigned int fe_numShots;

static entityState_t fe_turrets[FE_MAX_TURRETS];
static int           fe_numTurrets;

/*
===============
CG_FollowEyes_Init
===============
*/
void CG_FollowEyes_Init(void)
{
    cg_spectatefirstperson = cgi.Cvar_Get("cg_spectatefirstperson", "0", CVAR_ARCHIVE);

    memset(&fe, 0, sizeof(fe));
    fe.follow    = -1;
    fe.weaponEnt = -1;
    fe.viewEnt   = -1;
    fe.modeSent  = -1;
    fe.health    = -1;

    memset(fe_shots, 0, sizeof(fe_shots));
    fe_numShots = 0;

    memset(fe_turrets, 0, sizeof(fe_turrets));
    fe_numTurrets = 0;
}

/*
===============
CG_FollowEyes_Shot

A bullet the server sent, from its start to where it ended (CG_MakeBulletTracer)
===============
*/
void CG_FollowEyes_Shot(const vec3_t start, const vec3_t end)
{
    feShot_t *shot;

    if (!cg_spectatefirstperson || !cg_spectatefirstperson->integer) {
        return;
    }

    shot = &fe_shots[fe_numShots++ % FE_MAX_SHOTS];
    VectorCopy(start, shot->start);
    VectorCopy(end, shot->end);
    shot->time  = cgi.Milliseconds();
    shot->blast = qfalse;
}

/*
===============
CG_FollowEyes_Blast

An explosion the server sent (CG_MakeExplosionEffect)
===============
*/
void CG_FollowEyes_Blast(const vec3_t origin)
{
    feShot_t *shot;

    if (!cg_spectatefirstperson || !cg_spectatefirstperson->integer) {
        return;
    }

    shot = &fe_shots[fe_numShots++ % FE_MAX_SHOTS];
    VectorCopy(origin, shot->start);
    VectorCopy(origin, shot->end);
    shot->time  = cgi.Milliseconds();
    shot->blast = qtrue;
}

/*
===============
CG_FollowEyes_PlayerState

The playerstate the view is drawn from while a followed player is watched through their eyes here (the snapshot's
as rewritten), for what the client draws of it itself: the red flash over the view, the HUD's health and damage
arrows. NULL otherwise, a server of this code showing them itself too (the client's own snapshot has it all).
===============
*/
const playerState_t *CG_FollowEyes_PlayerState(void)
{
    if (!cg.snap || fe.serverMode || !(cg.snap->ps.pm_flags & PMF_SPECTATING) || cg.snap->ps.clientNum == cg.clientNum) {
        return NULL;
    }

    return &cg.snap->ps;
}

/*
===============
FE_ServerInitArg

An argument of the model's server init commands ("name", "weapontype"), read from the client's copy
===============
*/
static const char *FE_ServerInitArg(dtiki_t *tiki, const char *key)
{
    int i;

    if (!tiki || !tiki->a) {
        return NULL;
    }

    for (i = 0; i < tiki->a->num_server_initcmds; i++) {
        const dtikicmd_t *cmd = &tiki->a->server_initcmds[i];

        if (cmd->num_args >= 2 && !Q_stricmp(cmd->args[0], key)) {
            return cmd->args[1];
        }
    }

    return NULL;
}

static int FE_WeaponClass(const char *type)
{
    static const struct {
        const char *name;
        int         wclass;
    } classes[] = {
        {"pistol",  WEAPON_CLASS_PISTOL },
        {"rifle",   WEAPON_CLASS_RIFLE  },
        {"smg",     WEAPON_CLASS_SMG    },
        {"mg",      WEAPON_CLASS_MG     },
        {"grenade", WEAPON_CLASS_GRENADE},
        {"heavy",   WEAPON_CLASS_HEAVY  },
        {"cannon",  WEAPON_CLASS_CANNON },
        {"item",    WEAPON_CLASS_ITEM   },
        {"item1",   WEAPON_CLASS_ITEM1  },
        {"item2",   WEAPON_CLASS_ITEM2  },
        {"item3",   WEAPON_CLASS_ITEM3  },
        {"item4",   WEAPON_CLASS_ITEM4  },
    };
    int i;

    for (i = 0; type && i < (int)ARRAY_LEN(classes); i++) {
        if (!Q_stricmp(type, classes[i].name)) {
            return classes[i].wclass;
        }
    }

    return 0;
}

/*
===============
FE_Crosshair

Whether the player sees a crosshair with that gun, as the server works it out (Player::UpdateStats,
Weapon::GetUseCrosshair): not through a scope, nor with an item; always in Allied Assault; in Spearhead
and Breakthrough the gun's own setting (dmcrosshair over crosshair, off when not given), and off while
moving faster than the gun can be fired at (maxfiremovement x movementspeed of the run speed)
===============
*/
static int FE_Crosshair(dtiki_t *tiki, const playerState_t *ps)
{
    const char *arg;
    float       maxFire, moveSpeed;

    if (ps->stats[STAT_INZOOM] && ps->stats[STAT_INZOOM] <= 30) {
        return 0;
    }

    arg = FE_ServerInitArg(tiki, "classname");
    if (arg && !Q_stricmp(arg, "InventoryItem")) {
        return 0;
    }

    if (cg_protocol < PROTOCOL_MOHTA_MIN) {
        return 1;
    }

    arg = FE_ServerInitArg(tiki, "dmcrosshair");
    if (!arg) {
        arg = FE_ServerInitArg(tiki, "crosshair");
    }
    if (!arg || !atoi(arg)) {
        return 0;
    }

    arg     = FE_ServerInitArg(tiki, "maxfiremovement");
    maxFire = arg ? atof(arg) : 1;
    arg     = FE_ServerInitArg(tiki, "dmmovementspeed");
    if (!arg) {
        arg = FE_ServerInitArg(tiki, "movementspeed");
    }
    moveSpeed = arg ? atof(arg) : 1;

    // (of Spearhead's and Breakthrough's run speed, sv_runspeed 287)
    if (maxFire < 1 && sqrt(ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1]) / 287.0f > maxFire * moveSpeed) {
        return 0;
    }

    return 1;
}

/*
===============
FE_ItemIndex

The weapon's item number (playerState_t activeItems), from the names the server registered
===============
*/
static int FE_ItemIndex(const char *name)
{
    int i;

    for (i = 1; CS_WEAPONS + i < CS_SOUNDTRACK; i++) {
        if (!Q_stricmp(CG_ConfigString(CS_WEAPONS + i), name)) {
            return i;
        }
    }

    return -1;
}

/*
===============
FE_AnimChanged

Whether an animation started since it was last read (another one, or the same one again)
===============
*/
static qboolean FE_AnimChanged(feAnim_t *last, int index, float time)
{
    qboolean changed = index != last->index || time + 0.01f < last->time;

    last->index = index;
    last->time  = time;
    return changed;
}

/*
===============
FE_WeaponVMAnim

The first-person animation for a gun's own (Weapon::SetWeaponAnim), -1 for none; used only where the
upper body's animations aren't the stock state machine's (see FE_TorsoVMAnim)
===============
*/
static int FE_WeaponVMAnim(const char *anim)
{
    if (!Q_stricmpn(anim, "secondaryfire", 13)) {
        return VM_ANIM_FIRE_SECONDARY;
    }
    if (!Q_stricmpn(anim, "secondarycharge", 15) || !Q_stricmpn(anim, "charge", 6)) {
        return VM_ANIM_CHARGE;
    }
    if (!Q_stricmpn(anim, "fire", 4)) {
        return VM_ANIM_FIRE;
    }
    if (!Q_stricmp(anim, "reload_single")) {
        return VM_ANIM_RELOAD_SINGLE;
    }
    if (!Q_stricmp(anim, "reload_end")) {
        return VM_ANIM_RELOAD_END;
    }
    if (!Q_stricmpn(anim, "reload", 6)) {
        return VM_ANIM_RELOAD;
    }
    if (!Q_stricmp(anim, "raise")) {
        return VM_ANIM_PULLOUT;
    }
    if (!Q_stricmp(anim, "putaway")) {
        return VM_ANIM_PUTAWAY;
    }
    if (!Q_stricmpn(anim, "idle", 4)) {
        return VM_ANIM_IDLE;
    }

    return -1;
}

static qboolean FE_EndsWith(const char *s, const char *end)
{
    const size_t len = strlen(s), endLen = strlen(end);

    return len >= endLen && !Q_stricmp(s + len - endLen, end);
}

/*
===============
FE_TorsoVMAnim

The first-person animation for the upper body's, -1 for none. The server's state machine (global/mike_torso.st)
sets the first-person animation as it enters a state, and the state's upper body animation starts with it:
"smg_stand_fire" fire (each shot), "grenade_crouch_charge" charge, "kar98_stand_rechamber" rechamber,
"rifle_reload" / "springfield_reload_start" reload, "shotgun_reload_loop" reload_single (each shell),
"shotgun_reload_end" reload_end, "pistol_butt" fire_secondary, "smg_crouch_lower" putaway, "pistol_stand_raise"
pullout. With none it is back to idle (see CG_FollowEyes_Snapshot); a ladder's steps come from the animations'
own frame commands (FE_FrameVMAnim).
===============
*/
static int FE_TorsoVMAnim(const char *anim)
{
    if (strstr(anim, "rechamber")) {
        return VM_ANIM_RECHAMBER;
    }
    if (strstr(anim, "reload_end")) {
        return VM_ANIM_RELOAD_END;
    }
    if (strstr(anim, "reload_loop") || strstr(anim, "reload_single")) {
        return VM_ANIM_RELOAD_SINGLE;
    }
    if (strstr(anim, "reload")) {
        return VM_ANIM_RELOAD;
    }
    if (strstr(anim, "_charge")) {
        return VM_ANIM_CHARGE;
    }
    if (strstr(anim, "_butt")) {
        return VM_ANIM_FIRE_SECONDARY;
    }
    if (strstr(anim, "_fire")) {
        return VM_ANIM_FIRE;
    }
    if (FE_EndsWith(anim, "_lower")) {
        return VM_ANIM_PUTAWAY;
    }
    if (FE_EndsWith(anim, "_raise")) {
        return VM_ANIM_PULLOUT;
    }

    return -1;
}

/*
===============
FE_VMAnimForName

The first-person animation a "viewmodelanim" names (Player::ViewModelAnim), -1 for none
===============
*/
static int FE_VMAnimForName(const char *name)
{
    static const struct {
        const char *name;
        int         anim;
    } anims[] = {
        {"charge",         VM_ANIM_CHARGE        },
        {"fire",           VM_ANIM_FIRE          },
        {"fire_secondary", VM_ANIM_FIRE_SECONDARY},
        {"rechamber",      VM_ANIM_RECHAMBER     },
        {"reload",         VM_ANIM_RELOAD        },
        {"reload_single",  VM_ANIM_RELOAD_SINGLE },
        {"reload_end",     VM_ANIM_RELOAD_END    },
        {"pullout",        VM_ANIM_PULLOUT       },
        {"putaway",        VM_ANIM_PUTAWAY       },
        {"ladderstep",     VM_ANIM_LADDERSTEP    },
        {"idle",           VM_ANIM_IDLE          },
    };
    int i;

    for (i = 0; i < (int)ARRAY_LEN(anims); i++) {
        if (!Q_stricmp(name, anims[i].name)) {
            return anims[i].anim;
        }
    }

    return -1;
}

/*
===============
FE_FrameVMAnim

The first-person animation an animation's own frame commands set after its time from and up to to (its server
commands "viewmodelanim ladderstep" at a frame or its last: the ladders' in models/player/base/anims_misc.txt),
-1 for none
===============
*/
static int FE_FrameVMAnim(dtiki_t *tiki, int anim, float from, float to)
{
    const dtikianimdef_t *def;
    float                 frameTime, t;
    int                   i, frames, vm = -1;

    if (!tiki || !tiki->a || anim < 0 || anim >= tiki->a->num_anims) {
        return -1;
    }

    def       = tiki->a->animdefs[anim];
    frameTime = cgi.Anim_Frametime(tiki, anim);
    frames    = cgi.Anim_NumFrames(tiki, anim);

    for (i = 0; def && i < def->num_server_cmds; i++) {
        const dtikicmd_t *cmd = &def->server_cmds[i];

        if (cmd->num_args < 2 || Q_stricmp(cmd->args[0], "viewmodelanim")) {
            continue;
        }

        if (cmd->frame_num >= 0) {
            t = cmd->frame_num * frameTime;
        } else if (cmd->frame_num == TIKI_FRAME_LAST || cmd->frame_num == TIKI_FRAME_END) {
            t = (frames - 1) * frameTime;
        } else {
            continue;
        }
        // the server runs it that long after the animation began (Animate::NewAnim posts it), and the time
        // snapshots show is a frame further on (the first shows a frame's)
        t += FE_SERVER_FRAME / 1000.0f;

        if (t > from && t <= to && FE_VMAnimForName(cmd->args[1]) != -1) {
            vm = FE_VMAnimForName(cmd->args[1]);
        }
    }

    return vm;
}

/*
===============
FE_IsClass

Whether a model's server class is the one named
===============
*/
static qboolean FE_IsClass(dtiki_t *tiki, const char *classname)
{
    const char *arg = FE_ServerInitArg(tiki, "classname");

    return arg && !Q_stricmp(arg, classname);
}

/*
===============
FE_FindWeapon

The gun in the player's hands: an entity on them whose model is a weapon, or Spearhead's packed MG42
(CarryableTurret, an item with no weapon type)
===============
*/
static const entityState_t *FE_FindWeapon(const snapshot_t *snap, int follow, dtiki_t **tikiOut)
{
    int i;

    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];
        dtiki_t             *tiki;

        if (es->parent != follow || !es->modelindex) {
            continue;
        }

        tiki = cgi.R_Model_GetHandle(cgs.model_draw[es->modelindex]);
        if (FE_ServerInitArg(tiki, "name")
            && (FE_ServerInitArg(tiki, "weapontype") || FE_IsClass(tiki, "CarryableTurret"))) {
            *tikiOut = tiki;
            return es;
        }
    }

    return NULL;
}

/*
===============
FE_EyeHeightOff

How far an eye height worked out is from the nearest of the player's own (no prone in multiplayer)
===============
*/
static float FE_EyeHeightOff(float vh, float *nearest)
{
    // not static: the four are static const variables (bg_public.h), which C doesn't count as constants (MSVC's
    // C2099)
    const float heights[] = {
        DEFAULT_VIEWHEIGHT, CROUCH_RUN_VIEWHEIGHT, JUMP_START_VIEWHEIGHT, CROUCH_VIEWHEIGHT
    };
    float best = 1000;
    int   i;

    for (i = 0; i < (int)ARRAY_LEN(heights); i++) {
        if (fabs(vh - heights[i]) < best) {
            best     = fabs(vh - heights[i]);
            *nearest = heights[i];
        }
    }

    return best;
}

/*
===============
FE_SolveCamera

The follow camera worked back for a top of their box (see FE_Camera); solved is whether the eye height was
worked out too, not taken from theirs (the guess, the last one, unless the camera fits another clearly better).
Returns qfalse when the camera isn't one behind them.
===============
*/
static qboolean FE_SolveCamera(
    const playerState_t *ps,
    const vec3_t         origin,
    float                boxTop,
    float                guess,
    float               *fracOut,
    float               *vhOut,
    float               *lnOut,
    qboolean            *solvedOut,
    float               *fitOut
)
{
    float    heights[] = {0, DEFAULT_VIEWHEIGHT, CROUCH_RUN_VIEWHEIGHT, JUMP_START_VIEWHEIGHT, CROUCH_VIEWHEIGHT};
    vec3_t   start, d, ang, fwd, right, up, a, g, zr, cross;
    float    frac, vh, ln, det, gr, dr, gg, bestResid, fit, nearest;
    qboolean solved;
    int      i, j;

    heights[0] = guess;

    VectorCopy(origin, start);
    start[2] += boxTop - 2;
    VectorSubtract(ps->camera_origin, start, d);

    frac   = 1;
    vh     = guess;
    ln     = fe.lean;
    solved = qfalse;
    fit    = 0;
    VectorClear(g);
    VectorClear(right);

    // the pitch depends on f: three rounds settle it
    for (i = 0; i < 3; i++) {
        VectorCopy(ps->camera_angles, ang);
        ang[ROLL] = 0;
        ang[PITCH] -= FE_CAM_PITCH * frac;
        AngleVectors(ang, fwd, right, up);

        VectorScale(fwd, FE_CAM_FORWARD, a);
        VectorMA(a, FE_CAM_UP, up, a);
        a[2] += 2 - boxTop;

        // Cramer's rule on the columns a, z and right (right is level: z x right is level forward)
        VectorSet(zr, -right[1], right[0], 0);
        det = DotProduct(a, zr);

        solved = qfalse;
        if (fabs(det) > 12) {
            float f = DotProduct(d, zr) / det;

            if (f > 0.3f) {
                CrossProduct(d, right, cross);
                vh = DotProduct(a, cross) / det / f;
                VectorSet(cross, -d[1], d[0], 0); // z x d
                ln = DotProduct(a, cross) / det / (f * FE_CAM_LEAN);

                if (vh > 4 && vh < 100 && fabs(ln) < 50) {
                    frac   = f;
                    solved = qtrue;
                    continue;
                }
            }
        }

        // the eye height one of theirs, the one the camera lies nearest the line of (the guess unless another
        // is clearly nearer): f from the camera's offset off the lean's line, the lean from the rest
        bestResid = 1000;
        for (j = 0; j < (int)ARRAY_LEN(heights); j++) {
            vec3_t gj, r;
            float  fj, resid;

            VectorCopy(a, gj);
            gj[2] += heights[j];
            gr = DotProduct(gj, right);
            dr = DotProduct(d, right);
            gg = DotProduct(gj, gj) - gr * gr;
            if (gg < 1) {
                continue;
            }

            fj = (DotProduct(d, gj) - dr * gr) / gg;
            VectorMA(d, -fj, gj, r);
            VectorMA(r, -(dr - fj * gr), right, r);
            resid = VectorLength(r) + (heights[j] == guess ? 0 : 0.5f);
            if (resid < bestResid) {
                bestResid = resid;
                fit       = VectorLength(r);
                VectorCopy(gj, g);
                frac = fj;
                vh   = heights[j];
                ln   = fj > 0.15f ? (dr - fj * gr) / (fj * FE_CAM_LEAN) : fe.lean;
            }
        }
        if (bestResid >= 1000) {
            return qfalse;
        }
    }

    if (frac < -0.02f || frac > 1.02f || fabs(ln) > 50) {
        // not a camera behind them (another g_spectatefollow_forward or _up)
        return qfalse;
    }

    if (!solved) {
        // the camera must lie near the line the guess gives (a wrong guess of the eye height puts it off by
        // f x the difference)
        VectorScale(g, frac, a);
        VectorMA(a, frac * FE_CAM_LEAN * ln, right, a);
        VectorSubtract(d, a, a);
        if (VectorLength(a) > 32) {
            return qfalse;
        }
    }

    *fracOut   = Com_Clamp(0, 1, frac);
    *vhOut     = vh;
    *lnOut     = ln;
    *solvedOut = solved;
    // how far off the camera lies: the eye height's from one of theirs, or the camera's from the line
    *fitOut = solved ? frac * FE_EyeHeightOff(vh, &nearest) : fit;
    return qtrue;
}

/*
===============
FE_Camera

Works the followed player's view angles, eye height and lean back from the follow camera. The server traces it
from the top of their head (h) toward where it would be in the open, and puts it the trace's fraction f of the
way there, the pitch raised 2 x f:

  camera - h = f x (forward x -56 + up x 24 + (0, 0, 2 - box top)) + (f x eye height) x z + (f x 0.65 x lean) x right

three equations in f, the eye height and the lean, solved anywhere the camera isn't pulled in almost onto
their head; there (or while they look steeply down, where the eye height and f can't be told apart) the eye
height is the last one worked out (their box's when it changed) and the lean as it was when f is too small
to show it. The box sent can be a server frame off the camera's as it changes (crouching, jumping, landing),
which puts the eye height off by the change x (1 - f) / f: when the camera doesn't fit the box sent, the last
box and the player's other boxes are tried too, and the one it fits best kept. Returns qfalse when the camera
isn't one behind them.
===============
*/
static qboolean
FE_Camera(const playerState_t *ps, const entityState_t *pl, vec3_t viewangles, float *viewheight, float *lean)
{
    vec3_t   mins, maxs;
    float    tops[5], top, guess, frac, vh, ln, fit, bestFrac, bestVh, bestLn, bestTop, bestFit, nearest;
    qboolean solved, found, any;
    int      i, j;

    IntegerToBoundingBox(pl->solid, mins, maxs);
    tops[0]   = maxs[2];
    tops[1]   = fe.boxTop;
    tops[2]   = MAXS_Z;
    tops[3]   = CROUCH_RUN_MAXS_Z;
    tops[4]   = CROUCH_MAXS_Z;
    fe.boxTop = maxs[2];
    if (maxs[2] < 8) {
        return qfalse;
    }

    found    = qfalse;
    any      = qfalse;
    bestFit  = 1000;
    bestFrac = bestVh = bestLn = bestTop = 0;

    for (i = 0; i < (int)ARRAY_LEN(tops); i++) {
        top = tops[i];
        for (j = 0; j < i && tops[j] != top; j++) {}
        if (top < 8 || j < i) {
            continue;
        }

        // the eye height when it can't be worked out
        if (fe.eyeHeight > 0 && fe.eyeBoxTop == top) {
            guess = fe.eyeHeight;
        } else {
            guess = top < (CROUCH_MAXS_Z + MAXS_Z) / 2 ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT;
        }

        if (!FE_SolveCamera(ps, pl->origin, top, guess, &frac, &vh, &ln, &solved, &fit)) {
            continue;
        }

        // the box sent unless another fits clearly better
        if (!any || fit < bestFit - 1) {
            any      = qtrue;
            found    = solved;
            bestFit  = fit;
            bestFrac = frac;
            bestVh   = vh;
            bestLn   = ln;
            bestTop  = top;
        }

        if (i == 0 && fit < 1) {
            break;
        }
    }

    if (!any) {
        return qfalse;
    }

    // on the player's own eye height when that near one (the crouched weapon pose and the footsteps look for them)
    if (FE_EyeHeightOff(bestVh, &nearest) < 3) {
        bestVh = nearest;
    }

    if (found) {
        fe.eyeHeight = bestVh;
        fe.eyeBoxTop = bestTop;
    }

    VectorCopy(ps->camera_angles, viewangles);
    viewangles[PITCH] -= FE_CAM_PITCH * bestFrac;
    viewangles[ROLL] = 0;
    *viewheight      = bestVh;
    *lean            = bestLn;
    return qtrue;
}

/*
===============
FE_Airborne

Whether they are off the ground, from their legs' animations (the state machine's global/mike_legs.st: the
newest "smg_jump_takeoff" or "smg_fall" in the air, "smg_land_hard" in either slot landed): the servers never
set a player's ground in what they send (0, the first player, for each)
===============
*/
static qboolean FE_Airborne(const entityState_t *pl)
{
    const frameInfo_t *fi = NULL;
    dtiki_t           *tiki;
    const char        *anim;
    int                i;

    tiki = cgi.R_Model_GetHandle(cgs.model_draw[pl->modelindex]);
    if (!tiki) {
        return qfalse;
    }

    for (i = 0; i < FE_TORSO_SLOT; i++) {
        if (pl->frameInfo[i].weight <= 0) {
            continue;
        }
        // landing (the fall still blending out)
        anim = cgi.Anim_NameForNum(tiki, pl->frameInfo[i].index);
        if (anim && strstr(anim, "_land")) {
            return qfalse;
        }
        if (!fi || pl->frameInfo[i].time < fi->time) {
            fi = &pl->frameInfo[i];
        }
    }
    if (!fi) {
        return qfalse;
    }

    anim = cgi.Anim_NameForNum(tiki, fi->index);
    return anim && Q_stricmpn(anim, "death", 5) && (strstr(anim, "_fall") || strstr(anim, "_takeoff"));
}

/*
===============
FE_Shooter

A player in the snapshot who has just fired facing them (their upper body's fire animation started lately),
not of their team; the way from them to the player, the one facing them most squarely
===============
*/
static qboolean FE_Shooter(const snapshot_t *snap, const entityState_t *pl, const vec3_t center, vec3_t dir)
{
    float best = cos(DEG2RAD(30));
    int   i, j;

    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];
        const frameInfo_t   *fi = NULL;
        dtiki_t             *tiki;
        const char          *anim;
        vec3_t               to, facing;
        float                len, facingCos;

        if (es->eType != ET_PLAYER || es->number == pl->number || es->number >= MAX_CLIENTS || (es->eFlags & EF_DEAD)
            || (es->eFlags & pl->eFlags & EF_ANY_TEAM)) {
            continue;
        }

        for (j = FE_TORSO_SLOT; j < FE_TORSO_SLOT + 2; j++) {
            if (es->frameInfo[j].weight > 0 && (!fi || es->frameInfo[j].time < fi->time)) {
                fi = &es->frameInfo[j];
            }
        }
        tiki = cgi.R_Model_GetHandle(cgs.model_draw[es->modelindex]);
        if (!fi || !tiki || fi->time > 0.3f) {
            continue;
        }
        anim = cgi.Anim_NameForNum(tiki, fi->index);
        if (!anim || !strstr(anim, "_fire")) {
            continue;
        }

        // their aim from their body's facing (level)
        VectorSubtract(center, es->origin, to);
        to[2] = 0;
        len = VectorNormalize(to);
        if (len < 1) {
            continue;
        }
        YawToAxis(es->angles[YAW], facing);
        facingCos = to[0] * facing[0] + to[1] * facing[1];
        if (facingCos > best) {
            best = facingCos;
            // from their eye
            VectorSubtract(center, es->origin, dir);
            dir[2] -= DEFAULT_VIEWHEIGHT;
            VectorNormalize(dir);
        }
    }

    return best > cos(DEG2RAD(30));
}

/*
===============
FE_HitDirection

Which way the hit that just took their health came: a bullet the server sent lately that was going through
them, else an explosion near them, else a fall as they land (none), else a player who has just fired facing
them, else from in front of them (qfalse: not known).
(A stock server sends the bullets and explosions to a follower only when the follower's own place, kept below
the follow camera, can see them: not at all while that place is in a wall.)
===============
*/
static qboolean FE_HitDirection(const snapshot_t *snap, const entityState_t *pl, const vec3_t forward, vec3_t dir)
{
    vec3_t       mins, maxs, seg, p, center;
    float        best, bestBlast, top, bottom, len, t, dist;
    qboolean     found;
    int          now, i;
    unsigned int n;

    IntegerToBoundingBox(pl->solid, mins, maxs);
    bottom = pl->origin[2] + mins[2] - 16;
    top    = pl->origin[2] + maxs[2] + 16;
    VectorCopy(pl->origin, center);
    center[2] += maxs[2] / 2;

    VectorScale(forward, -1, dir);
    best      = 40; // from the line up through them
    bestBlast = 512;
    found     = qfalse;
    now       = cgi.Milliseconds();

    for (i = 0, n = fe_numShots; i < FE_MAX_SHOTS && n > 0; i++, n--) {
        const feShot_t *shot = &fe_shots[(n - 1) % FE_MAX_SHOTS];

        if (now - shot->time > FE_SHOT_TIME) {
            break;
        }

        if (shot->blast) {
            VectorSubtract(center, shot->start, seg);
            dist = VectorLength(seg);
            if (best == 40 && dist < bestBlast && dist > 1) {
                bestBlast = dist;
                found     = qtrue;
                VectorScale(seg, 1 / dist, dir);
            }
            continue;
        }

        // where it passed nearest the line up through them: past its start (their own go away from them) and
        // not far past its end (one that hit them ends on them)
        VectorSubtract(shot->end, shot->start, seg);
        len = seg[0] * seg[0] + seg[1] * seg[1];
        if (len < 1) {
            continue;
        }
        t = ((pl->origin[0] - shot->start[0]) * seg[0] + (pl->origin[1] - shot->start[1]) * seg[1]) / len;
        if (t <= 0 || (t - 1) * sqrt(len) > 32) {
            continue;
        }
        VectorMA(shot->start, t, seg, p);
        if (p[2] < bottom || p[2] > top) {
            continue;
        }

        dist = sqrt((p[0] - pl->origin[0]) * (p[0] - pl->origin[0]) + (p[1] - pl->origin[1]) * (p[1] - pl->origin[1]));
        if (dist < best) {
            best  = dist;
            found = qtrue;
            VectorNormalize2(seg, dir);
        }
    }

    if (found) {
        return qtrue;
    }

    // landing: a fall's (Player::ProcessPmoveEvents), which has no direction: no kick, and the arrow a world
    // yaw of 0's, as the player's own
    if (!fe.airborne && fe.airTime && snap->serverTime - fe.airTime <= 150) {
        VectorClear(dir);
        return qtrue;
    }

    return FE_Shooter(snap, pl, center, dir);
}

/*
===============
FE_AddBlend

A color over the screen's (Player::AddBlend)
===============
*/
static void FE_AddBlend(float *blend, float r, float g, float b, float a)
{
    float a2, a3;

    if (a <= 0) {
        return;
    }

    a2       = blend[3] + (1 - blend[3]) * a;
    a3       = blend[3] / a2;
    blend[0] = blend[0] * a3 + r * (1 - a3);
    blend[1] = blend[1] * a3 + g * (1 - a3);
    blend[2] = blend[2] * a3 + b * (1 - a3);
    blend[3] = a2;
}

/*
===============
FE_SetDamageDir

The direction of a hit for the HUD's arrows, as the server sends it (STAT_DAMAGEDIR: its yaw x 10, moved a
tenth when the same as the last so the hit shows)
===============
*/
static void FE_SetDamageDir(const vec3_t dir)
{
    int yaw = (int)(vectoyaw(dir) * 10);

    if (yaw == fe.damageDir) {
        yaw += yaw < 1800 ? 1 : -1;
    }
    fe.damageDir = Com_Clamp(0, 3600, yaw);
}

/*
===============
FE_HitFeedback

What their hits do to their view, as the server's Player::DamageFeedback and CalcBlend make it for them, once a
server frame: the kick and the damage it's for decay to 0.8, and a hit kicks the view against the way it came,
more the harder it was; the red flash goes up with a hit (0.001 a point, 10 points the least, between 0.2 and
0.6) and down 0.06; the damage's direction for the HUD's arrows, kept as it was for a hit from where isn't
known
===============
*/
static void FE_HitFeedback(playerState_t *ps, const snapshot_t *snap, const entityState_t *pl, int lastServerTime)
{
    vec3_t axis[3], dir;
    int    frames, health, damage;

    health = ps->stats[STAT_INFOCLIENT_HEALTH];
    frames = lastServerTime ? (snap->serverTime - lastServerTime + FE_SERVER_FRAME / 2) / FE_SERVER_FRAME : 1;
    frames = Com_Clamp(1, 40, frames);

    // the server frames before this snapshot's
    for (; frames > 1; frames--) {
        fe.flash = Q_max(0, fe.flash - 0.06f);
        if (fe.kickCount) {
            fe.kickCount *= 0.8f;
            VectorScale(fe.kick, 0.8f, fe.kick);
            if (fe.kickCount < 0.1f) {
                fe.kickCount = 0;
            }
        }
    }

    // this snapshot's: the kick decays before a hit adds to it, the flash after
    if (fe.kickCount && lastServerTime) {
        fe.kickCount *= 0.8f;
        VectorScale(fe.kick, 0.8f, fe.kick);
        if (fe.kickCount < 0.1f) {
            fe.kickCount = 0;
        }
    }

    // their health is sent as a share of 100 (STAT_INFOCLIENT_HEALTH)
    damage    = fe.health > 0 && health > 0 ? fe.health - health : 0;
    fe.health = health;

    if (damage > 0) {
        AnglesToAxis(pl->angles, axis);
        if (FE_HitDirection(snap, pl, axis[0], dir)) {
            FE_SetDamageDir(dir);
        }

        fe.kick[PITCH] = Com_Clamp(-30, 30, fe.kick[PITCH] - DotProduct(dir, axis[0]) * damage * FE_KICK_PITCH);
        fe.kick[YAW]   = Com_Clamp(-30, 30, fe.kick[YAW] - DotProduct(dir, axis[1]) * damage * FE_KICK_YAW);
        fe.kick[ROLL]  = Com_Clamp(-25, 25, fe.kick[ROLL] + DotProduct(dir, axis[2]) * damage * FE_KICK_ROLL);
        fe.kickCount += damage;

        fe.flash = Com_Clamp(0.2f, 0.6f, Q_max(0, fe.flash) + Q_max(damage, 10) * 0.001f);
    }
    fe.flash = Q_max(0, fe.flash - 0.06f);

    if (fe.kickCount) {
        VectorCopy(fe.kick, ps->damage_angles);
    } else {
        VectorClear(fe.kick);
        VectorClear(ps->damage_angles);
    }

    FE_AddBlend(ps->blend, 1, 0, 0, fe.flash);
    ps->stats[STAT_DAMAGEDIR] = fe.damageDir;
}

/*
===============
FE_OnTurret

A followed player on a turret: the flag servers set from Spearhead's code on (EF_PLAYER_ARTILLERY), or for
Allied Assault's the upper body's newest animation, the turret's (global/mike_torso.st, TURRET_IDLE: it starts
as they take the turret, and the next as they leave it)
===============
*/
static qboolean FE_OnTurret(const entityState_t *pl)
{
    const frameInfo_t *fi = NULL;
    const char        *name;
    int                i;

    if (pl->eFlags & EF_PLAYER_ARTILLERY) {
        return qtrue;
    }

    for (i = FE_TORSO_SLOT; i < FE_TORSO_SLOT + 2; i++) {
        if (pl->frameInfo[i].weight > 0 && (!fi || pl->frameInfo[i].time < fi->time)) {
            fi = &pl->frameInfo[i];
        }
    }
    name = fi ? cgi.Anim_NameForNum(cgi.R_Model_GetHandle(cgs.model_draw[pl->modelindex]), fi->index) : NULL;

    return name && !Q_stricmpn(name, "turret_", 7);
}

/*
===============
FE_IsTurret

A turret a player takes: TurretGun, or Spearhead's portable MG42 set up (PortableTurret)
===============
*/
static qboolean FE_IsTurret(dtiki_t *tiki)
{
    return FE_IsClass(tiki, "TurretGun") || FE_IsClass(tiki, "PortableTurret");
}

/*
===============
FE_KeepTurrets

The turrets in a snapshot, kept as sent: the servers stop sending one to someone while a player is on it. They hide
the turret from its user (TurretGun::P_CreateViewModel), but with the user's number ORed into the one they hid it
from before, never cleared: from the second user on, the turret is hidden from whoever has that number, perhaps a
follower, and not from its user
===============
*/
static void FE_KeepTurrets(const snapshot_t *snap)
{
    int i, j;

    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];

        if (!es->modelindex || es->number == FE_TURRET_VIEWMODEL
            || !FE_IsTurret(cgi.R_Model_GetHandle(cgs.model_draw[es->modelindex]))) {
            continue;
        }

        for (j = 0; j < fe_numTurrets && fe_turrets[j].number != es->number; j++) {}
        if (j == FE_MAX_TURRETS) {
            // more than a map has: the first kept goes
            memmove(&fe_turrets[0], &fe_turrets[1], (FE_MAX_TURRETS - 1) * sizeof(entityState_t));
            j = FE_MAX_TURRETS - 1;
        } else if (j == fe_numTurrets) {
            fe_numTurrets++;
        }
        fe_turrets[j] = *es;
    }
}

/*
===============
FE_TurretFire

The animation of a turret not sent, from the bullets the server sent from it since the last snapshot read: a
"fire" for them in the next slot, as the server starts each shot (TurretGun::P_SetWeaponAnim), then idle
===============
*/
static void FE_TurretFire(entityState_t *turret, dtiki_t *tiki, int serverTime)
{
    const int fire = cgi.Anim_NumForName(tiki, "fire");
    const int idle = cgi.Anim_NumForName(tiki, "idle");
    qboolean  shot = qfalse;
    int       elapsed;

    if (fe_numShots - fe.turretShots > FE_MAX_SHOTS) {
        fe.turretShots = fe_numShots - FE_MAX_SHOTS;
    }
    for (; fe.turretShots != fe_numShots; fe.turretShots++) {
        const feShot_t *s = &fe_shots[fe.turretShots % FE_MAX_SHOTS];

        if (!s->blast && DistanceSquared(s->start, turret->origin) < Square(FE_TURRET_MUZZLE)) {
            shot = qtrue;
        }
    }
    if (shot && fire >= 0) {
        fe.turretFireSlot = (fe.turretFireSlot + 1) % MAX_FRAMEINFOS;
        fe.turretFireTime = serverTime;
    }

    memset(turret->frameInfo, 0, sizeof(turret->frameInfo));
    elapsed = fe.turretFireTime ? serverTime - fe.turretFireTime : FE_TURRET_FIRE;
    if (elapsed >= FE_TURRET_FIRE) {
        turret->frameInfo[fe.turretFireSlot].index = idle;
        elapsed -= FE_TURRET_FIRE;
    } else {
        turret->frameInfo[fe.turretFireSlot].index = fire;
    }
    // the time as the snapshots show it: one server frame on (see FE_FrameVMAnim)
    turret->frameInfo[fe.turretFireSlot].time   = (elapsed + FE_SERVER_FRAME) / 1000.0f;
    turret->frameInfo[fe.turretFireSlot].weight = 1;
}

/*
===============
FE_Turret

A followed player on a turret (see FE_OnTurret): the server shows them the turret's view model on their eyes
instead of the turret (TurretGun::P_CreateViewModel: "<its model>_viewmodel.tik", playing the turret's
animation, sent to them only, the turret left out of their snapshots), with the turret their item and a
crosshair (Player::UpdateStats), and their view the turret's camera. The same made here from the turret in the
snapshot, or when the server didn't send it (see FE_KeepTurrets) the one kept, its shots from the bullets: its
view model taken from the models the server registered for it; with none (not registered) the turret stays as
it is.
===============
*/
static void FE_Turret(snapshot_t *snap, playerState_t *ps, int follow, int playerModel, const vec3_t eye)
{
    const entityState_t *turret = NULL;
    const frameInfo_t   *fi     = NULL;
    entityState_t        vm, kept;
    dtiki_t             *tiki, *vmTiki, *ptiki;
    char                 name[MAX_QPATH];
    const char          *arg, *anim;
    float                best = Square(96);
    vec3_t               ofs;
    qboolean             shot, sent = qtrue;
    int                  i, item, vmIndex = 0, slot, fiSlot = -1, idle;

    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];

        if (!es->modelindex || es->number == FE_TURRET_VIEWMODEL) {
            continue;
        }
        tiki = cgi.R_Model_GetHandle(cgs.model_draw[es->modelindex]);
        if (FE_IsTurret(tiki) && DistanceSquared(es->origin, eye) < best) {
            best   = DistanceSquared(es->origin, eye);
            turret = es;
        }
    }
    if (!turret) {
        for (i = 0; i < fe_numTurrets; i++) {
            if (DistanceSquared(fe_turrets[i].origin, eye) < best) {
                best   = DistanceSquared(fe_turrets[i].origin, eye);
                turret = &fe_turrets[i];
            }
        }
        if (!turret) {
            fe.turretShots = fe_numShots;
            return;
        }
        kept = *turret;
        FE_TurretFire(&kept, cgi.R_Model_GetHandle(cgs.model_draw[kept.modelindex]), snap->serverTime);
        turret = &kept;
        sent   = qfalse;
    }
    fe.turretShots = fe_numShots;

    tiki = cgi.R_Model_GetHandle(cgs.model_draw[turret->modelindex]);
    item = FE_ItemIndex(FE_ServerInitArg(tiki, "name"));
    if (item > 0) {
        ps->activeItems[1] = item;
    }
    ps->stats[STAT_EQUIPPED_WEAPON] = 0;
    ps->stats[STAT_CROSSHAIR]       = 1;

    // the turret's animation, its heaviest: each shot starts "fire" again, in the next slot
    // (Weapon::SetWeaponAnim)
    for (slot = 0; slot < MAX_FRAMEINFOS; slot++) {
        if (turret->frameInfo[slot].weight > 0 && (!fi || turret->frameInfo[slot].weight > fi->weight)) {
            fi     = &turret->frameInfo[slot];
            fiSlot = slot;
        }
    }
    anim = fi ? cgi.Anim_NameForNum(tiki, fi->index) : NULL;
    shot = FE_AnimChanged(&fe.turretAnim, fi ? fi->index : -1, fi ? fi->time : 0) || fiSlot != fe.turretSlot;
    shot = shot && anim && !Q_stricmp(anim, "fire");
    fe.turretSlot = fiSlot;

    // their view: the turret's camera (TurretGun::P_ThinkActive, with g_turretcameras 1, the default) at the
    // turret, its view offset turned with their aim, shaken for a moment by each shot (P_ApplyFiringViewJitter),
    // as wide as the server zoomed them (ToggleZoom(80))
    arg = FE_ServerInitArg(tiki, "viewOffset");
    if (arg && sscanf(arg, "%f %f %f", &ofs[0], &ofs[1], &ofs[2]) == 3) {
        if (shot) {
            arg             = FE_ServerInitArg(tiki, "viewjitter");
            fe.turretJitter = arg ? atof(arg) : 0;
        }
        ps->pm_flags |= PMF_CAMERA_VIEW | PMF_TURRET;
        ps->camera_flags |= CF_CAMERA_ANGLES_TURRETMODE;
        VectorCopy(turret->origin, ps->camera_origin);
        ps->camera_angles[PITCH] = ps->viewangles[PITCH] + crandom() * fe.turretJitter;
        ps->camera_angles[YAW]   = ps->viewangles[YAW] + crandom() * fe.turretJitter;
        ps->camera_angles[ROLL]  = ps->viewangles[ROLL] + crandom() * fe.turretJitter * 0.8f;
        VectorCopy(ofs, ps->camera_posofs);
        ps->stats[STAT_INZOOM] = ps->fov;
        fe.turretJitter        = Q_max(0, fe.turretJitter - 6 * FE_SERVER_FRAME / 1000.0f);
    }

    COM_StripExtension(CG_ConfigString(CS_MODELS + turret->modelindex), name, sizeof(name));
    Q_strcat(name, sizeof(name), "_viewmodel.tik");
    for (i = 1; i < MAX_MODELS && CS_MODELS + i < CS_SOUNDS; i++) {
        if (!Q_stricmp(CG_ConfigString(CS_MODELS + i), name)) {
            vmIndex = i;
            break;
        }
    }
    vmTiki = vmIndex ? cgi.R_Model_GetHandle(cgs.model_draw[vmIndex]) : NULL;
    ptiki  = cgi.R_Model_GetHandle(cgs.model_draw[playerModel]);
    if (!vmTiki || !ptiki || snap->numEntities >= MAX_ENTITIES_IN_SNAPSHOT) {
        return;
    }
    for (i = 0; i < snap->numEntities; i++) {
        if (snap->entities[i].number == FE_TURRET_VIEWMODEL) {
            return;
        }
    }

    // a new entity's state as the server makes it (Level::AllocEdict), the view model's (P_CreateViewModel)
    memset(&vm, 0, sizeof(vm));
    vm.number            = FE_TURRET_VIEWMODEL;
    vm.eType             = ET_MODELANIM;
    vm.modelindex        = vmIndex;
    vm.parent            = follow;
    vm.tag_num           = cgi.Tag_NumForName(ptiki, "eyes bone");
    vm.attach_use_angles = qtrue;
    vm.beam_entnum       = ENTITYNUM_NONE;
    vm.groundEntityNum   = ENTITYNUM_NONE;
    vm.scale             = turret->scale;
    vm.alpha             = 1;
    vm.constantLight     = 0xffffff;
    vm.renderfx          = RF_FRAMELERP | RF_ALWAYSDRAW;
    vm.usageIndex        = turret->usageIndex;
    vm.actionWeight      = 1;
    vm.quat[3]           = 1;
    for (i = 0; i < NUM_BONE_CONTROLLERS; i++) {
        vm.bone_tag[i]     = -1;
        vm.bone_quat[i][3] = 1;
    }
    // its animations: the turret's, slot by slot (TurretGun::P_SetWeaponAnim starts each on both; a new slot
    // each shot is what plays its sound and flash again)
    idle = cgi.Anim_NumForName(vmTiki, "idle");
    for (slot = 0; slot < MAX_FRAMEINFOS; slot++) {
        const char *slotAnim = cgi.Anim_NameForNum(tiki, turret->frameInfo[slot].index);
        const int   index    = slotAnim ? cgi.Anim_NumForName(vmTiki, slotAnim) : -1;

        vm.frameInfo[slot]       = turret->frameInfo[slot];
        vm.frameInfo[slot].index = index >= 0 ? index : idle;
    }
    if (vm.tag_num < 0 || idle < 0) {
        return;
    }

    // the turret out of the snapshot (as from theirs), the view model at its end (the numbers go up)
    if (sent) {
        i = turret - snap->entities;
        memmove(&snap->entities[i], &snap->entities[i + 1], (snap->numEntities - i - 1) * sizeof(entityState_t));
        snap->numEntities--;
    }
    snap->entities[snap->numEntities++] = vm;
}

/*
===============
FE_Salt

When the entity drawn as the first-person model changes, the old one goes back to its world model and
the new one takes the first-person model, each keeping the other model's animations; a new usageIndex
makes the cgame start both afresh (sv_snapshot.c does the same for g_spectatefollow_firstperson)
===============
*/
static void FE_Salt(snapshot_t *snap, int viewEnt)
{
    int i;

    if (viewEnt != fe.viewEnt) {
        if (fe.viewEnt >= 0 && fe.viewEnt < MAX_CLIENTS) {
            fe.salt[fe.viewEnt] = fe.salt[fe.viewEnt] % 65534 + 1;
        }
        if (viewEnt >= 0 && viewEnt < MAX_CLIENTS) {
            fe.salt[viewEnt] = fe.salt[viewEnt] % 65534 + 1;
        }
        fe.viewEnt = viewEnt;
    }

    for (i = 0; i < snap->numEntities; i++) {
        entityState_t *es = &snap->entities[i];

        if (es->number < MAX_CLIENTS && fe.salt[es->number] && es->usageIndex > 0) {
            es->usageIndex = (es->usageIndex - 1 + fe.salt[es->number]) % 65535 + 1;
        }
    }
}

/*
===============
CG_FollowEyes_Snapshot

Called for each snapshot read from the client, before the cgame uses it
===============
*/
void CG_FollowEyes_Snapshot(snapshot_t *snap)
{
    playerState_t       *ps = &snap->ps;
    const entityState_t *pl = NULL, *weapon;
    dtiki_t             *tiki;
    vec3_t               viewangles;
    float                viewheight, lean, dt;
    int                  follow, i, item, vmAnim, lastServerTime;
    qboolean             dead, ok;

    // A server of this code shows a follower the player's own view itself (g_spectatefollow_firstperson 1:
    // the snapshot under their number), with their ammo: one seen doing it is asked for the view set here
    // (its spectatemode), and does it from then on
    if (!cg.demoPlayback && (ps->pm_flags & PMF_SPECTATING)) {
        const int want = cg_spectatefirstperson->integer ? 1 : 0;

        if (ps->clientNum != cg.clientNum) {
            fe.serverMode = qtrue;
        }
        if (fe.serverMode && want != fe.modeSent) {
            cgi.SendClientCommand(va("spectatemode %i", want));
            fe.modeSent = want;
        }
    }

    if (cg_spectatefirstperson->integer) {
        FE_KeepTurrets(snap);
    }

    follow = -1;
    if (cg_spectatefirstperson->integer && !fe.serverMode && (ps->pm_flags & PMF_SPECTATING)
        && (ps->pm_flags & PMF_CAMERA_VIEW) && !(ps->pm_flags & PMF_INTERMISSION) && ps->clientNum == cg.clientNum) {
        follow = ps->stats[STAT_INFOCLIENT];
    }

    if (follow >= 0 && follow < MAX_CLIENTS && follow != cg.clientNum) {
        for (i = 0; i < snap->numEntities; i++) {
            if (snap->entities[i].number == follow && snap->entities[i].eType == ET_PLAYER) {
                pl = &snap->entities[i];
                break;
            }
        }
    }

    if (follow != fe.follow) {
        // nothing of the last one's box or eye height
        fe.boxTop    = 0;
        fe.eyeHeight = 0;
    }

    // dying: their death through their eyes (the cgame's own, for a health of 0), the camera no longer one behind them
    dead = pl && (pl->eFlags & EF_DEAD) && follow == fe.follow;
    if (dead) {
        VectorCopy(ps->camera_angles, viewangles);
        viewangles[ROLL] = 0;
        viewheight       = fe.eyeHeight > 0 ? fe.eyeHeight : DEFAULT_VIEWHEIGHT;
        lean             = 0;
    }

    ok = dead;
    if (pl && !dead) {
        ok = FE_Camera(ps, pl, viewangles, &viewheight, &lean);
        if (ok) {
            fe.keptFrames = 0;
        } else if (follow == fe.follow && fe.keptFrames < 10) {
            // a frame the camera can't be worked back while following them (their box a frame off it): as they
            // were, rather than a frame of the view from behind
            VectorCopy(ps->camera_angles, viewangles);
            viewangles[ROLL] = 0;
            viewheight       = fe.eyeHeight > 0 ? fe.eyeHeight : DEFAULT_VIEWHEIGHT;
            lean             = fe.lean;
            fe.keptFrames++;
            ok = qtrue;
        }
    }

    if (!pl || !ok) {
        fe.follow    = -1;
        fe.lean      = 0;
        fe.eyeHeight = 0;
        fe.health    = -1;
        fe.kickCount = 0;
        fe.flash     = 0;
        VectorClear(fe.kick);
        fe.turretShots = fe_numShots;
        FE_Salt(snap, -1);
        return;
    }

    lastServerTime = fe.serverTime;
    if (follow != fe.follow) {
        // someone new: no speed or kick from the last one, and their animations as they are now
        lastServerTime      = 0;
        fe.serverTime       = 0;
        fe.weaponEnt        = -1;
        fe.torsoAnim.index  = -1;
        fe.torsoWeight      = 0;
        fe.weaponAnim.index = -1;
        fe.vmAnim           = VM_ANIM_IDLE;
        fe.health           = -1;
        fe.kickCount        = 0;
        fe.flash            = 0;
        fe.damageDir        = ps->stats[STAT_DAMAGEDIR];
        fe.airTime          = 0;
        fe.turretAnim.index = -1;
        fe.turretSlot       = -1;
        fe.turretJitter     = 0;
        fe.turretFireTime   = 0;
        VectorClear(fe.kick);
        // their eye height at once (the first-person view eases changes of it in)
        cg.fCurrentViewHeight = 0;
    }

    //
    // where they are and look
    //
    ps->clientNum = follow;
    ps->pm_flags &= ~(PMF_CAMERA_VIEW | PMF_DUCKED);
    ps->pm_flags |= PMF_NO_PREDICTION;
    if (viewheight <= CROUCH_VIEWHEIGHT) {
        ps->pm_flags |= PMF_DUCKED;
    }

    VectorCopy(pl->origin, ps->origin);
    // in the range the player's own are (PM_UpdateViewAngles: the pitch between -90 and 90): the camera's come in
    // 0 to 360, and the first-person view tilts the eye and the gun by a share of the pitch (CG_OffsetFirstPersonView),
    // looking up as 355 put the eye on the floor and the gun upside down
    ps->viewangles[PITCH] = AngleNormalize180(viewangles[PITCH]);
    ps->viewangles[YAW]   = AngleNormalize180(viewangles[YAW]);
    ps->viewangles[ROLL]  = 0;
    ps->viewheight        = (int)(viewheight + 0.5f);
    ps->fLeanAngle = lean;

    // on the ground (the cgame's walking bob and weapon pose, the landing sounds) or not
    fe.airborne         = FE_Airborne(pl);
    ps->groundEntityNum = fe.airborne ? ENTITYNUM_NONE : ENTITYNUM_WORLD;
    ps->walking         = !fe.airborne;
    VectorClear(ps->damage_angles);

    // their speed: the server's when it sends it (g_smoothClients), else from their last origin
    dt = (snap->serverTime - fe.serverTime) / 1000.0f;
    if (pl->pos.trDelta[0] || pl->pos.trDelta[1] || pl->pos.trDelta[2]) {
        VectorCopy(pl->pos.trDelta, ps->velocity);
    } else if (fe.serverTime && dt > 0 && dt < 0.25f) {
        VectorSubtract(pl->origin, fe.origin, ps->velocity);
        VectorScale(ps->velocity, 1.0f / dt, ps->velocity);
    } else {
        VectorClear(ps->velocity);
    }

    fe.follow     = follow;
    fe.serverTime = snap->serverTime;
    fe.lean       = lean;
    VectorCopy(pl->origin, fe.origin);

    //
    // their health, and what their hits do to their view
    //
    ps->stats[STAT_MAXHEALTH] = 100;
    if (dead) {
        vec3_t axis[3], dir;

        // the hit that killed them shows where it came from (no kick nor flash once dead)
        AnglesToAxis(pl->angles, axis);
        if (fe.health > 0 && FE_HitDirection(snap, pl, axis[0], dir)) {
            FE_SetDamageDir(dir);
        }
        ps->stats[STAT_HEALTH]    = 0;
        ps->stats[STAT_DAMAGEDIR] = fe.damageDir;
        fe.health                 = -1;
        fe.kickCount              = 0;
        fe.flash                  = 0;
        VectorClear(fe.kick);
    } else {
        ps->stats[STAT_HEALTH] = ps->stats[STAT_INFOCLIENT_HEALTH] > 0 ? ps->stats[STAT_INFOCLIENT_HEALTH] : 1;
        FE_HitFeedback(ps, snap, pl, lastServerTime);
    }
    if (fe.airborne) {
        fe.airTime = snap->serverTime;
    }

    //
    // what they hold
    //
    ps->activeItems[1]              = -1;
    ps->stats[STAT_EQUIPPED_WEAPON] = 0;
    ps->stats[STAT_CROSSHAIR]       = 0;
    ps->stats[STAT_AMMO]            = 0;
    ps->stats[STAT_CLIPAMMO]        = 0;
    ps->stats[STAT_INZOOM]          = ps->fov > 0 && ps->fov < 60 ? (int)ps->fov : 0;

    tiki   = NULL;
    weapon = FE_FindWeapon(snap, follow, &tiki);
    vmAnim = -1;
    if (weapon) {
        const frameInfo_t *fi = NULL;

        item = FE_ItemIndex(FE_ServerInitArg(tiki, "name"));
        if (item > 0) {
            ps->activeItems[1] = item;
        }
        if (FE_ServerInitArg(tiki, "weapontype")) {
            ps->stats[STAT_EQUIPPED_WEAPON] = FE_WeaponClass(FE_ServerInitArg(tiki, "weapontype"));
        } else {
            // an item (the packed MG42): its class is its place among their items (Player::UpdateStats, ITEM1 on),
            // not sent; any item's gives the same first-person animations (CG_GetVMAnimPrefixIndex)
            ps->stats[STAT_EQUIPPED_WEAPON] = WEAPON_CLASS_ITEM1;
        }
        ps->stats[STAT_CROSSHAIR] = FE_Crosshair(tiki, ps);

        // the gun's own animation, its heaviest
        for (i = 0; i < MAX_FRAMEINFOS; i++) {
            if (weapon->frameInfo[i].weight > 0 && (!fi || weapon->frameInfo[i].weight > fi->weight)) {
                fi = &weapon->frameInfo[i];
            }
        }

        if (weapon->number != fe.weaponEnt) {
            fe.weaponEnt        = weapon->number;
            fe.weaponAnim.index = -1;
        }

        if (fi && FE_AnimChanged(&fe.weaponAnim, fi->index, fi->time) && !fe.torsoKnown) {
            vmAnim = FE_WeaponVMAnim(cgi.Anim_NameForNum(tiki, fi->index));
        }
    } else {
        fe.weaponEnt = -1;
    }

    // the upper body's, the newest of its two slots: a first-person animation starts with it, and with none
    // (or one fading out: the state machine is back in a state without one) the first-person one is idle
    {
        dtiki_t           *ptiki  = cgi.R_Model_GetHandle(cgs.model_draw[pl->modelindex]);
        const frameInfo_t *fi     = NULL;
        qboolean           active = qfalse;

        for (i = FE_TORSO_SLOT; i < FE_TORSO_SLOT + 2; i++) {
            if (pl->frameInfo[i].weight > 0 && (!fi || pl->frameInfo[i].time < fi->time)) {
                fi = &pl->frameInfo[i];
            }
        }

        if (ptiki && fi) {
            const int   lastIndex = fe.torsoAnim.index;
            const float lastTime  = fe.torsoAnim.time;
            int         frame;

            if (FE_AnimChanged(&fe.torsoAnim, fi->index, fi->time)) {
                int torso = FE_TorsoVMAnim(cgi.Anim_NameForNum(ptiki, fi->index));

                // the frame commands the last one had left as it ended, and the new one's since it started
                frame = FE_FrameVMAnim(ptiki, fi->index, -1, fi->time);
                if (frame == -1 && lastIndex >= 0) {
                    frame = FE_FrameVMAnim(ptiki, lastIndex, lastTime, 1e9f);
                }

                if (torso != -1) {
                    vmAnim        = torso;
                    fe.torsoKnown = qtrue;
                } else if (frame != -1) {
                    vmAnim = frame;
                }
                fe.torsoWeight = 0;
            } else if ((frame = FE_FrameVMAnim(ptiki, fi->index, lastTime, fi->time)) != -1) {
                vmAnim = frame;
            }

            active         = fi->weight + 0.01f >= fe.torsoWeight;
            fe.torsoWeight = fi->weight;
        } else {
            fe.torsoAnim.index = -1;
            fe.torsoWeight     = 0;
        }

        if (!active && fe.torsoKnown && vmAnim == -1 && fe.vmAnim != VM_ANIM_IDLE) {
            vmAnim = VM_ANIM_IDLE;
        }
    }

    if (vmAnim != -1) {
        fe.vmAnim        = vmAnim;
        fe.vmAnimChanged = (fe.vmAnimChanged + 1) & 3;
    }
    ps->iViewModelAnim        = fe.vmAnim;
    ps->iViewModelAnimChanged = fe.vmAnimChanged;

    // on a turret (last: it changes the snapshot's entities, pl among them)
    if (FE_OnTurret(pl)) {
        vec3_t eye;

        VectorCopy(pl->origin, eye);
        eye[2] += ps->viewheight;
        FE_Turret(snap, ps, follow, pl->modelindex, eye);
    } else {
        fe.turretAnim.index = -1;
        fe.turretSlot       = -1;
        fe.turretJitter     = 0;
        fe.turretFireTime   = 0;
        fe.turretShots      = fe_numShots;
    }

    FE_Salt(snap, follow);
}
