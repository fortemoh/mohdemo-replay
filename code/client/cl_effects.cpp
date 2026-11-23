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

// cl_effects.cpp -- Enhanced Rendering (r_temporal) and its effects, from
// Options, the console and the config alike (the rules are in
// cl_effects_logic.h). Project: Omaha's (70c35fc5), with r_dof and without
// its rule for HDR.
//
// r_temporal is read by the renderer only when it starts
// (VK_TemporalCheckDevice), so it is written at once, latch or not, and
// takes effect at the next vid_restart: before each start (CL_InitRef) the
// rules run first, so the renderer never starts with an effect on and the
// path off. Options' APPLY sees the latched value and restarts the video.

#include "client.h"
#include "cl_effects.h"
#include "cl_effects_logic.h"

static effectsLast_t effects_last;
static int           effects_startedTemporal;

// the cvar's value, or the latched one waiting for vid_restart; 0 before
// anything made it (the renderer registers them). Found once: cvars are
// never freed
static int CL_Effects_Value(cvar_t **var, const char *name, qboolean latched)
{
    if (!*var) {
        *var = Cvar_FindVar(name);
        if (!*var) {
            return 0;
        }
    }
    if (latched && (*var)->latchedString) {
        return atoi((*var)->latchedString);
    }
    return (*var)->integer;
}

void CL_Effects_Sync(effectsSync_t when)
{
    static cvar_t  *taa, *ao, *blur, *dof, *temporal;
    effectsState_t  now;
    effectsReason_t reason;
    int             writes;

    now.taa        = CL_Effects_Value(&taa, "r_taa", qfalse);
    now.ao         = CL_Effects_Value(&ao, "r_ao", qfalse);
    now.motionBlur = CL_Effects_Value(&blur, "r_motionBlur", qfalse);
    now.dof        = CL_Effects_Value(&dof, "r_dof", qfalse);
    now.temporal   = CL_Effects_Value(&temporal, "r_temporal", qtrue);

    writes = Effects_Decide(&now, &effects_last, &reason);

    if (writes & EFFECTS_WRITE_EFFECTS_OFF) {
        Cvar_Set("r_taa", "0");
        Cvar_Set("r_ao", "0");
        Cvar_Set("r_motionBlur", "0");
        Cvar_Set("r_dof", "0");
        if (when == EFFECTS_SYNC_FRAME) {
            Com_Printf("Enhanced Rendering off (r_temporal 0): r_taa, r_ao, r_motionBlur and r_dof turned off\n");
        }
    }
    if (writes & EFFECTS_WRITE_TEMPORAL_ON) {
        Cvar_Set("r_temporal", "1");
        if (when == EFFECTS_SYNC_FRAME) {
            // running already when it was only waiting to go off
            Com_Printf(
                effects_startedTemporal ? "Enhanced Rendering (r_temporal 1) kept on for %s\n"
                                        : "Enhanced Rendering (r_temporal 1) turned on for %s; vid_restart to start it\n",
                Effects_ReasonCvar(reason)
            );
        }
    }
}

void CL_Effects_RendererStarted(void)
{
    const cvar_t *temporal = Cvar_FindVar("r_temporal");

    effects_startedTemporal = temporal && temporal->integer;
}
