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

// Unit tests for Enhanced Rendering's rules (cl_effects_logic.h): Project:
// Omaha's (70c35fc5), with r_dof, without its rule for HDR

#include "../cl_effects_logic.h"

#include <stdio.h>

static int g_failures;

static void expect_int(int value, int expected, const char *message)
{
    if (value != expected) {
        fprintf(stderr, "FAIL: %s (got %d expected %d)\n", message, value, expected);
        g_failures++;
    }
}

static effectsState_t state(int taa, int ao, int blur, int temporal, int dof = 0)
{
    effectsState_t s;

    s.taa        = taa;
    s.ao         = ao;
    s.motionBlur = blur;
    s.dof        = dof;
    s.temporal   = temporal;
    return s;
}

// the state after the writes, as cl_effects.cpp leaves the cvars
static effectsState_t apply(effectsState_t s, int writes)
{
    if (writes & EFFECTS_WRITE_EFFECTS_OFF) {
        s.taa = s.ao = s.motionBlur = s.dof = 0;
    }
    if (writes & EFFECTS_WRITE_TEMPORAL_ON) {
        s.temporal = 1;
    }
    return s;
}

static void test_effects_turn_it_on(void)
{
    effectsLast_t   last = {};
    effectsReason_t why;
    effectsState_t  s;

    expect_int(Effects_Decide(&(s = state(0, 0, 0, 0)), &last, &why), 0, "nothing on: nothing to do");

    expect_int(Effects_Decide(&(s = state(1, 0, 0, 0)), &last, &why), EFFECTS_WRITE_TEMPORAL_ON, "TAA turns it on");
    expect_int(why, EFFECTS_REASON_TAA, "for r_taa");
    expect_int(Effects_Decide(&(s = state(1, 0, 0, 1)), &last, &why), 0, "then it stays");

    last = effectsLast_t{};
    Effects_Decide(&(s = state(0, 0, 0, 0)), &last, NULL);
    expect_int(Effects_Decide(&(s = state(0, 1, 0, 0)), &last, &why), EFFECTS_WRITE_TEMPORAL_ON, "AO turns it on");
    expect_int(why, EFFECTS_REASON_AO, "for r_ao");

    last = effectsLast_t{};
    Effects_Decide(&(s = state(0, 0, 0, 0)), &last, NULL);
    expect_int(Effects_Decide(&(s = state(0, 0, 1, 0)), &last, &why), EFFECTS_WRITE_TEMPORAL_ON, "motion blur turns it on");
    expect_int(why, EFFECTS_REASON_MOTION_BLUR, "for r_motionBlur");

    // a config with an effect on and the path off: the first call (before
    // the renderer starts) turns it on
    last = effectsLast_t{};
    expect_int(Effects_Decide(&(s = state(0, 1, 0, 0)), &last, &why), EFFECTS_WRITE_TEMPORAL_ON, "the first call turns it on for an effect");
}

static void test_off_takes_the_effects(void)
{
    effectsLast_t  last = {};
    effectsState_t s    = state(1, 1, 1, 1);
    int            writes;

    Effects_Decide(&s, &last, NULL);
    // the player turns Enhanced Rendering off
    s.temporal = 0;
    writes     = Effects_Decide(&s, &last, NULL);
    expect_int(writes, EFFECTS_WRITE_EFFECTS_OFF, "off turns the effects off, and doesn't turn itself back on");
    s = apply(s, writes);
    expect_int(Effects_Decide(&s, &last, NULL), 0, "nor on the next call");

    // the first call has no last state: an effect on wins over the path off
    last = effectsLast_t{};
    expect_int(Effects_Decide(&(s = state(1, 0, 0, 0)), &last, NULL), EFFECTS_WRITE_TEMPORAL_ON, "no transition on the first call");

    // off with no effect on: nothing to turn off
    last = effectsLast_t{};
    Effects_Decide(&(s = state(0, 0, 0, 1)), &last, NULL);
    expect_int(Effects_Decide(&(s = state(0, 0, 0, 0)), &last, NULL), 0, "off alone writes nothing");
}

// r_dof, which the Options page here offers (not in Omaha's rules)
static void test_dof(void)
{
    effectsLast_t   last = {};
    effectsReason_t why;
    effectsState_t  s;
    int             writes;

    Effects_Decide(&(s = state(0, 0, 0, 0)), &last, NULL);
    expect_int(Effects_Decide(&(s = state(0, 0, 0, 0, 1)), &last, &why), EFFECTS_WRITE_TEMPORAL_ON, "depth of field turns it on");
    expect_int(why, EFFECTS_REASON_DOF, "for r_dof");

    // on with the path, then the path off: it goes too
    s      = state(0, 0, 0, 1, 1);
    Effects_Decide(&s, &last, NULL);
    s.temporal = 0;
    writes     = Effects_Decide(&s, &last, NULL);
    expect_int(writes, EFFECTS_WRITE_EFFECTS_OFF, "off turns depth of field off");
    s = apply(s, writes);
    expect_int(s.dof, 0, "r_dof 0");
    expect_int(Effects_Decide(&s, &last, NULL), 0, "and stays off");

    // TAA before depth of field in the reason
    last = effectsLast_t{};
    Effects_Decide(&(s = state(0, 0, 0, 0)), &last, NULL);
    Effects_Decide(&(s = state(1, 0, 0, 0, 1)), &last, &why);
    expect_int(why, EFFECTS_REASON_TAA, "the first effect named");
}

int main(void)
{
    test_effects_turn_it_on();
    test_off_takes_the_effects();
    test_dof();

    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_effects_logic: all passed\n");
    return 0;
}
