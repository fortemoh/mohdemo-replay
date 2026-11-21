/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

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

// DESCRIPTION:
// Some tools used to drawing 2d stuff

#include "cg_local.h"

/*
================
CG_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void CG_AdjustFrom640(float *x, float *y, float *w, float *h)
{
#if 0
	// adjust for wide screens
	if ( cgs.glconfig.vidWidth * 480 > cgs.glconfig.vidHeight * 640 ) {
		*x += 0.5 * ( cgs.glconfig.vidWidth - ( cgs.glconfig.vidHeight * 640 / 480 ) );
	}
#endif
    // scale for screen sizes
    *x *= cgs.screenXScale;
    *y *= cgs.screenYScale;
    *w *= cgs.screenXScale;
    *h *= cgs.screenYScale;
}

/*
=============
CG_TileClearBox

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void CG_TileClearBox(int x, int y, int w, int h, qhandle_t hShader)
{
    float s1, t1, s2, t2;

    s1 = x / 64.0;
    t1 = y / 64.0;
    s2 = (x + w) / 64.0;
    t2 = (y + h) / 64.0;
    cgi.R_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, hShader);
}

/*
==============
CG_TileClear

Clear around a sized down screen
==============
*/
void CG_TileClear(void)
{
    int top, bottom, left, right;
    int w, h;

    w = cgs.glconfig.vidWidth;
    h = cgs.glconfig.vidHeight;

    if (cg.refdef.x == 0 && cg.refdef.y == 0 && cg.refdef.width == w && cg.refdef.height == h) {
        return; // full screen rendering
    }

    top    = cg.refdef.y;
    bottom = top + cg.refdef.height - 1;
    left   = cg.refdef.x;
    right  = left + cg.refdef.width - 1;

    // clear above view screen
    CG_TileClearBox(0, 0, w, top, cgs.media.backTileShader);

    // clear below view screen
    CG_TileClearBox(0, bottom, w, h - bottom, cgs.media.backTileShader);

    // clear left of view screen
    CG_TileClearBox(0, top, left, bottom - top + 1, cgs.media.backTileShader);

    // clear right of view screen
    CG_TileClearBox(right, top, w - right, bottom - top + 1, cgs.media.backTileShader);
}

/*
===============================================================================

LAGOMETER

===============================================================================
*/

/*
==============
CG_DrawDisconnect
==============
*/
void CG_DrawDisconnect(void)
{
    int       cmdNum;
    usercmd_t cmd;

    // Added in OPM
    //  A demo has no connection to lose: the commands below are made by the
    //  player watching it, and stop with the time when it's paused
    if (cg.demoPlayback) {
        return;
    }

    // draw the phone jack if we are completely past our buffers
    cmdNum = cgi.GetCurrentCmdNumber() - CMD_BACKUP + 1;
    cgi.GetUserCmd(cmdNum, &cmd);
    if (!cg.snap || cmd.serverTime <= cg.snap->ps.commandTime
        || cmd.serverTime > cg.time) { // special check for map_restart
        return;
    }

    // blink it
    if ((cg.time >> 9) & 1) {
        return;
    }

    // Changed: the client's, in the HUD's style
    cgi.UI_DrawStatusIcon(UI_ICON_NOSNAPSHOTS, qfalse);
}

/*
==============
CG_DrawLagometer
==============
*/
void CG_DrawLagometer(void)
{
    // Changed: the client's (Project: Omaha's netgraph in the HUD's style):
    // 1 the graph, 2 compact
    if (cg_lagometer->integer) {
        cgi.UI_DrawLagometer(cg_lagometer->integer);
    }
    CG_DrawDisconnect();
}

static void CG_DrawPauseIcon()
{
    if (!paused->integer) {
        return;
    }
    // Changed: the client's, in the HUD's style (higher over a faded screen)
    cgi.UI_DrawStatusIcon(UI_ICON_PAUSED, cg.snap && cg.snap->ps.blend[3] > 0 ? qtrue : qfalse);
}

