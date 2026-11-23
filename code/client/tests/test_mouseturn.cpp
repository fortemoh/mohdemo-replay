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

// Unit tests for the mouse's turn, the turn distance kept and CS Mode (cl_mouseturn_logic.h). The degrees a count turns were
// measured in the game (2026-11-21: 1000 counts sent as the mouse's, cl.viewangles read before and after)

#include "../cl_mouseturn_logic.h"

#include <math.h>
#include <stdio.h>

static int g_failures;

static void expect_near(double value, double expected, double tolerance, const char *message)
{
    if (!(fabs(value - expected) <= tolerance)) {
        fprintf(stderr, "FAIL: %s (got %.6f expected %.6f)\n", message, value, expected);
        g_failures++;
    }
}

static void expect_int(int value, int expected, const char *message)
{
    if (value != expected) {
        fprintf(stderr, "FAIL: %s (got %d expected %d)\n", message, value, expected);
        g_failures++;
    }
}

static mouseTurnState_t state(double sens, double fov, double dpi, double turn)
{
    mouseTurnState_t s;

    s.sens        = sens;
    s.yaw         = 0.022;
    s.fov         = fov;
    s.dpi         = dpi;
    s.turn        = turn;
    s.csMode      = 0;
    s.csSens      = 1.25;
    s.savedFov    = s.savedSens = 0;
    s.sensChanged = s.fovChanged = s.dpiChanged = s.turnChanged = s.csModeChanged = s.csSensChanged = 0;
    return s;
}

