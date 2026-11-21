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

// cl_uicomp.h: the Competitive HUD's data and widgets (Added, cg_hudstyle 1
// CS:GO's, 2 CS2's, ui/hud_comp_*.urc): the game's HUD

#pragma once

void     UI_CompHud_Files(void);          // the files compiled in, before the renderer starts (its shader script)
void     UI_CompHud_HeadIcons(void);      // the head icons' pictures, made once the UI can bake
// a file made while the game runs read as one compiled in (name and data kept until the client quits); qfalse: no room
qboolean UI_CompHud_AddFile(const char *name, const void *data, int size);
void     UI_CompHud_Init(void);
void     UI_CompHud_Frame(void);          // the ui_hud_* cvars, before the menus draw
void     UI_CompHud_Print(const char *msg); // a console line: the chat, the game messages, the kills
void     UI_CompHud_Clear(void);          // a new map or server
void     UI_CompHud_Shutdown(void);       // the UI's end
void     UI_CompHud_ConfigString(void);   // a configstring changed
void     UI_CompHud_MapLoaded(void);      // the radar's walk and the pictures, at the load
void     UI_CompHud_LoadEnd(void);        // the radar's picture to the renderer, at the load's end
void     UI_CompHud_PreBake(qboolean ahead); // the pictures (after the renderer's restart); ahead: their texels begun
// the cgame's teammates (cg.radars, the radar's), the player's place in them; NULL none
void     CL_InitRadar(radarClient_t *radars, int clientNum);
// a weapon's outline (ui/outlines/weapons/) by its name (its configstring's, its item name), NULL none
const char *UI_CompHud_WeaponOutline(const char *name);
// the player aimed at's tag (the cgame's cgi.UI_DrawTeamTag): their name, health (1 to 100) and an arrow, its tip at
// x, y (screen pixels: over their head)
void        UI_DrawTeamTag(int client, float x, float y, int health);

// the HUD's menus (ui/hud_comp_*.urc), in the order they are baked (the atlas is filled in it)
#define UI_COMPHUD_MENUS 14
extern const char *const ui_compHudMenus[UI_COMPHUD_MENUS];
// the followed player's plate's height at the bottom's middle (hud_comp_follow, while ui_hud_follow_on)
#define COMP_FOLLOW_H 62.0f
// which of them cg_hudstyle's preset shows (the bottom's health and ammo: CS2's strip or CS:GO's corners), set when it changes
extern bool ui_compHudInLayout[UI_COMPHUD_MENUS];