static void CG_DrawServerLag()
{
    if (cgs.gametype == GT_SINGLE_PLAYER) {
        // Fixed in 2.0
        //  Don't draw server lag in single-player mode
        return;
    }

    if (!cg_drawsvlag->integer) {
        return;
    }

    if (!developer->integer) {
        // Changed in 2.0
        //  Svlag should only be drawn when developer mode is enabled
        return;
    }

    if (!cgs.serverLagTime) {
        return;
    }

    if (cg.time - cgs.serverLagTime > 3000) {
        return;
    }

    // blink it
    if ((cg.time >> 9) & 1) {
        return;
    }

    // Changed: the client's, in the HUD's style
    cgi.UI_DrawStatusIcon(UI_ICON_SLOWSERVER, qfalse);
}

/*
==============
CG_DrawIcons
==============
*/
void CG_DrawIcons(void)
{
    if (!cg_hud->integer) {
        return;
    }

    CG_DrawPauseIcon();
    CG_DrawServerLag();
}

void CG_DrawZoomOverlay()
{
    static int   zoomType;
    static float fAlpha;
    const char  *weaponstring;
    qboolean     bDrawOverlay;

    weaponstring = "";
    bDrawOverlay = qtrue;

    // Added in OPM
    //  nor while a demo shows a player from behind
    if (!cg.snap || CG_DemoBehind() >= 0 || CG_RecorderBehind()) {
        return;
    }

    if (cg.snap->ps.activeItems[1] >= 0) {
        weaponstring = CG_ConfigString(CS_WEAPONS + cg.snap->ps.activeItems[1]);
    }

    if (!Q_stricmp(weaponstring, "Spy Camera")) {
        zoomType = 2;
    } else if (!Q_stricmp(weaponstring, "Binoculars")) {
        zoomType = 3;
    } else {
        if (cg.snap->ps.stats[STAT_INZOOM] && cg.snap->ps.stats[STAT_INZOOM] <= 30) {
            if (!Q_stricmp(weaponstring, "KAR98 - Sniper")) {
                zoomType = 1;
            } else {
                zoomType = 0;
            }
        } else {
            bDrawOverlay = qfalse;
        }
    }

    if (bDrawOverlay) {
        fAlpha += cg.frametime * 0.015;
        if (fAlpha > 1.0) {
            fAlpha = 1.0;
        }
    } else {
        fAlpha -= cg.frametime * 0.015;
        if (fAlpha < 0.0) {
            fAlpha = 0.0;
        }

        if (!fAlpha) {
            return;
        }
    }

    // Changed: drawn by the client (the Competitive HUD's mask and lines)
    cgi.UI_DrawZoomOverlay(zoomType, fAlpha);
}

void CG_HudDrawShader(int iInfo)
{
    if (cgi.HudDrawElements[iInfo].shaderName[0]) {
        cgi.HudDrawElements[iInfo].hShader = cgi.R_RegisterShaderNoMip(cgi.HudDrawElements[iInfo].shaderName);
    } else {
        cgi.HudDrawElements[iInfo].hShader = 0;
    }
}

void CG_HudDrawFont(int iInfo)
{
    if (cgi.HudDrawElements[iInfo].fontName[0]) {
        cgi.HudDrawElements[iInfo].pFont = cgi.R_LoadFont(cgi.HudDrawElements[iInfo].fontName);
    } else {
        cgi.HudDrawElements[iInfo].pFont = nullptr;
    }
}

void CG_RefreshHudDrawElements()
{
    int i;

    for (i = 0; i < MAX_HUDDRAW_ELEMENTS; ++i) {
        CG_HudDrawShader(i);
        CG_HudDrawFont(i);
    }
}

