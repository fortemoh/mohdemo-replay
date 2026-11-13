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

// Added in OPM
//  cl_demohud.cpp: a demo has the health and ammo of its recorder only, and
//  the health of the player they follow. For the HUD of the others, seen
//  from their eyes, they're worked out from all the demo has of each player
//  from its start, as it's read (a seek reads it all too):
//  - a life starts with 100 health and the ammo a deathmatch server gives
//    (as the recorder got it, if they had that gun), after they die (their
//    entity, or the kill in the index) or a round starts, the first time
//    they're seen alive;
//  - a hit takes what the hits from their eyes take (the client's, from the
//    pain and blood: CL_DemoFollowedHits); when the recorder follows them,
//    or looks at them (a team mate), the demo has it (STAT_INFOCLIENT_HEALTH);
//  - the gun in their hand takes a round for each shot it fires (its fire
//    animation: the server plays fire_empty for the one that empties it),
//    and a reload fills its clip from the rest of that kind of ammo, as their
//    torso's reload animation gets to the frame that does it on a server
//    (weaponcommand mainhand clip_fill, or clip_add for a round at a time);
//  - an item gone from beside them is theirs if they can take it, as a
//    server gives it: a health box (dm_50_healthbox, dropped by the dead) up
//    to 100, a gun dropped by the dead its two clips (g_dropclips) up to the
//    most of its ammo.
//  What the demo doesn't show of them (out of the recorder's sight) is
//  missed: a hit, shot, reload or pickup then, so it's a guess, though one
//  that goes as what's seen of them does.

#include "client.h"
#include "cl_demohud.h"
#include "../corepp/tiki.h"

#include <ctype.h>

typedef enum {
    AMMO_PISTOL,
    AMMO_RIFLE,
    AMMO_SMG,
    AMMO_MG,
    AMMO_HEAVY,
    AMMO_SHOTGUN,
    AMMO_GRENADE,
    NUM_DEMO_HUD_AMMO
} demoHudAmmo_t;

// the most of each, as a stock server has it, when the recorder doesn't tell
static const int demoHudMostAmmo[NUM_DEMO_HUD_AMMO] = {200, 200, 300, 500, 10, 50, 10};

typedef struct {
    const char   *model;
    const char   *name; // in CS_WEAPONS, as the HUD's hud_ammo_<name> is
    demoHudAmmo_t ammoType;
    int           clipSize;
    int           startAmmo; // a deathmatch player is given, the clip with it (dmstartammo)
} demoHudWeapon_t;

static const demoHudWeapon_t demoHudWeapons[] = {
    {"models/weapons/colt45.tik",           "Colt 45",                AMMO_PISTOL,  7,  100},
    {"models/weapons/p38.tik",              "Walther P38",            AMMO_PISTOL,  8,  100},
    {"models/weapons/silencedpistol.tik",   "Hi-Standard Silenced",   AMMO_PISTOL,  8,  100},
    {"models/weapons/m1_garand.tik",        "M1 Garand",              AMMO_RIFLE,   8,  200},
    {"models/weapons/kar98.tik",            "Mauser KAR 98K",         AMMO_RIFLE,   5,  200},
    {"models/weapons/kar98sniper.tik",      "KAR98 - Sniper",         AMMO_RIFLE,   5,  50 },
    {"models/weapons/springfield.tik",      "Springfield '03 Sniper", AMMO_RIFLE,   5,  50 },
    {"models/weapons/thompsonsmg.tik",      "Thompson",               AMMO_SMG,     30, 200},
    {"models/weapons/mp40.tik",             "MP40",                   AMMO_SMG,     32, 200},
    {"models/weapons/bar.tik",              "BAR",                    AMMO_MG,      20, 200},
    {"models/weapons/mp44.tik",             "StG 44",                 AMMO_MG,      30, 200},
    {"models/weapons/bazooka.tik",          "Bazooka",                AMMO_HEAVY,   1,  6  },
    {"models/weapons/panzerschreck.tik",    "Panzerschreck",          AMMO_HEAVY,   1,  6  },
    {"models/weapons/shotgun.tik",          "Shotgun",                AMMO_SHOTGUN, 5,  50 },
    // thrown: one in hand, the next taken out at once; servers give 4 and 3
    // (the most the recorders of 3 demos had)
    {"models/weapons/m2frag_grenade.tik",   "Frag Grenade",           AMMO_GRENADE, 1,  4  },
    {"models/weapons/steilhandgranate.tik", "Stielhandgranate",       AMMO_GRENADE, 1,  3  },
};

