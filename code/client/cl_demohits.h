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

// The hits of a player a demo follows: a demo has their health, not where the
// hits came from nor what they did to the view, so those are worked out from
// what else the server sent with the health drop

#pragma once

#include "../qcommon/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    vec3_t   start;
    vec3_t   end;
    qboolean explosion; // at start, without an end; else a shot from start to end
} demoHitSource_t;

typedef struct {
    vec3_t origin;
    float  yaw;
} demoHitPlayer_t;

// what Player::DamageFeedback keeps
typedef struct {
    float  count;
    vec3_t from;
    vec3_t angles;
    float  alpha;
} demoHitFeedback_t;

// The direction a hit went, from what came with it, given the player's origin.
// Returns qfalse, with no direction, if there is none, as for a fall.
qboolean DemoHits_Direction(
    const vec3_t origin,
    int damage,
    const demoHitSource_t *sources,
    int numSources,
    const demoHitPlayer_t *others,
    int numOthers,
    vec3_t dir
);

void DemoHits_Clear(demoHitFeedback_t *feedback);
// server frames going by
void DemoHits_Fade(demoHitFeedback_t *feedback, int frames);
// a hit of a player looking at yaw; a direction of zero doesn't move the view
void DemoHits_Hit(demoHitFeedback_t *feedback, int damage, const vec3_t dir, float yaw);
// the view kick to show
void DemoHits_ViewAngles(const demoHitFeedback_t *feedback, vec3_t angles);
// STAT_DAMAGEDIR after a hit, given what it was: it always changes, as the HUD flashes on a change
int DemoHits_DirStat(const vec3_t dir, int last);

#ifdef __cplusplus
}
#endif
