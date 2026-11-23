/*
===========================================================================
Copyright (C) 2026 Project: Omaha
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

// cg_hitmarker.c: the hitmarker (Added; Project: Omaha's). When the server says the local player hit someone (its
// hit and kill notifications, the stock dings' CGM_NOTIFY_HIT and CGM_NOTIFY_KILL), a white X round the crosshair
// fades out in a quarter second (cg_hitmarker), and a sound of the player's choice plays in place of the ding
// (cg_hitmarker_sound): each on its own, the stock ding only with both off (a silent file in all three games' data).
// Nothing is sent: it works on any server that sends the notifications (some send only kills).
//
// Omaha's X: four diagonal strokes of squares 2 pixels wide, from 6 to 14 pixels out from the middle along them,
// the squares overlapping (blended one over another). Here sizes are pixels at 1080 lines scaled with the screen's
// height, and every square is on whole pixels a pixel further out than the last, as the crosshair is laid out
// (CG_UpdateCrosshair): the X mirrors itself to the pixel round the crosshair's middle (CG_CrosshairMiddle) at
// any size (Omaha's squares at fractions of a pixel could fall a pixel apart on either side)

#include "cg_local.h"

#define HITMARKER_MS     250
#define HITMARKER_ALPHA  0.5f  // at the hit, fading to 0
#define HITMARKER_INNER  6.0f  // a stroke's ends from the middle, pixels at 1080 lines
#define HITMARKER_OUTER  14.0f
#define HITMARKER_THICK  2.0f
#define HITMARKER_VOLUME 2.0f  // the stock dings'
#define HITMARKER_BOXES  256   // four strokes of squares

static cvar_t     *cg_hitmarker;
static cvar_t     *cg_hitmarker_sound;
static int         hitTime;          // cg.time of the last hit, 0 none showing
static sfxHandle_t hitSound;         // 0: none
static int         hitSoundMod = -1; // cg_hitmarker_sound's modificationCount when it was registered

void CG_Hitmarker_RegisterCvars(void)
{
    cg_hitmarker       = cgi.Cvar_Get("cg_hitmarker", "0", CVAR_ARCHIVE);
    // a file of sound/prom/hitmarkers/ (thump, classic, beep), or none: both off by default
    cg_hitmarker_sound = cgi.Cvar_Get("cg_hitmarker_sound", "none", CVAR_ARCHIVE);
}

/*
====================
CG_Hitmarker_RegisterSound

At a map's load, and when cg_hitmarker_sound changes. A file name only (the archived setting can't reach outside
the folder), ".wav" may be left out; a name that isn't there plays thump
====================
*/
void CG_Hitmarker_RegisterSound(void)
{
    char        name[MAX_QPATH];
    const char *path;
    size_t      len;

    hitSoundMod = cg_hitmarker_sound->modificationCount;
    hitSound    = 0;

    Q_strncpyz(name, cg_hitmarker_sound->string, sizeof(name));
    if (!name[0] || strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) {
        Q_strncpyz(name, "thump", sizeof(name));
    }
    len = strlen(name);
    if (len > 4 && !Q_stricmp(name + len - 4, ".wav")) {
        name[len - 4] = 0;
    }
    if (!Q_stricmp(name, "none")) {
        return;
    }

    // Fixed: looked for first, as a sound not there registers as sound/default.wav (Omaha's fell back only if it failed)
    path = va("sound/prom/hitmarkers/%s.wav", name);
    if (cgi.FS_ReadFile(path, NULL, qtrue) == -1) {
        path = "sound/prom/hitmarkers/thump.wav";
    }
    hitSound = cgi.S_RegisterSound(path, qfalse);
}

/*
====================
CG_Hitmarker_Hit

The server's hit or kill notification for the local player: the X if it's on, the sound unless it's none. False
when both are off, for the stock ding
====================
*/
qboolean CG_Hitmarker_Hit(void)
{
    if (cg_hitmarker_sound->modificationCount != hitSoundMod) {
        CG_Hitmarker_RegisterSound();
    }
    if (!cg_hitmarker->integer && !hitSound) {
        return qfalse;
    }

    if (cg_hitmarker->integer) {
        hitTime = cg.time;
    }
    if (hitSound) {
        // on the local channel of the player's own entity, as the stock ding (Omaha: CHAN_MENU could be placed
        // out of hearing, and ENTITYNUM_NONE find no free channel while 2D sounds are busy)
        cgi.S_StartSound(
            NULL, cg.snap->ps.clientNum, CHAN_LOCAL, hitSound, HITMARKER_VOLUME, -1.0f, 1.0f, -1.0f, qfalse
        );
    }
    return qtrue;
}

/*
====================
CG_Hitmarker_Draw

Under the crosshair, shown where it would be (with the HUD, out of intermissions) whether or not it is
====================
*/
void CG_Hitmarker_Draw(void)
{
    static const float dirs[4][2] = {
        {1, 1},
        {1, -1},
        {-1, 1},
        {-1, -1}
    };
    float  boxes[HITMARKER_BOXES][4];
    vec4_t color;
    float  scale, thick, x, y;
    int    age, from, to, n, i, k;

    if (!hitTime || !cg_hitmarker->integer || !cg_hud->integer || !cg.snap
        || (cg.snap->ps.pm_flags & (PMF_NO_HUD | PMF_INTERMISSION))) {
        return;
    }
    age = cg.time - hitTime;
    if (age < 0 || age >= HITMARKER_MS) {
        hitTime = 0;
        return;
    }

    // the squares' centres from the middle, across and down, in whole pixels
    scale = cgs.glconfig.vidHeight / 1080.0f;
    thick = Q_max(floorf(HITMARKER_THICK * scale + 0.5f), 1.0f);
    from  = (int)(HITMARKER_INNER * scale * 0.70710678f + 0.5f);
    to    = Q_min((int)(HITMARKER_OUTER * scale * 0.70710678f + 0.5f), from + HITMARKER_BOXES / 4 - 1);
    // a square's corner on a whole pixel (half a pixel from the crosshair's middle at most, when the crosshair's
    // lines are an odd number of pixels thick and the X's an even number, or the other way round)
    CG_CrosshairMiddle(&x, &y);
    x = floorf(x - thick * 0.5f + 0.5f);
    y = floorf(y - thick * 0.5f + 0.5f);

    for (n = 0, k = 0; k < 4; k++) {
        for (i = from; i <= to; i++, n++) {
            VectorSet4(boxes[n], x + dirs[k][0] * i, y + dirs[k][1] * i, thick, thick);
        }
    }

    VectorSet4(color, 1, 1, 1, HITMARKER_ALPHA * (1.0f - (float)age / HITMARKER_MS));
    cgi.R_DrawBoxes((const float(*)[4])boxes, n, color);
}
