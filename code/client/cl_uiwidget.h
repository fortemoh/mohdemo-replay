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

// cl_uiwidget.h: the Reloaded menus' widget (Added), ReloadedMenu, defined
// in cl_uimenus.cpp, its main menu's screens in cl_uimain.cpp, and what
// those share: the player models, the blurred photos, the maps' names

#pragma once

#include "cl_uidraw.h"

#include <string>
#include <vector>

/*
ReloadedMenu: "screen weapon|esc|scoreboard|loading|main|confirm|end|connect|options|hdrblack|hdrpeak",
"layer back|front". The back layer draws what is under the 3D model views and
places them (labels the menu names); the front layer draws what is over
them and takes the mouse and the keys. Both lay the screen out alike from
the same data
*/

// (SCR_HDRBLACK, SCR_HDRPEAK: the HDR calibration's, r_hdrOutputTest: the black level's, the peak's)
enum { SCR_WEAPON, SCR_ESC, SCR_SCOREBOARD, SCR_LOADING, SCR_MAIN, SCR_CONFIRM, SCR_END, SCR_CONNECT, SCR_OPTIONS, SCR_HDRBLACK, SCR_HDRPEAK, SCR_NUM };


typedef struct {
    int x, y, w, h; // pixels
    int id;
} rlHit_t;

struct rlInfo_t;

class UIReloadedMenu : public UIWidget
{
public:
    CLASS_PROTOTYPE(UIReloadedMenu);

    int                  m_screen, m_back;
    int                  m_focus;    // the item hot (hovered or moved to), -1 none
    int                  m_mouse[2]; // where the mouse was
    std::vector<rlHit_t> m_hits;
    std::vector<int>     m_order;    // the items in order (the arrow keys)
    int                  m_cols;     // the items a row (left and right)

    int                  m_tick;     // the frame Tick ran (-2: none, nor the frame before the first)

    UIReloadedMenu() : m_screen(SCR_WEAPON), m_back(0), m_focus(-1), m_cols(1), m_tick(-2)
    {
        m_mouse[0] = m_mouse[1] = -1;
        AllowActivate(true);
    }

    void SetScreen(Event *ev);
    void SetLayer(Event *ev) { m_back = !str::icmp(ev->GetString(1), "back"); }
    void MouseUp(Event *ev);
    void MouseButtons(Event *ev);
    void Draw(void) override;
    qboolean KeyEvent(int key, unsigned int time) override;
    void CharEvent(int ch) override;
    // the draw cache: drawn again from its list while DrawKey stays (the data's
    // changes, the hot item, the clock's seconds) and the cvars it read
    bool CacheDraw(void) override { return true; }
    bool DrawKeyed(void) override { return true; }
    int  DrawKey(void) override;
    void Tick(void);

    void Hit(const rlView_t& v, float x, float y, float w, float h, int id);
    int  HitAt(int x, int y);
    bool Hot(int id) const { return m_focus == id; }
    void Choose(int id);
    void PlaceFigure(const rlView_t& v, float cx, float headY, float width);
    // a view (a label of the menu's) placed over x y w h, in units (none across: hidden)
    void PlaceView(const rlView_t& v, const char *name, float x, float y, float w, float h);

    void DrawWeapon(const rlView_t& v, const rlInfo_t& in);
    void DrawEsc(const rlView_t& v, const rlInfo_t& in);
    void DrawScoreboard(const rlView_t& v, const rlInfo_t& in);
    void DrawEnd(const rlView_t& v, const rlInfo_t& in);

    // the main menu, its confirmations, the connect's box and Options over the game (cl_uimain.cpp)
    bool     Main(void) const { return m_screen == SCR_MAIN || m_screen == SCR_CONFIRM || m_screen == SCR_CONNECT || m_screen == SCR_OPTIONS; }
    void     DrawMain(const rlView_t& v);
    void     DrawOptions(const rlView_t& v);
    void     DrawConfirm(const rlView_t& v);
    void     DrawConnect(const rlView_t& v);
    void     ChooseMain(int id);
    qboolean MainKey(int key);
    void     MainButtons(int buttons);
    void     MainChar(int ch);
    void     MainTick(bool opened);
    int      MainDrawKey(void);

    // the HDR calibration's screens (cl_uimain.cpp)
    bool     Hdr(void) const { return m_screen == SCR_HDRBLACK || m_screen == SCR_HDRPEAK; }
    void     DrawHdr(const rlView_t& v);
    void     ChooseHdr(int id);
    qboolean HdrKey(int key);
    void     HdrTick(void);
};

