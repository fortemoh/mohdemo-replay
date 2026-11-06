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
//  cg_followeyes.c: with cg_followcamera 2, a demo shows the player its
//  recorder follows from their own eyes. While followed, a snapshot's
//  playerstate is theirs for where they are, look, crouch and zoom, but not
//  for their gun, its animation, their lean or health; those come from
//  their entity and the gun in its hand, and the snapshot is made as if
//  they had recorded it, so that the first person view draws them as it
//  draws a recorder: their arms and gun, firing, rechambering and
//  reloading as their gun does, bobbing as they walk, and the scope.

#include "cg_local.h"

typedef struct {
    const char *model; // in their hand
    const char *name;  // in CS_WEAPONS
    int         weaponClass;
} followedWeapon_t;

static const followedWeapon_t followedWeapons[] = {
    {"models/weapons/colt45.tik",           "Colt 45",                WEAPON_CLASS_PISTOL },
    {"models/weapons/p38.tik",              "Walther P38",            WEAPON_CLASS_PISTOL },
    {"models/weapons/silencedpistol.tik",   "Hi-Standard Silenced",   WEAPON_CLASS_PISTOL },
    {"models/weapons/m1_garand.tik",        "M1 Garand",              WEAPON_CLASS_RIFLE  },
    {"models/weapons/kar98.tik",            "Mauser KAR 98K",         WEAPON_CLASS_RIFLE  },
    {"models/weapons/kar98sniper.tik",      "KAR98 - Sniper",         WEAPON_CLASS_RIFLE  },
    {"models/weapons/springfield.tik",      "Springfield '03 Sniper", WEAPON_CLASS_RIFLE  },
    {"models/weapons/thompsonsmg.tik",      "Thompson",               WEAPON_CLASS_SMG    },
    {"models/weapons/mp40.tik",             "MP40",                   WEAPON_CLASS_SMG    },
    {"models/weapons/bar.tik",              "BAR",                    WEAPON_CLASS_MG     },
    {"models/weapons/mp44.tik",             "StG 44",                 WEAPON_CLASS_MG     },
    {"models/weapons/bazooka.tik",          "Bazooka",                WEAPON_CLASS_HEAVY  },
    {"models/weapons/panzerschreck.tik",    "Panzerschreck",          WEAPON_CLASS_HEAVY  },
    {"models/weapons/shotgun.tik",          "Shotgun",                WEAPON_CLASS_HEAVY  },
    {"models/weapons/m2frag_grenade.tik",   "Frag Grenade",           WEAPON_CLASS_GRENADE},
    {"models/weapons/steilhandgranate.tik", "Stielhandgranate",       WEAPON_CLASS_GRENADE},
};

// the gun's animations, by the names the first person ones end with
static const struct {
    const char *name;
    int         vmAnim;
} gunAnims[] = {
    {"fire",           VM_ANIM_FIRE          },
    {"fire_empty",     VM_ANIM_FIRE          },
    {"fire_secondary", VM_ANIM_FIRE_SECONDARY},
    {"secondaryfire",  VM_ANIM_FIRE_SECONDARY},
    {"charge",         VM_ANIM_CHARGE        },
    {"rechamber",      VM_ANIM_RECHAMBER     },
    {"reload",         VM_ANIM_RELOAD        },
    {"reload_single",  VM_ANIM_RELOAD_SINGLE },
    {"reload_end",     VM_ANIM_RELOAD_END    },
};

// how long a weapon taken out is shown being taken out, in msec
#define FOLLOWED_PULLOUT_TIME 700

// the gun's animation (followed.gunAnim) while the player bashes with it
#define FOLLOWED_BASH -2

// what was made of the snapshots before, to tell what changes
static struct {
    int   serverTime;
    int   client;
    int   gunModel; // modelindex, 0 for none
    int   gunAnim;
    float gunAnimTime;
    int   vmAnim;
    int   vmAnimChanged;
    int   vmAnimTime; // server time it began
    int   pulloutTime;
} followed;

/*
===============
CG_FollowedWeapon

The weapon of a gun model, NULL for one not known
===============
*/
static const followedWeapon_t *CG_FollowedWeapon(int modelindex)
{
    const char *model = CG_ConfigString(CS_MODELS + modelindex);
    int         i;

    for (i = 0; i < (int)ARRAY_LEN(followedWeapons); i++) {
        if (!Q_stricmp(model, followedWeapons[i].model)) {
            return &followedWeapons[i];
        }
    }
    return NULL;
}

/*
===============
CG_FollowedBodyTiki

The model of the body of the player followed, or the default one when
theirs isn't in the game files: all have the same skeleton and animations
===============
*/
dtiki_t *CG_FollowedBodyTiki(const entityState_t *body)
{
    dtiki_t *tiki = cgi.R_Model_GetHandle(cgs.model_draw[body->modelindex]);

    if (!tiki) {
        tiki = cgi.R_Model_GetHandle(cgi.R_RegisterModel(va("models/player/%s.tik", dm_playermodel->resetString)));
    }
    return tiki;
}