#define DEMO_HUD_WEAPONS ((int)ARRAY_LEN(demoHudWeapons))

// clips a gun dropped by the dead has (g_dropclips)
#define DEMO_HUD_DROPPED_CLIPS 2
// how far from where an item was the player who takes it is
#define DEMO_HUD_PICKUP_DIST 96
#define MAX_DEMO_HUD_ITEMS   256
// msec after the recorder's life began that a gun they have is as given
#define DEMO_HUD_GIVEN_TIME 5000

typedef enum {
    GUNANIM_OTHER,
    GUNANIM_FIRE,
    GUNANIM_FIRE_EMPTY,
    GUNANIM_SECONDARY // a bash, or a grenade's short throw
} demoHudGunAnim_t;

typedef struct {
    qboolean alive; // seen alive in this life, with what it began with
    qboolean fresh; // a new life begins when they're next seen alive
    int      health;
    int      weapon;                      // in their hand, -1 for none known
    int      clip[DEMO_HUD_WEAPONS];      // -1: not had in this life yet
    int      ammo[NUM_DEMO_HUD_AMMO];     // besides the clips
    int      gunModel;                    // as last seen in their hand, 0 for none
    int      gunAnim;
    float    gunTime;
    int      bodyAnim[MAX_FRAMEINFOS]; // their animations, as last seen
    float    bodyTime[MAX_FRAMEINFOS];
    int      healthTime; // when the demo last had their health
    qboolean pain;
    vec3_t   painOrigin;
} demoHudPlayer_t;

typedef struct {
    int    number;
    int    modelindex;
    int    weapon; // a gun's, -1 for none
    int    health; // a health box's, 0 for none
    vec3_t origin;
} demoHudItem_t;

static demoHudPlayer_t demoHudPlayers[MAX_CLIENTS];
// what the recorder has, from their playerstate: a server's own amounts
static int      demoHudStartAmmo[DEMO_HUD_WEAPONS];
static int      demoHudMaxAmmo[NUM_DEMO_HUD_AMMO];
static qboolean demoHudRecorderAlive;
static qboolean demoHudRecorderHad[DEMO_HUD_WEAPONS]; // in their hand in this life
static int      demoHudRecorderLife;                   // when it began
// the items of the last snapshot
static demoHudItem_t demoHudItems[MAX_DEMO_HUD_ITEMS];
static int           demoHudNumItems;
// the next event of the index to go by
static int demoHudEvent;
static int demoHudLastTime;

// what a model is, by its configstring: a weapon (-1 for none), the health
// of a health box (0 for none), and its tiki
static struct {
    int      offset; // in the gamestate's strings, 0 when not looked up
    int      weapon;
    int      health;
    dtiki_t *tiki;
} demoHudModels[MAX_MODELS];

/*
====================
CL_DemoHudModelsChanged

A new gamestate: the models are others
====================
*/
void CL_DemoHudModelsChanged(void)
{
    memset(demoHudModels, 0, sizeof(demoHudModels));
    demoHudNumItems = 0;
}

/*
====================
CL_DemoHudClear

From the demo's start, as it begins playing again
====================
*/
void CL_DemoHudClear(void)
{
    int i;

    memset(demoHudPlayers, 0, sizeof(demoHudPlayers));
    for (i = 0; i < MAX_CLIENTS; i++) {
        demoHudPlayers[i].fresh  = qtrue;
        demoHudPlayers[i].weapon = -1;
        demoHudPlayers[i].health = -1;
    }
    memset(demoHudStartAmmo, 0, sizeof(demoHudStartAmmo));
    memset(demoHudMaxAmmo, 0, sizeof(demoHudMaxAmmo));
    demoHudRecorderAlive = qfalse;
    demoHudEvent         = 0;
    demoHudLastTime      = 0;
    CL_DemoHudModelsChanged();
}

