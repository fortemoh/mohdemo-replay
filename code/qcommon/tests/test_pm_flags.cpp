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

// AA 1.11 clients must get pm_flags in their own bit layout, or they predict
// a crouch with the wrong box. The ver_6 converters are static, so the
// source is included directly.

#include "../bg_compat.cpp"

#include <cstdio>

cvar_t *com_protocol;

static int failures;

#define CHECK_FLAGS(value, expected)                                                         \
    do {                                                                                     \
        unsigned got = (value);                                                              \
        if (got != (unsigned)(expected)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s = 0x%x, want 0x%x\n", __FILE__, __LINE__, #value, \
                    got, (unsigned)(expected));                                              \
            failures++;                                                                      \
        }                                                                                    \
    } while (0)

int main()
{
    cvar_t protocol = {};

    protocol.integer = PROTOCOL_MOH;
    com_protocol     = &protocol;
    failures         = 0;

    // The 54-high crouch: stock ClientMove sets 0x3 and PM_CheckDuck wants
    // (flags & 3) == 3. Nothing goes out on AA's bits 2 and 3.
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(0x3), 0x3);
    // Height 20: view prone alone, 0x2.
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(0x2), 0x2);
    // The 60-high crouch: ducked alone.
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(0x1), 0x1);

    // Bits 2-12 still shift by two: stock's 0x40, 0x800 and 0x1000 are
    // PMF_NO_PREDICTION (bit 4), PMF_VIEW_DUCK_RUN (bit 9) and
    // PMF_VIEW_JUMP_START (bit 10).
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(1 << 4), 0x40);
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(1 << 9), 0x800);
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(1 << 10), 0x1000);
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(1 << 12), 1 << 14);
    // Bits from 15 up pass through.
    CHECK_FLAGS(CPT_DenormalizePlayerStateFlags(1 << 15), 1 << 15);

    // Receiving: a stock server's 0x3 reads as the 54-high crouch.
    CHECK_FLAGS(CPT_NormalizePlayerStateFlags(0x3), 0x3);
    CHECK_FLAGS(CPT_NormalizePlayerStateFlags(0x2), 0x2);
    CHECK_FLAGS(CPT_NormalizePlayerStateFlags(0x40), 1 << 4);
    CHECK_FLAGS(CPT_NormalizePlayerStateFlags(0x800), 1 << 9);
    CHECK_FLAGS(CPT_NormalizePlayerStateFlags(0x1000), 1 << 10);
    // AA bits 2 and 3 have no Spearhead meaning and are dropped.
    CHECK_FLAGS(CPT_NormalizePlayerStateFlags(0x4), 0);
    CHECK_FLAGS(CPT_NormalizePlayerStateFlags(0x8), 0);

    // Every flag the game sets survives the round trip.
    for (int i = 0; i < 32; i++) {
        unsigned flag = 1u << i;
        if (i == 13 || i == 14) {
            // Spearhead bits 13 and 14 have no AA bit.
            continue;
        }
        CHECK_FLAGS(CPT_NormalizePlayerStateFlags(CPT_DenormalizePlayerStateFlags(flag)), flag);
    }

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("pm_flags: all checks passed\n");
    return 0;
}
