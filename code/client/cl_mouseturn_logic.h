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

// cl_mouseturn_logic.h -- how far the view turns for the mouse, the turn
// distance (cm/360) kept when the Field of View changes, and CS Mode, free
// of engine state so they can be unit-tested (tests/test_mouseturn.cpp).
// cl_mouseturn.cpp applies the rules to the cvars.
//
// A count of the mouse turns the view sensitivity * m_yaw * fov_y / 75
// degrees (cl_input.cpp's CL_MouseLook times the cgame's CG_SensitivityScale,
// cg.zoomSensitivity = fov_y / 75), fov_y the view's vertical field of view:
// cg_fov is the horizontal one of a 4:3 screen, so fov_y is
// 2 atan(3/4 tan(cg_fov / 2)) whatever the screen (cg_view.c, CG_CalcFov).
// Unlike CS's, the turn grows with the Field of View: cg_fov 80 turns 0.858
// of sensitivity * m_yaw a count, 90 0.983, 120 1.398. Zoomed, fov_y is
// the server's zoom's (not the turn distance's business: hip fire only).
//
// The turn distance, m_turn: 0 not kept (the game's way: the Field of View
// changes the turn). Set, it is kept:
//  1. m_turn set: the sensitivity for it;
//  2. else the sensitivity set: m_turn its distance;
//  3. else the Field of View or the mouse's DPI changed: the sensitivity
//     for m_turn again (the hand moves as far for a turn).
//
// CS Mode, m_csmode: CS's turn (1.6, Source, CS:GO, CS2: sensitivity x
// 0.022 degrees a count at any Field of View) with m_cssens, CS's
// sensitivity, at CS's Field of View, 90 (CS's 90 is the horizontal one of
// a 4:3 screen, as cg_fov). Scoped, the game's own turn (the owner's
// choice; CS's rule, zoom FOV / 90, turns about 9% faster).
//  a. turned on: cg_fov and the sensitivity saved (m_csmode_fov,
//     m_csmode_sens), cg_fov 90, the sensitivity CS's (x 1.01709 at 90);
//  b. on, m_turn set: the sensitivity for it, m_cssens from it;
//  c. on, m_cssens set, or cg_fov or m_yaw changed (cg_fov forced back to
//     90): the sensitivity CS's again;
//  d. on, the sensitivity set: m_cssens from it;
//     m_turn (kept) follows the sensitivity, the DPI changing it too (CS's
//     sensitivity is what counts here);
//  e. turned off: cg_fov and the sensitivity back to the saved ones, unless
//     either was set at the same time (Options' DEFAULTS, a config); m_turn
//     (kept) follows.

#pragma once

#include <math.h>

#define MOUSETURN_CM_INCH 2.54

// fov_y / 75 at a cg_fov: the cgame's scale of the turn (unzoomed)
static inline double MouseTurn_FovScale(double fov)
{
    const double rad = 3.14159265358979323846 / 180.0;

    return 2.0 * atan(0.75 * tan(fov * rad / 2.0)) / rad / 75.0;
}

// degrees a count turns the view
static inline double MouseTurn_Degrees(double sens, double yaw, double fov)
{
    return sens * fabs(yaw) * MouseTurn_FovScale(fov);
}

// centimetres of mouse travel for a full turn at dpi counts an inch (0: none)
static inline double MouseTurn_Cm(double sens, double yaw, double fov, double dpi)
{
    const double degrees = MouseTurn_Degrees(sens, yaw, fov);

    return degrees > 0.0 && dpi > 0.0 ? 360.0 / degrees / dpi * MOUSETURN_CM_INCH : 0.0;
}

// CS's degrees a count (m_yaw) and Field of View
#define MOUSETURN_CS_YAW 0.022
#define MOUSETURN_CS_FOV 90.0

// the sensitivity for a full turn in cm centimetres (0: none)
static inline double MouseTurn_Sens(double cm, double yaw, double fov, double dpi)
{
    const double perSens = MouseTurn_Degrees(1.0, yaw, fov);

    return cm > 0.0 && dpi > 0.0 && perSens > 0.0 ? 360.0 * MOUSETURN_CM_INCH / (cm * dpi * perSens) : 0.0;
}

// the sensitivity turning as CS's cs does (0: none)
static inline double MouseTurn_FromCS(double cs, double yaw, double fov)
{
    const double perSens = MouseTurn_Degrees(1.0, yaw, fov);

    return perSens > 0.0 ? cs * MOUSETURN_CS_YAW / perSens : 0.0;
}

