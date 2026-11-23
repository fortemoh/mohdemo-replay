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

// cl_effects.h -- Enhanced Rendering (r_temporal) kept on while an effect
// needs it, off with its effects (cl_effects_logic.h). Project: Omaha's
// (70c35fc5)

#pragma once

#include "../qcommon/q_shared.h"

typedef enum {
    EFFECTS_SYNC_START, // CL_InitRef, before the renderer starts (every
                        // vid_restart, Options' APPLY too): quiet
    EFFECTS_SYNC_FRAME  // every frame: says on the console what it changed
} effectsSync_t;

// applies the rules to the cvars
void CL_Effects_Sync(effectsSync_t when);
// the renderer has started: r_temporal's value then is the running one
void CL_Effects_RendererStarted(void);
