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

// Spawnpoints must be picked as randomly as stock AA 1.11 picks them.
// Every possible rand() & 0x7fff value is tried, so the shares are exact.

#include "../g_spawnpick.h"

#include <cmath>
#include <cstdio>

static int failures;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                       \
        }                                                                     \
    } while (0)

#define CHECK_SHARE(metrics, index, expected)                                                \
    do {                                                                                     \
        double got = Share(metrics, sizeof(metrics) / sizeof(metrics[0]), index);            \
        if (fabs(got - (expected)) > 0.002) {                                                \
            fprintf(stderr, "FAIL %s:%d: share of spot %d = %.4f, want %.4f\n", __FILE__,     \
                    __LINE__, index, got, (double)(expected));                               \
            failures++;                                                                      \
        }                                                                                    \
    } while (0)

// The share of all 32768 random values that pick spot index.
static double Share(const float *metrics, int count, int index)
{
    int hits = 0;

    for (int bits = 0; bits <= 0x7fff; bits++) {
        int picked = G_PickSpawnIndex(metrics, count, bits);
        if (picked < 0 || picked >= (count > 5 ? 5 : count)) {
            fprintf(stderr, "FAIL: picked %d out of range\n", picked);
            failures++;
            return -1;
        }
        if (picked == index) {
            hits++;
        }
    }

    return hits / 32768.0;
}

int main()
{
    failures = 0;

    // Five equal spots: weights 4.1, 3.1, 2.1, 1.1, 0.1 (of 10.5). The
    // decompiled loop gave spot 0 every time.
    {
        const float equal[] = {100, 100, 100, 100, 100};
        CHECK_SHARE(equal, 0, 4.1 / 10.5);
        CHECK_SHARE(equal, 1, 3.1 / 10.5);
        CHECK_SHARE(equal, 2, 2.1 / 10.5);
        CHECK_SHARE(equal, 3, 1.1 / 10.5);
        CHECK_SHARE(equal, 4, 0.1 / 10.5);
    }

    // 10/8/6/4/2: weights 48.2, 30.2, 16.2, 6.2, 0.2 (of 101).
    {
        const float falling[] = {10, 8, 6, 4, 2};
        CHECK_SHARE(falling, 0, 48.2 / 101);
        CHECK_SHARE(falling, 1, 30.2 / 101);
        CHECK_SHARE(falling, 2, 16.2 / 101);
        CHECK_SHARE(falling, 3, 6.2 / 101);
    }

    // Only the best 5 of more spots count.
    {
        const float many[] = {100, 100, 100, 100, 100, 100, 100, 100};
        CHECK_SHARE(many, 0, 4.1 / 10.5);
        CHECK_SHARE(many, 4, 0.1 / 10.5);
    }

    // Two positive spots of five: weights 50 - 18 and 20 - 18 (of 34).
    {
        const float two[] = {10, 5, 0, -3, -9};
        CHECK_SHARE(two, 0, 32.0 / 34);
        CHECK_SHARE(two, 1, 2.0 / 34);
        CHECK_SHARE(two, 2, 0);
    }

    // One spot, or a best metric of 0 or less: spot 0.
    {
        const float one[]  = {42};
        const float none[] = {0, -1, -2};
        CHECK_SHARE(one, 0, 1.0);
        CHECK_SHARE(none, 0, 1.0);
    }

    // The ends of the random range stay in range.
    {
        const float equal[] = {7, 7, 7};
        CHECK(G_PickSpawnIndex(equal, 3, 0) == 0);
        CHECK(G_PickSpawnIndex(equal, 3, 0x7fff) >= 0);
        CHECK(G_PickSpawnIndex(equal, 3, 0x7fff) <= 2);
    }

    // Team metric (SD-030): stock's far constant, not 23170 squared.
    CHECK(SPAWN_METRIC_FAR == 536895232.0f);
    CHECK(SPAWN_METRIC_FAR != 23170.f * 23170.f);

    // An enemy 500 units away: the spot stays below 0 however close the
    // teammates are (the fork added their term anyway and made it the best).
    {
        float nearEnemy = G_TeamSpawnMetric(500.f * 500.f, 0, 3, 0.5f);
        CHECK(nearEnemy < 0);
        CHECK(nearEnemy == (float)(250000.0 - 1.125 * 1048576.0));
    }

    // No enemy near: teammates at 100 units on average add their term.
    {
        float far    = G_TeamSpawnMetric(SPAWN_METRIC_FAR, 3 * 10000.f, 3, 0);
        float expect = (float)(SPAWN_METRIC_FAR - 1048576.0);
        expect += (SPAWN_METRIC_FAR - 10000.f) * 0.25f;
        CHECK(far == expect);
    }

    // No teammates: the enemy term alone, with the random 1 to 1.25.
    CHECK(G_TeamSpawnMetric(4000.f * 4000.f, 0, 0, 1.f) == (float)(16000000.0 - 1.25 * 1048576.0));

    // A metric of exactly 0 gets no teammates' term (jbe).
    CHECK(G_TeamSpawnMetric(1048576.f, 5.f, 1, 0) == 0);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("spawn pick: all checks passed\n");
    return 0;
}