/*
====================
CL_DemoHudModel

What the model of a modelindex is, looked up once
====================
*/
static void CL_DemoHudModel(int modelindex, int *weapon, int *health, dtiki_t **tiki)
{
    int         offset;
    const char *model, *box;
    int         i;

    *weapon = -1;
    *health = 0;
    *tiki   = NULL;
    if (modelindex <= 0 || modelindex >= MAX_MODELS) {
        return;
    }
    offset = cl.gameState.stringOffsets[CS_MODELS + modelindex];
    if (demoHudModels[modelindex].offset != offset || !offset) {
        model                            = CL_ConfigString(CS_MODELS + modelindex);
        demoHudModels[modelindex].offset = offset;
        demoHudModels[modelindex].weapon = -1;
        demoHudModels[modelindex].health = 0;
        demoHudModels[modelindex].tiki   = *model ? TIKI_FindTiki(model) : NULL;
        for (i = 0; i < DEMO_HUD_WEAPONS; i++) {
            if (!Q_stricmp(model, demoHudWeapons[i].model)) {
                demoHudModels[modelindex].weapon = i;
            }
        }
        // models/items/dm_50_healthbox.tik, item_25_healthbox.tik...
        box = Q_stristr(model, "_healthbox");
        if (!Q_stricmpn(model, "models/items/", 13) && box) {
            while (box > model && isdigit((unsigned char)box[-1])) {
                box--;
            }
            demoHudModels[modelindex].health = atoi(box);
        }
    }
    *weapon = demoHudModels[modelindex].weapon;
    *health = demoHudModels[modelindex].health;
    *tiki   = demoHudModels[modelindex].tiki;
}

/*
====================
CL_DemoHudItem

The CS_WEAPONS index of a weapon, -1 when the server doesn't have it
====================
*/
static int CL_DemoHudItem(int weapon)
{
    int i;

    for (i = 0; i < MAX_WEAPONS; i++) {
        if (!Q_stricmp(CL_ConfigString(CS_WEAPONS + i), demoHudWeapons[weapon].name)) {
            return i;
        }
    }
    return -1;
}

static int CL_DemoHudMost(demoHudAmmo_t type)
{
    return demoHudMaxAmmo[type] ? demoHudMaxAmmo[type] : demoHudMostAmmo[type];
}

/*
====================
CL_DemoHudInHand

Whether a gun is in the hand of the player whose body is body, not carried
on them (as some servers show them)
====================
*/
static qboolean CL_DemoHudInHand(const entityState_t *body, const entityState_t *gun)
{
    dtiki_t    *tiki;
    const char *tag;
    int         weapon, health;

    CL_DemoHudModel(body->modelindex, &weapon, &health, &tiki);
    if (!tiki) {
        return qtrue;
    }
    tag = TIKI_Tag_NumToName(tiki, gun->tag_num & TAG_MASK);
    return tag && (!Q_stricmp(tag, "tag_weapon_right") || !Q_stricmp(tag, "tag_weapon_left"));
}

/*
====================
CL_DemoHudGunAnim

What a gun does, its animation and the time in it
====================
*/
static demoHudGunAnim_t CL_DemoHudGunAnim(const entityState_t *gun, int *anim, float *time)
{
    dtiki_t    *tiki;
    const char *name;
    int         best = -1, weapon, health;
    int         i;

    CL_DemoHudModel(gun->modelindex, &weapon, &health, &tiki);
    for (i = 0; i < MAX_FRAMEINFOS; i++) {
        if (gun->frameInfo[i].weight > 0 && (best < 0 || gun->frameInfo[i].weight > gun->frameInfo[best].weight)) {
            best = i;
        }
    }
    *anim = best < 0 ? -1 : gun->frameInfo[best].index;
    *time = best < 0 ? 0 : gun->frameInfo[best].time;
    if (best < 0 || !tiki) {
        return GUNANIM_OTHER;
    }

    name = TIKI_Anim_NameForNum(tiki, *anim);
    if (!name) {
        return GUNANIM_OTHER;
    }
    if (!Q_stricmp(name, "fire_empty")) {
        return GUNANIM_FIRE_EMPTY;
    }
    // fire, or one of several: fire_1, fire_2...
    if (!Q_stricmp(name, "fire") || (!Q_stricmpn(name, "fire_", 5) && isdigit((unsigned char)name[5]))) {
        return GUNANIM_FIRE;
    }
    if (!Q_stricmpn(name, "secondaryfire", 13)) {
        return GUNANIM_SECONDARY;
    }
    return GUNANIM_OTHER;
}

/*
====================
CL_DemoHudSlot

What a weapon is carried as: a pistol, grenades or a gun, one of each
====================
*/
static int CL_DemoHudSlot(int weapon)
{
    switch (demoHudWeapons[weapon].ammoType) {
    case AMMO_PISTOL:
        return 0;
    case AMMO_GRENADE:
        return 1;
    default:
        return 2;
    }
}

