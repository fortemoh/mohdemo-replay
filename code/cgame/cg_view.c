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

#include "cg_local.h"
#include "cg_parsemsg.h"

//============================================================================

/*
=================
CG_CalcVrect

Sets the coordinates of the rendered window
=================
*/
static void CG_CalcVrect(void)
{
    int size;

    // the intermission should allways be full screen
    if (cg.snap->ps.pm_flags & PMF_INTERMISSION) {
        size = 100;
    } else {
        // bound normal viewsize
        if (cg_viewsize->integer < 30) {
            cgi.Cvar_Set("viewsize", "30");
            size = 30;
        } else if (cg_viewsize->integer > 100) {
            cgi.Cvar_Set("viewsize", "100");
            size = 100;
        } else {
            size = cg_viewsize->integer;
        }
    }
    cg.refdef.width = cgs.glconfig.vidWidth * size / 100;
    cg.refdef.width &= ~1;

    cg.refdef.height = cgs.glconfig.vidHeight * size / 100;
    cg.refdef.height &= ~1;

    cg.refdef.x = (cgs.glconfig.vidWidth - cg.refdef.width) / 2;
    cg.refdef.y = (cgs.glconfig.vidHeight - cg.refdef.height) / 2;
}

//==============================================================================

/*
===============
CG_OffsetThirdPersonView

===============
*/
#define CAMERA_MINIMUM_DISTANCE 40

static void CG_OffsetThirdPersonView(void)
{
    vec3_t        forward;
    vec3_t        original_camera_position;
    vec3_t        new_vieworg;
    trace_t       trace;
    vec3_t        min, max;
    float        *look_offset;
    float        *target_angles;
    float        *target_position;
    vec3_t        delta;
    vec3_t        original_angles;
    qboolean      lookactive, resetview;
    static vec3_t saved_look_offset;
    vec3_t        camera_offset;

    target_angles   = cg.refdefViewAngles;
    target_position = cg.refdef.vieworg;

    // see if angles are absolute
    if (cg.predicted_player_state.camera_flags & CF_CAMERA_ANGLES_ABSOLUTE) {
        VectorClear(target_angles);
    }

    // see if we need to ignore yaw
    if (cg.predicted_player_state.camera_flags & CF_CAMERA_ANGLES_IGNORE_YAW) {
        target_angles[YAW] = 0;
    }

    // see if we need to ignore pitch
    if (cg.predicted_player_state.camera_flags & CF_CAMERA_ANGLES_IGNORE_PITCH) {
        target_angles[PITCH] = 0;
    }

    // offset the current angles by the camera offset
    VectorSubtract(target_angles, cg.predicted_player_state.camera_offset, target_angles);

    // Get the position of the camera after any needed rotation
    look_offset = cgi.get_camera_offset(&lookactive, &resetview);

    if ((!resetview) && ((cg.predicted_player_state.camera_flags & CF_CAMERA_ANGLES_ALLOWOFFSET) || (lookactive))) {
        VectorSubtract(look_offset, saved_look_offset, camera_offset);
        VectorAdd(target_angles, camera_offset, target_angles);
        if (target_angles[PITCH] > 90) {
            target_angles[PITCH] = 90;
        } else if (target_angles[PITCH] < -90) {
            target_angles[PITCH] = -90;
        }
    } else {
        VectorCopy(look_offset, saved_look_offset);
    }

    target_angles[YAW]   = AngleNormalize360(target_angles[YAW]);
    target_angles[PITCH] = AngleNormalize180(target_angles[PITCH]);

    // Move reference point up

    target_position[2] += cg_cameraheight->value;

    VectorCopy(target_position, original_camera_position);

    // Move camera back from reference point

    AngleVectors(target_angles, forward, NULL, NULL);

    VectorMA(target_position, -cg_cameradist->value, forward, new_vieworg);

    new_vieworg[2] += cg_cameraverticaldisplacement->value;

    // Create a bounding box for our camera

    min[0] = -5;
    min[1] = -5;
    min[2] = -5;

    max[0] = 5;
    max[1] = 5;
    max[2] = 5;

    // Make sure camera does not collide with anything
    CG_Trace(&trace, cg.playerHeadPos, min, max, new_vieworg, 0, MASK_CAMERASOLID, qfalse, qtrue, "ThirdPersonTrace 1");

    VectorCopy(trace.endpos, target_position);

    // calculate distance from end position to head position
    VectorSubtract(target_position, cg.playerHeadPos, delta);
    // kill any negative z difference in delta
    if (delta[2] < CAMERA_MINIMUM_DISTANCE) {
        delta[2] = 0;
    }
    if (VectorLength(delta) < CAMERA_MINIMUM_DISTANCE) {
        VectorNormalize(delta);
        /*
      // see if we are going straight up
      if ( ( delta[ 2 ] > 0.75 ) && ( height > 0.85f * cg.predicted_player_state.viewheight ) )
         {
         // we just need to lower our start position slightly, since we are on top of the player
         new_vieworg[ 2 ] -= 16;
	      CG_Trace(&trace, cg.playerHeadPos, min, max, new_vieworg, 0, MASK_CAMERASOLID, qfalse, true, "ThirdPersonTrace 2" );
	      VectorCopy(trace.endpos, target_position);
         }
      else
*/
        {
            // we are probably up against the wall so we want the camera to pitch up on top of the player
            // save off the original angles
            VectorCopy(target_angles, original_angles);
            // start cranking up the target angles, pitch until we are the correct distance away from the player
            while (target_angles[PITCH] < 90) {
                target_angles[PITCH] += 2;

                AngleVectors(target_angles, forward, NULL, NULL);

                VectorMA(original_camera_position, -cg_cameradist->value, forward, new_vieworg);

                new_vieworg[2] += cg_cameraverticaldisplacement->value;

                CG_Trace(
                    &trace,
                    cg.playerHeadPos,
                    min,
                    max,
                    new_vieworg,
                    0,
                    MASK_CAMERASOLID,
                    qfalse,
                    qtrue,
                    "ThirdPersonTrace 3"
                );

                VectorCopy(trace.endpos, target_position);

                // calculate distance from end position to head position
                VectorSubtract(target_position, cg.playerHeadPos, delta);
                // kill any negative z difference in delta
                if (delta[2] < 0) {
                    delta[2] = 0;
                }
                if (VectorLength(delta) >= CAMERA_MINIMUM_DISTANCE) {
                    target_angles[PITCH] = (0.25f * target_angles[PITCH]) + (0.75f * original_angles[PITCH]);
                    // set the pitch to be that of the angle we are currently looking
                    //target_angles[ PITCH ] = original_angles[ PITCH ];
                    break;
                }
            }
            if (target_angles[PITCH] > 90) {
                // if we failed, go with the original angles
                target_angles[PITCH] = original_angles[PITCH];
            }
        }
    }
}

