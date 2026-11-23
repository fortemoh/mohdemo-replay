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
ReloadedMenu: "screen weapon|team|esc|scoreboard|loading|main|confirm|end|connect|options|hdrblack|hdrpeak",
"layer back|front". The back layer draws what is under the 3D model views and
places them (labels the menu names); the front layer draws what is over
them and takes the mouse and the keys. Both lay the screen out alike from
the same data
*/

// (SCR_HDRBLACK, SCR_HDRPEAK: the HDR calibration's, r_hdrOutputTest: the black level's, the peak's)
enum {
    SCR_WEAPON,
    SCR_TEAM,
    SCR_ESC,
    SCR_SCOREBOARD,
    SCR_LOADING,
    SCR_MAIN,
    SCR_CONFIRM,
    SCR_END,
    SCR_CONNECT,
    SCR_OPTIONS,
    SCR_HDRBLACK,
    SCR_HDRPEAK,
    SCR_NUM
};

typedef struct {
    int x, y, w, h; // pixels
    int id;
} rlHit_t;

// a tile of the in-game block (the weapon page's, the Esc menu's): its label in the corner (a state, a class; warn:
// a refusal, red), its picture (an outline, picW across at most, 0: as in a tile two a row; NULL: the caller draws it),
// its name under it, in the colours of team (-1 none); off: dimmed, not chosen; marked: the last pick's dot
typedef struct {
    const char *label, *name, *outline;
    float       picW;
    int         team;
    bool        off, warn, marked;
} rlTile_t;

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
    void SetLayer(Event *ev);
    void AllowKeys(void);
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
    // a figure's view (a label of the menu's, its modelscale scale, mid model units up from his feet in its middle)
    // placed with his head's top at headY, k units a model unit, width wide about cx
    void PlaceFigure(const rlView_t& v, const char *name, float scale, float mid, float cx, float headY, float k, float width);
    // a view (a label of the menu's) placed over x y w h, in units (none across: hidden)
    void PlaceView(const rlView_t& v, const char *name, float x, float y, float w, float h);
    // the in-game block's tile at x y w h (its hit id), drawn; the picture's middle (the caller's to draw when
    // t.outline is NULL) is x + w / 2, y + h * RL_TILEPIC
    void Tile(const rlView_t& v, float x, float y, float w, float h, int id, const rlTile_t& t);
    // a side's model cycler, its middle at cx cy, w across h high: the chevrons at its ends (idPrev, idPrev + 2), the
    // model previewed between, its place in the list after it; false: no other model to cycle to (the chevrons off)
    bool ModelCycler(const rlView_t& v, float cx, float cy, float w, float h, int side, int idPrev, float nameSize, float countSize);

    void DrawWeapon(const rlView_t& v, const rlInfo_t& in);
    void DrawTeam(const rlView_t& v, const rlInfo_t& in);
    void DrawEsc(const rlView_t& v, const rlInfo_t& in);
    void DrawScoreboard(const rlView_t& v, const rlInfo_t& in);
    void DrawEnd(const rlView_t& v, const rlInfo_t& in);

    // the main menu, its confirmations, the connect screen and Options over the game (cl_uimain.cpp)
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

// the page over the Esc menu (ui/reloaded_options.urc: the main menu's page over the game, the Esc menu's block
// hidden under it): Options, or CALL A VOTE's (Spearhead's and Breakthrough's matches; cl_uimain.cpp)
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
// the Esc menu's QUIT GAME (quit) or DISCONNECT (LEAVE MISSION in a mission) asked first: the main menu's confirmation
// over it; DISCONNECT's answer shows the main menu on its page show (RL_SHOW_*) after leaving
void        RL_EscConfirm(bool quit, int show);
// the in-game block's width (the Esc menu's, the weapon page's) in v's units (a view at the menus' size)
float       RL_BlockWidth(const rlView_t& v);
// an item of the main menu's top bar (its pages, its foot's: a glyph before each; cl_uimain.cpp)
typedef struct {
    int         id;
    const char *label, *glyph;
} rlBarItem_t;
// the Esc menu's caption in a mission ("MISSION 2  ·  SCUTTLING THE U-529  ·  MEDIUM")
std::string RL_MissionLine(const char *map);
// a Spearhead or Breakthrough level's painting (its load screen's, else its mission's; the missions' crop),
// registered, the shape its aspect; 0 none (Allied Assault's: the end screen's own)
qhandle_t   RL_LevelPainting(const char *map, float *aspect);
// Omaha's slant (the tabs' slashes', about 19 degrees): an edge leaning forward, its top this much right of its foot for
// each unit of its height; a box's edges slanted: none, its left, its right, both
#define RL_SLANT 0.34f
enum { RL_SL_NONE, RL_SL_LEFT, RL_SL_RIGHT, RL_SL_BOTH };
// a box with its left and/or right edge slanted (edges: RL_SL_*), the slant's middle on the box's edge (boxes side by
// side tile, the gaps between them as wide at every height); a straight edge as RL_Box's
void        RL_SlantBox(const rlView_t& v, float x, float y, float w, float h, int edges, rlColor_t c);
// the main menu's button (primary: brass; off: dimmed; lit while m's hot item; edges: RL_SL_*, those slanted; its label
// size high, the in-game block's its names' size); its hit (id) added to m
void        RL_Button(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h, const char *label, bool primary, int id,
                      bool off = false, int edges = RL_SL_BOTH, float size = 18.0f);

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
// the connect screen shown for a stock error menu (its name: errormessage, servertimeout, ...)
void        RL_ConnectError(const char *stock);
// the server's last word (its "disconnect" command's reason)
void        RL_ConnectDropped(const char *reason);
// a map of the server's loads (its serverinfo; the map "dm/mohdm6"): its name and map, for the connect screen should
// it drop the player
void        RL_ConnectLoaded(const char *info, const char *map);
// the load screen's layout, one screen from a connect to the game (the load screen's, the connect screen's;
// cl_uimenus.cpp): the card 4:3, 720 wide at most, in the middle (x y w h), the bar under it, the line under the bar
typedef struct {
    float x, y, w, h;
    float bar, line;
} rlLoadLayout_t;
rlLoadLayout_t RL_LoadLayout(const rlView_t& v);
// the load screen's look on it: map, the pictures of the map the load screen or RL_LoadMap made (its blurred copy
// over the screen, its photo in the card), else the card empty over the caller's backdrop; framed; the title over the
// card on the left (made smaller, then cut, to leave the word its room), the word on its right in wordColor; the bar
// (frac how far, < 0 a dash sweeping)
void        RL_LoadDraw(const rlView_t& v, const rlLoadLayout_t& l, bool map, const char *title, const char *word, rlColor_t wordColor,
                        float frac);
// the load screen's pictures made for a map ("dm/mohdm6") ahead of its load: the connect screen's, the browser's map
void        RL_LoadMap(const char *map);