/*
====================
CL_DemoHudGive

A weapon they have, given with the life it's had in: a full clip and the
rest the server gives. It takes the place of one carried as it is (as a
server gives a gun chosen as a round begins)
====================
*/
static void CL_DemoHudGive(demoHudPlayer_t *p, int weapon)
{
    const demoHudWeapon_t *w = &demoHudWeapons[weapon];
    int                    start, i;

    if (p->clip[weapon] >= 0) {
        return;
    }
    for (i = 0; i < DEMO_HUD_WEAPONS; i++) {
        if (p->clip[i] >= 0 && CL_DemoHudSlot(i) == CL_DemoHudSlot(weapon)) {
            p->clip[i]                          = -1;
            p->ammo[demoHudWeapons[i].ammoType] = 0;
        }
    }
    start                = demoHudStartAmmo[weapon] ? demoHudStartAmmo[weapon] : w->startAmmo;
    p->clip[weapon]      = Q_min(w->clipSize, start);
    p->ammo[w->ammoType] = Q_min(start - p->clip[weapon], CL_DemoHudMost(w->ammoType));
}

/*
====================
CL_DemoHudLife

A new life: 100 health, and the weapons as given
====================
*/
static void CL_DemoHudLife(demoHudPlayer_t *p)
{
    int i;

    p->alive      = qtrue;
    p->fresh      = qfalse;
    p->health     = 100;
    p->healthTime = 0;
    p->gunModel   = 0;
    for (i = 0; i < MAX_FRAMEINFOS; i++) {
        p->bodyAnim[i] = -1;
    }
    for (i = 0; i < DEMO_HUD_WEAPONS; i++) {
        p->clip[i] = -1;
    }
    for (i = 0; i < NUM_DEMO_HUD_AMMO; i++) {
        p->ammo[i] = 0;
    }
}

/*
====================
CL_DemoHudFill

A reload: as much in the clip as there is, at most rounds
====================
*/
static void CL_DemoHudFill(demoHudPlayer_t *p, int weapon, int rounds)
{
    const demoHudWeapon_t *w   = &demoHudWeapons[weapon];
    int                    add = Q_min(Q_min(rounds, w->clipSize - p->clip[weapon]), p->ammo[w->ammoType]);

    if (add > 0) {
        p->clip[weapon] += add;
        p->ammo[w->ammoType] -= add;
    }
}

/*
====================
CL_DemoHudShot

A shot of the gun in their hand, empty after it for fire_empty
====================
*/
static void CL_DemoHudShot(demoHudPlayer_t *p, int weapon, qboolean empty)
{
    const demoHudWeapon_t *w = &demoHudWeapons[weapon];

    if (w->ammoType == AMMO_GRENADE) {
        // the next one is taken out at once
        if (p->ammo[AMMO_GRENADE] > 0) {
            p->ammo[AMMO_GRENADE]--;
        } else {
            p->clip[weapon] = 0;
        }
        return;
    }
    if (p->clip[weapon] <= 0) {
        // a reload missed
        CL_DemoHudFill(p, weapon, w->clipSize);
    }
    if (p->clip[weapon] > 0) {
        p->clip[weapon]--;
    }
    if (empty) {
        p->clip[weapon] = 0;
    }
}

/*
====================
CL_DemoHudGunSeen

The gun in their hand, as it's seen in a snapshot: what it does since
====================
*/
static void CL_DemoHudGunSeen(demoHudPlayer_t *p, const entityState_t *gun, int weapon)
{
    demoHudGunAnim_t kind;
    int              anim;
    float            time;
    qboolean         again;

    if (weapon < 0 || weapon >= DEMO_HUD_WEAPONS) {
        return;
    }

    CL_DemoHudGive(p, weapon);
    kind = CL_DemoHudGunAnim(gun, &anim, &time);

    if (gun->modelindex != p->gunModel) {
        // taken out
        p->gunModel = gun->modelindex;
        p->gunAnim  = anim;
        p->gunTime  = time;
        p->weapon   = weapon;
        return;
    }
    p->weapon = weapon;

    // the gun's animation begins again, or another
    again = anim != p->gunAnim || time < p->gunTime - 0.001f;
    if (again) {
        switch (kind) {
        case GUNANIM_FIRE:
            CL_DemoHudShot(p, weapon, qfalse);
            break;
        case GUNANIM_FIRE_EMPTY:
            CL_DemoHudShot(p, weapon, qtrue);
            break;
        case GUNANIM_SECONDARY:
            // a grenade's short throw, else a bash
            if (demoHudWeapons[weapon].ammoType == AMMO_GRENADE) {
                CL_DemoHudShot(p, weapon, qfalse);
            }
            break;
        default:
            break;
        }
    }
    p->gunAnim = anim;
    p->gunTime = time;
}