/*
===============
CG_ApplyViewBob

Added in OPM
  Split out of CG_OffsetFirstPersonView() to bob both first person views
===============
*/
static void CG_ApplyViewBob(vec3_t origin, const vec3_t vVelocity, const vec3_t vLeft)
{
    float fPhase, fVel;

    if (cg.predicted_player_state.walking) {
        fVel   = VectorLength(vVelocity);
        fPhase = fVel * 0.0015 + 0.9;
        cg.fCurrentViewBobPhase += (cg.frametime / 1000.0 + cg.frametime / 1000.0) * M_PI * fPhase;

        if (cg.fCurrentViewBobAmp) {
            cg.fCurrentViewBobAmp = fVel;
        } else {
            cg.fCurrentViewBobAmp = fVel * 0.5;
        }

        if (cg.predicted_player_state.fLeanAngle != 0.0) {
            cg.fCurrentViewBobAmp *= 0.75;
        }

        cg.fCurrentViewBobAmp *= (1.0 - fabs(cg.refdefViewAngles[0]) * (1.0 / 90.0) * 0.5) * 0.5;
    } else if (cg.fCurrentViewBobAmp > 0.0) {
        cg.fCurrentViewBobAmp -=
            (cg.frametime / 1000.0 * cg.fCurrentViewBobAmp) + (cg.frametime / 1000.0 * cg.fCurrentViewBobAmp);

        if (cg.fCurrentViewBobAmp < 0.1) {
            cg.fCurrentViewBobAmp = 0.0;
        }
    }

    if (cg.fCurrentViewBobAmp > 0.0) {
        fPhase = sin(cg.fCurrentViewBobPhase) * cg.fCurrentViewBobAmp * 0.03;

        if (fPhase > 16.0) {
            fPhase = 16.0;
        } else if (fPhase < -16.0) {
            fPhase = -16.0;
        }

        VectorMA(origin, fPhase, vLeft, origin);

        fPhase = sin(cg.fCurrentViewBobPhase - 0.94);
        fPhase = (fabs(fPhase) - 0.5) * cg.fCurrentViewBobAmp * 0.06;

        if (fPhase > 16.0) {
            fPhase = 16.0;
        } else if (fPhase < -16.0) {
            fPhase = -16.0;
        }

        origin[2] += fPhase;
    }
}

/*
===============
CG_DemoLeanedEye

Added in OPM
  Where a demo's player looks from while leaning (leaned, their eye
  otherwise): their game kept the eye unleaned when leaning would have put
  it in front of a wall (closer than 16 units) that the eye unleaned doesn't
  face (64 units or more), as where their shots start shows (the server
  fires from the eye their game gives); a change takes 150 ms
===============
*/
static void CG_DemoLeanedEye(const vec3_t eye, vec3_t leaned)
{
    static float fLeaned = 1; // how much of the lean moves the eye
    static int   iLastTime;
    vec3_t       vForward, vEnd;
    trace_t      trace;
    float        fTarget, fStep;

    if (!cg.demoPlayback) {
        return;
    }

    fTarget = 1;
    AngleVectors(cg.refdefViewAngles, vForward, NULL, NULL);
    VectorMA(leaned, 16, vForward, vEnd);
    CG_Trace(
        &trace, leaned, vec3_origin, vec3_origin, vEnd, cg.snap->ps.clientNum, MASK_PLAYERSOLID, qfalse, qfalse,
        "Demo Lean Front Check"
    );
    if (trace.fraction < 1) {
        VectorMA(eye, 64, vForward, vEnd);
        CG_Trace(
            &trace, eye, vec3_origin, vec3_origin, vEnd, cg.snap->ps.clientNum, MASK_PLAYERSOLID, qfalse, qfalse,
            "Demo Lean Front Check"
        );
        if (trace.fraction == 1) {
            fTarget = 0;
        }
    }

    fStep     = (cg.time - iLastTime) / 150.f;
    iLastTime = cg.time;
    if (fStep < 0 || fStep >= 1) {
        // a jump
        fLeaned = fTarget;
    } else if (fLeaned < fTarget) {
        fLeaned = Q_min(fLeaned + fStep, fTarget);
    } else {
        fLeaned = Q_max(fLeaned - fStep, fTarget);
    }

    VectorSubtract(leaned, eye, vEnd);
    VectorMA(eye, fLeaned, vEnd, leaned);
}