int main(void)
{
    mouseTurnOut_t o;

    // the cgame's scale (CG_SensitivityScale, measured: 0.85822, 0.98320, 1.39762)
    expect_near(MouseTurn_FovScale(80), 0.85822, 0.00001, "fov_y / 75 at cg_fov 80");
    expect_near(MouseTurn_FovScale(90), 0.98320, 0.00001, "fov_y / 75 at cg_fov 90");
    expect_near(MouseTurn_FovScale(120), 1.39762, 0.00001, "fov_y / 75 at cg_fov 120");
    // the degrees 1000 counts turned in the game
    expect_near(1000 * MouseTurn_Degrees(5, 0.022, 80), 94.4041, 0.0001, "sensitivity 5, cg_fov 80");
    expect_near(1000 * MouseTurn_Degrees(5, 0.022, 90), 108.1517, 0.0001, "sensitivity 5, cg_fov 90");
    expect_near(1000 * MouseTurn_Degrees(1, 0.022, 120), 30.7477, 0.0001, "sensitivity 1, cg_fov 120");
    // the turn distance: 360 / 0.0944041 counts at 800 an inch, 2.54 cm an inch
    expect_near(MouseTurn_Cm(5, 0.022, 80, 800), 12.1075, 0.0001, "12.1 cm a turn at 5, cg_fov 80, 800 DPI");
    expect_near(MouseTurn_Sens(MouseTurn_Cm(3.7, 0.022, 95, 1600), 0.022, 95, 1600), 3.7, 1e-9, "the sensitivity for a distance, back");
    expect_near(MouseTurn_Cm(5, 0.022, 80, 0), 0, 0, "no DPI: no distance");
    expect_near(MouseTurn_Sens(30, 0.022, 80, 0), 0, 0, "no DPI: no sensitivity");

    // not kept (m_turn 0): nothing, whatever changed
    {
        mouseTurnState_t s = state(5, 100, 800, 0);

        s.fovChanged = s.sensChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), 0, "not kept: nothing");
    }
    // 1. the distance set: the sensitivity for it
    {
        mouseTurnState_t s = state(5, 80, 800, 30);

        s.turnChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_SENS, "m_turn set: sensitivity");
        expect_near(MouseTurn_Cm(o.sens, 0.022, 80, 800), 30, 1e-9, "m_turn set: the sensitivity turns 30 cm");
    }
    // 2. the sensitivity set while kept: the distance follows
    {
        mouseTurnState_t s = state(2, 80, 800, 30);

        s.sensChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_TURN, "sensitivity set: m_turn");
        expect_near(o.turn, MouseTurn_Cm(2, 0.022, 80, 800), 1e-9, "sensitivity set: its distance");
    }
    // 3. the Field of View changed while kept: the hand moves as far for a turn
    {
        mouseTurnState_t s = state(MouseTurn_Sens(30, 0.022, 80, 800), 110, 800, 30);

        s.fovChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_SENS, "FOV changed: sensitivity");
        expect_near(MouseTurn_Cm(o.sens, 0.022, 110, 800), 30, 1e-9, "FOV 80 -> 110: still 30 cm");
        expect_near(o.sens, MouseTurn_Sens(30, 0.022, 80, 800) * MouseTurn_FovScale(80) / MouseTurn_FovScale(110), 1e-9, "FOV 80 -> 110: the sensitivity lower");
    }
    // ... and the DPI
    {
        mouseTurnState_t s = state(4, 80, 1600, 30);

        s.dpiChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_SENS, "DPI changed: sensitivity");
        expect_near(MouseTurn_Cm(o.sens, 0.022, 80, 1600), 30, 1e-9, "DPI 800 -> 1600: still 30 cm");
    }
    // the sensitivity and the Field of View at once (Options' DEFAULTS, a config): the sensitivity wins
    {
        mouseTurnState_t s = state(5, 80, 800, 30);

        s.sensChanged = s.fovChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_TURN, "sensitivity and FOV: m_turn follows");
    }
    // nothing changed: nothing
    {
        mouseTurnState_t s = state(5, 80, 800, 30);

        expect_int(MouseTurn_Decide(&s, &o), 0, "nothing changed");
    }

    // CS: a count turns sensitivity x 0.022 at any FOV; at cg_fov 90 MOHAA's sensitivity is CS's x 75 / 73.740 (measured:
    // CS 2 as 2.0342 turned 44.0004 degrees a 1000 counts)
    expect_near(MouseTurn_FromCS(2, 0.022, 90), 2.0342, 0.0001, "CS 2 at cg_fov 90");
    expect_near(1000 * MouseTurn_Degrees(MouseTurn_FromCS(2, 0.022, 90), 0.022, 90), 44.0, 1e-9, "CS 2: 44 degrees a 1000 counts");
    expect_near(MouseTurn_ToCS(MouseTurn_FromCS(1.25, 0.022, 90), 0.022, 90), 1.25, 1e-12, "CS and back");
    // a. turned on: cg_fov and the sensitivity saved, cg_fov 90, CS's sensitivity
    {
        mouseTurnState_t s = state(5, 80, 800, 0);

        s.csMode = s.csModeChanged = 1;
        s.csSens = 2;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_SAVED | MOUSETURN_WRITE_FOV | MOUSETURN_WRITE_SENS, "CS Mode on: saved, FOV, sensitivity");
        expect_near(o.savedFov, 80, 0, "CS Mode on: FOV saved");
        expect_near(o.savedSens, 5, 0, "CS Mode on: sensitivity saved");
        expect_near(o.fov, 90, 0, "CS Mode on: FOV 90");
        expect_near(o.sens, MouseTurn_FromCS(2, 0.022, 90), 1e-12, "CS Mode on: CS's sensitivity");
    }
    // ... with the distance kept: it follows
    {
        mouseTurnState_t s = state(5, 80, 800, 30);

        s.csMode = s.csModeChanged = 1;
        s.csSens = 2;
        expect_int(MouseTurn_Decide(&s, &o) & MOUSETURN_WRITE_TURN, MOUSETURN_WRITE_TURN, "CS Mode on, kept: m_turn follows");
        expect_near(o.turn, MouseTurn_Cm(o.sens, 0.022, 90, 800), 1e-9, "CS Mode on, kept: CS's distance");
    }
    // b. on, m_turn set: the sensitivity for it, m_cssens from it
    {
        mouseTurnState_t s = state(MouseTurn_FromCS(2, 0.022, 90), 90, 800, 40);

        s.csMode = 1;
        s.csSens = 2;
        s.turnChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_SENS | MOUSETURN_WRITE_CSSENS, "CS Mode, m_turn set: sensitivity, m_cssens");
        expect_near(MouseTurn_Cm(o.sens, 0.022, 90, 800), 40, 1e-9, "CS Mode, m_turn set: 40 cm");
        expect_near(o.csSens, MouseTurn_ToCS(o.sens, 0.022, 90), 1e-12, "CS Mode, m_turn set: m_cssens its");
    }
    // c. on, m_cssens set: the sensitivity; cg_fov changed: forced back to 90
    {
        mouseTurnState_t s = state(MouseTurn_FromCS(2, 0.022, 90), 90, 800, 0);

        s.csMode = 1;
        s.csSens = 3;
        s.csSensChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_SENS, "CS Mode, m_cssens set: sensitivity");
        expect_near(o.sens, MouseTurn_FromCS(3, 0.022, 90), 1e-12, "CS Mode, m_cssens 3");
        s        = state(MouseTurn_FromCS(2, 0.022, 90), 100, 800, 0);
        s.csMode = 1;
        s.csSens = 2;
        s.fovChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_FOV, "CS Mode, FOV set: back to 90, the sensitivity as it was");
        expect_near(o.fov, 90, 0, "CS Mode: FOV 90");
    }
    // d. on, the sensitivity set: m_cssens from it
    {
        mouseTurnState_t s = state(3, 90, 800, 0);

        s.csMode = 1;
        s.csSens = 2;
        s.sensChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_CSSENS, "CS Mode, sensitivity set: m_cssens");
        expect_near(o.csSens, MouseTurn_ToCS(3, 0.022, 90), 1e-12, "CS Mode, sensitivity 3: its CS");
    }
    // ... the DPI changed with the distance kept: CS's sensitivity stays, the distance follows
    {
        mouseTurnState_t s = state(MouseTurn_FromCS(2, 0.022, 90), 90, 1600, 30);

        s.csMode = 1;
        s.csSens = 2;
        s.dpiChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_TURN, "CS Mode, DPI changed: m_turn only");
        expect_near(o.turn, MouseTurn_Cm(MouseTurn_FromCS(2, 0.022, 90), 0.022, 90, 1600), 1e-9, "CS Mode, DPI 1600: the distance");
    }
    // e. turned off: cg_fov and the sensitivity back
    {
        mouseTurnState_t s = state(MouseTurn_FromCS(2, 0.022, 90), 90, 800, 0);

        s.csModeChanged = 1;
        s.savedFov      = 80;
        s.savedSens     = 5;
        expect_int(MouseTurn_Decide(&s, &o), MOUSETURN_WRITE_FOV | MOUSETURN_WRITE_SENS, "CS Mode off: restored");
        expect_near(o.fov, 80, 0, "CS Mode off: FOV 80 again");
        expect_near(o.sens, 5, 0, "CS Mode off: sensitivity 5 again");
        // ... not when they were set at the same time (Options' DEFAULTS)
        s.sensChanged = s.fovChanged = 1;
        expect_int(MouseTurn_Decide(&s, &o), 0, "CS Mode off with DEFAULTS: nothing restored");
    }

    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_mouseturn: all passed\n");
    return 0;
}