/*
====================
CL_DemoHudReloads

What the animations of their body did to the clip of the gun in their hand
since the last snapshot: a reload fills it at the frame a server does
(weaponcommand mainhand clip_fill, or clip_add <rounds>), if it got there
====================
*/
static void CL_DemoHudReloads(demoHudPlayer_t *p, const entityState_t *body)
{
    dtiki_t *tiki;
    int      weapon, health, slot, i;

    CL_DemoHudModel(body->modelindex, &weapon, &health, &tiki);
    for (slot = 0; slot < MAX_FRAMEINFOS; slot++) {
        const frameInfo_t    *frame = &body->frameInfo[slot];
        const dtikianimdef_t *def;
        float                 from, frametime;

        if (frame->weight <= 0 || frame->index < 0) {
            p->bodyAnim[slot] = -1;
            continue;
        }
        // from the start of an animation that begins, or again
        from = frame->index == p->bodyAnim[slot] && frame->time >= p->bodyTime[slot] ? p->bodyTime[slot] : 0;
        p->bodyAnim[slot] = frame->index;
        p->bodyTime[slot] = frame->time;

        if (!tiki || p->weapon < 0 || frame->index >= tiki->a->num_anims) {
            continue;
        }
        def = tiki->a->animdefs[frame->index];
        if (!def->num_server_cmds) {
            continue;
        }
        frametime = TIKI_Anim_Frametime(tiki, frame->index);
        for (i = 0; i < def->num_server_cmds; i++) {
            const dtikicmd_t *cmd = &def->server_cmds[i];
            float             at  = cmd->frame_num * frametime;

            if (cmd->frame_num < 0 || cmd->num_args < 3 || Q_stricmp(cmd->args[0], "weaponcommand")
                || at <= from || at > frame->time) {
                continue;
            }
            if (!Q_stricmp(cmd->args[2], "clip_fill")) {
                CL_DemoHudFill(p, p->weapon, demoHudWeapons[p->weapon].clipSize);
            } else if (!Q_stricmp(cmd->args[2], "clip_add")) {
                CL_DemoHudFill(p, p->weapon, cmd->num_args > 3 ? atoi(cmd->args[3]) : 1);
            }
        }
    }
}

/*
====================
CL_DemoHudRecorder

What the recorder's playerstate tells of the server's amounts: what a life
begins with for a weapon (clip and rest), and the most of its ammo
====================
*/
static void CL_DemoHudRecorder(const playerState_t *ps, int time)
{
    qboolean alive  = !(ps->pm_flags & (PMF_SPECTATING | PMF_CAMERA_VIEW)) && ps->stats[STAT_HEALTH] > 0;
    int      item   = ps->activeItems[ITEM_WEAPON];
    int      weapon = -1, i;

    if (!alive) {
        demoHudRecorderAlive = qfalse;
        // back from watching: a new life
        if ((ps->pm_flags & PMF_SPECTATING) && ps->clientNum >= 0 && ps->clientNum < MAX_CLIENTS) {
            demoHudPlayers[ps->clientNum].fresh = qtrue;
        }
        return;
    }
    if (!demoHudRecorderAlive) {
        memset(demoHudRecorderHad, 0, sizeof(demoHudRecorderHad));
        demoHudRecorderLife = time;
    }
    if (item >= 0 && item < MAX_WEAPONS) {
        const char *name = CL_ConfigString(CS_WEAPONS + item);

        for (i = 0; i < DEMO_HUD_WEAPONS; i++) {
            if (!Q_stricmp(name, demoHudWeapons[i].name)) {
                weapon = i;
            }
        }
    }
    if (weapon >= 0 && ps->stats[STAT_MAXCLIPAMMO] == demoHudWeapons[weapon].clipSize) {
        if (demoHudWeapons[weapon].ammoType == AMMO_GRENADE) {
            // the most they had: they're rarely taken out at once
            demoHudStartAmmo[weapon] =
                Q_max(demoHudStartAmmo[weapon], ps->stats[STAT_AMMO] + ps->stats[STAT_CLIPAMMO]);
        } else if (!demoHudRecorderHad[weapon] && !demoHudStartAmmo[weapon]
                   && time - demoHudRecorderLife <= DEMO_HUD_GIVEN_TIME
                   && ps->stats[STAT_CLIPAMMO] == demoHudWeapons[weapon].clipSize) {
            // first in their hand, right as the life began: as given
            demoHudStartAmmo[weapon] = ps->stats[STAT_AMMO] + ps->stats[STAT_CLIPAMMO];
        }
        if (ps->stats[STAT_MAXAMMO] > 0) {
            demoHudMaxAmmo[demoHudWeapons[weapon].ammoType] = ps->stats[STAT_MAXAMMO];
        }
        demoHudRecorderHad[weapon] = qtrue;
    }
    demoHudRecorderAlive = qtrue;
}

