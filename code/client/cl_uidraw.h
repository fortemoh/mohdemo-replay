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

// cl_uidraw.h: the Reloaded UI's drawing (Added), defined in cl_uimenus.cpp
// and shared with cl_uiextras.cpp and cl_uimain.cpp: colors, design units,
// boxes, baked shapes, the moh-icons outlines and Oswald text baked at the
// pixel size drawn

#pragma once

#include <string>

#define RL_OUTLINES "ui/outlines/"

typedef struct {
    float r, g, b, a;
} rlColor_t;

// the palette (the approved design)
static const rlColor_t RL_TEXT  = {238 / 255.0f, 234 / 255.0f, 224 / 255.0f, 1};
static const rlColor_t RL_MUTED = {160 / 255.0f, 156 / 255.0f, 146 / 255.0f, 1};
static const rlColor_t RL_FAINT = {110 / 255.0f, 107 / 255.0f, 100 / 255.0f, 1};
// the accent, the game's (set at the UI's start, UI_Menus_Init): Allied Assault's
// brass, Spearhead's olive, Breakthrough's bronze; RL_BRASS_HOT under the mouse
extern rlColor_t RL_BRASS, RL_BRASS_HOT;
static const rlColor_t RL_INK   = {8 / 255.0f, 9 / 255.0f, 10 / 255.0f, 1}; // on brass

// icon: the badge (the star or cross on its disc); mark: the star or cross
// alone (the team page's medallions, where the badge's disc drawn faint in
// the medallion's showed as a ring), each as wide as CS:GO's in its disc
// (the star 0.95 of its viewBox, the cross's viewBox cropped to the cross)
typedef struct {
    int         deep[3], base[3], mid[3], light[3], pale[3];
    const char *name, *icon, *mark;
} rlTeam_t;

static const rlTeam_t rl_teams[2] = {
    {{18, 30, 43}, {30, 50, 70}, {58, 94, 128}, {150, 192, 228}, {218, 233, 246}, "ALLIES", RL_OUTLINES "team/allies.svg", RL_OUTLINES "team/allies-mark.svg"},
    {{44, 18, 16}, {70, 32, 28}, {134, 62, 50}, {234, 162, 142}, {248, 218, 208}, "AXIS", RL_OUTLINES "team/axis.svg", RL_OUTLINES "team/axis-mark.svg"},
};

// a font the HUD's small text can take (cg_hud_font, Options' HUD Font): the
// setting's value, its name, its files (fonts/<file>.ttf) for Oswald Medium's
// and SemiBold's parts. Oswald first
typedef struct {
    const char *value, *name, *medium, *semibold;
} rlFont_t;

extern const rlFont_t rl_fonts[];
extern const int      rl_numFonts;
// the font a value names (Oswald: none)
const rlFont_t *RL_FindFont(const char *value);

// the screen in design units
typedef struct {
    float           s;      // pixels a unit
    float           dw, dh; // the screen's units across and down
    bool            hud;    // the HUD's: its small text in the HUD's font (RL_Face)
    const rlFont_t *font;   // its text in this font (NULL: Oswald, or the HUD's)
} rlView_t;

enum { RL_LEFT, RL_CENTER, RL_RIGHT };
enum { RL_TOP, RL_MID, RL_BASE }; // the capitals' top, middle, the baseline at y

typedef struct {
    fontheader_t *f;
    float         page; // drawn at it, a texel a pixel
    int           px;
} rlFace_t;

// laid out to bake what it draws ahead (the PreBakes): nothing drawn
extern bool rl_bakeOnly;

rlColor_t RL_Rgb(int r, int g, int b, int a = 255);
rlColor_t RL_Alpha(rlColor_t c, int a);
rlColor_t RL_T(const int *c, int a = 255);
rlView_t RL_View(void);
int RL_P(const rlView_t& v, float d);
void RL_SetColor(rlColor_t c);
void RL_Box(const rlView_t& v, float x, float y, float w, float h, rlColor_t c);
void RL_BoxPx(int x0, int y0, int x1, int y1, rlColor_t c);
void RL_Shape(int x, int y, int w, int h, const char *text, rlColor_t c, float s);
// a box rounded r; its frame, width thick inside its edge
void RL_Round(const rlView_t& v, float x, float y, float w, float h, float r, rlColor_t c);
void RL_RoundFrame(const rlView_t& v, float x, float y, float w, float h, float r, float width, rlColor_t c);
// a small triangle, pointing down (or up), in w x h
void RL_Arrow(const rlView_t& v, float x, float y, float w, float h, bool up, rlColor_t c);
// a horizontal gradient from a to b
void RL_HGradient(const rlView_t& v, float x, float y, float w, float h, rlColor_t a, rlColor_t b);
bool RL_Outline(const rlView_t& v, const char *name, float cx, float cy, float bw, float bh, rlColor_t c, bool mirror = false);
void RL_TeamIcon(const rlView_t& v, int t, float cx, float cy, float d);
// shadow: with the HUD's drop shadow (its labels'); the view's font, else the
// HUD's for its small text (v.hud, up to UI_HUD_SMALL), else Oswald
rlFace_t RL_Face(const rlView_t& v, float size, bool bold, bool shadow = false);
float RL_Advance(const rlFace_t& face, unsigned char ch);
float RL_WidthPx(const rlFace_t& face, const char *text, float trackPx);
float RL_TextW(const rlView_t& v, const char *str, float size, bool bold, float track = 0.0f);
float RL_Text(
    const rlView_t& v,
    float           x,
    float           y,
    const char     *str,
    float           size,
    rlColor_t       c,
    bool            bold,
    int             anchor = RL_LEFT,
    float           track  = 0.0f,
    int             valign = RL_TOP,
    float           maxw   = 0.0f,
    bool            shadow = false
);
// a name cut with "…" to fit maxw
std::string RL_Ellipsize(const rlView_t& v, const char *text, float size, bool bold, float maxw);
std::string RL_Upper(const char *s);