// Options over the Esc menu clicked off its page, at x y (a ReloadedMenu's, all of them the screen's): the Esc menu's
// item there chosen, Options closed first (cl_uimenus.cpp); false: none there
bool        RL_EscClick(int x, int y);
// the page over the Esc menu (ui/reloaded_options.urc: the main menu's page beside the Esc menu's rail): Options, or
// VOTE's (Spearhead's and Breakthrough's matches; cl_uimain.cpp)
enum { RL_OVER_OPTIONS, RL_OVER_VOTE };
// that page shown (over the Esc menu up, else the Esc menu opened, the page shown over it from its Tick: RL_OverTick);
// the one shown, -1 none
void        RL_OverShow(int page);
void        RL_OverTick(void);
int         RL_OverPage(void);
// a part of the server's votes (its command vo0, vo1 or vo2: part 0, 1 or 2; read as the cgame reads them)
void        RL_VoteRead(int part, const char *text);
// the main menu shown next on a page (the Esc menu's LOAD, MAIN MENU in a mission, DISCONNECT; F3 and F4; cl_uimain.cpp);
// KEYS: Options' KEYS tab (in a game the Options over the Esc menu's, the main menu's page kept unless it is up)
enum { RL_SHOW_PLAY, RL_SHOW_MISSIONS, RL_SHOW_SAVES, RL_SHOW_KEYS };
void        RL_MainShow(int what);
// the rail (the main menu's, the Esc menu's; cl_uimain.cpp): the lockup at its head (head: the match under it, a title
// over two lines), the items from 380 down (on: the id of the one filled brass, -1 none), the foot's up from its foot (a
// glyph before each; the last muted, those over it red)
typedef struct {
    int         id;
    const char *label, *glyph;
} rlRailItem_t;
void        RL_Rail(UIReloadedMenu *m, const rlView_t& v, const rlRailItem_t *items, int n, int on, const rlRailItem_t *foot, int nf,
                    const std::string *head = NULL);
// the Esc menu's caption in a mission ("MISSION 2  ·  SCUTTLING THE U-529  ·  MEDIUM")
std::string RL_MissionLine(const char *map);
// a Spearhead or Breakthrough level's painting (its load screen's, else its mission's; the missions' crop),
// registered, the shape its aspect; 0 none (Allied Assault's: the end screen's own)
qhandle_t   RL_LevelPainting(const char *map, float *aspect);
// the main menu's button (primary: brass; off: dimmed; lit while m's hot item); its hit (id) added to m
void        RL_Button(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h, const char *label, bool primary, int id,
                      bool off = false);

// the player models a side can pick, in the order of their names (PM_FilenameToDisplayname)
typedef struct {
    std::string file;  // models/player/<file>.tik
    std::string order; // the picker's name ("DE - Wehrmacht Soldier")
    std::string name;  // shown ("Wehrmacht Soldier")
} rlModel_t;

extern std::vector<rlModel_t> rl_models[2];

// the models listed again (a menu showing them opens)
void        RL_ListModels(void);
// a side's own model (dm_playermodel, dm_playergermanmodel): its file; its place in the list, -1 not listed
std::string RL_OwnModel(int side);
int         RL_OwnIndex(int side);
// a model's name shown, from its file
std::string RL_ModelName(const char *file);
// the figure of a model's file: a TIKI of it standing in an idle (the views' linkcvar)
std::string RL_Figure(const std::string& file);
// a map's display name ("dm/mohdm6": "Stalingrad"); its own buffer
const char *RL_MapName(const char *map);
// a stock load screen's photo (the menu named after a dm or obj map), RGBA,
// opaque, NULL none; freed with uii.Rend_FreeRawImage
byte       *RL_MenuPhoto(class Menu *stock, int *width, int *height);
// a photo (RGBA, shown photo wide a unit high) as a screen-shaped texture rows
// texels high, blurred, its light times dim, darker to the corners, registered as name
qhandle_t   RL_Blur(const char *name, const byte *pic, int pw, int ph, int rows, float dim, float photo = 4.0f / 3.0f);
// the connect's box shown for a stock error menu (its name: errormessage, servertimeout, ...)
void        RL_ConnectError(const char *stock);
// the server's last word (its "disconnect" command's reason)
void        RL_ConnectDropped(const char *reason);
// a map of the server's loads (its serverinfo; the map "dm/mohdm6"): its name and map for a drop's box
void        RL_ConnectLoaded(const char *info, const char *map);
