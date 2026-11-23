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

// cl_uimenus.h: the Reloaded menus (Added): the team page, the weapon page,
// the Esc menu, the scoreboard, the load screen, the end of a mission's level
// and the connect screen (ui/reloaded_*.urc), shown in place of the stock
// menus, and the cursor

#pragma once

void        UI_Menus_Init(void);
// a player model's name ("US - Soldier"), else the file's (fileName)
const char *PM_FilenameToDisplayname(const char *fileName);
// a menu about to be pushed by that name (the team and weapon pages' preview,
// the weapon page's nation); qfalse: not pushed (F2's mpoptions, F3's and F4's
// pages and the vote menus shown instead)
qboolean    UI_Menus_Pushing(const char *name);
// the menu pushed by that name takes the place of the menu up, not over it: the Esc menu's (the team page, the weapon
// page, the main menu: TEAM, WEAPON, MAIN MENU, LOAD), the team page's (the weapon page, the player's own side picked);
// qfalse: pushed over the menu up as before
qboolean    UI_Menus_Replacing(const char *name);
// the keys to the menu up's front layer now (unless a floating window has them), not at its first Tick: the menu it
// replaced, hidden next, would give them to the game's view for a frame (its catcher off: a click a shot)
void        UI_Menus_TakeKeys(void);
// the cgame shows (show) or hides a stock menu m was found for: the end of a
// mission's level (reloaded_end) pushed or popped instead (its buttons take
// the mouse and the keys); qfalse: not that one
qboolean    UI_Menus_End(const char *name, class Menu *m, qboolean show);
// a server command just tokenized (the scoreboard's "scores")
void        UI_Menus_ServerCommand(void);
// a player's team (TEAM_*) in the latest "scores", 0 none: stock servers'
// only source (OpenMoHAA's add the player configstrings' "team")
int         UI_Menus_ScoreTeam(int client);
// the scoreboard of this (stock) name is the Reloaded one
qboolean    UI_Menus_Scoreboard(const char *name);
// the menu up is the Reloaded team page, weapon page or Esc menu (or Options over it): no HUD under it (the scoreboard
// keeps it)
qboolean    UI_Menus_Covering(void);
// a configstring changed
void        UI_Menus_ConfigString(void);
// the menus laid out to bake their faces and shapes (a map's load, a new screen size)
void        UI_Menus_PreBake(void);
// a mission's objectives as the scoreboard's card while TAB is held (shown; the cgame's
// call each frame)
void        UI_Menus_DrawObjectives(qboolean shown);
// a map's load begins (UI_BeginLoad): the Reloaded menu that shows it
// (reloaded_loading, with no continuebutton: the level starts once loaded;
// the pictures the stock load screen's, stock); NULL: the stock one (no
// Reloaded menu loaded)
const char *UI_Menus_Loading(class Menu *stock, const char *map);
// a connect to server begins (CL_Connect, its address resolved): the connect screen's
// state anew, the browser's row's name kept for it (cl_uimain.cpp)
void        UI_Menus_Connect(const char *server);
// at the client's start, the configs read: each of our own actions (Options' Controls: Spectator View) given its key
// once, where neither is bound already (cl_reloadedbinds; cl_uimain.cpp)
void        RL_DefaultBinds(void);
