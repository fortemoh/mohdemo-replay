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

// Crates must wobble, settle and take damage as stock 1.11's func_crate.

#include "../g_crate.h"

#include <cmath>
#include <cstdio>

static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

// the game's means-of-death numbers, which are AA's
enum {
    MOD_EXPLOSION   = 9,
    MOD_GRENADE     = 14,
    MOD_ROCKET      = 16,
    MOD_BULLET      = 18,
    MOD_FAST_BULLET = 19,
    MOD_VEHICLE     = 20,
    MOD_BASH        = 26,
    MOD_SHOTGUN     = 27,
};

static void TestClamp()
{
    // a 64-high crate has scale 1: limit 1.25 either way (SD-047)
    CHECK(G_CrateClampJitter(0.5f, 1) == 0.5f);
    CHECK(G_CrateClampJitter(-0.5f, 1) == -0.5f);
    CHECK(G_CrateClampJitter(2.f, 1) == 1.25f);
    CHECK(G_CrateClampJitter(-2.f, 1) == -1.25f);
    CHECK(G_CrateClampJitter(0, 1) == 0);
    CHECK(G_CrateClampJitter(3.f, 2) == 2.5f);
    CHECK(G_CrateClampJitter(-3.f, 2) == -2.5f);
}

static void TestWobble()
{
    // a crate pushed to the limit swings back and forth, shrinking by a
    // third of the scale each think, and stops (SD-046)
    float j     = 1.25f;
    int   steps = 0;
    float last  = j;

    while (j != 0 && steps < 100) {
        j = G_CrateJitterStep(j, 1);
        CHECK(j * last <= 0); // flips sign
        CHECK(fabsf(j) < fabsf(last));
        last = j;
        steps++;
    }
    CHECK(j == 0);
    CHECK(steps == 4);

    // the decay is 0.3333 times the scale, not 1/3 and not 0
    CHECK(fabsf(G_CrateJitterStep(1.f, 1) - -(1.f - 0.3333f)) < 1e-6f);
    CHECK(fabsf(G_CrateJitterStep(-1.f, 1) - (1.f - 0.3333f)) < 1e-6f);
    CHECK(fabsf(G_CrateJitterStep(1.f, 0.5f) - -(1.f - 0.16665f)) < 1e-6f);

    // it stops at 0 rather than overshooting
    CHECK(G_CrateJitterStep(0.1f, 1) == 0);
    CHECK(G_CrateJitterStep(-0.1f, 1) == 0);
    CHECK(G_CrateJitterStep(0, 1) == 0);
}

static void TestDamage()
{
    // bullets are halved, bashing doubled, the rest whole (SD-048)
    CHECK(G_CrateDamage(30, MOD_BULLET, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 15);
    CHECK(G_CrateDamage(31, MOD_FAST_BULLET, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 15);
    CHECK(G_CrateDamage(40, MOD_BASH, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 80);
    CHECK(G_CrateDamage(200, MOD_GRENADE, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 200);
    CHECK(G_CrateDamage(200, MOD_EXPLOSION, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 200);
    CHECK(G_CrateDamage(200, MOD_ROCKET, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 200);
    CHECK(G_CrateDamage(60, MOD_SHOTGUN, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 60);
    CHECK(G_CrateDamage(60, MOD_VEHICLE, MOD_BULLET, MOD_FAST_BULLET, MOD_BASH) == 60);
}

int main()
{
    TestClamp();
    TestWobble();
    TestDamage();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("test_crate: all passed\n");
    return 0;
}
