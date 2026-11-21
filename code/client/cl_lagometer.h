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

// cl_lagometer.h: the lagometer's data (Added; cl_lagometer.cpp, Project:
// Omaha's netgraph), with the cgame's remote prediction's (cg_remotepredict.cpp).
// Drawn by cl_uiextras.cpp (UI_DrawLagometer).

#pragma once

#define LG_SLOT_MS      50  // a column: 50 ms of server time
#define LG_COLUMNS      120 // drawn by the graph: 6 s
#define LG_STAT_COLUMNS 100 // loss, rate, jitter and the frame rate: the last 5 s
#define LG_PING_COLUMNS 20  // ping: the last 1 s
#define LG_FAIL_UNITS   12  // remote prediction this far off (units) is a failure: a shot at the middle of the drawn player starts to miss

enum {
    LG_F_UPDATE = 1 << 0, // an update arrived
    LG_F_LATE   = 1 << 1, // one that the server held back for the client's rate
    LG_F_HELD   = 1 << 2, // none came: the server held it back
    LG_F_LOST   = 1 << 3, // none came, or one could not be read: lost
    LG_F_BUFFER = 1 << 4, // buffer is set
    LG_F_LEAD   = 1 << 5, // remote prediction's lead is set
    LG_F_ERROR  = 1 << 6, // and its error was measured
    LG_F_FAIL   = 1 << 7  // and its worst was a failure
};

typedef struct {
    int      slot;       // the slot it holds; any other: empty
    uint8_t  flags;
    uint8_t  updates;    // updates that arrived
    uint8_t  late;       // of them, held back by the rate
    uint8_t  lostMsgs;   // messages lost before them
    int16_t  ping;       // the worst update's ping
    int16_t  buffer;     // the worst frame's client time past the newest update (ms, < 0: buffer left)
    uint16_t frames;     // frames drawn with the lagometer up
    uint32_t frameUs;    // their time
    int16_t  lead;       // ms other players were drawn ahead (remote prediction)
    uint8_t  errPlayers; // the enemies in view its error was measured on
    float    errAvg;     // units: their average error
    float    errWorst;   // and the worst
} lgColumn_t;

typedef struct {
    int   serial;        // one more each time they are worked out
    int   edge;          // the newest drawn column (slot)
    int   ping;          // ms, the last 1 s
    int   jitter;        // ms, mean change between updates, the last 5 s
    int   scaleMs;       // the graph's top: its peak with room
    float lossPct;       // lost of all messages, the last 5 s
    float ratePct;       // held back or late of all updates, the last 5 s
    int   fps;           // the last 5 s
    int   sinceUpdateMs; // client time past the newest update
    int   interrupted;   // and it is 500 ms or more
    int   predActive;    // remote prediction is on and reporting (the cgame's cg_remotePrediction)
    int   leadPeak;      // ms, its worst lead in the drawn 6 s
    float errAvg;        // units: its average error, the last 5 s; < 0 none measured
    int   failures;      // columns with a failure in the drawn 6 s
    int   failAgeMs;     // since the newest failure, -1 none in view
} lgStats_t;

// the word the compact style says, and how bad it is
enum { LG_STATUS_STABLE, LG_STATUS_HIGH_PING, LG_STATUS_JITTER, LG_STATUS_RATE, LG_STATUS_LOSS, LG_STATUS_INTERRUPTED };
enum { LG_SEV_OK, LG_SEV_WARN, LG_SEV_BAD };

void CL_Lagometer_Reset(void);
// a snapshot was parsed (valid qfalse: it couldn't be read)
void CL_Lagometer_Snapshot(int serverTime, int messageNum, int ping, int snapFlags, qboolean valid);
// the cgame's remote prediction, once an update (cgi.Lagometer_Prediction)
void CL_Lagometer_Prediction(int serverTime, float leadMs, int players, float avgUnits, float worstUnits);
// a frame drawn with the lagometer up: the numbers (worked out every 100 ms)
const lgStats_t *CL_Lagometer_Frame(void);
// a slot's column, NULL: nothing kept for it
const lgColumn_t *CL_Lagometer_Column(int slot);
// nothing came for a column after the newest update, and it is overdue
qboolean CL_Lagometer_Missing(const lgStats_t *st, int slot);
int      CL_Lagometer_Status(const lgStats_t *st, int *severity);
// remote prediction failed in the last 1.5 s (the compact style says so under the word)
qboolean CL_Lagometer_PredFailed(const lgStats_t *st);
// the signal bars lit, 0 to 4
int CL_Lagometer_Bars(const lgStats_t *st, int severity);

static inline int LG_Slot(int serverTime)
{
    return serverTime >= 0 ? serverTime / LG_SLOT_MS : -((-serverTime + LG_SLOT_MS - 1) / LG_SLOT_MS);
}
