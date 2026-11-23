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

// A player getting onto a ladder must be put where stock 1.11 puts them.

#include "../g_ladder.h"

#include <cstdio>

static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

int main()
{
    // a ladder from z 0 to 256, a standing player 94 high
    CHECK(G_LadderMount(170.f, 94.f, 0, 256) == LADDER_MOUNT_TOP);
    CHECK(G_LadderMount(162.f, 94.f, 0, 256) == LADDER_MOUNT_RUNG);
    CHECK(G_LadderMount(31.f, 94.f, 0, 256) == LADDER_MOUNT_BOTTOM);
    CHECK(G_LadderMount(32.f, 94.f, 0, 256) == LADDER_MOUNT_RUNG);

    // mid-way the height snaps to the nearest 16-unit rung (SD-070)
    CHECK(G_LadderRungHeight(100.f) == 96.f);
    CHECK(G_LadderRungHeight(103.9f) == 96.f);
    CHECK(G_LadderRungHeight(104.f) == 112.f);
    CHECK(G_LadderRungHeight(128.f) == 128.f);
    CHECK(G_LadderRungHeight(-20.f) == -16.f); // (int)-12 & ~15

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("test_ladder: all passed\n");
    return 0;
}
