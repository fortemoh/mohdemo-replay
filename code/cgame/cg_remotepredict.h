/*
===========================================================================
Copyright (C) 2026 Project: Omaha
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

// cg_remotepredict.h: remote prediction (Added; cg_remotepredict.cpp,
// Project: Omaha's): other players drawn where the server will have them
// when a shot fired now is traced (cg_remotePrediction, on by default)

#pragma once

#include "cg_local.h"

#ifdef __cplusplus
extern "C" {
#endif

void CG_RP_RegisterCvars(void);
// a new connection or map: forget all players and snapshots
void CG_RP_Init(void);
// once a frame, after CG_ProcessSnapshots: reads the snapshots that came since
void CG_RP_BeginFrame(void);
// after an entity's stock position is worked out: keeps it in netLerpOrigin
// (for collision) and draws another player where the prediction has them
void CG_RP_UpdateEntity(centity_t *cent);

#ifdef __cplusplus
}
#endif
