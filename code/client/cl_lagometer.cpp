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

// cl_lagometer.cpp: the lagometer's data (Added). Project: Omaha's netgraph
// (its cl_netgraph.cpp and cl_netgraph_logic.h, copied with notuern's
// permission), without what isn't drawn here: the data rates, the frame
// times' percentiles, the target's connection.
//
// The graph is made of columns of 50 ms of server time, so it moves at an
// even pace whatever the frame rate or the server's update rate. Updates
// (snapshots) go to the column of their server time: a stock 20 Hz server
// gives one per column, a 40 Hz one two, and a column keeps the worst of
// what landed in it. Frames go to the column of the client's server time.
//
// A gap between two updates is told apart when the second one comes: lost
// (message numbers skipped, or nothing explains it), held back by the
// server for the client's rate (the second one has SNAPFLAG_RATE_DELAYED),
// or just the expected spacing (snaps below sv_fps). While nothing comes,
// the columns since the newest update count as missing once it is
// LG_OVERDUE_MS old, so an outage grows on the graph as it happens.
//
// Remote prediction (the cgame's) goes to the column of the update it was
// measured on: the lead other players were drawn with, and how far off they
// were drawn for that server frame (the enemies in view only, so the graph
// never tells of one behind a wall).

#include "client.h"
#include "cl_lagometer.h"

#define LG_SLOTS          128    // columns kept (a power of two): 6.4 s
#define LG_OVERDUE_MS     250    // no update for this long: the columns since are missing
#define LG_INTERRUPTED_MS 500    // and for this long: the connection is interrupted
#define LG_STATS_US       100000 // the numbers are worked out this often; the scroll and the outage every frame
#define LG_LEAD_FRESH_MS  1000   // no lead from the cgame for this long: remote prediction is off

static struct {
    lgColumn_t col[LG_SLOTS];
    int        haveUpdate;
    int        lastTime;    // the newest update's server time
    int        lastMessage; // and message number
    int        lastPing;    // and ping
    int        lastFrame;   // the client frame last drawn with the lagometer up
    int64_t    lastFrameUs;
    int        haveLead;    // remote prediction reported, the newest at leadTime
    int        leadTime;
    int        haveFail;    // and failed, the newest at failTime
    int        failTime;
    int64_t    lastStatsUs;
    lgStats_t  st;
} lg;

void CL_Lagometer_Reset(void)
{
    memset(&lg, 0, sizeof(lg));
    for (lgColumn_t& c : lg.col) {
        c.slot = INT_MIN;
    }
}

// the column of a slot, emptied if it held an older one
static lgColumn_t *LG_Column(int slot)
{
    lgColumn_t *c = &lg.col[slot & (LG_SLOTS - 1)];

    if (c->slot != slot) {
        memset(c, 0, sizeof(*c));
        c->slot = slot;
    }
    return c;
}

const lgColumn_t *CL_Lagometer_Column(int slot)
{
    const lgColumn_t *c = &lg.col[slot & (LG_SLOTS - 1)];

    return c->slot == slot ? c : NULL;
}

static int LG_Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/*
 * An update arrived: its server time and message number, its ping and
 * whether the server held it back for the client's rate. valid qfalse: it
 * could not be read (a delta from a lost one), so it counts as lost.
 */
void CL_Lagometer_Snapshot(int serverTime, int message, int ping, int snapFlags, qboolean valid)
{
    const int   slot = LG_Slot(serverTime);
    const int   late = (snapFlags & SNAPFLAG_RATE_DELAYED) ? 1 : 0;
    float       frameMs;
    int         expectMs, lostMsgs = 0, gapMarked = 0;
    lgColumn_t *c;

    if (lg.haveUpdate && serverTime < lg.lastTime) {
        // time went back (a demo seek, a new map): start again
        CL_Lagometer_Reset();
    }
    if (lg.haveUpdate) {
        lostMsgs = LG_Clamp(message - lg.lastMessage - 1, 0, 255);
    }

    if (!valid) {
        if (lg.haveUpdate && message > lg.lastMessage) {
            c = LG_Column(slot);
            c->flags |= LG_F_LOST;
            c->lostMsgs     = (uint8_t)LG_Clamp(c->lostMsgs + lostMsgs + 1, 0, 255);
            lg.lastMessage = message;
        }
        return;
    }

    // the spacing updates come at: a server frame, or the client's snaps if fewer
    // (a protocol 6/8 server without sv_fps gives 1/0)
    frameMs = cls.serverFrameTime * 1000.0f;
    if (!(frameMs > 0.0f && frameMs < 1000.0f)) {
        frameMs = LG_SLOT_MS;
    }
    expectMs = Q_max((int)(frameMs + 0.5f), 1000 / Q_max(1, Cvar_VariableIntegerValue("snaps")));

    if (lg.haveUpdate && serverTime - lg.lastTime > expectMs) {
        int from = LG_Slot(lg.lastTime) + 1;
        int to   = slot - 1;

        if (to - from >= LG_SLOTS) {
            from = to - LG_SLOTS + 1;
        }
        for (int s = from; s <= to; s++) {
            c = LG_Column(s);
            if (!(c->flags & (LG_F_UPDATE | LG_F_LOST))) {
                // lost, unless the server says it held them back
                c->flags |= (lostMsgs > 0 || !late) ? LG_F_LOST : LG_F_HELD;
            }
            gapMarked = 1;
        }
    }

    c = LG_Column(slot);
    c->flags |= LG_F_UPDATE;
    c->updates = (uint8_t)LG_Clamp(c->updates + 1, 0, 255);
    if (late) {
        c->flags |= LG_F_LATE;
        c->late = (uint8_t)LG_Clamp(c->late + 1, 0, 255);
    }
    c->lostMsgs = (uint8_t)LG_Clamp(c->lostMsgs + lostMsgs, 0, 255);
    if (lostMsgs > 0 && !gapMarked) {
        // lost ones without a gap to show them in (two updates a column)
        c->flags |= LG_F_LOST;
    }
    ping    = LG_Clamp(ping, 0, 999);
    c->ping = (int16_t)Q_max((int)c->ping, ping);

    lg.haveUpdate  = 1;
    lg.lastTime    = serverTime;
    lg.lastMessage = message;
    lg.lastPing    = ping;
}

