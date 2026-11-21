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

#include "cl_ui.h"
#include "../corepp/tiki.h"

void CL_Draw3DModel(
    float     x,
    float     y,
    float     w,
    float     h,
    qhandle_t model,
    vec3_t    origin,
    vec3_t    rotateoffset,
    vec3_t    offset,
    vec3_t    angle,
    vec3_t    color,
    str       anim,
    bool      keepAspect,
    bool      studioLight
)
{
    refdef_t    inv_refdef {};
    refEntity_t ent {};
    vec3_t      unprojoffset;
    vec3_t      entaxis[3];

    inv_refdef.fov_x   = 30;
    inv_refdef.fov_y   = 30;
    if (keepAspect && h > 0) {
        // Added: the model unstretched in a frame that isn't square
        inv_refdef.fov_x = RAD2DEG(2.0f * atanf(tanf(DEG2RAD(15.0f)) * w / h));
    }
    inv_refdef.x       = x;
    inv_refdef.y       = y;
    inv_refdef.width   = w;
    inv_refdef.rdflags = RDF_HUD | RDF_NOWORLDMODEL;
    if (studioLight) {
        // Added: the model is placed at the player's eyes, which would light
        // it with the map there; a studio's light instead (the renderer's)
        inv_refdef.rdflags |= RDF_STUDIOLIGHT;
    }
    inv_refdef.height  = h;
    inv_refdef.time    = cl.serverTime;
    re.ClearScene();

    AnglesToAxis(angle, entaxis);

    MatrixTransformVector(rotateoffset, entaxis, unprojoffset);

    ent.scale        = 1.0;
    ent.hModel       = model;
    ent.entityNumber = 1023;
    VectorAdd(origin, offset, ent.origin);
    VectorAdd(ent.origin, unprojoffset, ent.origin);

    ent.tiki = re.R_Model_GetHandle(model);
    if (!ent.tiki) {
        return;
    }

    if (anim.length()) {
        static qboolean bInited = false;
        static int      hModel[8];
        static int      iUpdateTime[8];
        static int      iIndex[8];
        static float    fAnimTime[8];
        static char     szAnimName[8][256];
        int             i;
        int             iCurrNum;

        if (!bInited) {
            for (i = 0; i < ARRAY_LEN(hModel); i++) {
                iUpdateTime[i] = 0;
                iIndex[i]      = -1;
                hModel[i]      = -1;
            }
            bInited = true;
        }

        for (i = 0; i < ARRAY_LEN(hModel); i++) {
            if (hModel[i] == model) {
                break;
            }
        }

        if (i == ARRAY_LEN(hModel)) {
            iCurrNum = 0;

            // Find a free slot
            for (i = 0; i < ARRAY_LEN(hModel); i++) {
                if (hModel[i] == -1) {
                    iCurrNum = i;
                    break;
                }
            }

            if (i == ARRAY_LEN(hModel)) {
                int iOldestNum, iOldestTime;

                iOldestTime = iUpdateTime[0];
                iOldestNum  = 0;

                for (i = 1; i < ARRAY_LEN(hModel); i++) {
                    if (iUpdateTime[i] < iOldestTime) {
                        iOldestNum  = i;
                        iOldestTime = iUpdateTime[i];
                    }
                }

                iCurrNum = iOldestNum;
            }

            hModel[iCurrNum] = model;
            iIndex[iCurrNum] = -1;
            Q_strncpyz(szAnimName[iCurrNum], anim, sizeof(szAnimName[iCurrNum]));
        } else {
            iCurrNum = i;

            if (!Q_stricmp(szAnimName[iCurrNum], anim)) {
                fAnimTime[iCurrNum] += (cls.realtime - iUpdateTime[i]) * 0.001f;
                iUpdateTime[iCurrNum] = cls.realtime;

                if (TIKI_Anim_Time(ent.tiki, iIndex[iCurrNum]) < fAnimTime[iCurrNum]) {
                    // Added in 2.11
                    //  Clear tunak easter egg
                    Cvar_Set("tunak", "0");
                    iIndex[iCurrNum] = -1;
                }
            } else {
                iIndex[iCurrNum] = -1;
                Q_strncpyz(szAnimName[iCurrNum], anim, sizeof(szAnimName[iCurrNum]));
            }
        }

        if (iIndex[iCurrNum] == -1) {
            iIndex[iCurrNum]      = TIKI_Anim_NumForName(ent.tiki, szAnimName[iCurrNum]);
            fAnimTime[iCurrNum]   = 0;
            iUpdateTime[iCurrNum] = cls.realtime;
        }

        ent.frameInfo[0].index  = iIndex[iCurrNum];
        ent.frameInfo[0].time   = fAnimTime[iCurrNum];
        ent.frameInfo[0].weight = 1.f;
        ent.actionWeight        = 1.f;
    }

    ent.shaderRGBA[0] = color[0] * 255;
    ent.shaderRGBA[1] = color[1] * 255;
    ent.shaderRGBA[2] = color[2] * 255;
    ent.shaderRGBA[3] = color[3] * 255;

    if (cge) {
        vec3_t vViewAngles;

        cge->CG_EyePosition(&inv_refdef.vieworg);
        cge->CG_EyeAngles(&vViewAngles);
        AnglesToAxis(vViewAngles, inv_refdef.viewaxis);

        VectorCopy(ent.origin, unprojoffset);
        MatrixTransformVector(unprojoffset, inv_refdef.viewaxis, ent.origin);
        MatrixMultiply(entaxis, inv_refdef.viewaxis, ent.axis);
        VectorAdd(ent.origin, inv_refdef.vieworg, ent.origin);
    } else {
        AnglesToAxis(angle, ent.axis);
        AxisClear(inv_refdef.viewaxis);
    }

    re.AddRefEntityToScene(&ent, ENTITYNUM_NONE);
    re.RenderScene(&inv_refdef);
}
