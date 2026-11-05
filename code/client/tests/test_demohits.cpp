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

// Where the hits of a player a demo follows came from, and what they do

#include "../cl_demohits.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static demoHitSource_t Shot(float x, float y, float z, float toX, float toY, float toZ)
{
    demoHitSource_t source = {};

    VectorSet(source.start, x, y, z);
    VectorSet(source.end, toX, toY, toZ);
    return source;
}

static demoHitSource_t Explosion(float x, float y, float z)
{
    demoHitSource_t source = {};

    VectorSet(source.start, x, y, z);
    source.explosion = qtrue;
    return source;
}

static void TestShots()
{
    const vec3_t    origin = {100, 100, 0};
    vec3_t          dir;
    demoHitSource_t sources[3];

    // nothing: a fall, no direction
    CHECK(!DemoHits_Direction(origin, 5, NULL, 0, NULL, 0, dir));
    CHECK(dir[0] == 0 && dir[1] == 0 && dir[2] == 0);

    // through them, going on beyond them
    sources[0] = Shot(1100, 100, 40, -900, 100, 40);
    CHECK(DemoHits_Direction(origin, 20, sources, 1, NULL, 0, dir));
    CHECK(Near(dir[0], -1) && Near(dir[1], 0) && Near(dir[2], 0));

    // past them
    sources[0] = Shot(1100, 300, 40, -900, 300, 40);
    CHECK(!DemoHits_Direction(origin, 5, sources, 1, NULL, 0, dir));

    // their own, from their eye
    sources[0] = Shot(100, 100, 82, 1100, 100, 82);
    CHECK(!DemoHits_Direction(origin, 5, sources, 1, NULL, 0, dir));

    // theirs starting further than it should, and the one that hit them
    sources[0] = Shot(130, 100, 82, 1100, 100, 82);
    sources[1] = Shot(100, 1100, 60, 100, 50, 60);
    CHECK(DemoHits_Direction(origin, 20, sources, 2, NULL, 0, dir));
    CHECK(Near(dir[0], 0) && Near(dir[1], -1));
}

static void TestExplosions()
{
    const vec3_t    origin = {0, 0, 0};
    vec3_t          dir;
    demoHitSource_t sources[3];

    // to their middle, the nearest
    sources[0] = Explosion(900, 0, 40);
    sources[1] = Explosion(-100, 0, 40);
    CHECK(DemoHits_Direction(origin, 40, sources, 2, NULL, 0, dir));
    CHECK(Near(dir[0], 1) && Near(dir[1], 0) && Near(dir[2], 0));

    // too far
    sources[0] = Explosion(2000, 0, 40);
    CHECK(!DemoHits_Direction(origin, 5, sources, 1, NULL, 0, dir));

    // a shot goes first
    sources[0] = Explosion(-100, 0, 40);
    sources[1] = Shot(0, 500, 40, 0, -500, 40);
    CHECK(DemoHits_Direction(origin, 40, sources, 2, NULL, 0, dir));
    CHECK(Near(dir[1], -1));
}

static void TestMelee()
{
    const vec3_t    origin = {0, 0, 0};
    vec3_t          dir;
    demoHitPlayer_t others[2] = {};

    VectorSet(others[0].origin, 300, 0, 0);
    others[0].yaw = 0;
    VectorSet(others[1].origin, 50, 0, 0);
    others[1].yaw = 90;

    // as the nearest player faces
    CHECK(DemoHits_Direction(origin, 30, NULL, 0, others, 2, dir));
    CHECK(Near(dir[0], 0) && Near(dir[1], 1));

    // falling next to them
    CHECK(!DemoHits_Direction(origin, 10, NULL, 0, others, 2, dir));

    // no one near
    CHECK(!DemoHits_Direction(origin, 30, NULL, 0, others, 1, dir));
}

static void TestFeedback()
{
    demoHitFeedback_t feedback;
    const vec3_t      fromFront = {-1, 0, 0};
    const vec3_t      fromLeft  = {0, -1, 0};
    const vec3_t      none      = {0, 0, 0};
    vec3_t            angles;
    int               i;

    // looking along x: up, as the view takes these away
    DemoHits_Clear(&feedback);
    DemoHits_Hit(&feedback, 30, fromFront, 0);
    DemoHits_ViewAngles(&feedback, angles);
    CHECK(Near(angles[PITCH], 9) && Near(angles[YAW], 0) && Near(angles[ROLL], 0));
    CHECK(Near(feedback.alpha, 0.2f));

    // looking along y, the same shot comes from their right
    DemoHits_Clear(&feedback);
    DemoHits_Hit(&feedback, 30, fromFront, 90);
    DemoHits_ViewAngles(&feedback, angles);
    CHECK(Near(angles[PITCH], 0) && Near(angles[YAW], -9) && Near(angles[ROLL], 4.5f));

    // from their left
    DemoHits_Clear(&feedback);
    DemoHits_Hit(&feedback, 30, fromLeft, 0);
    DemoHits_ViewAngles(&feedback, angles);
    CHECK(Near(angles[PITCH], 0) && Near(angles[YAW], 9) && Near(angles[ROLL], -4.5f));

    // going, each server frame
    DemoHits_Fade(&feedback, 1);
    DemoHits_ViewAngles(&feedback, angles);
    CHECK(Near(angles[YAW], 7.2f) && Near(angles[ROLL], -3.6f));
    CHECK(Near(feedback.alpha, 0.14f));

    // gone, the view straight when nothing is left to fade
    DemoHits_Fade(&feedback, 25);
    DemoHits_ViewAngles(&feedback, angles);
    CHECK(angles[PITCH] == 0 && angles[YAW] == 0 && angles[ROLL] == 0);
    CHECK(feedback.alpha == 0);

    // a fall: a flash, no kick
    DemoHits_Clear(&feedback);
    DemoHits_Hit(&feedback, 5, none, 0);
    DemoHits_ViewAngles(&feedback, angles);
    CHECK(angles[PITCH] == 0 && angles[YAW] == 0 && angles[ROLL] == 0);
    CHECK(Near(feedback.alpha, 0.2f));

    // not further than it goes
    DemoHits_Clear(&feedback);
    for (i = 0; i < 20; i++) {
        DemoHits_Hit(&feedback, 100, fromFront, 0);
    }
    DemoHits_ViewAngles(&feedback, angles);
    CHECK(Near(angles[PITCH], 30));
    CHECK(Near(feedback.alpha, 0.6f));
}

static void TestDirStat()
{
    const vec3_t east  = {1, 0, 0};
    const vec3_t north = {0, 1, 0};
    const vec3_t south = {0, -1, 0};
    const vec3_t west  = {-1, 0, 0};
    const vec3_t none  = {0, 0, 0};
    const vec3_t aside = {0.999f, 0.0349f, 0};

    CHECK(DemoHits_DirStat(east, -1) == 0);
    CHECK(DemoHits_DirStat(north, -1) == 900);
    CHECK(DemoHits_DirStat(south, -1) == 2700);
    CHECK(DemoHits_DirStat(west, -1) == 1800);
    CHECK(DemoHits_DirStat(none, -1) == 0);
    // whole degrees
    CHECK(DemoHits_DirStat(aside, -1) == 20);
    // it always changes
    CHECK(DemoHits_DirStat(north, 900) == 901);
    CHECK(DemoHits_DirStat(south, 2700) == 2699);
    CHECK(DemoHits_DirStat(none, 0) == 1);
}

int main()
{
    TestShots();
    TestExplosions();
    TestMelee();
    TestFeedback();
    TestDirStat();

    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("demo hits ok\n");
    return 0;
}
