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

// bg_lean.h: one frame of a held lean button, kept pure so it can be tested.

#pragma once

//
// Stock AA 1.11's PmoveSingle (Linux fgamededmohaa.so 0x1e3f8a-0x1e3fe5 for
// the left lean, 0x1e4598-0x1e45de for the right; the client game has the
// same code) steps the lean by the larger of leanAdd times the frame's share
// of what is left and a minimum of leanSpeed per second, then clamps it to
// leanMax. AA's values are 40, 10 and 4.
//
static inline float PM_LeanLeft(float lean, float frametime, float leanMax, float leanAdd, float leanSpeed)
{
    float step;
    float minStep;

    if (lean <= -leanMax) {
        return -leanMax;
    }

    step    = frametime * (-leanMax - lean) * leanAdd;
    minStep = frametime * -leanSpeed;
    if (step > minStep) {
        step = minStep;
    }

    lean += step;
    if (lean < -leanMax) {
        lean = -leanMax;
    }

    return lean;
}

static inline float PM_LeanRight(float lean, float frametime, float leanMax, float leanAdd, float leanSpeed)
{
    float step;
    float minStep;

    if (lean >= leanMax) {
        return leanMax;
    }

    step    = frametime * (leanMax - lean) * leanAdd;
    minStep = frametime * leanSpeed;
    if (step < minStep) {
        step = minStep;
    }

    lean += step;
    if (lean > leanMax) {
        lean = leanMax;
    }

    return lean;
}