/*
 * The cgame's remote prediction for the update at serverTime: the lead (ms; <
 * 0 off) and, over players enemies in view, how far off they were drawn for
 * its frame: their average and the worst (units)
 */
void CL_Lagometer_Prediction(int serverTime, float leadMs, int players, float avg, float worst)
{
    lgColumn_t *c;

    if (!lg.haveUpdate || leadMs < 0.0f) {
        return;
    }
    c = LG_Column(LG_Slot(serverTime));
    c->flags |= LG_F_LEAD;
    c->lead     = (int16_t)LG_Clamp((int)(leadMs + 0.5f), 0, 32000);
    lg.haveLead = 1;
    lg.leadTime = serverTime;
    if (players <= 0) {
        return;
    }
    if (c->flags & LG_F_ERROR) {
        // two updates in the column (40 Hz): one average over both
        const int total = c->errPlayers + players;

        c->errAvg   = (c->errAvg * c->errPlayers + avg * players) / total;
        c->errWorst = Q_max(c->errWorst, worst);
    } else {
        c->errAvg   = avg;
        c->errWorst = worst;
    }
    c->errPlayers = (uint8_t)LG_Clamp(c->errPlayers + players, 0, 255);
    c->flags |= LG_F_ERROR;
    if (worst >= LG_FAIL_UNITS) {
        c->flags |= LG_F_FAIL;
        if (!lg.haveFail || serverTime > lg.failTime) {
            lg.haveFail = 1;
            lg.failTime = serverTime;
        }
    }
}

// the numbers drawn, at the client's server time
static void LG_Stats(void)
{
    lgStats_t *st = &lg.st;
    int        pingSum = 0, pingN = 0, jitSum = 0, jitN = 0, prev = -1, peak = 0;
    int        updates = 0, lost = 0, late = 0, held = 0, frames = 0, errN = 0;
    int64_t    frameUs = 0;
    float      errSum  = 0.0f;
    const int  serial  = st->serial + 1;

    memset(st, 0, sizeof(*st));
    st->serial = serial;
    st->edge   = LG_Slot(cl.serverTime + LG_SLOT_MS);
    if (lg.haveUpdate) {
        st->sinceUpdateMs = Q_max(0, cl.serverTime - lg.lastTime);
        st->interrupted   = st->sinceUpdateMs >= LG_INTERRUPTED_MS;
    }

    for (int s = st->edge - LG_COLUMNS + 1; s <= st->edge; s++) {
        const lgColumn_t *c = CL_Lagometer_Column(s);

        if (!c) {
            continue;
        }
        if (c->flags & LG_F_UPDATE) {
            peak = Q_max(peak, (int)c->ping);
            if (s > st->edge - LG_PING_COLUMNS) {
                pingSum += c->ping;
                pingN++;
            }
        }
        if (s > st->edge - LG_STAT_COLUMNS) {
            if (c->flags & LG_F_UPDATE) {
                if (prev >= 0) {
                    jitSum += abs(c->ping - prev);
                    jitN++;
                }
                prev = c->ping;
            }
            updates += c->updates;
            late += c->late;
            lost += c->lostMsgs;
            if ((c->flags & LG_F_HELD) && !(c->flags & LG_F_UPDATE)) {
                held++;
            }
            frames += c->frames;
            frameUs += c->frameUs;
            if (c->flags & LG_F_ERROR) {
                errSum += c->errAvg * c->errPlayers;
                errN += c->errPlayers;
            }
        }
        if (c->flags & LG_F_LEAD) {
            st->leadPeak = Q_max(st->leadPeak, (int)c->lead);
        }
        if (c->flags & LG_F_FAIL) {
            st->failures++;
        }
    }

    // (none in the last 1 s: the newest update's)
    st->ping = pingN ? (pingSum + pingN / 2) / pingN : lg.lastPing;
    st->jitter = jitN ? (jitSum + jitN / 2) / jitN : 0;
    // remote prediction's: on while it reports; its lead is drawn on the ping's scale
    st->predActive = lg.haveLead && cl.serverTime - lg.leadTime < LG_LEAD_FRESH_MS;
    st->errAvg     = errN ? errSum / errN : -1.0f;
    st->failAgeMs  = lg.haveFail && cl.serverTime - lg.failTime < LG_COLUMNS * LG_SLOT_MS ? Q_max(0, cl.serverTime - lg.failTime) : -1;
    if (st->predActive) {
        peak = Q_max(peak, st->leadPeak);
    }
    // the peak with room, 150 to 500 ms in steps of 50
    st->scaleMs = LG_Clamp(((peak * 115 + 4999) / 5000) * 50, 150, 500);
    if (updates + lost > 0) {
        st->lossPct = 100.0f * lost / (updates + lost);
    }
    if (updates + held > 0) {
        st->ratePct = 100.0f * (late + held) / (updates + held);
    }
    if (frameUs > 0) {
        st->fps = (int)(frames * 1000000.0 / frameUs + 0.5);
    }
}

