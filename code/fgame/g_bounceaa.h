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

// g_bounceaa.h: which sound stock AA 1.11 plays when a bouncing projectile
// (a grenade) touches something, kept pure so it can be tested.
// Projectile::Touch, Linux fgamededmohaa.so 0x2f37a1-0x2f3b33.

#pragma once

#include "../qcommon/surfaceflags.h"

enum bounceSoundAA_e {
    BOUNCE_SOUND_NONE,  // no bouncesound: nothing at all
    BOUNCE_SOUND_SOFT,  // bouncesound
    BOUNCE_SOUND_HARD,  // bouncesound_hard
    BOUNCE_SOUND_METAL, // bouncesound_metal
    BOUNCE_SOUND_WATER, // bouncesound_water
};

//
// Without a bouncesound nothing plays (0x2f37b2). In water (a puddle
// surface, or water, slime or lava at the touch point: & 0x38) the water
// sound plays (0x2f37b4, 0x2f38a1). Otherwise the surface picks only when
// the projectile has a metal or hard sound and the thing touched is a brush
// (SOLID_BSP, 0x2f38d0): the typed part of the surface flags taken as one
// value, so a face with two types is soft; paper, wood, rock and glass are
// hard, metal and grill metal, falling back to hard, then soft.
//
static inline int G_BounceSoundAA(int hasSoft, int hasHard, int hasMetal, int inWater, int hitBsp, int surfaceFlags)
{
    if (!hasSoft) {
        return BOUNCE_SOUND_NONE;
    }

    if (inWater) {
        return BOUNCE_SOUND_WATER;
    }

    if (!(hasMetal || hasHard) || !hitBsp) {
        return BOUNCE_SOUND_SOFT;
    }

    switch (surfaceFlags & MASK_SURF_TYPE) {
    case SURF_METAL:
    case SURF_GRILL:
        if (hasMetal) {
            return BOUNCE_SOUND_METAL;
        }
        // fall through
    case SURF_PAPER:
    case SURF_WOOD:
    case SURF_ROCK:
    case SURF_GLASS:
        return hasHard ? BOUNCE_SOUND_HARD : BOUNCE_SOUND_SOFT;
    default:
        return BOUNCE_SOUND_SOFT;
    }
}