/*
===============
CG_OffsetFirstPersonView

===============
*/
void CG_OffsetFirstPersonView(refEntity_t *pREnt, qboolean bUseWorldPosition)
{
    float     *origin;
    centity_t *pCent;
    dtiki_t   *tiki;
    int        iTag;
    int        i;
    int        iMask;
    vec3_t     vDelta;
    float      mat[3][3];
    vec3_t     vOldOrigin;
    vec3_t     vStart, vEnd, vMins, vMaxs;
    vec3_t     vVelocity;
    trace_t    trace;

    VectorSet(vMins, -6, -6, -6);
    VectorSet(vMaxs, 6, 6, 6);

    //
    //
    //
    //
    origin = cg.refdef.vieworg;

    pCent = &cg_entities[cg.predicted_player_state.clientNum];

    tiki = cgi.R_Model_GetHandle(cgs.model_draw[pCent->currentState.modelindex]);
    iTag = cgi.Tag_NumForName(tiki, "eyes bone");
    if (iTag != -1) {
        if (bUseWorldPosition) {
            orientation_t oHead;
            float         mat3[3][3];
            vec3_t        vHeadAng, vDelta;

            VectorCopy(pCent->lerpOrigin, origin);
            AxisCopy(pREnt->axis, mat);
            oHead = cgi.TIKI_Orientation(pREnt, iTag);

            for (i = 0; i < 3; i++) {
                VectorMA(origin, oHead.origin[i], mat[i], origin);
            }

            R_ConcatRotations(oHead.axis, mat, mat3);
            MatrixToEulerAngles(mat3, vHeadAng);
            AnglesSubtract(vHeadAng, cg.refdefViewAngles, vDelta);
            VectorMA(cg.refdefViewAngles, cg.fEyeOffsetFrac, vDelta, cg.refdefViewAngles);
            VectorCopy(vHeadAng, cg.refdefViewAngles);
        } else {
            orientation_t oHead;
            vec3_t        vHeadAng;

            VectorCopy(pCent->lerpOrigin, origin);
            AxisCopy(pREnt->axis, mat);
            MatrixToEulerAngles(mat, vHeadAng);
            oHead = cgi.TIKI_Orientation(pREnt, iTag);

            for (i = 0; i < 3; i++) {
                VectorMA(origin, oHead.origin[i], mat[i], origin);
            }

            if (cg_target_game >= TG_MOHTA) {
                // Changed in 2.0
                //  Slight less angle
                cg.refdefViewAngles[2] += cg.predicted_player_state.fLeanAngle * 0.2;
            } else {
                cg.refdefViewAngles[2] += cg.predicted_player_state.fLeanAngle * 0.3;
            }
        }
    } else {
        cgi.DPrintf("CG_OffsetFirstPersonView warning: Couldn't find 'eyes bone' for player\n");
    }

    VectorCopy(origin, vOldOrigin);

    if (!cg.predicted_player_state.walking || (!(cg.predicted_player_state.pm_flags & PMF_FROZEN) && !(cg.predicted_player_state.pm_flags & PMF_NO_MOVE))) {
        VectorCopy(cg.predicted_player_state.velocity, vVelocity);
    } else {
        //
        // Added in OPM
        //  When frozen, there must be no movement at all
        VectorClear(vVelocity);
    }

    if (bUseWorldPosition) {
        iMask = MASK_VIEWSOLID;

        // Added in OPM
        //  Bob the view here too while alive, as without cg_animationviewmodel
        if (cg.snap->ps.stats[STAT_HEALTH] > 0) {
            vec3_t vPivotPoint;
            vec3_t vForward, vLeft;

            vPivotPoint[0] = cg.refdefViewAngles[0];
            vPivotPoint[1] = cg.refdefViewAngles[1];
            vPivotPoint[2] = 0.0;
            AngleVectorsLeft(vPivotPoint, vForward, vLeft, NULL);
            CG_ApplyViewBob(origin, vVelocity, vLeft);
        }
    } else {
        float  fTargHeight;
        float  fHeightDelta, fHeightChange;
        vec3_t vDelta;
        vec3_t vPivotPoint;
        vec3_t vForward, vLeft;

        origin[0]    = cg.predicted_player_state.origin[0];
        origin[1]    = cg.predicted_player_state.origin[1];
        fTargHeight  = cg.predicted_player_state.origin[2] + cg.predicted_player_state.viewheight;
        fHeightDelta = fTargHeight - cg.fCurrentViewHeight;

        if (fabs(fHeightDelta) < 0.1 || !cg.fCurrentViewHeight) {
            cg.fCurrentViewHeight = fTargHeight;
        } else {
            if (fHeightDelta > 32.f) {
                fHeightDelta          = 32.f;
                cg.fCurrentViewHeight = fTargHeight - 32.0;
            } else if (fHeightDelta < -32.f) {
                fHeightDelta          = -32.f;
                cg.fCurrentViewHeight = fTargHeight + 32.0;
            }

            fHeightChange = cg.frametime / 1000.0 * fHeightDelta * 12.5;
            if (!cg.predicted_player_state.walking) {
                fHeightChange += fHeightChange;
            }

            if (fabs(fHeightDelta) < fabs(fHeightChange)) {
                fHeightChange = fHeightDelta;
            }

            cg.fCurrentViewHeight += fHeightChange;
        }

        origin[2]      = cg.fCurrentViewHeight;
        vPivotPoint[0] = cg.refdefViewAngles[0];
        vPivotPoint[1] = cg.refdefViewAngles[1];
        vPivotPoint[2] = 0.0;
        AngleVectorsLeft(vPivotPoint, vForward, vLeft, NULL);

        VectorCopy(origin, vStart);

        if (cg.predicted_player_state.pm_type != PM_CLIMBWALL) {
            if (cg.refdefViewAngles[0] > 0.0) {
                vStart[2] -= (cg.fCurrentViewHeight - cg.predicted_player_state.origin[2]) * 0.4;
            } else {
                vStart[2] -= (cg.fCurrentViewHeight - cg.predicted_player_state.origin[2]) * 0.2;
            }
        } else {
            vStart[2] -= (cg.fCurrentViewHeight - cg.predicted_player_state.origin[2]) * 0.15;
        }

        VectorSubtract(origin, vStart, vDelta);
        RotatePointAroundVector(vEnd, vLeft, vDelta, cg.refdefViewAngles[0] * 0.4);
        VectorAdd(vStart, vEnd, origin);

        if (cg.predicted_player_state.fLeanAngle) {
            VectorCopy(origin, vStart);
            vStart[2] -= 28.7f;

            VectorSubtract(origin, vStart, vDelta);
            RotatePointAroundVector(vEnd, vForward, vDelta, cg.predicted_player_state.fLeanAngle);
            // Added in OPM
            VectorAdd(vStart, vEnd, vEnd);
            CG_DemoLeanedEye(origin, vEnd);
            VectorCopy(vEnd, origin);
        }

        CG_ApplyViewBob(origin, vVelocity, vLeft);

        iMask = MASK_PLAYERSOLID;
    }

    vStart[0] = cg.predicted_player_state.origin[0];
    vStart[1] = cg.predicted_player_state.origin[1];
    vStart[2] = cg.predicted_player_state.origin[2] + cg.predicted_player_state.viewheight;
    vEnd[0]   = cg.predicted_player_state.origin[0];
    vEnd[1]   = cg.predicted_player_state.origin[1];
    vEnd[2]   = origin[2];

    CG_Trace(
        &trace, vStart, vMins, vMaxs, vEnd, cg.snap->ps.clientNum, iMask, qfalse, qtrue, "FirstPerson Height Check"
    );

    VectorCopy(trace.endpos, vStart);
    vEnd[0] = origin[0];
    vEnd[1] = origin[1];
    vEnd[2] = trace.endpos[2];
    CG_Trace(&trace, vStart, vMins, vMaxs, vEnd, cg.snap->ps.clientNum, iMask, 0, 1, "FirstPerson Lateral Check");

    VectorCopy(trace.endpos, origin);
    VectorSubtract(origin, vOldOrigin, vDelta);
    VectorAdd(pREnt->origin, vDelta, pREnt->origin);

    if (!bUseWorldPosition) {
        VectorCopy(cg.refdefViewAngles, vDelta);
        vDelta[0] *= 0.5;
        vDelta[2] *= 0.75;

        AngleVectorsLeft(vDelta, mat[0], mat[1], mat[2]);

        CG_CalcViewModelMovement(
            cg.fCurrentViewBobPhase, cg.fCurrentViewBobAmp, vVelocity, vDelta
        );

        VectorMA(pREnt->origin, vDelta[0], mat[0], pREnt->origin);
        VectorMA(pREnt->origin, vDelta[1], mat[1], pREnt->origin);
        VectorMA(pREnt->origin, vDelta[2], mat[2], pREnt->origin);
    }

    VectorCopy(origin, cg.playerHeadPos);
}

/*
====================
CG_CalcFov

Fixed fov at intermissions, otherwise account for fov variable and zooms.
====================
*/
#define WAVE_AMPLITUDE 1
#define WAVE_FREQUENCY 0.4

static int CG_CalcFov(void)
{
    float x;
    float phase;
    float v;
    int   contents;
    float fov_x, fov_y;
    int   inwater;
    float fov_ratio;

    fov_ratio = (float)cg.refdef.width / (float)cg.refdef.height * (3.0 / 4.0);
    if (fov_ratio == 1) {
        fov_x = cg.camera_fov;
    } else {
        fov_x = RAD2DEG(atan(tan(DEG2RAD(cg.camera_fov / 2.0)) * fov_ratio)) * 2.0;
    }

    x = cg.refdef.width / tan(fov_x / 360 * M_PI);
    fov_y = atan2(cg.refdef.height, x);
    fov_y = fov_y * 360 / M_PI;

    // warp if underwater
    contents = CG_PointContents(cg.refdef.vieworg, -1);
    if (contents & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA)) {
        phase = cg.time / 1000.0 * WAVE_FREQUENCY * M_PI * 2;
        v     = WAVE_AMPLITUDE * sin(phase);
        fov_x += v;
        fov_y -= v;
        inwater = qtrue;
    } else {
        inwater = qfalse;
    }

    // set it
    cg.refdef.fov_x    = fov_x;
    cg.refdef.fov_y    = fov_y;
    cg.fRefFovXCos     = cos(fov_x / 114.0f);
    cg.fRefFovXSin     = sin(fov_x / 114.0f);
    cg.fRefFovYCos     = cos(fov_y / 114.0f);
    cg.fRefFovYSin     = sin(fov_y / 114.0f);
    cg.zoomSensitivity = cg.refdef.fov_y / 75.0;
    return inwater;
}