const lgStats_t *CL_Lagometer_Frame(void)
{
    const int64_t now = Sys_Microseconds();

    if (lg.haveUpdate) {
        lgColumn_t *c      = LG_Column(LG_Slot(cl.serverTime));
        const int   buffer = LG_Clamp(cl.serverTime - cl.snap.serverTime, -32000, 32000);

        if (!(c->flags & LG_F_BUFFER) || buffer > c->buffer) {
            c->buffer = (int16_t)buffer;
        }
        c->flags |= LG_F_BUFFER;
        // its time, if the frame before was drawn with it too (a frame of more
        // than 2 s is a stall: loading, a breakpoint)
        if (lg.lastFrame == cls.framecount - 1 && now - lg.lastFrameUs > 0 && now - lg.lastFrameUs < 2000000) {
            c->frames++;
            c->frameUs += (uint32_t)(now - lg.lastFrameUs);
        }
    }
    lg.lastFrame   = cls.framecount;
    lg.lastFrameUs = now;

    if (!lg.lastStatsUs || now - lg.lastStatsUs >= LG_STATS_US || now < lg.lastStatsUs) {
        LG_Stats();
        lg.lastStatsUs = now;
    } else {
        // what must move every frame: the scroll, an outage
        lg.st.edge = LG_Slot(cl.serverTime + LG_SLOT_MS);
        if (lg.haveUpdate) {
            lg.st.sinceUpdateMs = Q_max(0, cl.serverTime - lg.lastTime);
            lg.st.interrupted   = lg.st.sinceUpdateMs >= LG_INTERRUPTED_MS;
        }
    }
    return &lg.st;
}

qboolean CL_Lagometer_Missing(const lgStats_t *st, int slot)
{
    return lg.haveUpdate && slot > LG_Slot(lg.lastTime) && st->sinceUpdateMs >= LG_OVERDUE_MS ? qtrue : qfalse;
}

int CL_Lagometer_Status(const lgStats_t *st, int *severity)
{
    if (st->interrupted) {
        *severity = LG_SEV_BAD;
        return LG_STATUS_INTERRUPTED;
    }
    if (st->lossPct >= 5.0f || st->ping > 150) {
        *severity = LG_SEV_BAD;
    } else if (st->lossPct > 0.0f || st->ratePct > 5.0f || st->jitter > 12 || st->ping > 100) {
        *severity = LG_SEV_WARN;
    } else {
        *severity = LG_SEV_OK;
    }
    if (st->lossPct > 0.0f) {
        return LG_STATUS_LOSS;
    }
    if (st->ratePct > 5.0f) {
        return LG_STATUS_RATE;
    }
    if (st->jitter > 12) {
        return LG_STATUS_JITTER;
    }
    if (st->ping > 100) {
        return LG_STATUS_HIGH_PING;
    }
    return LG_STATUS_STABLE;
}

qboolean CL_Lagometer_PredFailed(const lgStats_t *st)
{
    return st->predActive && st->failAgeMs >= 0 && st->failAgeMs <= 1500 ? qtrue : qfalse;
}

int CL_Lagometer_Bars(const lgStats_t *st, int severity)
{
    if (st->interrupted) {
        return 0;
    }
    if (severity == LG_SEV_BAD) {
        return 1;
    }
    if (severity == LG_SEV_WARN) {
        return 2;
    }
    return st->ping >= 60 ? 3 : 4;
}
