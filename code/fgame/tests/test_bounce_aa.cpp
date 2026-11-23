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

// A bouncing grenade must make the sound stock AA 1.11 makes.

#include "../g_bounceaa.h"

#include <cstdio>

static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

// the MP grenades have all four sounds
static int Grenade(int inWater, int hitBsp, int surfaceFlags)
{
    return G_BounceSoundAA(1, 1, 1, inWater, hitBsp, surfaceFlags);
}

int main()
{
    // brushes by surface type
    CHECK(Grenade(0, 1, SURF_PAPER) == BOUNCE_SOUND_HARD); // the fork played soft
    CHECK(Grenade(0, 1, SURF_WOOD) == BOUNCE_SOUND_HARD);
    CHECK(Grenade(0, 1, SURF_ROCK) == BOUNCE_SOUND_HARD);
    CHECK(Grenade(0, 1, SURF_GLASS) == BOUNCE_SOUND_HARD);
    CHECK(Grenade(0, 1, SURF_METAL) == BOUNCE_SOUND_METAL);
    CHECK(Grenade(0, 1, SURF_GRILL) == BOUNCE_SOUND_METAL);
    CHECK(Grenade(0, 1, SURF_DIRT) == BOUNCE_SOUND_SOFT);
    CHECK(Grenade(0, 1, SURF_GRASS) == BOUNCE_SOUND_SOFT);
    CHECK(Grenade(0, 1, SURF_MUD) == BOUNCE_SOUND_SOFT);
    CHECK(Grenade(0, 1, SURF_SAND) == BOUNCE_SOUND_SOFT);
    CHECK(Grenade(0, 1, SURF_CARPET) == BOUNCE_SOUND_SOFT);
    CHECK(Grenade(0, 1, 0) == BOUNCE_SOUND_SOFT);

    // two types on one face are soft: the type is matched as one value
    CHECK(Grenade(0, 1, SURF_METAL | SURF_ROCK) == BOUNCE_SOUND_SOFT);
    CHECK(Grenade(0, 1, SURF_WOOD | SURF_DIRT) == BOUNCE_SOUND_SOFT);

    // flags outside the type bits don't count
    CHECK(Grenade(0, 1, SURF_METAL | SURF_NOMARKS) == BOUNCE_SOUND_METAL);

    // anything that isn't a brush is soft, whatever the surface
    CHECK(Grenade(0, 0, SURF_METAL) == BOUNCE_SOUND_SOFT);
    CHECK(Grenade(0, 0, SURF_ROCK) == BOUNCE_SOUND_SOFT);

    // water first
    CHECK(Grenade(1, 1, SURF_ROCK) == BOUNCE_SOUND_WATER);
    CHECK(Grenade(1, 0, 0) == BOUNCE_SOUND_WATER);

    // no bouncesound: nothing, not even in water
    CHECK(G_BounceSoundAA(0, 1, 1, 1, 1, SURF_METAL) == BOUNCE_SOUND_NONE);
    CHECK(G_BounceSoundAA(0, 1, 1, 0, 1, SURF_METAL) == BOUNCE_SOUND_NONE);

    // fallbacks: metal to hard to soft
    CHECK(G_BounceSoundAA(1, 1, 0, 0, 1, SURF_METAL) == BOUNCE_SOUND_HARD);
    CHECK(G_BounceSoundAA(1, 0, 1, 0, 1, SURF_ROCK) == BOUNCE_SOUND_SOFT);
    CHECK(G_BounceSoundAA(1, 0, 0, 0, 1, SURF_METAL) == BOUNCE_SOUND_SOFT);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("test_bounce_aa: all passed\n");
    return 0;
}
