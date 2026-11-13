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

// Added in OPM
//  The health and ammo of the players a demo shows, but for the recorder's: a
//  demo doesn't have them, so they're worked out from all it has of each
//  player, from its start

#pragma once

#include "cl_demoindex.h"

typedef struct {
    int health;     // -1 when not known
    int weaponItem; // CS_WEAPONS index of the gun in their hand, -1 for none known
    int clip;
    int clipSize;
    int ammo; // besides the clip
    int maxAmmo;
} demoHudState_t;

// the index of the demo playing, NULL for none (cl_main.cpp)
const demoIndex_t *CL_DemoIndex(void);

void CL_DemoHudClear(void);
void CL_DemoHudModelsChanged(void);
void CL_DemoHudSnapshot(const clSnapshot_t *snap);
int  CL_DemoHudPained(int *clients, vec3_t *origins);
void CL_DemoHudHit(int client, int damage);
void CL_DemoHudState(int client, demoHudState_t *state);
