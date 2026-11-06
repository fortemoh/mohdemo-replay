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
//  cg_demoguns.c: a demo holds what the server sent its recorder, so while it
//  shows someone else from behind (cl_demoBehind), the player's body can be
//  in a snapshot without the gun in their hand, which is a separate entity
//  and carries the sound of its shots. Their body's animations still tell
//  what they hold and when they fire ("smg_stand_run_fire"), so with
//  cg_demoguns the gun is put in the snapshot as the server would have sent
//  it: the one last seen with those animations in their hand, or else their
//  team's. Each shot restarts the body's animation and the gun's alike.

#include "cg_local.h"

typedef struct {
    const char *prefix; // of the body's animations
    const char *allied;
    const char *axis;
} demoGun_t;

// longer prefixes first, as they begin with the shorter ones
static const demoGun_t demoGuns[] = {
    {"pistol_silenced",  "models/weapons/silencedpistol.tik",   "models/weapons/silencedpistol.tik"  },
    {"histandard",       "models/weapons/silencedpistol.tik",   "models/weapons/silencedpistol.tik"  },
    {"pistol",           "models/weapons/colt45.tik",           "models/weapons/p38.tik"             },
    {"p38",              "models/weapons/p38.tik",              "models/weapons/p38.tik"             },
    {"smg",              "models/weapons/thompsonsmg.tik",      "models/weapons/thompsonsmg.tik"     },
    {"mp40",             "models/weapons/mp40.tik",             "models/weapons/mp40.tik"            },
    {"mp44",             "models/weapons/mp44.tik",             "models/weapons/mp44.tik"            },
    {"mg",               "models/weapons/bar.tik",              "models/weapons/bar.tik"             },
    {"kar98",            "models/weapons/kar98.tik",            "models/weapons/kar98.tik"           },
    {"rifle",            "models/weapons/m1_garand.tik",        "models/weapons/kar98.tik"           },
    {"shotgun",          "models/weapons/shotgun.tik",          "models/weapons/shotgun.tik"         },
    {"panzerschreck",    "models/weapons/panzerschreck.tik",    "models/weapons/panzerschreck.tik"   },
    {"bazooka",          "models/weapons/bazooka.tik",          "models/weapons/panzerschreck.tik"   },
    {"steilhandgranate", "models/weapons/steilhandgranate.tik", "models/weapons/steilhandgranate.tik"},
    {"grenade",          "models/weapons/m2frag_grenade.tik",   "models/weapons/m2frag_grenade.tik"  },
};

#define NUM_DEMO_GUNS ((int)ARRAY_LEN(demoGuns))

// the entity numbers of the guns put in the snapshots, one for each player,
// the highest that aren't special, as the server hardly gets to them
#define DEMO_GUN_NUMBER(client) (ENTITYNUM_WORLD - 1 - (client))

static int           seenGun[MAX_CLIENTS][NUM_DEMO_GUNS]; // model index last seen in their hand, 0 if none
static entityState_t gunTemplate;
static qboolean      haveTemplate;
static int           lastFlags;

// each shot goes on to the other animation of the two, as a gun's do on a
// server, which is what makes it fire again: its sound and flash
typedef struct {
    qboolean firing;
    float    time;
    int      slot;
} demoGunShots_t;

static demoGunShots_t gunShots[MAX_CLIENTS];

/*
===============
CG_DemoGunModel

The index of a model the server told the client of, 0 if it didn't
===============
*/
static int CG_DemoGunModel(const char *name)
{
    int i;

    for (i = 1; i < MAX_MODELS; i++) {
        if (!Q_stricmp(CG_ConfigString(CS_MODELS + i), name)) {
            return i;
        }
    }
    return 0;
}

/*
===============
CG_DemoBodyAnims

What a player's body is doing: the gun in demoGuns its animations go with, -1
for none, and the gun's animation and time, from the last of the animations
playing and those for what the gun does
===============
*/
static int CG_DemoBodyAnims(const entityState_t *body, dtiki_t *tiki, const char **anim, float *time)
{
    int      gun      = -1;
    qboolean firing   = qfalse;
    float    fireTime = 0;
    int      i, j;

    *anim = "idle";
    *time = 0;

    for (i = 0; i < MAX_FRAMEINFOS; i++) {
        const char *name;

        if (body->frameInfo[i].weight <= 0) {
            continue;
        }

        name = cgi.Anim_NameForNum(tiki, body->frameInfo[i].index);
        if (!name) {
            continue;
        }

        gun = -1;
        for (j = 0; j < NUM_DEMO_GUNS; j++) {
            size_t length = strlen(demoGuns[j].prefix);

            if (!Q_stricmpn(name, demoGuns[j].prefix, length) && name[length] == '_') {
                gun = j;
                break;
            }
        }

        if (strstr(name, "_fire")) {
            // the last shot is the one that restarted it, while the one
            // before it fades
            if (!firing || body->frameInfo[i].time < fireTime) {
                fireTime = body->frameInfo[i].time;
            }
            firing = qtrue;
        } else if (strstr(name, "_reload")) {
            *anim = "reload";
            *time = body->frameInfo[i].time;
        } else if (strstr(name, "_rechamber")) {
            *anim = "rechamber";
            *time = body->frameInfo[i].time;
        }
    }

    if (firing && !Q_stricmp(*anim, "idle")) {
        *anim = "fire";
        *time = fireTime;
    }
    return gun;
}

