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

// g_ladder.h: where stock 1.11 puts a player getting onto a ladder, kept
// pure so it can be tested. FuncLadder::PositionOnLadder, Linux
// fgamededmohaa.so 0x26b410.

#pragma once

enum ladderMount_e {
    LADDER_MOUNT_TOP,    // head above the ladder: 26 units out, 2 above its top
    LADDER_MOUNT_BOTTOM, // within 32 of its bottom: 29 units back, 2 above it
    LADDER_MOUNT_RUNG,   // in between: 16 units back, on a 16-unit rung
};

static inline int G_LadderMount(float userZ, float userMaxsZ, float ladderMinZ, float ladderMaxZ)
{
    if (userZ + userMaxsZ > ladderMaxZ) {
        return LADDER_MOUNT_TOP;
    }
    if (ladderMinZ + 32.0f > userZ) {
        return LADDER_MOUNT_BOTTOM;
    }
    return LADDER_MOUNT_RUNG;
}

//
// The rung height, (int)(z + 8) & ~15 (0x26b994-0x26b9b6), the same rule
// AdjustPositionOnLadder keeps the player on.
//
static inline float G_LadderRungHeight(float userZ)
{
    return (float)((int)(userZ + 8.0f) & ~15);
}