/*
====================
CL_DemoHudPickup

An item gone from a snapshot: taken by the player nearest it who can take
it, if one is beside it
====================
*/
static void CL_DemoHudPickup(const demoHudItem_t *item, const entityState_t **bodies, int numBodies)
{
    demoHudPlayer_t       *best = NULL;
    const demoHudWeapon_t *w    = NULL;
    float                  bestDist = DEMO_HUD_PICKUP_DIST, dist;
    int                    weapon = item->weapon, health = item->health, type = 0, i, j;

    if (weapon >= 0) {
        w    = &demoHudWeapons[weapon];
        type = w->ammoType;
    } else if (!health) {
        return;
    }

    for (i = 0; i < numBodies; i++) {
        demoHudPlayer_t *p = &demoHudPlayers[bodies[i]->number];
        qboolean         can = qfalse;

        if (!p->alive || (bodies[i]->eFlags & EF_DEAD)) {
            continue;
        }
        if (health) {
            can = p->health < 100;
        } else {
            // with a gun of its kind, short of ammo for it, or without a
            // gun but a pistol or grenades (Weapon::PickupWeapon)
            qboolean kind = qfalse, primary = qfalse;

            for (j = 0; j < DEMO_HUD_WEAPONS; j++) {
                kind |= p->clip[j] >= 0 && demoHudWeapons[j].ammoType == type;
                primary |= p->clip[j] >= 0 && demoHudWeapons[j].ammoType != AMMO_PISTOL
                        && demoHudWeapons[j].ammoType != AMMO_GRENADE;
            }
            can = kind ? p->ammo[type] < CL_DemoHudMost(w->ammoType) : !primary;
        }
        dist = Distance(bodies[i]->origin, item->origin);
        if (can && dist < bestDist) {
            best     = p;
            bestDist = dist;
        }
    }
    if (!best) {
        return;
    }
    if (health) {
        best->health = Q_min(100, best->health + health);
        return;
    }
    if (best->clip[weapon] < 0) {
        // the gun itself, its clips to load
        best->clip[weapon] = 0;
    }
    best->ammo[type] = Q_min(best->ammo[type] + DEMO_HUD_DROPPED_CLIPS * w->clipSize, CL_DemoHudMost(w->ammoType));
}

/*
====================
CL_DemoHudDrop

A gun come in a snapshot: the one a player beside it had and didn't have in
their hand, dropped (as some servers let them), with its ammo
====================
*/
static void CL_DemoHudDrop(const demoHudItem_t *item, const entityState_t **bodies, int numBodies)
{
    const demoHudWeapon_t *w = &demoHudWeapons[item->weapon];
    demoHudPlayer_t       *best = NULL;
    float                  bestDist = DEMO_HUD_PICKUP_DIST, dist;
    int                    i;

    for (i = 0; i < numBodies; i++) {
        demoHudPlayer_t *p = &demoHudPlayers[bodies[i]->number];

        if (!p->alive || (bodies[i]->eFlags & EF_DEAD) || p->clip[item->weapon] < 0 || p->weapon == item->weapon) {
            continue;
        }
        dist = Distance(bodies[i]->origin, item->origin);
        if (dist < bestDist) {
            best     = p;
            bestDist = dist;
        }
    }
    if (!best) {
        return;
    }
    best->clip[item->weapon] = -1;
    for (i = 0; i < DEMO_HUD_WEAPONS; i++) {
        if (best->clip[i] >= 0 && demoHudWeapons[i].ammoType == w->ammoType) {
            return;
        }
    }
    best->ammo[w->ammoType] = 0;
}