/*
===============
CG_FollowedGun

Whether es is a gun in the hand of the player whose body is body
===============
*/
qboolean CG_FollowedGun(const entityState_t *es, const entityState_t *body)
{
    dtiki_t    *tiki;
    const char *tag;

    if (es->parent != body->number || !CG_FollowedWeapon(es->modelindex)) {
        return qfalse;
    }

    tiki = CG_FollowedBodyTiki(body);
    if (!tiki) {
        return qfalse;
    }
    tag = cgi.Tag_NameForNum(tiki, es->tag_num & TAG_MASK);
    return tag && (!Q_stricmp(tag, "tag_weapon_right") || !Q_stricmp(tag, "tag_weapon_left"));
}

/*
===============
CG_FollowedGunAnim

The first person animation of what the gun does, and the animation and its
time
===============
*/
static int CG_FollowedGunAnim(const entityState_t *gun, int *anim, float *time)
{
    dtiki_t    *tiki = cgi.R_Model_GetHandle(cgs.model_draw[gun->modelindex]);
    const char *name;
    int         best = -1;
    int         i;

    for (i = 0; i < MAX_FRAMEINFOS; i++) {
        if (gun->frameInfo[i].weight > 0 && (best < 0 || gun->frameInfo[i].weight > gun->frameInfo[best].weight)) {
            best = i;
        }
    }
    if (best < 0 || !tiki) {
        *anim = -1;
        *time = 0;
        return VM_ANIM_IDLE;
    }

    *anim = gun->frameInfo[best].index;
    *time = gun->frameInfo[best].time;
    name  = cgi.Anim_NameForNum(tiki, *anim);
    for (i = 0; name && i < (int)ARRAY_LEN(gunAnims); i++) {
        if (!Q_stricmp(name, gunAnims[i].name)) {
            return gunAnims[i].vmAnim;
        }
    }
    return VM_ANIM_IDLE;
}

/*
===============
CG_FollowedBash

Whether the player followed bashes with their gun (pistol_butt), and how
long ago it began (time): their body shows it, with the same animation as
the first person one, but their gun only for its first snapshot
===============
*/
static qboolean CG_FollowedBash(const entityState_t *body, float *time)
{
    dtiki_t    *tiki = CG_FollowedBodyTiki(body);
    const char *name;
    int         i;

    for (i = 0; tiki && i < MAX_FRAMEINFOS; i++) {
        if (body->frameInfo[i].weight > 0) {
            name = cgi.Anim_NameForNum(tiki, body->frameInfo[i].index);
            if (name && strstr(name, "_butt")) {
                *time = body->frameInfo[i].time;
                return qtrue;
            }
        }
    }
    return qfalse;
}

/*
===============
CG_FollowedWeaponItem

The item number of a weapon's name (CS_WEAPONS), -1 if it has none yet
===============
*/
static int CG_FollowedWeaponItem(const char *name)
{
    int i;

    for (i = 0; i < MAX_WEAPONS; i++) {
        if (!Q_stricmp(CG_ConfigString(CS_WEAPONS + i), name)) {
            return i;
        }
    }
    return -1;
}

/*
===============
CG_FollowedKill

Prints the "You killed <victim>" the server tells the killer only, for the
player followed from their eyes, when a kill message (text) is theirs: it
begins with the victim's name, and theirs comes after as a whole word (a
teamkill too, not a suicide)
===============
*/
void CG_FollowedKill(const char *text)
{
    char        killer[MAX_STRING_CHARS];
    const char *name, *s;
    size_t      len, best = 0;
    int         i, victim = -1;

    if (!cg.snap || !(cg.snap->snapFlags & SNAPFLAG_FOLLOWED_FROM_EYES) || text[0] != HUD_MESSAGE_CHAT_RED[0]) {
        return;
    }
    text++;

    for (i = 0; i < MAX_CLIENTS; i++) {
        name = Info_ValueForKey(CG_ConfigString(CS_PLAYERS + i), "name");
        len  = strlen(name);
        if (len > best && !strncmp(text, name, len) && text[len] == ' ') {
            best   = len;
            victim = i;
        }
    }
    if (victim < 0 || victim == cg.snap->ps.clientNum) {
        return;
    }

    Q_strncpyz(killer, Info_ValueForKey(CG_ConfigString(CS_PLAYERS + cg.snap->ps.clientNum), "name"), sizeof(killer));
    len = strlen(killer);
    if (!len) {
        return;
    }
    for (s = strstr(text + best, killer); s; s = strstr(s + 1, killer)) {
        if (s[-1] == ' ' && !isalnum((unsigned char)s[len])) {
            cgi.Printf(
                HUD_MESSAGE_WHITE "You killed %s\n", Info_ValueForKey(CG_ConfigString(CS_PLAYERS + victim), "name")
            );
            return;
        }
    }
}