/*
===============
CG_DemoGunAdd

Puts it in the snapshot in order, as parents come before what's attached to
them
===============
*/
static void CG_DemoGunAdd(snapshot_t *snap, const entityState_t *es)
{
    int i;

    if (snap->numEntities >= MAX_ENTITIES_IN_SNAPSHOT) {
        return;
    }

    for (i = snap->numEntities; i > 0 && snap->entities[i - 1].number > es->number; i--) {
        snap->entities[i] = snap->entities[i - 1];
    }
    snap->entities[i] = *es;
    snap->numEntities++;
}

/*
===============
CG_DemoHolsters

Takes the weapons players carry on their bodies out of the snapshot, unless
cg_demoholsters: the game's own files don't put them there, some servers' mods
do. What's in their hands stays, and what isn't a weapon, as a clip in a reload.
===============
*/
void CG_DemoHolsters(snapshot_t *snap)
{
    static int      body[MAX_CLIENTS];
    static qboolean carried[MAX_ENTITIES_IN_SNAPSHOT];
    int             count;
    int             i;

    if (!cg.demoPlayback || cg_demoholsters->integer) {
        return;
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        body[i] = -1;
    }
    for (i = 0; i < snap->numEntities; i++) {
        if (snap->entities[i].eType == ET_PLAYER && snap->entities[i].number < MAX_CLIENTS) {
            body[snap->entities[i].number] = i;
        }
    }

    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];
        dtiki_t             *tiki;
        const char          *tag;

        carried[i] = qfalse;
        if (es->parent >= MAX_CLIENTS || body[es->parent] < 0
            || Q_stricmpn(CG_ConfigString(CS_MODELS + es->modelindex), "models/weapons/", 15)) {
            continue;
        }

        tiki = CG_FollowedBodyTiki(&snap->entities[body[es->parent]]);
        tag  = tiki ? cgi.Tag_NameForNum(tiki, es->tag_num & TAG_MASK) : NULL;
        if (tag && Q_stricmp(tag, "tag_weapon_right") && Q_stricmp(tag, "tag_weapon_left")) {
            carried[i] = qtrue;
        }
    }

    count = 0;
    for (i = 0; i < snap->numEntities; i++) {
        if (!carried[i]) {
            if (count != i) {
                snap->entities[count] = snap->entities[i];
            }
            count++;
        }
    }
    snap->numEntities = count;
}

