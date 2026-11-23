/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/

// sv_farplane.h: AA 1.11's snapshot fog cull, kept pure so it can be tested.

#pragma once

//
// True when stock AA 1.11 leaves an entity out of a snapshot for distance.
// With a farplane set (sv.farplane, which SV_SetFarPlane stores as
// (farplane + 32) squared, 0 without fog), the entity is dropped when its
// centroid's distance squared from the view origin, less its radius
// squared, passes it. The caller passes the parent's centroid and radius
// when the entity has a parent. No facing, FOV or clamp.
//
// Read in all three stock engines: MOHAA.exe 0x479158-0x4791da,
// MOHAA_server.exe from 0x34798, mohaa_lnxded 0x808e3ff-0x808e45c. They do
// it on the x87 and compare against the integer with a strict "greater";
// doubles match that.
//
static inline int SV_StockFarplaneCulls(const float *centroid, float radius, const float *origin, int farplaneSquared)
{
    double dx;
    double dy;
    double dz;
    double r;

    if (!farplaneSquared) {
        return 0;
    }

    dx = (double)centroid[0] - origin[0];
    dy = (double)centroid[1] - origin[1];
    dz = (double)centroid[2] - origin[2];
    r  = radius;

    return dx * dx + dy * dy + dz * dz - r * r > (double)farplaneSquared;
}