void CG_HudDrawElements()
{
    static cvar_t *cg_hud_score = cgi.Cvar_Get("cg_hud_score", "1", CVAR_ARCHIVE);
    int            i;
    float          fX, fY;
    float          fWidth, fHeight;
    vec2_t         virtualScale;
    qboolean       jail;

    if (!cg_huddraw_force->integer && !cg_hud->integer) {
        return;
    }
    // Added: Liberation's jail (global/lib_dm.scr's 101-105, its lock 105) the
    // Competitive HUD's top bar shows (the client's cl_uicomp.cpp): not drawn here
    jail = cgs.gametype == GT_LIBERATION && cg_hud_score->integer
        && !Q_stricmp(cgi.HudDrawElements[105].shaderName, "textures/hud/lock_icon");

    virtualScale[0] = cgs.glconfig.vidWidth / 640.0;
    virtualScale[1] = cgs.glconfig.vidHeight / 480.0;

    for (i = 0; i < MAX_HUDDRAW_ELEMENTS; i++) {
        if ((!cgi.HudDrawElements[i].hShader && !cgi.HudDrawElements[i].string[0])
            || !cgi.HudDrawElements[i].vColor[3] || (jail && i >= 101 && i <= 105)) {
            // skip invisible elements
            continue;
        }

        fX      = cgi.HudDrawElements[i].iX;
        fY      = cgi.HudDrawElements[i].iY;
        fWidth  = cgi.HudDrawElements[i].iWidth;
        fHeight = cgi.HudDrawElements[i].iHeight;

        if (!cgi.HudDrawElements[i].bVirtualScreen) {
            fWidth *= cgs.uiHiResScale[0];
            fHeight *= cgs.uiHiResScale[1];
            fX *= cgs.uiHiResScale[0];
            fY *= cgs.uiHiResScale[1];
        }

        if (cgi.HudDrawElements[i].iHorizontalAlign == HUD_ALIGN_X_CENTER) {
            if (cgi.HudDrawElements[i].bVirtualScreen) {
                fX += 320.0 - fWidth * 0.5;
            } else {
                fX += cgs.glconfig.vidWidth * 0.5 - fWidth * 0.5;
            }
        } else if (cgi.HudDrawElements[i].iHorizontalAlign == HUD_ALIGN_X_RIGHT) {
            if (cgi.HudDrawElements[i].bVirtualScreen) {
                fX += 640.0;
            } else {
                fX += cgs.glconfig.vidWidth;
            }
        }

        if (cgi.HudDrawElements[i].iVerticalAlign == HUD_ALIGN_Y_CENTER) {
            if (cgi.HudDrawElements[i].bVirtualScreen) {
                fY += 240.0 - fHeight * 0.5;
            } else {
                fY += cgs.glconfig.vidHeight * 0.5 - fHeight * 0.5;
            }
        } else if (cgi.HudDrawElements[i].iVerticalAlign == HUD_ALIGN_Y_BOTTOM) {
            if (cgi.HudDrawElements[i].bVirtualScreen) {
                fY += 480.0;
            } else {
                fY += cgs.glconfig.vidHeight;
            }
        }

        cgi.R_SetColor(cgi.HudDrawElements[i].vColor);
        if (cgi.HudDrawElements[i].string[0]) {
            fontheader_t *pFont = cgi.HudDrawElements[i].pFont;
            if (!pFont) {
                pFont = cgs.media.hudDrawFont;
            }

            if (cgi.HudDrawElements[i].bVirtualScreen) {
                cgi.R_DrawString(pFont, cgi.LV_ConvertString(cgi.HudDrawElements[i].string), fX, fY, -1, virtualScale);
            } else {
                cgi.R_DrawString(
                    pFont,
                    cgi.LV_ConvertString(cgi.HudDrawElements[i].string),
                    fX / cgs.uiHiResScale[0],
                    fY / cgs.uiHiResScale[1],
                    -1,
                    cgs.uiHiResScale
                );
            }
        } else {
            if (cgi.HudDrawElements[i].bVirtualScreen) {
                CG_AdjustFrom640(&fX, &fY, &fWidth, &fHeight);
            }

            cgi.R_DrawStretchPic(fX, fY, fWidth, fHeight, 0.0, 0.0, 1.0, 1.0, cgi.HudDrawElements[i].hShader);
        }
    }
}

void CG_InitializeObjectives()
{
    cg.ObjectivesDesiredAlpha = 0.0;
}

void CG_DrawInstantMessageMenu()
{
    const bool               ta     = cg_protocol >= PROTOCOL_MOHTA_MIN;
    const int                g      = cg.iInstaMessageMenu - 1;
    const char *const *const groups = ta ? pInstantMsgGroupsEng : pInstantMsgGroupsEng_ver6;

    if (!cg.iInstaMessageMenu) {
        return;
    }

    // Changed: the client's list (the Competitive HUD's) with the protocol's
    // groups and lines (1.11's, or 2.0's and later's)
    if (g < 0) {
        cgi.UI_DrawInstaMessage(NULL, groups, CG_InstaMessageGroups());
    } else {
        cgi.UI_DrawInstaMessage(groups[g], ta ? pInstantMsgEng[g] : pInstantMsgEng_ver6[g], ARRAY_LEN(pInstantMsgEng[0]));
    }
}