/*
===============
CG_SetupFog

Prepares fog values for rendering
===============
*/
void CG_SetupFog() {
    cg.refdef.farplane_distance = cg.farplane_distance;
    cg.refdef.farplane_bias = cg.farplane_bias;
    cg.refdef.farplane_color[0] = cg.farplane_color[0];
    cg.refdef.farplane_color[1] = cg.farplane_color[1];
    cg.refdef.farplane_color[2] = cg.farplane_color[2];
    cg.refdef.farplane_cull = cg.farplane_cull;
    cg.refdef.skybox_farplane = cg.skyboxFarplane;
    cg.refdef.renderTerrain = cg.renderTerrain;
    cg.refdef.farclipOverride = cg.farclipOverride;
    cg.refdef.farplaneColorOverride[0] = cg.farplaneColorOverride[0];
    cg.refdef.farplaneColorOverride[1] = cg.farplaneColorOverride[1];
    cg.refdef.farplaneColorOverride[2] = cg.farplaneColorOverride[2];
}

/*
===============
CG_SetupPortalSky

Sets portalsky values for rendering
===============
*/
void CG_SetupPortalSky() {
    cg.refdef.sky_alpha = cg.sky_alpha;
    cg.refdef.sky_portal = cg.sky_portal;
    VectorCopy(cg.sky_axis[0], cg.refdef.sky_axis[0]);
    VectorCopy(cg.sky_axis[1], cg.refdef.sky_axis[1]);
    VectorCopy(cg.sky_axis[2], cg.refdef.sky_axis[2]);
    VectorMA(cg.sky_origin, cg.skyboxSpeed, cg.refdef.vieworg, cg.refdef.sky_origin);
}

/*
===============
CG_CameraInHead

Added in OPM
  Whether ps follows a player with the camera at their eyes, as most servers
  with the 1.12 Reborn patch do, instead of behind them like stock servers
===============
*/
static qboolean CG_CameraInHead(const playerState_t *ps)
{
    vec3_t eye;

    if ((ps->pm_flags & (PMF_SPECTATING | PMF_CAMERA_VIEW)) != (PMF_SPECTATING | PMF_CAMERA_VIEW)) {
        return qfalse;
    }

    if (ps->stats[STAT_INFOCLIENT] < 0 || ps->stats[STAT_INFOCLIENT] >= MAX_CLIENTS) {
        return qfalse;
    }

    VectorCopy(ps->origin, eye);
    eye[2] += ps->viewheight;

    return DistanceSquared(eye, ps->camera_origin) < 1;
}

/*
===============
CG_StockFollowCameraAt

Added in OPM
  Where a stock server puts the camera that follows a player, from the
  playerstate (the followed player's origin, view angles and height) and
  their entity (their lean): 56 units behind their eyes and 24 above, moved
  aside when they lean, pulled in by walls and looking 2 degrees lower, as
  Player::GetSpectateFollowOrientation does with the default
  g_spectatefollow_ values; client is theirs, and snap the snapshot of ps,
  where the players and doors in the way are
===============
*/
static void CG_StockFollowCameraAt(
    const playerState_t *ps, const entityState_t *es, int client, const snapshot_t *snap, vec3_t origin, vec3_t angles
)
{
    static const vec3_t mins = {-2, -2, 2};
    static const vec3_t maxs = {2, 2, 2};
    vec3_t              forward, right, up, start, end;
    float               lean, top;
    trace_t             trace;

    lean = 0;
    if (es) {
        // the pelvis (bone controller 3) rolls by 0.8 times the lean
        lean = AngleNormalize180(es->bone_angles[3][2]) / 0.8f;
    }

    // the top of their box, which goes with their view height on the server
    // when their entity's box lags behind
    if (ps->viewheight > CROUCH_VIEWHEIGHT) {
        top = MAXS_Z;
    } else if (es && (es->eFlags & EF_DEAD)) {
        top = DEAD_MINS_Z;
    } else {
        top = CROUCH_MAXS_Z;
    }

    VectorCopy(ps->viewangles, angles);
    AngleVectors(angles, forward, right, up);

    VectorCopy(ps->origin, end);
    end[2] += ps->viewheight;
    VectorMA(end, -56, forward, end);
    VectorMA(end, 24, up, end);
    VectorMA(end, lean * 0.65f, right, end);

    VectorCopy(ps->origin, start);
    start[2] += top - 2;

    CG_SnapshotTrace(&trace, snap, start, mins, maxs, end, client, MASK_SHOT);

    angles[PITCH] += 2 * trace.fraction;
    VectorCopy(trace.endpos, origin);
}

/*
===============
CG_StockFollowCamera

Added in OPM
  With cg_followcamera 1, puts the camera that follows a player where stock
  servers put it, behind them, when the server puts it in their head (2
  shows them from their eyes: cg_followeyes.c)
===============
*/
static void CG_StockFollowCamera(void)
{
    const playerState_t *next;
    centity_t           *cent;
    vec3_t               origin, angles;
    float                f;
    int                  client;
    int                  i;

    if (cg_followcamera->integer != 1 || !CG_CameraInHead(&cg.snap->ps)) {
        return;
    }

    client = cg.snap->ps.stats[STAT_INFOCLIENT];
    cent   = &cg_entities[client];

    CG_StockFollowCameraAt(
        &cg.snap->ps,
        cent->currentValid ? &cent->currentState : NULL,
        client,
        cg.snap,
        cg.camera_origin,
        cg.camera_angles
    );

    // interpolate it like a camera from the server
    if (!cg.nextSnap || cg.nextSnap->serverTime <= cg.snap->serverTime || cg.nextFrameCameraCut) {
        return;
    }

    next = &cg.nextSnap->ps;
    if (!CG_CameraInHead(next) || next->stats[STAT_INFOCLIENT] != client) {
        return;
    }

    CG_StockFollowCameraAt(
        next,
        cent->interpolate ? &cent->nextState : (cent->currentValid ? &cent->currentState : NULL),
        client,
        cg.nextSnap,
        origin,
        angles
    );

    f = (float)(cg.time - cg.snap->serverTime) / (cg.nextSnap->serverTime - cg.snap->serverTime);
    for (i = 0; i < 3; i++) {
        cg.camera_origin[i] += f * (origin[i] - cg.camera_origin[i]);
        cg.camera_angles[i] = LerpAngle(cg.camera_angles[i], angles[i], f);
    }
}

/*
===============
CG_RecorderBehind

Added in OPM
  Whether a demo shows its recorder from behind, where the camera that
  follows a player is (cg_recordercamera), while they play: not while they
  follow someone or look around
===============
*/
qboolean CG_RecorderBehind(void)
{
    return cg_recordercamera->integer && cg.demoPlayback && cg.snap
        && !(cg.snap->ps.pm_flags & (PMF_SPECTATING | PMF_CAMERA_VIEW))
        && !(cg.snap->snapFlags & SNAPFLAG_FOLLOWED_FROM_EYES);
}

