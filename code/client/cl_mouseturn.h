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

// cl_mouseturn.h -- the turn distance (cm/360, m_turn) kept when the Field of
// View or the mouse's DPI changes, and CS Mode (m_csmode: CS's turn at Field
// of View 90) (cl_mouseturn_logic.h); Options' MOUSE rows read them

#pragma once

#include "../qcommon/q_shared.h"

// registers the cvars (m_dpi, m_turn, m_csmode, m_cssens)
void  CL_MouseTurn_Init(void);
// every frame: applies the rule to the cvars
void  CL_MouseTurn_Sync(void);
// the mouse's travel for a full turn now, in centimetres (0: none)
float CL_MouseTurn_Distance(void);
