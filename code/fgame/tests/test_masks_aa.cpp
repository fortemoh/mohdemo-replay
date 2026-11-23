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

// The game module's masks must be AA 1.11's on an AA server (no fence bit
// in MASK_SOLID and its four siblings) and keep the fence bit for
// Spearhead and Breakthrough.

#ifndef GAME_DLL
#    define GAME_DLL
#endif

#include "qcommon/q_shared.h"
#include "fgame/bg_public.h"

#include <cstdio>

int g_protocol;

static int failures;

#define CHECK_MASK(mask, expected)                                                                \
    do {                                                                                          \
        unsigned int got = (unsigned int)(mask);                                                  \
        if (got != (unsigned int)(expected)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s = 0x%x at protocol %d, want 0x%x\n", __FILE__, __LINE__, \
                    #mask, got, g_protocol, (unsigned int)(expected));                            \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

int main()
{
    // AA 1.11's values, as found in the Linux fgamededmohaa.so
    for (int protocol = PROTOCOL_MOH_MIN; protocol < PROTOCOL_MOHTA_MIN; protocol++) {
        g_protocol = protocol;
        CHECK_MASK(MASK_SOLID, 0x2000b01);      // 44 times (MeleeAttack 0x2f1933)
        CHECK_MASK(MASK_CANSEE, 0x2040b19);     // 16 times
        CHECK_MASK(MASK_TRANSITION, 0x40919);   // PassesTransitionConditions_Disguise 0x1487a2
        CHECK_MASK(MASK_AI_CANSEE, 0x40919);
        CHECK_MASK(MASK_CLICKITEM, 0x2001b01);  // ClickItemAttack 0x2f652e
        CHECK_MASK(MASK_GUNTARGET, 0x46001b9b); // Player::GunTarget 0x297268

        // these have the fence bit in AA too
        CHECK_MASK(MASK_PLAYERSOLID, 0x42012b01);
        CHECK_MASK(MASK_SHOT_TRIG, 0x42042ba1);
    }

    // Spearhead and Breakthrough keep the fence bit
    for (int protocol = PROTOCOL_MOHTA_MIN; protocol <= PROTOCOL_MOHTA; protocol++) {
        g_protocol = protocol;
        CHECK_MASK(MASK_SOLID, 0x2002b01);
        CHECK_MASK(MASK_CANSEE, 0x2042b19);
        CHECK_MASK(MASK_TRANSITION, 0x42919);
        CHECK_MASK(MASK_AI_CANSEE, 0x42919);
        CHECK_MASK(MASK_CLICKITEM, 0x2003b01);
        CHECK_MASK(MASK_GUNTARGET, 0x46003b9b);
    }

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("test_masks_aa: all passed\n");
    return 0;
}