/*
===============
CG_DemoGuns

Puts the guns of the players who have none in the snapshot, while the demo
shows someone from behind
===============
*/
void CG_DemoGuns(snapshot_t *snap)
{
    static qboolean present[MAX_GENTITIES];
    static qboolean armed[MAX_CLIENTS];
    int             count;
    int             i;

    if (!cg.demoPlayback || !cg_demoguns->integer) {
        return;
    }

    // a new level: the models aren't the same ones
    if ((snap->snapFlags ^ lastFlags) & SNAPFLAG_SERVERCOUNT) {
        memset(seenGun, 0, sizeof(seenGun));
    }
    lastFlags = snap->snapFlags;

    memset(present, 0, sizeof(present));
    memset(armed, 0, sizeof(armed));
    for (i = 0; i < snap->numEntities; i++) {
        present[snap->entities[i].number] = qtrue;
    }

    // what's in their hands, seen with what their bodies do
    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *es = &snap->entities[i];
        int                  j;

        if (es->parent >= MAX_CLIENTS) {
            continue;
        }

        for (j = 0; j < snap->numEntities; j++) {
            const entityState_t *body = &snap->entities[j];

            if (body->number == es->parent && body->eType == ET_PLAYER && CG_FollowedGun(es, body)) {
                dtiki_t *tiki = CG_FollowedBodyTiki(body);
                const char *anim;
                float       time;
                int         gun = tiki ? CG_DemoBodyAnims(body, tiki, &anim, &time) : -1;

                armed[es->parent] = qtrue;
                if (gun >= 0) {
                    seenGun[es->parent][gun] = es->modelindex;
                }
                if (!haveTemplate) {
                    gunTemplate  = *es;
                    haveTemplate = qtrue;
                }
                break;
            }
        }
    }

    if (cl_demoBehind->integer < 0 || cl_demoBehind->integer >= MAX_CLIENTS) {
        return;
    }

    // over the ones the snapshot came with, as they get moved
    count = snap->numEntities;
    for (i = 0; i < count && i < snap->numEntities; i++) {
        const entityState_t body = snap->entities[i];
        entityState_t       gun;
        dtiki_t            *bodyTiki, *gunTiki;
        const char         *anim;
        float               time, length;
        int                 which, model, number, index, tag;
        demoGunShots_t     *shots;

        if (body.eType != ET_PLAYER || body.number >= MAX_CLIENTS || armed[body.number]
            || body.number == snap->ps.clientNum || (body.eFlags & EF_DEAD)) {
            continue;
        }

        bodyTiki = CG_FollowedBodyTiki(&body);
        if (!bodyTiki) {
            continue;
        }

        which = CG_DemoBodyAnims(&body, bodyTiki, &anim, &time);
        if (which < 0) {
            continue;
        }

        model = seenGun[body.number][which];
        if (!model) {
            model = CG_DemoGunModel((body.eFlags & EF_ALLIES) ? demoGuns[which].allied : demoGuns[which].axis);
        }
        number = DEMO_GUN_NUMBER(body.number);
        tag    = cgi.Tag_NumForName(bodyTiki, "tag_weapon_right");
        if (!model || present[number] || tag < 0) {
            continue;
        }

        gunTiki = cgi.R_Model_GetHandle(cgs.model_draw[model]);
        if (!gunTiki) {
            continue;
        }

        if (haveTemplate) {
            gun = gunTemplate;
        } else {
            memset(&gun, 0, sizeof(gun));
            gun.eType         = ET_ITEM;
            gun.eFlags        = 4;
            gun.renderfx      = 16;
            gun.scale         = 1;
            gun.alpha         = 1;
            gun.constantLight = 0xffffff;
            memset(gun.bone_tag, -1, sizeof(gun.bone_tag));
        }

        gun.number     = number;
        gun.parent     = body.number;
        gun.tag_num    = tag;
        gun.modelindex = model;
        VectorCopy(body.origin, gun.origin);
        memset(gun.frameInfo, 0, sizeof(gun.frameInfo));

        index = cgi.Anim_NumForName(gunTiki, anim);
        if (index < 0) {
            index = Q_max(0, cgi.Anim_NumForName(gunTiki, "idle"));
            time  = 0;
        }
        shots = &gunShots[body.number];
        if (!Q_stricmp(anim, "fire")) {
            if (!shots->firing || time < shots->time) {
                shots->slot ^= 1;
            }
            shots->firing = qtrue;
            shots->time   = time;
        } else {
            shots->firing = qfalse;
        }

        length                           = cgi.Anim_Time(gunTiki, index);
        gun.frameInfo[shots->slot].index  = index;
        gun.frameInfo[shots->slot].time   = length > 0 ? Q_min(time, length) : 0;
        gun.frameInfo[shots->slot].weight = 1;

        CG_DemoGunAdd(snap, &gun);
        present[number] = qtrue;
    }
}

/*
===============
CG_DemoShots

Tells the client when the player it asks about last fired (cl_demoShooter), in
cg_demoLastShot, for a clip to go on until they've stopped: each shot restarts
their body's animation
===============
*/
void CG_DemoShots(const snapshot_t *snap)
{
    static int      shooter = -1;
    static qboolean firing;
    static float    firedAt;
    const char     *anim;
    float           time;
    dtiki_t        *tiki;
    int             i;

    if (!cg.demoPlayback) {
        return;
    }

    if (cl_demoShooter->integer != shooter) {
        shooter = cl_demoShooter->integer;
        firing  = qfalse;
    }
    if (shooter < 0) {
        return;
    }

    for (i = 0; i < snap->numEntities; i++) {
        const entityState_t *body = &snap->entities[i];

        if (body->number != shooter) {
            continue;
        }

        tiki = body->eType == ET_PLAYER && !(body->eFlags & EF_DEAD) ? CG_FollowedBodyTiki(body) : NULL;
        if (!tiki || CG_DemoBodyAnims(body, tiki, &anim, &time) < 0 || Q_stricmp(anim, "fire")) {
            break;
        }

        // not one that began before a seek, seen part way through
        if (firing ? time < firedAt : time < 0.15f) {
            cgi.Cvar_Set("cg_demoLastShot", va("%i", snap->serverTime));
        }
        firing  = qtrue;
        firedAt = time;
        return;
    }
    firing = qfalse;
}