/*
===============
CG_RecorderBehindCamera

Added in OPM
  Where the camera behind a recorder is, done as for a player followed
  (CG_StockFollowCamera), from the snapshot to the next, with cg_recordersmooth:
  their body, drawn as other players are, moves ahead of it (cg_smoothClients)
  as a player followed's does. Without it, they stay fixed in the view.
===============
*/
static void CG_RecorderBehindCamera(const playerState_t *ps, vec3_t origin, vec3_t angles)
{
    centity_t *cent = &cg_entities[ps->clientNum];
    vec3_t     nextOrigin, nextAngles;
    int        i;

    if (!cg_recordersmooth->integer || !cg.nextSnap || cg.nextSnap->serverTime <= cg.snap->serverTime
        || !cent->interpolate || cg.nextFrameCameraCut
        || (cg.nextSnap->ps.pm_flags & (PMF_SPECTATING | PMF_CAMERA_VIEW))) {
        CG_StockFollowCameraAt(ps, &cent->currentState, ps->clientNum, cg.snap, origin, angles);
        return;
    }

    CG_StockFollowCameraAt(&cg.snap->ps, &cent->currentState, ps->clientNum, cg.snap, origin, angles);
    CG_StockFollowCameraAt(&cg.nextSnap->ps, &cent->nextState, ps->clientNum, cg.nextSnap, nextOrigin, nextAngles);

    for (i = 0; i < 3; i++) {
        origin[i] += cg.frameInterpolation * (nextOrigin[i] - origin[i]);
        angles[i] = LerpAngle(angles[i], nextAngles[i], cg.frameInterpolation);
    }
}

/*
===============
CG_DemoBehind

Added in OPM
  The player a demo shows from behind, -1 for none: the killer of a kill
  demoonly plays while they aren't the one shown (demofilter unshown,
  cl_demoBehind), when their entity is in the snapshot, unless they're seen
  from their eyes (cg_unspeccedcamera 2, CG_UnspeccedEyes)
===============
*/
int CG_DemoBehind(void)
{
    int client = cl_demoBehind->integer;

    if (!cg.demoPlayback || !cg.snap || client < 0 || client >= MAX_CLIENTS || !cg_entities[client].currentValid
        || cg_unspeccedcamera->integer == 2) {
        return -1;
    }
    return client;
}

/*
===============
CG_DemoBodyPlays

Added in OPM
  Whether a player's body plays an animation of more than weight whose name
  has part in it, and part2 too if not NULL
===============
*/
static qboolean CG_DemoBodyPlays(const entityState_t *es, float weight, const char *part, const char *part2)
{
    dtiki_t    *tiki = CG_FollowedBodyTiki(es);
    const char *name;
    int         i;

    if (!tiki) {
        return qfalse;
    }
    for (i = 0; i < MAX_FRAMEINFOS; i++) {
        if (es->frameInfo[i].weight <= weight) {
            continue;
        }
        name = cgi.Anim_NameForNum(tiki, es->frameInfo[i].index);
        if (name && strstr(name, part) && (!part2 || strstr(name, part2))) {
            return qtrue;
        }
    }
    return qfalse;
}

/*
===============
CG_DemoJumpStart

Added in OPM
  Whether a player's body takes off for a jump, when a server has their eyes
  lower (JUMP_START_VIEWHEIGHT) for the 200 to 300 msec it takes: all 204 such
  snapshots of a player followed in a demo had a takeoff of more than 0.3
  weight, with 3 more at their usual height
===============
*/
static qboolean CG_DemoJumpStart(const entityState_t *es)
{
    return CG_DemoBodyPlays(es, 0.3f, "_jump_", "_takeoff");
}

/*
===============
CG_DemoHeadTurn

Added in OPM
  How many times more a player's view turns from their body than their head
  does when only their head turns, 0 when not: on a ladder their head turns by
  half of it, on a turret by all of it, their torso, arms and pelvis staying at
  rest (PmoveAdjustAngleSettings). Of 17,976 such snapshots of players
  followed in 400 demos from stock servers, 17,397 were on ladders and 571 on
  a turret. Their animations tell which, as a small pitch can leave only the
  head turned otherwise, the others' share rounding to nothing
===============
*/
static float CG_DemoHeadTurn(const entityState_t *es)
{
    int i, j;

    // bone controllers 0 to 3: head, torso, arms, pelvis
    for (i = 1; i <= 3; i++) {
        for (j = 0; j < 3; j++) {
            if (es->bone_angles[i][j]) {
                return 0;
            }
        }
    }
    if (CG_DemoBodyPlays(es, 0, "ladder_", NULL)) {
        return 2;
    }
    if (CG_DemoBodyPlays(es, 0, "turret", NULL)) {
        return 1;
    }
    return 0;
}

/*
===============
CG_DemoOnLadder

Added in OPM
  Whether a player's body is on a ladder, as CG_DemoHeadTurn tells it
===============
*/
qboolean CG_DemoOnLadder(const entityState_t *es)
{
    return CG_DemoHeadTurn(es) == 2;
}

/*
===============
CG_DemoBehindState

Added in OPM
  What a stock server's camera behind a player goes by, from their entity: its
  place and yaw, their pitch that their head, torso, arms and pelvis share
  between them (PmoveAdjustAngleSettings), or their head alone on a ladder or
  a turret, and their eyes' height that their box tells (standing or
  crouching), lower as they take off for a jump. A box comes a snapshot after
  the height when they crouch or stand up, which is left: telling it from
  their animations put the camera wrong as often. Dead, their bones are at
  rest, while a server keeps the pitch they had
===============
*/
void CG_DemoBehindState(const entityState_t *es, playerState_t *ps)
{
    static float alivePitch[MAX_CLIENTS];
    vec3_t       mins, maxs;
    float        head;
    int          i;

    memset(ps, 0, sizeof(*ps));
    VectorCopy(es->origin, ps->origin);
    ps->viewangles[YAW] = es->angles[YAW];
    head = CG_DemoHeadTurn(es);
    if (head) {
        ps->viewangles[PITCH] = head * AngleNormalize180(es->bone_angles[0][PITCH]);
        ps->viewangles[YAW] += head * AngleNormalize180(es->bone_angles[0][YAW]);
    } else {
        for (i = 0; i < 4; i++) {
            ps->viewangles[PITCH] += AngleNormalize180(es->bone_angles[i][PITCH]);
        }
    }
    if (es->number >= 0 && es->number < MAX_CLIENTS) {
        if (es->eFlags & EF_DEAD) {
            ps->viewangles[PITCH] = alivePitch[es->number];
        } else {
            alivePitch[es->number] = ps->viewangles[PITCH];
        }
    }
    IntegerToBoundingBox(es->solid, mins, maxs);
    if (maxs[2] <= CROUCH_MAXS_Z) {
        ps->viewheight = CROUCH_VIEWHEIGHT;
    } else if (CG_DemoJumpStart(es)) {
        ps->viewheight = JUMP_START_VIEWHEIGHT;
    } else {
        ps->viewheight = DEFAULT_VIEWHEIGHT;
    }
}

/*
===============
CG_DemoBehindCamera

Added in OPM
  Where the camera behind a player is, from their entity, done as for a player
  followed (CG_StockFollowCamera): from the snapshot to the next. It doesn't
  go by where they're drawn this frame, which cg_smoothClients moves ahead of
  them, so that they move against it as a player followed does. offset is
  added to their aim
===============
*/
void CG_DemoBehindCamera(int client, const vec3_t offset, vec3_t origin, vec3_t angles)
{
    centity_t    *cent = &cg_entities[client];
    playerState_t ps;
    vec3_t        nextOrigin, nextAngles;
    int           i;

    CG_DemoBehindState(&cent->currentState, &ps);
    VectorAdd(ps.viewangles, offset, ps.viewangles);
    CG_StockFollowCameraAt(&ps, &cent->currentState, client, cg.snap, origin, angles);

    if (!cent->interpolate) {
        return;
    }

    CG_DemoBehindState(&cent->nextState, &ps);
    VectorAdd(ps.viewangles, offset, ps.viewangles);
    CG_StockFollowCameraAt(&ps, &cent->nextState, client, cg.nextSnap, nextOrigin, nextAngles);

    for (i = 0; i < 3; i++) {
        origin[i] += cg.frameInterpolation * (nextOrigin[i] - origin[i]);
        angles[i] = LerpAngle(angles[i], nextAngles[i], cg.frameInterpolation);
    }
}