// The crosshair: its boxes in whole pixels, laid out again on a resolution change and when a setting changes
#define CROSSHAIR_PIECES 5  // four arms and the center dot
#define CROSSHAIR_BOXES  64 // the pieces and their outline strips as boxes that don't overlap

static struct {
    vec4_t color, outlineColor;
    float  boxes[CROSSHAIR_BOXES][4];   // x, y, width, height: the pieces
    float  outline[CROSSHAIR_BOXES][4]; // the outline, drawn first in its own color
    int    count, outlineCount;
    int    settings; // the settings' modification counts added up, as laid out
} crosshairLayout;

static cvar_t **const crosshairSettings[] = {
    &cg_crosshair_length, &cg_crosshair_gap, &cg_crosshair_thickness, &cg_crosshair_color, &cg_crosshair_alpha,
    &cg_crosshair_outline, &cg_crosshair_outline_color, &cg_crosshair_outline_alpha, &cg_crosshair_dot, &cg_crosshair_t
};

// modification counts only grow: their sum changes whenever a setting does
static int CG_CrosshairSettings(void)
{
    int i, sum = 0;

    for (i = 0; i < (int)ARRAY_LEN(crosshairSettings); i++) {
        sum += (*crosshairSettings[i])->modificationCount;
    }
    return sum;
}

// a size in pixels at 1080 lines in whole pixels at this height, as CS2 rescales its sizes: rounded half away from
// zero, and at least 1 if above 0 (0 below 0 and for NaN: a negative gap draws as 0, as on CS2's static cross)
static int CG_CrosshairPixels(float value)
{
    if (!(value > 0.0f)) {
        return 0;
    }
    return Q_max((int)(Q_min(value, 4096.0f) * cgs.glconfig.vidHeight / 1080.0f + 0.5f), 1);
}

static void CG_CrosshairColor(const cvar_t *hex, unsigned int fallback, const cvar_t *alpha, vec4_t color)
{
    unsigned int rgb = Com_HexColor(hex->string, fallback);

    VectorSet4(
        color,
        (rgb >> 16) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        Q_min(Q_max(alpha->integer, 0), 255) / 255.0f
    );
}

// the spans of the boxes (x, y, width, height) across the band y0 to y1, sorted and joined where they touch: how many
static int CG_CrosshairSpans(int (*boxes)[4], int n, int y0, int y1, int (*spans)[2])
{
    int ns = 0, m = 0, i, j, x0, x1;

    for (i = 0; i < n; i++) {
        if (boxes[i][1] <= y0 && boxes[i][1] + boxes[i][3] >= y1 && boxes[i][2] > 0) {
            for (x0 = boxes[i][0], x1 = x0 + boxes[i][2], j = ns++; j > 0 && spans[j - 1][0] > x0; j--) {
                spans[j][0] = spans[j - 1][0];
                spans[j][1] = spans[j - 1][1];
            }
            spans[j][0] = x0;
            spans[j][1] = x1;
        }
    }
    for (i = 0; i < ns; i++) {
        if (m && spans[i][0] <= spans[m - 1][1]) {
            spans[m - 1][1] = Q_max(spans[m - 1][1], spans[i][1]);
        } else {
            spans[m][0] = spans[i][0];
            spans[m][1] = spans[i][1];
            m++;
        }
    }
    return m;
}

// the span x0 to x1 across the band y0 to y1 as a box: a box of the bands above going on down when just as wide, else
// a new one if it fits in max. How many boxes there are
static int CG_CrosshairBox(float (*out)[4], int count, int above, int max, int x0, int x1, int y0, int y1)
{
    int i;

    for (i = 0; i < above; i++) {
        if (out[i][0] == x0 && out[i][2] == x1 - x0 && out[i][1] + out[i][3] == y0) {
            out[i][3] += y1 - y0;
            return count;
        }
    }
    if (count < max) {
        VectorSet4(out[count], x0, y0, x1 - x0, y1 - y0);
        count++;
    }
    return count;
}

