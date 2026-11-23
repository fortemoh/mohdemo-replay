/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/

// g_bulletaa.h: stock AA 1.11's BulletAttack rules, kept pure so they can be
// tested. Read in the Linux fgamededmohaa.so, BulletAttack at 0x2f4990.

#pragma once

#include "../qcommon/surfaceflags.h"

//
// The impact message stock sends for a bullet hitting an entity other than
// the world and the shooter, in AA numbering (both AA clients treat 7 and 8
// as flesh; stock sends 7).
//
#define BULLET_AA_IMPACT_NONE    0
#define BULLET_AA_IMPACT_SURFACE 6 // written with the trace's plane normal
#define BULLET_AA_IMPACT_FLESH   7 // written with the bullet's direction

//
// An entity that takes damage gets Damage() first, then nothing on a clay
// pigeon, else 6 for any typed surface (surfaceFlags & 0xfffe000, 0x2f5119),
// else 7 for a sentient hit with a location (0x2f512d), else nothing. There
// is no solid check: a dying player still gets its flesh impact. One that
// doesn't take damage gets 6 only when it is SOLID_BSP and not a clay pigeon
// (0x2f5a27).
//
static inline int G_BulletImpactAA(
    int takedamage, int surfaceFlags, int contents, int sentient, int location, int solidBsp
)
{
    if (contents & CONTENTS_CLAYPIDGEON) {
        return BULLET_AA_IMPACT_NONE;
    }

    if (takedamage) {
        if (surfaceFlags & MASK_SURF_TYPE) {
            return BULLET_AA_IMPACT_SURFACE;
        }
        if (location >= 0 && sentient) {
            return BULLET_AA_IMPACT_FLESH;
        }
        return BULLET_AA_IMPACT_NONE;
    }

    return solidBsp ? BULLET_AA_IMPACT_SURFACE : BULLET_AA_IMPACT_NONE;
}

//
// Stock travels one hop of 9216 units (add 0x2400 at 0x2f4d10, loop bound
// 0x23ff at 0x2f527e); the owner keeps OpenMoHAA's 16216 (SD-015), so it
// isn't here.
//

//
// After a hit (fraction < 1) the bullet goes on through paper, puddle,
// glass, foliage and snow surfaces (0x6602000, 0x2f5153), clay pigeons and
// water (contents & 0xa0), a trace that started in solid, and, for a large
// bullet, any damageable entity that isn't a brush model. Every pass, for
// any reason, counts: the sixth ends the bullet (cmp 4; jg at 0x2f51b0, the
// count is reset per bullet at 0x2f4cd0 and raised at 0x2f5256). The next
// trace starts 1 unit past the hit (endpos + dir, 0x2f51bd-0x2f5234).
//
#define BULLET_AA_PASS_SURFACES (SURF_PAPER | SURF_PUDDLE | SURF_GLASS | SURF_FOLIAGE | SURF_SNOW)
#define BULLET_AA_PASS_CONTENTS (CONTENTS_CLAYPIDGEON | CONTENTS_WATER)
#define BULLET_AA_MAX_PASSES    5
#define BULLET_AA_PASS_STEP     1

static inline int G_BulletPassesAA(
    int surfaceFlags, int contents, int startsolid, int bulletlarge, int hitDamageableModel
)
{
    return (surfaceFlags & BULLET_AA_PASS_SURFACES) || (contents & BULLET_AA_PASS_CONTENTS) || startsolid
        || (bulletlarge && hitDamageableModel);
}