/*
===============
CG_DemoBehindView

Added in OPM
  Where the camera behind the player of CG_DemoBehind is, as a stock server
  puts the camera that follows them
===============
*/
static qboolean CG_DemoBehindView(vec3_t origin, vec3_t angles)
{
    int client = CG_DemoBehind();

    if (client < 0) {
        return qfalse;
    }

    CG_DemoBehindCamera(client, vec3_origin, origin, angles);
    return qtrue;
}

/*
===============
CG_CalcViewValues

Sets cg.refdef view values
===============
*/
static int CG_CalcViewValues(void)
{
    playerState_t *ps;
    float          SoundAngles[3];

    memset(&cg.refdef, 0, sizeof(cg.refdef));

    // calculate size of 3D view
    CG_CalcVrect();
    CG_SetupFog();

    ps = &cg.predicted_player_state;

    VectorCopy(ps->origin, cg.refdef.vieworg);
    VectorCopy(ps->viewangles, cg.refdefViewAngles);

    cg.refdefViewAngles[2] += ps->fLeanAngle * 0.1;

    if (cg.snap->ps.stats[STAT_HEALTH] > 0) {
        VectorSubtract(cg.refdefViewAngles, cg.predicted_player_state.damage_angles, cg.refdefViewAngles);
        cg.refdefViewAngles[0] += cg.viewkick[0];
        cg.refdefViewAngles[1] += cg.viewkick[1];

        if (cg.viewkick[0] || cg.viewkick[1]) {
            int   i;
            float fDecay;

            for (i = 0; i < 2; i++) {
                fDecay = cg.viewkick[i] * cg.viewkickRecenter;
                if (fDecay > cg.viewkickMaxDecay) {
                    fDecay = cg.viewkickMaxDecay;
                } else if (fDecay < -cg.viewkickMaxDecay) {
                    fDecay = -cg.viewkickMaxDecay;
                }

                if (fabs(fDecay) < cg.viewkickMinDecay) {
                    if (fDecay > 0.0) {
                        fDecay = cg.viewkickMinDecay;
                    } else {
                        fDecay = -cg.viewkickMinDecay;
                    }
                }

                if (cg.viewkick[i] > 0.0) {
                    cg.viewkick[i] -= fDecay * (float)cg.frametime / 1000.0;
                    if (cg.viewkick[i] < 0.0) {
                        cg.viewkick[i] = 0.0;
                    }
                } else {
                    cg.viewkick[i] -= fDecay * (float)cg.frametime / 1000.0;
                    if (cg.viewkick[i] > 0.0) {
                        cg.viewkick[i] = 0.0;
                    }
                }
            }
        }
    }

    // FIXME: fffx screen shake on win32 builds?

    // add error decay
    if (cg_errorDecay->value > 0) {
        int   t;
        float f;

        t = cg.time - cg.predictedErrorTime;
        f = (cg_errorDecay->value - t) / cg_errorDecay->value;
        if (f > 0 && f < 1) {
            VectorMA(cg.refdef.vieworg, f, cg.predictedError, cg.refdef.vieworg);
        } else {
            cg.predictedErrorTime = 0;
        }
    }

    // calculate position of player's head
    cg.refdef.vieworg[2] += cg.predicted_player_state.viewheight;
    // save off the position of the player's head
    VectorCopy(cg.refdef.vieworg, cg.playerHeadPos);

    // Set the aural position of the player
    VectorCopy(cg.playerHeadPos, cg.SoundOrg);

    // Set the aural axis of the player
    VectorCopy(cg.refdefViewAngles, SoundAngles);
    // yaw is purposely inverted because of the miles sound system
    // Commented out in OPM
    //  Useless as SDL audio/AL is used
    //SoundAngles[YAW] = -SoundAngles[YAW];
    AnglesToAxis(SoundAngles, cg.SoundAxis);

    // decide on third person view
    cg.renderingThirdPerson = cg_3rd_person->integer;

    // Added in OPM
    if (CG_RecorderBehind()) {
        cg.renderingThirdPerson = qtrue;
        CG_RecorderBehindCamera(ps, cg.refdef.vieworg, cg.refdefViewAngles);
        // not the zoom of their scope
        cg.camera_fov = cg_fov->value;
    } else if (cg.renderingThirdPerson) {
        // back away from character
        CG_OffsetThirdPersonView();
    }

    // if we are in a camera view, we take our audio cues directly from the camera
    if (ps->pm_flags & PMF_CAMERA_VIEW) {
        // Added in OPM
        CG_StockFollowCamera();

        // Set the aural position to that of the camera
        VectorCopy(cg.camera_origin, cg.refdef.vieworg);

        // Set the aural axis to the camera's angles
        VectorCopy(cg.camera_angles, cg.refdefViewAngles);

        if (cg_protocol >= PROTOCOL_MOHTA_MIN && (ps->pm_flags & PMF_DAMAGE_ANGLES)) {
            // Handle camera shake
            VectorSubtract(cg.refdefViewAngles, cg.predicted_player_state.damage_angles, cg.refdefViewAngles);
        }

        if (ps->camera_posofs[0] || ps->camera_posofs[1] || ps->camera_posofs[2]) {
            vec3_t vAxis[3], vOrg;
            AnglesToAxis(cg.refdefViewAngles, vAxis);
            MatrixTransformVector(ps->camera_posofs, vAxis, vOrg);
            VectorAdd(cg.refdef.vieworg, vOrg, cg.refdef.vieworg);
        }

        // copy view values
        VectorCopy(cg.refdef.vieworg, cg.currentViewPos);
        VectorCopy(cg.refdefViewAngles, cg.currentViewAngles);
        // Added in 2.0
        //  Also copy location data for sound
        VectorCopy(cg.refdef.vieworg, cg.SoundOrg);
        AnglesToAxis(cg.refdefViewAngles, cg.SoundAxis);
    }

    // Added in OPM
    //  a kill of a player not shown, seen from behind them
    if (CG_DemoBehindView(cg.refdef.vieworg, cg.refdefViewAngles)) {
        cg.renderingThirdPerson = qtrue;
        // not the zoom of the player shown
        cg.camera_fov = cg_fov->value;
        // but the view with the scope made up for their long shot
        CG_DemoScopeView(cg.refdef.vieworg, cg.refdefViewAngles, &cg.camera_fov);
        VectorCopy(cg.refdef.vieworg, cg.currentViewPos);
        VectorCopy(cg.refdefViewAngles, cg.currentViewAngles);
        VectorCopy(cg.refdef.vieworg, cg.SoundOrg);
        AnglesToAxis(cg.refdefViewAngles, cg.SoundAxis);
    }

    // position eye reletive to origin
    AnglesToAxis(cg.refdefViewAngles, cg.refdef.viewaxis);

    if (cg.hyperspace) {
        cg.refdef.rdflags |= RDF_NOWORLDMODEL | RDF_HYPERSPACE;
    }

    // field of view
    return CG_CalcFov();
}

void CG_EyePosition(vec3_t *o_vPos)
{
    (*o_vPos)[0] = cg.playerHeadPos[0];
    (*o_vPos)[1] = cg.playerHeadPos[1];
    (*o_vPos)[2] = cg.playerHeadPos[2];
}

