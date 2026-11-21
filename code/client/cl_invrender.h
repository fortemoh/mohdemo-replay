/*
===========================================================================
Copyright (C) 2023 the OpenMoHAA team

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

#pragma once

void CL_Draw3DModel(
    float     x,
    float     y,
    float     w,
    float     h,
    qhandle_t model,
    vec3_t    origin,
    vec3_t    rotateoffset,
    vec3_t    offset,
    vec3_t    angle,
    vec3_t    color,
    str       anim,
    bool      keepAspect, // Added: the field of view as wide as the frame (30 degrees down)
    bool      studioLight // Added: lit by a light of its own, not the map's where the player is (RDF_STUDIOLIGHT)
);
