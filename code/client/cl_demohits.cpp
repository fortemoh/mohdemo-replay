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

#include "cl_demohits.h"

#include <math.h>

// A shot hits if its line goes through this around the player's origin, wide
// as they moved since, and a player's own start this close to it
#define HIT_BOX_SIDE   40.f
#define HIT_BOX_BOTTOM -16.f
#define HIT_BOX_TOP    112.f
#define OWN_SHOT_DIST  24.f
// An explosion that far hurts no one
#define EXPLOSION_DIST 1024.f
// The server sends nothing for a melee hit, the only ones that have the
// direction their attacker faces; falls, with no direction, do 5 or 10
#define MELEE_DIST   128.f
#define MELEE_DAMAGE 10
// A player's middle above their origin, where an explosion aims (RadiusDamage)
#define CENTROID_HEIGHT 40.f

static float HorizontalDistance(const vec3_t a, const vec3_t b)
{
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]));
}

static qboolean ThroughBox(const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs)
{
    float from = 0, to = 1;
    int   i;

    for (i = 0; i < 3; i++) {
        float delta = end[i] - start[i];
        if (delta == 0) {
            if (start[i] < mins[i] || start[i] > maxs[i]) {
                return qfalse;
            }
        } else {
            float enter = (mins[i] - start[i]) / delta;
            float leave = (maxs[i] - start[i]) / delta;
            if (enter > leave) {
                float swap = enter;
                enter      = leave;
                leave      = swap;
            }
            from = enter > from ? enter : from;
            to   = leave < to ? leave : to;
            if (from > to) {
                return qfalse;
            }
        }
    }
    return qtrue;
}

static qboolean NormalizedDir(const vec3_t from, const vec3_t to, vec3_t dir)
{
    float length;

    VectorSubtract(to, from, dir);
    length = sqrtf(DotProduct(dir, dir));
    if (length == 0) {
        return qfalse;
    }
    VectorScale(dir, 1 / length, dir);
    return qtrue;
}

qboolean DemoHits_Direction(
    const vec3_t origin,
    int damage,
    const demoHitSource_t *sources,
    int numSources,
    const demoHitPlayer_t *others,
    int numOthers,
    vec3_t dir
)
{
    vec3_t mins, maxs, centroid;
    float  best, dist;
    int    i;

    VectorClear(dir);

    VectorSet(mins, origin[0] - HIT_BOX_SIDE, origin[1] - HIT_BOX_SIDE, origin[2] + HIT_BOX_BOTTOM);
    VectorSet(maxs, origin[0] + HIT_BOX_SIDE, origin[1] + HIT_BOX_SIDE, origin[2] + HIT_BOX_TOP);

    // A shot through them, not theirs; the one from the furthest if more,
    // as theirs can start further than it should when they move
    best = -1;
    for (i = 0; i < numSources; i++) {
        const demoHitSource_t *source = &sources[i];

        if (source->explosion || HorizontalDistance(source->start, origin) < OWN_SHOT_DIST
            || !ThroughBox(source->start, source->end, mins, maxs)) {
            continue;
        }
        dist = Distance(source->start, origin);
        if (dist > best && NormalizedDir(source->start, source->end, dir)) {
            best = dist;
        }
    }
    if (best >= 0) {
        return qtrue;
    }

    // The nearest explosion
    VectorSet(centroid, origin[0], origin[1], origin[2] + CENTROID_HEIGHT);
    best = EXPLOSION_DIST;
    for (i = 0; i < numSources; i++) {
        if (sources[i].explosion) {
            dist = Distance(sources[i].start, origin);
            if (dist < best && NormalizedDir(sources[i].start, centroid, dir)) {
                best = dist;
            }
        }
    }
    if (best < EXPLOSION_DIST) {
        return qtrue;
    }

    // The melee of the nearest player
    if (damage > MELEE_DAMAGE) {
        const demoHitPlayer_t *nearest = NULL;

        best = MELEE_DIST;
        for (i = 0; i < numOthers; i++) {
            dist = HorizontalDistance(others[i].origin, origin);
            if (dist < best) {
                best    = dist;
                nearest = &others[i];
            }
        }
        if (nearest) {
            VectorSet(dir, cosf(DEG2RAD(nearest->yaw)), sinf(DEG2RAD(nearest->yaw)), 0);
            return qtrue;
        }
    }

    return qfalse;
}

void DemoHits_Clear(demoHitFeedback_t *feedback)
{
    feedback->count = 0;
    feedback->alpha = 0;
    VectorClear(feedback->from);
    VectorClear(feedback->angles);
}

void DemoHits_Fade(demoHitFeedback_t *feedback, int frames)
{
    int i;

    for (i = 0; i < frames; i++) {
        if (feedback->count) {
            feedback->count *= 0.8f;
            VectorScale(feedback->from, 0.8f, feedback->from);
            VectorScale(feedback->angles, 0.8f, feedback->angles);
            if (feedback->count < 0.1f) {
                feedback->count = 0;
                VectorClear(feedback->from);
            }
        }
        feedback->alpha -= 0.06f;
        if (feedback->alpha < 0) {
            feedback->alpha = 0;
        }
    }
}

static float Clamped(float value, float limit)
{
    return value < -limit ? -limit : (value > limit ? limit : value);
}

void DemoHits_Hit(demoHitFeedback_t *feedback, int damage, const vec3_t dir, float yaw)
{
    vec3_t from;
    float  length, forward, left, sinYaw, cosYaw;

    VectorMA(feedback->from, damage, dir, feedback->from);
    VectorCopy(feedback->from, from);
    length = sqrtf(DotProduct(from, from));
    if (length > 0) {
        VectorScale(from, 1 / length, from);
    }

    sinYaw  = sinf(DEG2RAD(yaw));
    cosYaw  = cosf(DEG2RAD(yaw));
    forward = from[0] * cosYaw + from[1] * sinYaw;
    left    = -from[0] * sinYaw + from[1] * cosYaw;

    // As demos of the game have it: the roll goes with the side too
    feedback->angles[PITCH] = Clamped(feedback->angles[PITCH] - forward * damage * 0.3f, 30);
    feedback->angles[YAW]   = Clamped(feedback->angles[YAW] - left * damage * 0.3f, 30);
    feedback->angles[ROLL]  = Clamped(feedback->angles[ROLL] + left * damage * 0.15f, 25);

    feedback->count += damage;
    feedback->alpha += (damage < 10 ? 10 : damage) * 0.001f;
    if (feedback->alpha < 0.2f) {
        feedback->alpha = 0.2f;
    }
    if (feedback->alpha > 0.6f) {
        feedback->alpha = 0.6f;
    }
}

void DemoHits_ViewAngles(const demoHitFeedback_t *feedback, vec3_t angles)
{
    if (feedback->count) {
        VectorCopy(feedback->angles, angles);
    } else {
        VectorClear(angles);
    }
}

int DemoHits_DirStat(const vec3_t dir, int last)
{
    float yaw = 0;
    int   stat;

    // Vector::toYaw, whole degrees
    if (dir[0] != 0 || dir[1] != 0) {
        yaw = (float)(int)(atan2f(dir[1], dir[0]) * 180 / M_PI);
        if (yaw < 0) {
            yaw += 360;
        }
    }

    stat = (int)(yaw * 10);
    if (stat == last) {
        stat += stat < 1800 ? 1 : -1;
    }
    return stat;
}