void CG_EyeOffset(vec3_t *o_vOfs)
{
    (*o_vOfs)[0] = cg.playerHeadPos[0] - cg.predicted_player_state.origin[0];
    (*o_vOfs)[1] = cg.playerHeadPos[1] - cg.predicted_player_state.origin[1];
    (*o_vOfs)[2] = cg.playerHeadPos[2] - cg.predicted_player_state.origin[2];
}

void CG_EyeAngles(vec3_t *o_vAngles)
{
    (*o_vAngles)[0] = cg.refdefViewAngles[0];
    (*o_vAngles)[1] = cg.refdefViewAngles[1];
    (*o_vAngles)[2] = cg.refdefViewAngles[2];
}

float CG_SensitivityScale()
{
    return cg.zoomSensitivity;
}

void CG_AddLightShow()
{
    int i;
    float fSlopeY, fSlopeZ;
    float x, y, z;
    vec3_t vOrg;
    float r, g, b;
    float fMax;

    fSlopeY = tan(cg.refdef.fov_x * 0.5);
    fSlopeZ = tan(cg.refdef.fov_y * 0.5);

    for (i = 0; i < cg_acidtrip->integer; i++) {
        x = pow(random(), 1.0 / 3.0) * 2048.0;
        y = crandom() * x * fSlopeY;
        z = crandom() * x * fSlopeZ;

        VectorCopy(cg.refdef.vieworg, vOrg);
        VectorMA(vOrg, x, cg.refdef.viewaxis[0], vOrg);
        VectorMA(vOrg, y, cg.refdef.viewaxis[1], vOrg);
        VectorMA(vOrg, z, cg.refdef.viewaxis[2], vOrg);

        r = random();
        g = random();
        b = random();

        fMax = Q_max(r, Q_max(g, b));
        r /= fMax;
        g /= fMax;
        b /= fMax;

        cgi.R_AddLightToScene(vOrg, (rand() & 0x1FF) + 0x80, r, g, b, 0);
    }
}

qboolean CG_FrustumCullSphere(const vec3_t vPos, float fRadius) {
    vec3_t delta;
    float fDotFwd, fDotSide, fDotUp;

    VectorSubtract(vPos, cg.refdef.vieworg, delta);

    fDotFwd = DotProduct(delta, cg.refdef.viewaxis[0]);
    if (-fRadius >= fDotFwd) {
        return qtrue;
    }

    if (cg.refdef.farplane_distance && cg.refdef.farplane_distance + fRadius <= fDotFwd) {
        return qtrue;
    }

    fDotSide = DotProduct(delta, cg.refdef.viewaxis[1]);
    if (fDotSide < 1.f) {
        fDotSide = -fDotSide;
    }

    if (fDotSide * cg.fRefFovXCos - fDotFwd * cg.fRefFovXSin >= fRadius) {
        return qtrue;
    }

    fDotUp = DotProduct(delta, cg.refdef.viewaxis[2]);
    if (fDotUp < 0.f) {
        fDotUp = -fDotUp;
    }

    if (fDotUp * cg.fRefFovYCos - fDotFwd * cg.fRefFovYSin >= fRadius) {
        return qtrue;
    }

    return qfalse;
}

qboolean CG_SimpleDistanceCull(const vec3_t origin, float maxDist) {
    vec3_t delta;

    VectorSubtract(origin, cg.refdef.vieworg, delta);

    return VectorLengthSquared(delta) > maxDist;
}

/*
=================
CG_UpdateFollowPlayer

Added in OPM
  While spectating, presses the key that switches to the next player until the
  player named by cg_followplayer is followed. The server only sees key presses,
  so this works on any server and keeps to its spectating rules.
=================
*/
static struct {
    const char *key;         // key held down to switch players
    int         keyTime;     // when it was pressed
    int         keyFollowed; // player followed then
    int         numPresses;  // since the search started
    int         pauseTime;   // don't search before then
} cg_follow = {NULL, 0, -1, 0, 0};

void CG_ReleaseFollowKey(void)
{
    if (cg_follow.key) {
        cgi.SendConsoleCommand(va("-%s\n", cg_follow.key));
        cg_follow.key = NULL;
    }
}

static void CG_UpdateFollowPlayer(void)
{
    const char *name = cg_followplayer->string;
    int         now  = cgi.Milliseconds();
    int         followed;
    int         numPlayers;
    int         i;
    qboolean    found;

    if (cg_follow.key) {
        // the server only looks at jump once per frame, so hold it for a few
        if (now - cg_follow.keyTime >= 200) {
            CG_ReleaseFollowKey();
        }
        return;
    }

    if (!name[0] || cg.demoPlayback || !(cg.snap->ps.pm_flags & PMF_SPECTATING) || now < cg_follow.pauseTime) {
        cg_follow.numPresses = 0;
        return;
    }

    followed = -1;
    if (cg.snap->ps.pm_flags & PMF_CAMERA_VIEW) {
        followed = cg.snap->ps.stats[STAT_INFOCLIENT];
    }

    found      = qfalse;
    numPlayers = 0;
    for (i = 0; i < cgs.maxclients; i++) {
        if (!cg.clientinfo[i].name[0]) {
            continue;
        }

        numPlayers++;
        if (!Q_stricmp(cg.clientinfo[i].name, name)) {
            if (i == followed) {
                cg_follow.numPresses = 0;
                return;
            }
            found = qtrue;
        }
    }

    if (!found) {
        cg_follow.numPresses = 0;
        return;
    }

    if (followed == cg_follow.keyFollowed && now - cg_follow.keyTime < 1000) {
        // give the server time to switch players
        return;
    }

    if (cg_follow.numPresses >= numPlayers) {
        // went past everyone, they can't be followed right now (dead, on the
        // other team...), so leave the view alone for a while
        cg_follow.numPresses = 0;
        cg_follow.pauseTime  = now + 5000;
        return;
    }

    if ((cg.snap->ps.pm_flags & PMF_CAMERA_VIEW) && cg_protocol >= PROTOCOL_MOHTA_MIN) {
        // since 2.0, jump goes to the next player and use stops following
        cg_follow.key = "moveup";
    } else {
        cg_follow.key = "use";
    }

    cgi.SendConsoleCommand(va("+%s\n", cg_follow.key));
    cg_follow.keyTime     = now;
    cg_follow.keyFollowed = followed;
    cg_follow.numPresses++;
}

//=========================================================================