/*
===============
CG_FollowFromEyes

Makes snap as if the player it follows had recorded it, with cg_followcamera
2 in a demo, when their entity is in it
===============
*/
void CG_FollowFromEyes(snapshot_t *snap)
{
    playerState_t          *ps = &snap->ps;
    entityState_t          *body = NULL;
    const entityState_t    *gun  = NULL;
    const followedWeapon_t *weapon;
    int                     client, anim, vmAnim, item;
    float                   time;
    int                     i;

    if (cg_followcamera->integer != 2 || !cg.demoPlayback || !(ps->pm_flags & PMF_CAMERA_VIEW)) {
        return;
    }
    client = ps->stats[STAT_INFOCLIENT];
    if (client < 0 || client >= MAX_CLIENTS) {
        return;
    }

    for (i = 0; i < snap->numEntities; i++) {
        if (snap->entities[i].number == client) {
            body = &snap->entities[i];
        }
    }
    if (!body) {
        return;
    }
    for (i = 0; i < snap->numEntities; i++) {
        if (CG_FollowedGun(&snap->entities[i], body)) {
            gun = &snap->entities[i];
        }
    }

    // another player, back in time or a jump (a seek, the next clip of
    // demoonly, a new round or demo, as CG_ProcessSnapshots tells): nothing to
    // go on from
    if (client != followed.client || snap->serverTime < followed.serverTime || !cg.snap
        || cg.snap->serverTime <= cgi.GetServerStartTime()) {
        memset(&followed, 0, sizeof(followed));
        followed.client  = client;
        followed.gunAnim = -1;
    }
    followed.serverTime = snap->serverTime;

    ps->clientNum = client;
    ps->pm_flags &= ~(PMF_CAMERA_VIEW | PMF_SPECTATING);
    // on the ground, as their entity is: the view and the gun bob as they walk
    ps->groundEntityNum = body->groundEntityNum;
    ps->walking         = body->groundEntityNum != ENTITYNUM_NONE;
    // the pelvis (bone controller 3) rolls by 0.8 times the lean
    ps->fLeanAngle               = AngleNormalize180(body->bone_angles[3][2]) / 0.8f;
    ps->stats[STAT_HEALTH]       = (body->eFlags & EF_DEAD) ? 0 : ps->stats[STAT_INFOCLIENT_HEALTH];
    ps->stats[STAT_INFOCLIENT]   = -1;
    ps->stats[STAT_INZOOM]       = ps->fov <= 30 ? (int)ps->fov : 0;
    ps->stats[STAT_CROSSHAIR]    = !ps->stats[STAT_INZOOM];
    if (body->eFlags & EF_AXIS) {
        ps->stats[STAT_TEAM] = TEAM_AXIS;
    } else if (body->eFlags & EF_ALLIES) {
        ps->stats[STAT_TEAM] = TEAM_ALLIES;
    }

    weapon = gun ? CG_FollowedWeapon(gun->modelindex) : NULL;
    item   = weapon ? CG_FollowedWeaponItem(weapon->name) : -1;
    if (item < 0) {
        // no gun in their hand that's known: no arms
        body->eFlags |= EF_UNARMED;
        ps->stats[STAT_EQUIPPED_WEAPON] = 0;
        ps->activeItems[1]              = -1;
        followed.gunModel               = 0;
    } else {
        ps->stats[STAT_EQUIPPED_WEAPON] = weapon->weaponClass;
        ps->activeItems[1]              = item;

        vmAnim = CG_FollowedGunAnim(gun, &anim, &time);
        if (CG_FollowedBash(body, &time)) {
            vmAnim = VM_ANIM_FIRE_SECONDARY;
            anim   = FOLLOWED_BASH;
        }
        if (gun->modelindex != followed.gunModel && followed.gunAnim < 0 && !followed.vmAnimChanged) {
            // the weapon they have as following them begins (a clip, a seek):
            // doing what it does, from when it began, as the end of a reload
            followed.gunModel   = gun->modelindex;
            followed.vmAnim     = vmAnim;
            followed.vmAnimTime = snap->serverTime - (int)(time * 1000);
            followed.vmAnimChanged++;
        } else if (gun->modelindex != followed.gunModel) {
            // another weapon, taken out
            followed.gunModel    = gun->modelindex;
            followed.vmAnim      = VM_ANIM_PULLOUT;
            followed.vmAnimTime  = snap->serverTime;
            followed.pulloutTime = snap->serverTime;
            followed.vmAnimChanged++;
        } else if (anim != followed.gunAnim || time < followed.gunAnimTime) {
            // the gun does something else, or again (a shot after a shot),
            // from when it began
            followed.vmAnim     = vmAnim;
            followed.vmAnimTime = snap->serverTime - (int)(time * 1000);
            followed.vmAnimChanged++;
        } else if (followed.vmAnim == VM_ANIM_PULLOUT && snap->serverTime - followed.pulloutTime > FOLLOWED_PULLOUT_TIME) {
            followed.vmAnim     = VM_ANIM_IDLE;
            followed.vmAnimTime = snap->serverTime;
            followed.vmAnimChanged++;
        }
        followed.gunAnim     = anim;
        followed.gunAnimTime = time;
    }

    ps->iViewModelAnim        = followed.vmAnim;
    ps->iViewModelAnimChanged = followed.vmAnimChanged & 3;
    // as the client tells it from the snapshots before, which it doesn't
    // for these
    snap->vmAnimTime = followed.vmAnimTime;
    snap->snapFlags |= SNAPFLAG_FOLLOWED_FROM_EYES;
}
