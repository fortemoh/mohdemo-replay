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

// cl_uiextras.h: the Competitive HUD's extras (Added): the zoom overlays, the status icons, the lagometer, the centre and location
// prints, the chat's typing line, the round's end banner and the quick messages' menu

#pragma once

#include "cl_uidraw.h"

void UI_Extras_Init(void);
// the zoom overlay drawn (the cgame's: 0 a scope, 1 the Kar98's, 2 the spy
// camera's, 3 the binoculars'), faded by alpha
void UI_DrawZoomOverlay(int type, float alpha);
// a status icon (the cgame's: UI_ICON_*), PAUSED raised over a faded screen
void UI_DrawStatusIcon(int icon, qboolean raised);
// the lagometer (the cgame's cg_lagometer: 1 the graph, 2 compact)
void UI_DrawLagometer(int style);
// the quick messages' menu (the cgame's: its title, NULL the groups', its
// lines, the localization's keys)
void UI_DrawInstaMessage(const char *title, const char *const *lines, int count);
// a centre print (x, y -2) or a location print (640 x 480 units, -1 centred)
// drawn at alpha
void UI_Extras_Print(const char *text, float alpha, int x, int y);
// the round's end banner over the game and the chat's typing line (View3D's 2D)
void UI_Extras_Draw2D(void);
// the chat's typing line is up (the chat shows its lines while it is)
qboolean UI_Extras_Typing(void);
// what is baked (a map's load, a new screen size); ahead: the overlays' masks begun on other threads
void UI_Extras_PreBake(qboolean ahead);

// the round's end banner on the Reloaded scoreboard: its height in the menus'
// units (0: none), drawn with its top at y; its key (the draw cache's)
float UI_Extras_BannerHeight(void);
void  UI_Extras_DrawBanner(const rlView_t& v, float y);
int   UI_Extras_BannerKey(void);