/*
=================
CG_DrawActiveFrame

Generates and draws a game scene and status information at the given time.
=================
*/
void CG_DrawActiveFrame(int serverTime, int frameTime, stereoFrame_t stereoView, qboolean demoPlayback)
{
    cg.time         = serverTime;
    cg.frametime    = frameTime;
    cg.demoPlayback = demoPlayback;

    // any looped sounds will be respecified as entities
    // are added to the render list
    cgi.S_ClearLoopingSounds();

    // clear all the render lists
    cgi.R_ClearScene();

    // set up cg.snap and possibly cg.nextSnap
    CG_ProcessSnapshots();

    // if we haven't received any snapshots yet, all
    // we can draw is the information screen
    if (!cg.snap || (cg.snap->snapFlags & SNAPFLAG_NOT_ACTIVE)) {
        return;
    }

    // this counter will be bumped for every valid scene we generate
    cg.clientFrame++;

    // set cg.frameInterpolation
    if (cg.nextSnap && r_lerpmodels->integer) {
        int delta;

        delta = (cg.nextSnap->serverTime - cg.snap->serverTime);
        if (delta == 0) {
            cg.frameInterpolation = 0;
        } else {
            cg.frameInterpolation = (float)(cg.time - cg.snap->serverTime) / delta;
        }
    } else {
        cg.frameInterpolation = 0; // actually, it should never be used, because
        // no entities should be marked as interpolating
    }

    //
    // Added in OPM
    //  Clamp the fov to avoid artifacts
    if (cg_fov->value < 65) {
        cgi.Cvar_Set("cg_fov", "65");
    } else if (cg_fov->value > 120) {
        cgi.Cvar_Set("cg_fov", "120");
    }

    // update cg.predicted_player_state
    CG_PredictPlayerState();
    // Added in OPM
    CG_DemoScopeEyes();

    // Added in OPM
    CG_UpdateFollowPlayer();

    // build cg.refdef
    CG_CalcViewValues();

    // display the intermission
    if (cg.snap->ps.pm_flags & PMF_INTERMISSION) {
        if (cgs.gametype != GT_SINGLE_PLAYER) {
            CG_ScoresDown_f();
        } else if (cg.bIntermissionDisplay) {
            if (cg.nextSnap) {
                if (cg_protocol >= PROTOCOL_MOHTA_MIN) {
                    cvar_t* pMission = cgi.Cvar_Get("g_mission", "", CVAR_ARCHIVE);

                    if (cgi.Cvar_Get("g_success", "", 0)->integer) {
                        switch (pMission->integer)
                        {
                        default:
                        case 0:
                            cgi.UI_ShowMenu("mission_success_1", 0);
                            cgi.Cvar_Set("g_t2l1", "1");
                            break;
                        case 2:
                            cgi.UI_ShowMenu("mission_success_2", 0);
                            cgi.Cvar_Set("g_t3l1", "1");
                            break;
                        case 3:
                            cgi.UI_ShowMenu("mission_success_3", 0);
                            break;
                        }
                    } else {
                        switch (pMission->integer)
                        {
                        default:
                        case 0:
                            cgi.UI_ShowMenu("mission_failed_1", 0);
                            break;
                        case 2:
                            cgi.UI_ShowMenu("mission_failed_2", 0);
                            break;
                        case 3:
                            cgi.UI_ShowMenu("mission_failed_3", 0);
                            break;
                        }
                    }
                } else {
                    if (cgi.Cvar_Get("g_success", "", 0)->integer) {
                        cgi.UI_ShowMenu("StatsScreen_Success", qfalse);
                    } else {
                        cgi.UI_ShowMenu("StatsScreen_Failed", qfalse);
                    }
                }
            }
        } else {
            cgi.SendClientCommand("stats");
        }

        cg.bIntermissionDisplay = qtrue;
    } else if (cg.bIntermissionDisplay) {
        if (cgs.gametype != GT_SINGLE_PLAYER) {
            CG_ScoresUp_f();
        } else {
            if (cg_protocol >= PROTOCOL_MOHTA_MIN) {
                cvar_t* pMission = cgi.Cvar_Get("g_mission", "", CVAR_ARCHIVE);

                if (cgi.Cvar_Get("g_success", "", 0)->integer) {
                    switch (pMission->integer)
                    {
                    default:
                    case 0:
                        cgi.UI_HideMenu("mission_success_1", qtrue);
                        break;
                    case 2:
                        cgi.UI_HideMenu("mission_success_2", qtrue);
                        break;
                    case 3:
                        cgi.UI_HideMenu("mission_success_3", qtrue);
                        break;
                    }
                } else {
                    switch (pMission->integer)
                    {
                    default:
                    case 0:
                        cgi.UI_HideMenu("mission_failed_1", qtrue);
                        break;
                    case 2:
                        cgi.UI_HideMenu("mission_failed_2", qtrue);
                        break;
                    case 3:
                        cgi.UI_HideMenu("mission_failed_3", qtrue);
                        break;
                    }
                }
            } else {
                if (cgi.Cvar_Get("g_success", "", 0)->integer) {
                    cgi.UI_HideMenu("StatsScreen_Success", qtrue);
                } else {
                    cgi.UI_HideMenu("StatsScreen_Failed", qtrue);
                }
            }
        }

        cg.bIntermissionDisplay = qfalse;
    } else {
        // Added in OPM
        //  In vanilla, scores are updated because when pressing a key,
        //  it is sent every frame.
        //  In OPM, an event for a key is sent only once, even when it's being held.
        if (cgs.gametype != GT_SINGLE_PLAYER && cg.showScores) {
            CG_ScoresDown_f();
        }
    }

    // Added in OPM
    CG_ProcessPlayerModel();

    // build the render lists
    if (!cg.hyperspace) {
        CG_AddPacketEntities(); // after calcViewValues, so predicted player state is correct
        CG_AddMarks();
    }

    // finish up the rest of the refdef
    CG_SetupPortalSky();

    cg.refdef.time = cg.time;
    memcpy(cg.refdef.areamask, cg.snap->areamask, sizeof(cg.refdef.areamask));
    // Added in OPM
    //  It tells what the recorder can see from where they are. From behind a
    //  player elsewhere, the rooms they can't see would be black
    if (CG_DemoBehind() >= 0) {
        memset(cg.refdef.areamask, 0, sizeof(cg.refdef.areamask));
    }

    // update audio positions
    // Changed in OPM
    //  From behind a player not shown, the listener is no one: the recorder's
    //  sounds come from where they are, as anyone else's, and aren't their own
    if (CG_DemoBehind() >= 0) {
        cgi.S_UpdateEntity(
            cg.snap->ps.clientNum, cg.predicted_player_state.origin, cg.predicted_player_state.velocity, qfalse
        );
        cgi.S_Respatialize(ENTITYNUM_NONE, cg.SoundOrg, cg.SoundAxis);
    } else if (CG_UnspeccedEyes() >= 0) {
        // and from their eyes, as the client would take the recorder's sounds
        // as one's own: theirs, their gun's, come from where they are
        int    recorder;
        vec3_t origin, velocity;

        CG_UnspeccedRecorder(&recorder, origin, velocity);
        cgi.S_UpdateEntity(recorder, origin, velocity, qfalse);
        cgi.S_Respatialize(ENTITYNUM_NONE, cg.SoundOrg, cg.SoundAxis);
    } else {
        cgi.S_Respatialize(cg.snap->ps.clientNum, cg.SoundOrg, cg.SoundAxis);
    }

    // make sure the lagometerSample and frame timing isn't done twice when in stereo
    if (stereoView != STEREO_RIGHT) {
        CG_AddLagometerFrameInfo();
    }

    CG_UpdateTestEmitter();
    CG_AddPendingEffects();

    if (!cg_hidetempmodels->integer) {
        CG_AddTempModels();
    }

    if (vss_draw->integer) {
        CG_AddVSSSources();
    }

    CG_AddBulletTracers();
    CG_AddBulletImpacts();
    CG_AddBeams();

    if (cg_acidtrip->integer) {
        // lol disco
        CG_AddLightShow();
    }

    // actually issue the rendering calls
    CG_DrawActive(stereoView);

    if (cg_stats->integer) {
        cgi.Printf("cg.clientFrame:%i\n", cg.clientFrame);
    }
}