// the pixels of n boxes less those of nm others (x, y, width, height), as boxes that don't overlap (at most max):
// in each band between the boxes' top and bottom edges, the first boxes' spans less the others'. CS2 draws each pixel
// once where pieces (or outline strips) meet, and the outline nowhere under a piece
static int CG_CrosshairUnion(int (*in)[4], int n, int (*minus)[4], int nm, float (*out)[4], int max)
{
    int edges[CROSSHAIR_PIECES * 10], spans[CROSSHAIR_PIECES * 4][2], cut[CROSSHAIR_PIECES][2];
    int ne = 0, count = 0, i, j, k, e, ns, nc, x0, x1;

    for (i = 0; i < n + nm; i++) {
        const int *b = i < n ? in[i] : minus[i - n];

        edges[ne++] = b[1];
        edges[ne++] = b[1] + b[3];
    }
    for (i = 1; i < ne; i++) {
        for (e = edges[i], j = i; j > 0 && edges[j - 1] > e; j--) {
            edges[j] = edges[j - 1];
        }
        edges[j] = e;
    }

    for (k = 0; k + 1 < ne; k++) {
        const int y0 = edges[k], y1 = edges[k + 1], above = count;

        if (y0 == y1) {
            continue;
        }
        ns = CG_CrosshairSpans(in, n, y0, y1, spans);
        nc = CG_CrosshairSpans(minus, nm, y0, y1, cut);
        for (i = 0; i < ns; i++) {
            // the span up to each cut, then on from its end
            for (x0 = spans[i][0], j = 0; j <= nc && x0 < spans[i][1]; j++) {
                x1 = j < nc ? Q_min(Q_max(cut[j][0], x0), spans[i][1]) : spans[i][1];
                if (x1 > x0) {
                    count = CG_CrosshairBox(out, count, above, max, x0, x1, y0, y1);
                }
                if (j < nc) {
                    x0 = Q_max(x0, cut[j][1]);
                }
            }
        }
    }
    return count;
}

/*
====================
CG_UpdateCrosshair

As CS2 draws its static cross, the sizes pixels at 1080 lines scaled with the screen's height (CG_CrosshairPixels).
Around the screen's middle pixel (width / 2, height / 2), a line t pixels thick has (t + 1) / 2 of them before it.
The gap counts from that pixel: the left arm ends gap pixels before it, the right arm starts gap pixels after it,
a pixel sooner when t is odd (the same for the top and bottom); at gap 0 the arms meet. Length 0 draws no arms, the
dot is the middle t by t, cg_crosshair_t has no top arm. The outline is always one pixel, round every piece (1) or
above and left of it only (2, CS2's half outline), and nowhere under a piece. By default CS2's default crosshair
without its outline, white: arms two pixels thick and eight long, a gap of four. Out-of-range settings are clamped
here, never rewritten: crosshair_export gives back the code they were imported from
====================
*/
void CG_UpdateCrosshair(void)
{
    int pieces[CROSSHAIR_PIECES][4], strips[CROSSHAIR_PIECES * 4][4];
    int length, thickness, gap, before, odd, x, y, n, ns, mode, i;

    length    = CG_CrosshairPixels(cg_crosshair_length->value);
    thickness = CG_CrosshairPixels(cg_crosshair_thickness->value);
    gap       = CG_CrosshairPixels(cg_crosshair_gap->value);
    before    = (thickness + 1) / 2;
    odd       = thickness & 1;
    x         = cgs.glconfig.vidWidth / 2;
    y         = cgs.glconfig.vidHeight / 2;

    CG_CrosshairColor(cg_crosshair_color, 0xFFFFFF, cg_crosshair_alpha, crosshairLayout.color);
    CG_CrosshairColor(cg_crosshair_outline_color, 0x000000, cg_crosshair_outline_alpha, crosshairLayout.outlineColor);

    n = 0;
    if (thickness && length) {
        VectorSet4(pieces[n], x - gap - length, y - before, length, thickness);
        n++;
        VectorSet4(pieces[n], x + gap - odd, y - before, length, thickness);
        n++;
        VectorSet4(pieces[n], x - before, y + gap - odd, thickness, length);
        n++;
        if (!cg_crosshair_t->integer) {
            VectorSet4(pieces[n], x - before, y - gap - length, thickness, length);
            n++;
        }
    }
    if (thickness && cg_crosshair_dot->integer) {
        VectorSet4(pieces[n], x - before, y - before, thickness, thickness);
        n++;
    }

    mode = Q_min(Q_max(cg_crosshair_outline->integer, 0), 2);
    for (ns = 0, i = 0; mode && i < n; i++) {
        const int *p = pieces[i];

        // above (over the corners) and left; full: below and right too
        VectorSet4(strips[ns], p[0] - 1, p[1] - 1, p[2] + (mode == 1 ? 2 : 1), 1);
        ns++;
        VectorSet4(strips[ns], p[0] - 1, p[1], 1, p[3]);
        ns++;
        if (mode == 1) {
            VectorSet4(strips[ns], p[0] - 1, p[1] + p[3], p[2] + 2, 1);
            ns++;
            VectorSet4(strips[ns], p[0] + p[2], p[1], 1, p[3]);
            ns++;
        }
    }

    crosshairLayout.count        = CG_CrosshairUnion(pieces, n, NULL, 0, crosshairLayout.boxes, CROSSHAIR_BOXES);
    crosshairLayout.outlineCount = CG_CrosshairUnion(strips, ns, pieces, n, crosshairLayout.outline, CROSSHAIR_BOXES);
    crosshairLayout.settings     = CG_CrosshairSettings();
}

