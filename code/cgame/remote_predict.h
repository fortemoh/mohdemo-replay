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

// remote_predict.h: remote prediction's model (Added; Project: Omaha's
// qcommon/remote_predict.cpp, copied with notuern's permission). No engine
// state, so it is unit tested (tests/test_remote_predict.cpp); the cgame's
// side is cg_remotepredict.cpp.
//
// Stock servers have no lag compensation: a shot is traced on the server frame
// after the shooter's command arrives, against where everyone is then. Each
// snapshot gives a remote player's exact velocity at their exact last command
// time. Between two snapshots the keys they held (at most one switch) must turn
// the old velocity into the new one under the real movement physics, which
// tells the keys they hold now. The prediction runs those keys forward, and a
// small fitted blend mixes in "keys released" and "velocity held" by how
// reliable that guess is.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define RP_HISTORY    16
#define RP_PATH_STEP  8   // ms per simulated command
#define RP_PATH_STEPS 40  // so paths reach 320 ms

// legs animation classes and key directions (RP_ClassifyAnim)
enum { RPL_NONE, RPL_IDLE, RPL_MOVE, RPL_JUMP, RPL_FALL, RPL_LAND, RPL_WALLRUN, RPL_DEAD, RPL_OTHER };
enum { RPD_NONE, RPD_FWD, RPD_BACK, RPD_LEFT, RPD_RIGHT };

typedef struct {
    int cls;
    int dir;
    int crouch;
    int run;
} rpAnim_t;

// Classifies a player animation by name, from the right: <prefix>_<stand|crouch>_<run|walk>_<dir>
void RP_ClassifyAnim(const char *name, rpAnim_t *out);

// what the server runs, learned from the players
typedef struct {
    int oldCmdScale; // upstream OpenMoHAA's PM_CmdScale (faster backpedal and strafe)
    int frameMsec;   // 1000 / sv_fps
} rpServer_t;

// one snapshot of one player
typedef struct {
    int      serverTime;
    int      trTime; // their last command time; 0 when the server doesn't send it
    float    origin[3];
    float    velocity[3];
    float    yaw;
    rpAnim_t legs;     // the newest legs animation
    int      runSpeed; // their weapon's run speed on this server
    int      reset;    // teleported, respawned or changed model: forget the past
    int      teleportBit;
} rpObs_t;

typedef struct {
    rpObs_t obs;
    int     W;     // wish speed (horizontal share) for these keys
    int     air;
    int     trans; // the step from the previous record is usable for key inference
    int     key;   // inferred keys index (-1 unknown)
    int     sw;    // ms into the interval where the keys switched (0 = held)
    float   res;   // velocity residual of the inference (u/s)
    int     dwell; // snapshots the keys have been held
} rpRecord_t;

typedef struct {
    rpRecord_t rec[RP_HISTORY];
    int        count; // records in rec, newest at rec[(head) % RP_HISTORY]
    int        head;

    // forward paths from the newest record, at RP_PATH_STEP ms steps (XY offsets from origin)
    int   pathValid;
    float keep[RP_PATH_STEPS + 1][2];
    float stop[RP_PATH_STEPS + 1][2];
} rpPlayer_t;

void RP_Reset(rpPlayer_t *p);
// adds the player's state in a new snapshot, infers keys and rebuilds the paths
void RP_Ingest(rpPlayer_t *p, const rpObs_t *obs, const rpServer_t *srv);
// The predicted XY displacement from the newest origin for a target server time
// (the frame the shot will be traced on, as a continuous time). Returns 0 when
// there is nothing to predict from. horizonMsec gets the horizon in the
// player's command time: the lead to the target plus a push of half the
// backlog of their late commands, which adds at most maxPushMsec (0: no push,
// < 0: no limit). A player none of whose commands the server has run for
// RP_STALL_FRAMES frames is held where the server has them (displacement 0,
// horizon 0).
int RP_Predict(const rpPlayer_t *p, const rpServer_t *srv, double targetTime, int maxPushMsec, float out[2], float *horizonMsec);
// the newest record, or NULL
const rpRecord_t *RP_Newest(const rpPlayer_t *p);

// One flat-ground command of the movement physics in XY, exactly as bg_pmove
// does it (exposed for tests). st = x, y, vx, vy.
void RP_PmoveStep(float st[4], int msec, int forwardmove, int rightmove, float yaw, int speed, int oldCmdScale);

// Server-wide learning: the steady speeds players reach tell the run speed of
// each weapon and which PM_CmdScale the server runs.
typedef struct {
    int   runSpeed[1024][8]; // last steady forward speeds per weapon model
    int   runCount[1024];
    float strafeStock;       // evidence weights for the two CmdScale versions
    float strafeOld;
} rpLearn_t;

void RP_LearnReset(rpLearn_t *l);
// feeds one steady observation (velocity not changing) of a player with a weapon model
void RP_LearnObserve(rpLearn_t *l, rpServer_t *srv, int weaponModel, const rpAnim_t *legs, float speed);
// the learned run speed for a weapon (0 when unknown)
int RP_LearnRunSpeed(const rpLearn_t *l, int weaponModel);

// Shooter lead: brackets of when our commands reach the server, from snapshots
typedef struct {
    float samples[64];
    int   count;
    int   head;
} rpLead_t;

void RP_LeadReset(rpLead_t *l);
// snapshot at serverTime acked commands up to commandTime; nextCmdTime is our
// first command after it (-1 unknown)
void RP_LeadPush(rpLead_t *l, int serverTime, int commandTime, int nextCmdTime);
// the mean time from a command's serverTime to the frame that runs it, or -1
float RP_LeadMean(const rpLead_t *l, int frameMsec);

#ifdef __cplusplus
}
#endif