/*
====================
CL_DemoHudItems

The items in a snapshot, those gone since the last taken by who can
====================
*/
static void CL_DemoHudItems(const clSnapshot_t *snap, const entityState_t **bodies, int numBodies)
{
    demoHudItem_t items[MAX_DEMO_HUD_ITEMS];
    int           numItems = 0, i, j, weapon, health;
    dtiki_t      *tiki;

    for (i = 0; i < snap->numEntities && numItems < MAX_DEMO_HUD_ITEMS; i++) {
        const entityState_t *ent = &cl.parseEntities[(snap->parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];

        if (ent->eType != ET_ITEM || ent->parent != ENTITYNUM_NONE) {
            continue;
        }
        CL_DemoHudModel(ent->modelindex, &weapon, &health, &tiki);
        if (weapon < 0 && !health) {
            continue;
        }
        items[numItems].number     = ent->number;
        items[numItems].modelindex = ent->modelindex;
        items[numItems].weapon     = weapon;
        items[numItems].health     = health;
        VectorCopy(ent->origin, items[numItems].origin);
        numItems++;
    }

    for (i = 0; i < demoHudNumItems; i++) {
        for (j = 0; j < numItems; j++) {
            if (items[j].number == demoHudItems[i].number && items[j].modelindex == demoHudItems[i].modelindex) {
                break;
            }
        }
        if (j == numItems) {
            CL_DemoHudPickup(&demoHudItems[i], bodies, numBodies);
        }
    }
    for (i = 0; i < numItems; i++) {
        for (j = 0; j < demoHudNumItems; j++) {
            if (items[i].number == demoHudItems[j].number && items[i].modelindex == demoHudItems[j].modelindex) {
                break;
            }
        }
        if (j == demoHudNumItems && items[i].weapon >= 0) {
            CL_DemoHudDrop(&items[i], bodies, numBodies);
        }
    }

    memcpy(demoHudItems, items, numItems * sizeof(items[0]));
    demoHudNumItems = numItems;
}

/*
====================
CL_DemoHudSnapshot

A snapshot read: the deaths and rounds until it, and what's seen of each
player in it
====================
*/
void CL_DemoHudSnapshot(const clSnapshot_t *snap)
{
    const demoIndex_t   *index = CL_DemoIndex();
    int                  time  = snap->serverTime - clc.demoStartTime;
    const entityState_t *bodies[MAX_CLIENTS];
    const entityState_t *guns[MAX_CLIENTS];
    qboolean             twoGuns[MAX_CLIENTS];
    qboolean             seen[MAX_CLIENTS];
    int                  numBodies = 0;
    int                  i, j, weapon, health;
    dtiki_t             *tiki;

    if (time < demoHudLastTime) {
        CL_DemoHudClear();
    }
    demoHudLastTime = time;

    // deaths and rounds, as the index has them
    for (; index && demoHudEvent < index->numEvents && index->events[demoHudEvent].time <= time; demoHudEvent++) {
        const demoEvent_t *ev = &index->events[demoHudEvent];

        if (ev->type == DEMOEVENT_KILL && ev->other >= 0 && ev->other < MAX_CLIENTS) {
            demoHudPlayers[ev->other].alive  = qfalse;
            demoHudPlayers[ev->other].fresh  = qtrue;
            demoHudPlayers[ev->other].health = 0;
        } else if (ev->type == DEMOEVENT_ROUNDSTART || ev->type == DEMOEVENT_MAP) {
            for (i = 0; i < MAX_CLIENTS; i++) {
                demoHudPlayers[i].fresh = qtrue;
            }
        }
    }

    CL_DemoHudRecorder(&snap->ps, time);

    // the pain the server plays on them
    for (i = 0; i < snap->number_of_sounds; i++) {
        const server_sound_t *sound = &snap->sounds[i];

        if (sound->entity_number >= 0 && sound->entity_number < MAX_CLIENTS
            && sound->channel == CHAN_DIALOG_SECONDARY && !sound->stop_flag) {
            demoHudPlayers[sound->entity_number].pain = qtrue;
            VectorCopy(sound->origin, demoHudPlayers[sound->entity_number].painOrigin);
        }
    }

    // the bodies, and the guns on them
    for (i = 0; i < MAX_CLIENTS; i++) {
        guns[i]    = NULL;
        twoGuns[i] = qfalse;
        seen[i]    = qfalse;
    }
    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *ent = &cl.parseEntities[(snap->parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];

        if (ent->number >= 0 && ent->number < MAX_CLIENTS) {
            // once each, and only with a body: right after a demo begins
            // again, a snapshot read from frames kept from before can have
            // blank entities, all numbered 0
            if (ent->modelindex && !seen[ent->number]) {
                seen[ent->number]   = qtrue;
                bodies[numBodies++] = ent;
            }
            continue;
        }
        if (ent->parent < 0 || ent->parent >= MAX_CLIENTS) {
            continue;
        }
        CL_DemoHudModel(ent->modelindex, &weapon, &health, &tiki);
        if (weapon >= 0) {
            twoGuns[ent->parent] = guns[ent->parent] != NULL;
            if (!guns[ent->parent]) {
                guns[ent->parent] = ent;
            }
        }
    }

    for (i = 0; i < numBodies; i++) {
        const entityState_t *body = bodies[i];
        demoHudPlayer_t     *p    = &demoHudPlayers[body->number];
        const entityState_t *gun;

        if (body->eFlags & EF_DEAD) {
            p->alive  = qfalse;
            p->fresh  = qtrue;
            p->health = 0;
            continue;
        }
        if (p->fresh || !p->alive) {
            CL_DemoHudLife(p);
        }
        if (p->pain) {
            VectorCopy(body->netorigin, p->painOrigin);
        }

        gun = guns[body->number];
        if (gun && twoGuns[body->number]) {
            // one in their hand, the others carried
            gun = NULL;
            for (j = 0; j < snap->numEntities && !gun; j++) {
                const entityState_t *ent =
                    &cl.parseEntities[(snap->parseEntitiesNum + j) & (MAX_PARSE_ENTITIES - 1)];

                CL_DemoHudModel(ent->modelindex, &weapon, &health, &tiki);
                if (ent->number >= MAX_CLIENTS && ent->parent == body->number && weapon >= 0
                    && CL_DemoHudInHand(body, ent)) {
                    gun = ent;
                }
            }
        } else if (gun && !CL_DemoHudInHand(body, gun)) {
            gun = NULL;
        }
        if (gun) {
            CL_DemoHudModel(gun->modelindex, &weapon, &health, &tiki);
            CL_DemoHudGunSeen(p, gun, weapon);
        } else {
            p->gunModel = 0;
            p->weapon   = -1;
        }
        CL_DemoHudReloads(p, body);
    }

    CL_DemoHudItems(snap, bodies, numBodies);

    // their health, when the demo has it
    i = snap->ps.stats[STAT_INFOCLIENT];
    if (i >= 0 && i < MAX_CLIENTS && demoHudPlayers[i].alive && snap->ps.stats[STAT_INFOCLIENT_HEALTH] > 0) {
        demoHudPlayers[i].health     = snap->ps.stats[STAT_INFOCLIENT_HEALTH];
        demoHudPlayers[i].healthTime = time;
    }
}

/*
====================
CL_DemoHudPained

The players the server played pain on in the last snapshot, and where they
were, for their hits (CL_DemoHudHit), once
====================
*/
int CL_DemoHudPained(int *clients, vec3_t *origins)
{
    int i, n = 0;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (demoHudPlayers[i].pain && demoHudPlayers[i].alive) {
            clients[n] = i;
            VectorCopy(demoHudPlayers[i].painOrigin, origins[n]);
            n++;
        }
        demoHudPlayers[i].pain = qfalse;
    }
    return n;
}

