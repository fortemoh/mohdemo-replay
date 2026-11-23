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

// AA snapshots must drop entities for distance exactly as stock AA 1.11
// does: past the fog, in every direction, and never on a map without fog.

#include "../sv_farplane.h"

#include <cstdio>

static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

// What SV_SetFarPlane stores (stock MOHAA.exe 0x475180 does the same).
static int StoredFarplane(int farplane)
{
    return farplane ? (farplane + 32) * (farplane + 32) : 0;
}

static int CullsAt(float x, float y, float z, float radius, int farplane)
{
    const float origin[3]   = {100, 200, 50};
    const float centroid[3] = {origin[0] + x, origin[1] + y, origin[2] + z};

    return SV_StockFarplaneCulls(centroid, radius, origin, StoredFarplane(farplane));
}

int main()
{
    failures = 0;

    // A map without fog (mohdm1/6/7, obj_team2/3): never dropped. The
    // Spearhead cull OpenMoHAA used on AA dropped a moving player about 3030
    // units behind at FOV 80; stock keeps it.
    CHECK(!CullsAt(-4000, 0, 0, 0, 0));
    CHECK(!CullsAt(100000, 0, 0, 0, 0));

    // mohdm5's fog of 1800: kept out to 1832, dropped past it (strictly
    // greater), the same in front, behind, to the side and above.
    CHECK(!CullsAt(1832, 0, 0, 0, 1800));
    CHECK(CullsAt(1833, 0, 0, 0, 1800));
    CHECK(!CullsAt(-1832, 0, 0, 0, 1800));
    CHECK(CullsAt(-1833, 0, 0, 0, 1800));
    CHECK(!CullsAt(0, 1832, 0, 0, 1800));
    CHECK(CullsAt(0, -1833, 0, 0, 1800));
    CHECK(CullsAt(0, 0, 1833, 0, 1800));

    // The radius counts as distance squared less radius squared:
    // sqrt(1832^2 + 30^2) = 1832.25.
    CHECK(!CullsAt(1832.2f, 0, 0, 30, 1800));
    CHECK(CullsAt(1832.3f, 0, 0, 30, 1800));

    // A diagonal: (1000, 1000, 1000) is 1732 away: inside mohdm5's 1832 and
    // obj_team1's 4032, outside a fog of 1500 (1532).
    CHECK(!CullsAt(1000, 1000, 1000, 0, 1800));
    CHECK(!CullsAt(1000, 1000, 1000, 0, 4000));
    CHECK(CullsAt(1000, 1000, 1000, 0, 1500));

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("farplane: all checks passed\n");
    return 0;
}