// CS's sensitivity turning as sens does
static inline double MouseTurn_ToCS(double sens, double yaw, double fov)
{
    return MouseTurn_Degrees(sens, yaw, fov) / MOUSETURN_CS_YAW;
}

// what the cvars hold now, and which changed since the last sync (not by it)
typedef struct {
    double sens, yaw, fov, dpi;
    double turn;                // m_turn: 0 not kept
    int    csMode;              // m_csmode
    double csSens;              // m_cssens
    double savedFov, savedSens; // m_csmode_fov, m_csmode_sens: before CS Mode (0: none)
    int    sensChanged, fovChanged, dpiChanged, turnChanged, csModeChanged, csSensChanged; // fovChanged: m_yaw too
} mouseTurnState_t;

// the values to write (MouseTurn_Decide's result: MOUSETURN_WRITE_*)
typedef struct {
    double sens, turn, fov, csSens, savedFov, savedSens;
} mouseTurnOut_t;

#define MOUSETURN_WRITE_SENS   1  // sensitivity
#define MOUSETURN_WRITE_TURN   2  // m_turn
#define MOUSETURN_WRITE_FOV    4  // cg_fov
#define MOUSETURN_WRITE_CSSENS 8  // m_cssens
#define MOUSETURN_WRITE_SAVED  16 // m_csmode_fov, m_csmode_sens

static inline int MouseTurn_Decide(const mouseTurnState_t *now, mouseTurnOut_t *out)
{
    const int kept   = now->turn > 0.0 && now->dpi > 0.0;
    int       writes = 0;
    double    sens   = now->sens, fov = now->fov;

    if (now->csMode) {
        // (m_turn set: as typed, the rest from it)
        const int turnSet = !now->csModeChanged && now->turnChanged && kept;
        int       follow  = now->sensChanged || now->dpiChanged || now->fovChanged;

        if (now->csModeChanged) {
            // a.
            out->savedFov  = fov;
            out->savedSens = sens;
            writes |= MOUSETURN_WRITE_SAVED;
        }
        if (turnSet) {
            // b.
            sens        = MouseTurn_Sens(now->turn, now->yaw, MOUSETURN_CS_FOV, now->dpi);
            out->csSens = MouseTurn_ToCS(sens, now->yaw, MOUSETURN_CS_FOV);
            writes |= MOUSETURN_WRITE_CSSENS;
        } else if (now->csModeChanged || now->csSensChanged || now->fovChanged || fov != MOUSETURN_CS_FOV) {
            // a., c.
            sens = MouseTurn_FromCS(now->csSens, now->yaw, MOUSETURN_CS_FOV);
        } else if (now->sensChanged) {
            // d.
            out->csSens = MouseTurn_ToCS(sens, now->yaw, MOUSETURN_CS_FOV);
            writes |= MOUSETURN_WRITE_CSSENS;
        }
        if (fov != MOUSETURN_CS_FOV) {
            fov      = MOUSETURN_CS_FOV;
            out->fov = fov;
            writes |= MOUSETURN_WRITE_FOV;
        }
        if (sens != now->sens && sens > 0.0) {
            out->sens = sens;
            writes |= MOUSETURN_WRITE_SENS;
            follow = 1;
        }
        if (kept && follow && !turnSet) {
            out->turn = MouseTurn_Cm(sens, now->yaw, fov, now->dpi);
            writes |= MOUSETURN_WRITE_TURN;
        }
        return writes;
    }
    if (now->csModeChanged && !now->sensChanged && !now->fovChanged && now->savedFov > 0.0 && now->savedSens > 0.0) {
        // e.
        out->fov  = now->savedFov;
        out->sens = now->savedSens;
        writes |= MOUSETURN_WRITE_FOV | MOUSETURN_WRITE_SENS;
        if (kept) {
            out->turn = MouseTurn_Cm(out->sens, now->yaw, out->fov, now->dpi);
            writes |= MOUSETURN_WRITE_TURN;
        }
        return writes;
    }
    if (!kept) {
        return 0;
    }
    if (now->turnChanged || ((now->fovChanged || now->dpiChanged) && !now->sensChanged)) {
        // 1., 3.
        out->sens = MouseTurn_Sens(now->turn, now->yaw, fov, now->dpi);
        return out->sens > 0.0 ? MOUSETURN_WRITE_SENS : 0;
    }
    if (now->sensChanged) {
        // 2.
        out->turn = MouseTurn_Cm(sens, now->yaw, fov, now->dpi);
        return out->turn > 0.0 ? MOUSETURN_WRITE_TURN : 0;
    }
    return 0;
}
