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

// g_spawnpick.h: stock AA 1.11's weighted spawnpoint pick, kept pure so it
// can be tested.

#pragma once

//
// Picks an index into metrics[0..count-1], sorted best first, the way stock
// 1.11's GetRandomSpawnpointFromList does (Linux fgamededmohaa.so 0x20c7f0).
// Only the best 5 count. Each leading positive spot i weighs
// (n - i) * metrics[i], less 0.9 times the last positive spot's weight, so
// with five equal metrics the picks are about 39, 30, 20, 10 and 1%.
// randomBits is rand() & 0x7fff. A best metric of 0 or less returns 0.
//
// Stock runs this on the x87 at 53-bit precision (D-019) and stores only the
// cut and the range as floats; doubles and those two floats match it.
//
static inline int G_PickSpawnIndex(const float *metrics, int count, int randomBits)
{
    double fTotal;
    double fLast;
    double fLeft;
    float  fCut;
    float  fRange;
    int    n;
    int    i;

    if (count <= 0 || metrics[0] <= 0) {
        return 0;
    }

    n = count > 5 ? 5 : count;

    fTotal = (double)metrics[0] * n;
    fLast  = fTotal;
    for (i = 1; i < n; i++) {
        if (metrics[i] <= 0) {
            break;
        }

        fLast = (double)metrics[i] * (n - i);
        fTotal += fLast;
    }

    // i spots are weighted now
    fCut   = (float)(fLast * 0.9f);
    fRange = (float)(fTotal - i * (fLast * 0.9f));
    fLeft  = fRange * ((double)randomBits / 32767.0f);

    for (i = 0; i < n - 1; i++) {
        fLeft -= (n - i) * (double)metrics[i] - fCut;
        if (fLeft <= 0) {
            break;
        }
    }

    return i;
}

//
// Stock 1.11's SpawnpointMetric_Team (Linux fgamededmohaa.so 0x20ca40) once
// the living, non-spectating players other than the one spawning are
// counted: the nearest enemy's distance squared (536895232 with none, the
// constant at 0x3785ac, which every game DLL has; 23170 squared is not it),
// less 1 to 1.25 times 1024 squared, plus a quarter of how much closer than
// that constant the teammates are on average, but only when the enemy term
// is above 0 (the jbe at 0x20cad7), so a spot near an enemy stays bad
// however close the teammates are. randomValue is (rand() & 0x7fff) /
// 32767.
//
#define SPAWN_METRIC_FAR 536895232.0f

static inline float G_TeamSpawnMetric(
    float minEnemyDistSquared, float sumFriendDistSquared, int nFriends, float randomValue
)
{
    float fMetric;

    fMetric = minEnemyDistSquared - (randomValue * 0.25 + 1.0) * 1048576.0f;

    if (nFriends && fMetric > 0) {
        fMetric += (SPAWN_METRIC_FAR - sumFriendDistSquared / nFriends) * 0.25f;
    }

    return fMetric;
}
