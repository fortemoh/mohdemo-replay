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

// Allied Assault bullets must follow stock 1.11's BulletAttack rules.

#include "../g_bulletaa.h"

#include <cstdio>

static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static void TestImpacts()
{
    // the mask stock tests at 0x2f5119
    CHECK(MASK_SURF_TYPE == 0xfffe000);

    // a player: flesh with a location, nothing without one
    CHECK(G_BulletImpactAA(1, 0, CONTENTS_BODY, 1, 0, 0) == BULLET_AA_IMPACT_FLESH);
    CHECK(G_BulletImpactAA(1, 0, CONTENTS_BODY, 1, -1, 0) == BULLET_AA_IMPACT_NONE);

    // a typed surface on anything that takes damage, sentient or not
    CHECK(G_BulletImpactAA(1, SURF_WOOD, CONTENTS_SOLID, 0, -1, 1) == BULLET_AA_IMPACT_SURFACE);
    CHECK(G_BulletImpactAA(1, SURF_METAL, CONTENTS_BBOX, 0, -1, 0) == BULLET_AA_IMPACT_SURFACE);
    CHECK(G_BulletImpactAA(1, SURF_CARPET, CONTENTS_BODY, 1, 2, 0) == BULLET_AA_IMPACT_SURFACE);

    // an untyped face of a damageable prop: nothing, BSP or not (SD-002)
    CHECK(G_BulletImpactAA(1, 0, CONTENTS_SOLID, 0, -1, 1) == BULLET_AA_IMPACT_NONE);
    CHECK(G_BulletImpactAA(1, 0, CONTENTS_SOLID, 0, -1, 0) == BULLET_AA_IMPACT_NONE);

    // a prop that takes no damage: a surface impact only when it is BSP
    CHECK(G_BulletImpactAA(0, 0, CONTENTS_SOLID, 0, -1, 1) == BULLET_AA_IMPACT_SURFACE);
    CHECK(G_BulletImpactAA(0, SURF_ROCK, CONTENTS_SOLID, 0, -1, 0) == BULLET_AA_IMPACT_NONE);
    CHECK(G_BulletImpactAA(0, 0, CONTENTS_BODY, 1, 0, 0) == BULLET_AA_IMPACT_NONE);

    // clay pigeons never get one
    CHECK(G_BulletImpactAA(1, SURF_WOOD, CONTENTS_CLAYPIDGEON, 0, -1, 1) == BULLET_AA_IMPACT_NONE);
    CHECK(G_BulletImpactAA(1, 0, CONTENTS_CLAYPIDGEON, 1, 0, 0) == BULLET_AA_IMPACT_NONE);
    CHECK(G_BulletImpactAA(0, 0, CONTENTS_CLAYPIDGEON, 0, -1, 1) == BULLET_AA_IMPACT_NONE);
}

static void TestPasses()
{
    // the masks and limits stock uses (0x2f5153, 0x2f515c, 0x2f51b0)
    CHECK(BULLET_AA_PASS_SURFACES == 0x6602000);
    CHECK(BULLET_AA_PASS_CONTENTS == 0xa0);
    CHECK(BULLET_AA_MAX_PASSES == 5);
    CHECK(BULLET_AA_PASS_STEP == 1);

    // snow passes in AA (SD-016), as do glass, foliage, paper and puddles
    CHECK(G_BulletPassesAA(SURF_SNOW, CONTENTS_SOLID, 0, 0, 0));
    CHECK(G_BulletPassesAA(SURF_GLASS, CONTENTS_SOLID, 0, 0, 0));
    CHECK(G_BulletPassesAA(SURF_FOLIAGE, CONTENTS_SOLID, 0, 0, 0));
    CHECK(G_BulletPassesAA(SURF_PAPER, CONTENTS_SOLID, 0, 0, 0));
    CHECK(G_BulletPassesAA(SURF_PUDDLE, CONTENTS_SOLID, 0, 0, 0));

    // water, clay pigeons and a trace that started in solid
    CHECK(G_BulletPassesAA(0, CONTENTS_WATER, 0, 0, 0));
    CHECK(G_BulletPassesAA(0, CONTENTS_CLAYPIDGEON, 0, 0, 0));
    CHECK(G_BulletPassesAA(SURF_ROCK, CONTENTS_SOLID, 1, 0, 0));

    // a large bullet through a damageable model, a small one stops
    CHECK(G_BulletPassesAA(0, CONTENTS_BODY, 0, 1, 1));
    CHECK(!G_BulletPassesAA(0, CONTENTS_BODY, 0, 0, 1));
    CHECK(!G_BulletPassesAA(0, CONTENTS_BODY, 0, 1, 0));

    // wood, metal, rock and the rest stop it
    CHECK(!G_BulletPassesAA(SURF_WOOD, CONTENTS_SOLID, 0, 0, 0));
    CHECK(!G_BulletPassesAA(SURF_METAL | SURF_GRILL, CONTENTS_SOLID, 0, 1, 0));
    CHECK(!G_BulletPassesAA(SURF_ROCK | SURF_DIRT | SURF_CARPET, CONTENTS_SOLID, 0, 0, 0));
    CHECK(!G_BulletPassesAA(0, CONTENTS_SOLID, 0, 0, 0));
}

int main()
{
    TestImpacts();
    TestPasses();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("test_bullet_aa: all passed\n");
    return 0;
}
