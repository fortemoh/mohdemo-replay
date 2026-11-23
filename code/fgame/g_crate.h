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

// g_crate.h: stock 1.11's func_crate wobble and damage rules, kept pure so
// they can be tested. Read in the Linux fgamededmohaa.so.

#pragma once

//
// A hit adds to the pitch and roll jitter, which is clamped to 1.25 times
// the jitter scale either way (0x378018; CrateDamaged 0x2017cb-0x201854 for
// the crate hit, TellNeighborsToJitter 0x201fb1-0x20203f for each neighbour,
// with the crate that was hit's scale).
//
static inline float G_CrateClampJitter(float jitter, float jitterScale)
{
    float limit = jitterScale * 1.25f;

    if (jitter > limit) {
        return limit;
    }
    if (-limit > jitter) {
        return -limit;
    }
    return jitter;
}

//
// Each 0.05 s think the crate is drawn at its start angles plus the jitter,
// then the jitter moves 0.3333 times the scale towards 0 (0x378020), stops
// at 0, and flips sign (CrateThink 0x2020e1-0x2021bf).
//
static inline float G_CrateJitterStep(float jitter, float jitterScale)
{
    float decay = 0.3333f * jitterScale;

    if (jitter < 0) {
        jitter += decay;
        if (jitter > 0) {
            jitter = 0;
        }
    } else if (jitter > 0) {
        jitter -= decay;
        if (jitter < 0) {
            jitter = 0;
        }
    }

    return jitter * -1.0f;
}

//
// Only bullets do half damage, and bashing double (CrateDamaged
// 0x2018bd-0x2019a4: mod - 18 <= 1, mod == 26); explosions, grenades and
// the rest do their full damage. The MOD numbers are AA's, which the game's
// enum keeps (MOD_BULLET, MOD_FAST_BULLET, MOD_BASH).
//
static inline int G_CrateDamage(int damage, int meansOfDeath, int modBullet, int modFastBullet, int modBash)
{
    if (meansOfDeath == modBullet || meansOfDeath == modFastBullet) {
        return damage / 2;
    }
    if (meansOfDeath == modBash) {
        return damage * 2;
    }
    return damage;
}