// Added: the crosshair's middle in screen pixels (the hitmarker's, cg_hitmarker.c): the corner the screen's middle
// pixel starts at, half a pixel up and left when the lines are an odd number of pixels thick (CG_UpdateCrosshair)
void CG_CrosshairMiddle(float *x, float *y)
{
    const float odd = (float)(CG_CrosshairPixels(cg_crosshair_thickness->value) & 1);

    *x = cgs.glconfig.vidWidth / 2 - odd * 0.5f;
    *y = cgs.glconfig.vidHeight / 2 - odd * 0.5f;
}

void CG_DrawCrosshair()
{
    if (!cg_hud->integer || !ui_crosshair->integer || !cg.snap) {
        return;
    }

    // where the stock image was drawn, without its friend check (the teammate image is in Spearhead and
    // Breakthrough only), leaving the 2D color white as it did
    if ((cg.snap->ps.pm_flags & (PMF_NO_HUD | PMF_INTERMISSION)) || !cg.snap->ps.stats[STAT_CROSSHAIR]) {
        return;
    }

    // Added in OPM
    //  Not from behind the recorder, as not from behind a player followed
    if (CG_RecorderBehind() || CG_DemoBehind() >= 0) {
        return;
    }

    if (CG_CrosshairSettings() != crosshairLayout.settings) {
        CG_UpdateCrosshair();
    }

    cgi.R_SetColor(NULL);
    if (crosshairLayout.outlineCount) {
        cgi.R_DrawBoxes(crosshairLayout.outline, crosshairLayout.outlineCount, crosshairLayout.outlineColor);
    }
    if (crosshairLayout.count) {
        cgi.R_DrawBoxes(crosshairLayout.boxes, crosshairLayout.count, crosshairLayout.color);
    }
}

/*
==============
CG_Draw2D
==============
*/
void CG_Draw2D(void)
{
    // Added: no HUD under the Reloaded weapon and Esc menus (the client's
    // ui_menucover); a scope's mask stays, being the view
    static cvar_t *ui_menucover = cgi.Cvar_Get("ui_menucover", "0", 0);

    CG_DrawZoomOverlay();
    if (ui_menucover->integer) {
        return;
    }
    CG_DrawLagometer();
    CG_HudDrawElements();
    // Added: the player aimed at's tag over their head (the client's, in the HUD's look)
    CG_DrawTeamTag();
    // Added: a mission's objectives on TAB as the Reloaded menus' card (the
    // client's, at once as their scoreboard; no crosshair over it)
    cgi.UI_DrawObjectives(cg.ObjectivesDesiredAlpha > 0.0f ? qtrue : qfalse);
    CG_DrawIcons();
    // (the stopwatch, the spectator's keys, the team, the player aimed at, the
    // attacker and the vote: the Competitive HUD's, the client's cl_uicomp.cpp)
    CG_DrawInstantMessageMenu();
    if (cg.ObjectivesDesiredAlpha <= 0.0f) {
        CG_Hitmarker_Draw(); // Added: round the crosshair, under it (Project: Omaha's)
        CG_DrawCrosshair();
    }
}