/*
====================
CL_DemoHudHit

A hit of a player that they live through
====================
*/
void CL_DemoHudHit(int client, int damage)
{
    demoHudPlayer_t *p;

    if (client < 0 || client >= MAX_CLIENTS || !demoHudPlayers[client].alive) {
        return;
    }
    p = &demoHudPlayers[client];
    if (p->healthTime && demoHudLastTime - p->healthTime <= 100) {
        // the demo had it, with the hit: its pain comes a snapshot later
        return;
    }
    p->health = Q_max(1, p->health - damage);
}

/*
====================
CL_DemoHudState

What the HUD shows of a player, as of the last snapshot
====================
*/
void CL_DemoHudState(int client, demoHudState_t *state)
{
    const demoHudPlayer_t *p;
    const demoHudWeapon_t *w;

    state->health     = -1;
    state->weaponItem = -1;
    state->clip = state->clipSize = state->ammo = state->maxAmmo = 0;
    if (client < 0 || client >= MAX_CLIENTS) {
        return;
    }

    p             = &demoHudPlayers[client];
    state->health = p->alive ? p->health : 0;
    if (p->weapon < 0 || p->clip[p->weapon] < 0) {
        return;
    }
    w                 = &demoHudWeapons[p->weapon];
    state->weaponItem = CL_DemoHudItem(p->weapon);
    state->maxAmmo    = CL_DemoHudMost(w->ammoType);
    state->clip       = p->clip[p->weapon];
    state->clipSize   = w->clipSize;
    state->ammo       = p->ammo[w->ammoType];
}
