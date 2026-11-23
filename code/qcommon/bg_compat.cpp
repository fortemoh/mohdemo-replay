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

// bg_compat.cpp

#include "./bg_compat.h"
#include "./qcommon.h"

static size_t CPT_NormalizeConfigstring_ver_6(size_t num)
{
    if (num <= 5 || num >= 26) {
        return num;
    }

    return num - 2;
}

static size_t CPT_DenormalizeConfigstring_ver_6(size_t num)
{
    if (num <= 5 || num >= 26) {
        return num;
    }

    return num + 2;
}

static size_t CPT_NormalizeConfigstring_ver_15(size_t num)
{
    return num;
}

static size_t CPT_DenormalizeConfigstring_ver_15(size_t num)
{
    return num;
}

//
// AA 1.11's pm_flags bits 0 (ducked) and 1 (view prone, which with ducked is
// the 54-high crouch) mean the same as Spearhead's. AA has two more flags at
// bits 2 and 3 that Spearhead removed, so AA bits 4-14 are Spearhead bits
// 2-12. Stock's ClientMove sets 0x3 for the 54-high crouch, 0x2 at height 20,
// 0x40, 0x800 and 0x1000, and its PM_CheckDuck tests (flags & 3) == 3, & 1
// and & 2 (Linux fgamededmohaa.so 0x288988, 0x2889a9, 0x1e3b5d).
//
// OpenMoHAA used to shift bit 1 as well, sending view prone on AA bit 3, so
// stock clients predicted a crouch with the 60-high box. Bits 0 and 1 now
// pass through unchanged and AA's bits 2 and 3 carry nothing, as in stock.
//
#define CPT_PMF_DUCKED     (1 << 0) // PMF_DUCKED in bg_public.h
#define CPT_PMF_VIEW_PRONE (1 << 1) // PMF_VIEW_PRONE in bg_public.h

static uint32_t CPT_NormalizePlayerStateFlags_ver_6(uint32_t flags)
{
    uint32_t normalizedFlags = 0;

    // Convert AA PlayerMove flags to SH/BT flags
    normalizedFlags |= flags & (CPT_PMF_DUCKED | CPT_PMF_VIEW_PRONE);
    for (size_t i = 2; i < 13; i++) {
        if (flags & (1 << (i + 2))) {
            normalizedFlags |= (1 << i);
        }
    }

    for (size_t i = 15; i < 32; i++) {
        if (flags & (1 << i)) {
            normalizedFlags |= (1 << i);
        }
    }

    // So that flags are normalized across modules
    return normalizedFlags;
}

static uint32_t CPT_DenormalizePlayerStateFlags_ver_6(uint32_t flags)
{
    uint32_t normalizedFlags = 0;

    // Convert SH/BT PlayerMove flags to AA flags
    normalizedFlags |= flags & (CPT_PMF_DUCKED | CPT_PMF_VIEW_PRONE);
    for (size_t i = 2; i < 13; i++) {
        if (flags & (1 << i)) {
            normalizedFlags |= (1 << (i + 2));
        }
    }

    for (size_t i = 15; i < 32; i++) {
        if (flags & (1 << i)) {
            normalizedFlags |= (1 << i);
        }
    }

    // So that flags are normalized across modules
    return normalizedFlags;
}

static int CPT_NormalizeViewModelAnim_ver_6(int iViewModelAnim)
{
    return iViewModelAnim + 1;
}

static int CPT_DenormalizeViewModelAnim_ver_6(int iViewModelAnim)
{
    return iViewModelAnim - 1;
}

static uint32_t CPT_NormalizePlayerStateFlags_ver_15(uint32_t flags)
{
    return flags;
}

static uint32_t CPT_DenormalizePlayerStateFlags_ver_15(uint32_t flags)
{
    return flags;
}

static int CPT_NormalizeViewModelAnim_ver_15(int iViewModelAnim)
{
    return iViewModelAnim;
}

static int CPT_DenormalizeViewModelAnim_ver_15(int iViewModelAnim)
{
    return iViewModelAnim;
}

size_t CPT_NormalizeConfigstring(size_t num)
{
    if (com_protocol->integer <= protocol_e::PROTOCOL_MOH) {
        return CPT_NormalizeConfigstring_ver_6(num);
    } else if (com_protocol->integer >= protocol_e::PROTOCOL_MOHTA_MIN) {
        return CPT_NormalizeConfigstring_ver_15(num);
    } else {
        Com_Error(ERR_DROP, "CPT Normalizer: unknown protocol %d", com_protocol->integer);
        return 0;
    }
}

size_t CPT_DenormalizeConfigstring(size_t num)
{
    if (com_protocol->integer <= protocol_e::PROTOCOL_MOH) {
        return CPT_DenormalizeConfigstring_ver_6(num);
    } else if (com_protocol->integer >= protocol_e::PROTOCOL_MOHTA_MIN) {
        return CPT_DenormalizeConfigstring_ver_15(num);
    } else {
        Com_Error(ERR_DROP, "CPT Normalizer: unknown protocol %d", com_protocol->integer);
        return 0;
    }
}

uint32_t CPT_NormalizePlayerStateFlags(uint32_t flags)
{
    if (com_protocol->integer <= protocol_e::PROTOCOL_MOH) {
        return CPT_NormalizePlayerStateFlags_ver_6(flags);
    } else if (com_protocol->integer >= protocol_e::PROTOCOL_MOHTA_MIN) {
        return CPT_NormalizePlayerStateFlags_ver_15(flags);
    } else {
        Com_Error(ERR_DROP, "CPT Normalizer: unknown protocol %d", com_protocol->integer);
        return 0;
    }
}

uint32_t CPT_DenormalizePlayerStateFlags(uint32_t flags)
{
    if (com_protocol->integer <= protocol_e::PROTOCOL_MOH) {
        return CPT_DenormalizePlayerStateFlags_ver_6(flags);
    } else if (com_protocol->integer >= protocol_e::PROTOCOL_MOHTA_MIN) {
        return CPT_DenormalizePlayerStateFlags_ver_15(flags);
    } else {
        Com_Error(ERR_DROP, "CPT Normalizer: unknown protocol %d", com_protocol->integer);
        return 0;
    }
}

int CPT_NormalizeViewModelAnim(int iViewModelAnim)
{
    if (com_protocol->integer <= protocol_e::PROTOCOL_MOH) {
        return CPT_NormalizeViewModelAnim_ver_6(iViewModelAnim);
    } else if (com_protocol->integer >= protocol_e::PROTOCOL_MOHTA_MIN) {
        return CPT_NormalizeViewModelAnim_ver_15(iViewModelAnim);
    } else {
        Com_Error(ERR_DROP, "CPT Normalizer: unknown protocol %d", com_protocol->integer);
        return 0;
    }
}

int CPT_DenormalizeViewModelAnim(int iViewModelAnim)
{
    if (com_protocol->integer <= protocol_e::PROTOCOL_MOH) {
        return CPT_DenormalizeViewModelAnim_ver_6(iViewModelAnim);
    } else if (com_protocol->integer >= protocol_e::PROTOCOL_MOHTA_MIN) {
        return CPT_DenormalizeViewModelAnim_ver_15(iViewModelAnim);
    } else {
        Com_Error(ERR_DROP, "CPT Normalizer: unknown protocol %d", com_protocol->integer);
        return 0;
    }
}
