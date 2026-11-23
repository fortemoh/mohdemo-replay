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

// cl_effects_logic.h -- the rules that tie Enhanced Rendering (r_temporal,
// the renderer's temporal path) to the effects that need it, kept free of
// engine state so they can be unit-tested (tests/test_effects_logic.cpp).
// cl_effects.cpp applies them. Project: Omaha's (70c35fc5), with r_dof,
// which the Options page here offers too, and without its third rule
// (r_hdrOutput turned on turned r_temporal on): Enhanced Rendering is the
// master switch (notuern's advice), which HDR doesn't turn on.
//
// r_taa, r_ao, r_motionBlur and r_dof do nothing without the temporal path:
//  1. r_temporal turned off turns them off (so rule 2 doesn't turn it back
//     on);
//  2. any of them on turns r_temporal on (from the console or a config:
//     Options' rows are off while it is).
// HDR output works without the path (the 3D view at paper white, no
// expansion); the HDR look needs it. r_saturation and r_hdrExpand play no
// part: their defaults would turn the path on for everyone.

#pragma once

// what the cvars hold now: r_temporal as it will be after the next
// vid_restart (a latched value if there is one)
typedef struct {
    int taa;
    int ao;
    int motionBlur;
    int dof;
    int temporal;
} effectsState_t;

// what the last call left
typedef struct {
    int valid;  // 0: no call yet, the first only takes its baseline
    int temporal;
} effectsLast_t;

// writes to make (Effects_Decide's result)
#define EFFECTS_WRITE_TEMPORAL_ON 1  // r_temporal 1
#define EFFECTS_WRITE_EFFECTS_OFF 2  // r_taa, r_ao, r_motionBlur, r_dof 0

// why r_temporal is turned on
typedef enum {
    EFFECTS_REASON_NONE,
    EFFECTS_REASON_TAA,
    EFFECTS_REASON_AO,
    EFFECTS_REASON_MOTION_BLUR,
    EFFECTS_REASON_DOF
} effectsReason_t;

static inline const char *Effects_ReasonCvar(effectsReason_t reason)
{
    switch (reason) {
    case EFFECTS_REASON_TAA:
        return "r_taa";
    case EFFECTS_REASON_AO:
        return "r_ao";
    case EFFECTS_REASON_MOTION_BLUR:
        return "r_motionBlur";
    case EFFECTS_REASON_DOF:
        return "r_dof";
    default:
        return "";
    }
}

// The writes the rules ask for (EFFECTS_WRITE_*), with the reason r_temporal
// is turned on in *reason (may be NULL). *last becomes the state after them.
static inline int Effects_Decide(const effectsState_t *now, effectsLast_t *last, effectsReason_t *reason)
{
    int             writes   = 0;
    int             temporal = now->temporal != 0;
    int             taa      = now->taa != 0;
    int             ao       = now->ao != 0;
    int             blur     = now->motionBlur != 0;
    int             dof      = now->dof != 0;
    effectsReason_t why      = EFFECTS_REASON_NONE;

    // 1. turned off: the effects go with it
    if (last->valid && last->temporal && !temporal && (taa || ao || blur || dof)) {
        writes |= EFFECTS_WRITE_EFFECTS_OFF;
        taa = ao = blur = dof = 0;
    }

    if (!temporal) {
        // 2. an effect that needs it
        if (taa) {
            why = EFFECTS_REASON_TAA;
        } else if (ao) {
            why = EFFECTS_REASON_AO;
        } else if (blur) {
            why = EFFECTS_REASON_MOTION_BLUR;
        } else if (dof) {
            why = EFFECTS_REASON_DOF;
        }
        if (why != EFFECTS_REASON_NONE) {
            writes |= EFFECTS_WRITE_TEMPORAL_ON;
            temporal = 1;
        }
    }

    last->valid    = 1;
    last->temporal = temporal;
    if (reason) {
        *reason = why;
    }
    return writes;
}
